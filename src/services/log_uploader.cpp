#include "log_uploader.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <time.h>

#include "config/config.h"
#include "services/diagnostics.h"
#include "services/logger.h"

namespace
{
  const char *const TAG = "upload";

  constexpr uint32_t LOG_UPLOAD_INTERVAL_MS = 5000;
  constexpr uint16_t LOG_UPLOAD_MAX_RECORDS = 30;
  constexpr uint16_t LOG_UPLOAD_MAX_BYTES = 6000;
  constexpr uint32_t LOG_UPLOAD_BACKOFF_MIN_MS = 2000;
  constexpr uint32_t LOG_UPLOAD_BACKOFF_MAX_MS = 60000;
  constexpr uint16_t LOG_HTTP_CONNECT_TIMEOUT_MS = 4000;
  constexpr uint16_t LOG_HTTP_TIMEOUT_MS = 6000;
  constexpr uint16_t LOG_UPLOADER_STACK = 6144;
  static_assert(LOG_UPLOAD_BACKOFF_MAX_MS >= LOG_UPLOAD_BACKOFF_MIN_MS,
                "upload backoff ceiling must not be below its floor");

  constexpr uint8_t MAX_BATCHES_PER_WAKE = 4;

  char gUrl[160] = {0};
  SemaphoreHandle_t gUrlMutex = nullptr;
  TaskHandle_t gTask = nullptr;

  uint32_t gSentRecords = 0;
  uint32_t gSentBatches = 0;
  uint32_t gFailedBatches = 0;
  int gLastHttpCode = 0;
  uint32_t gLastSuccessMs = 0;
  uint32_t gBackoffMs = 0;
  uint32_t gNextAttemptMs = 0;
  bool gBackendReachable = false;
  bool gFailureLogged = false;
  uint32_t gFailingSinceMs = 0;

  void copyUrl(char *out, size_t outSize)
  {
    out[0] = '\0';
    if (gUrlMutex == nullptr)
      return;
    if (xSemaphoreTake(gUrlMutex, pdMS_TO_TICKS(100)) != pdTRUE)
      return;
    strlcpy(out, gUrl, outSize);
    xSemaphoreGive(gUrlMutex);
  }

  void buildBody(String &body, uint16_t &countOut, uint32_t &lastSeqOut)
  {
    const LogRingStats ls = logGetStats();

    char head[320];
    const time_t nowEpoch = time(nullptr);
    snprintf(head, sizeof(head),
             "{\"device\":\"%s\",\"mac\":\"%s\",\"bootId\":\"%08lX\",\"bootCount\":%lu,"
             "\"fw\":\"%s %s\",\"uptimeMs\":%lu,\"sentAt\":%lu,"
             "\"droppedRecords\":%lu,\"evictedUnsent\":%lu,\"records\":[",
             HOST_NAME,
             WiFi.macAddress().c_str(),
             (unsigned long)diagBootId(),
             (unsigned long)diagBootCount(),
             __DATE__, __TIME__,
             (unsigned long)millis(),
             (unsigned long)(nowEpoch > 1700000000 ? nowEpoch : 0),
             (unsigned long)(ls.droppedOverflow + ls.droppedLock),
             (unsigned long)ls.evictedUnsent);

    body = head;
    countOut = logAppendPendingJson(body, LOG_UPLOAD_MAX_RECORDS, LOG_UPLOAD_MAX_BYTES, lastSeqOut);
    body += "]}";
  }

  void noteFailure(int code, const char *what)
  {
    ++gFailedBatches;
    gLastHttpCode = code;
    gBackendReachable = false;

    gBackoffMs = gBackoffMs == 0 ? LOG_UPLOAD_BACKOFF_MIN_MS
                                 : min<uint32_t>(gBackoffMs * 2, LOG_UPLOAD_BACKOFF_MAX_MS);
    gNextAttemptMs = millis() + gBackoffMs;

    logWriteSerialOnly(RLOG_WARN, TAG, "upload failed (%s, code=%d), retry in %lu ms",
                       what, code, (unsigned long)gBackoffMs);

    if (!gFailureLogged)
    {
      gFailureLogged = true;
      gFailingSinceMs = millis();
      RLOGW(TAG, "backend unreachable (%s, code=%d) — buffering logs in RAM", what, code);
    }
  }

  void noteSuccess(uint16_t records, uint32_t lastSeq)
  {
    logMarkSent(lastSeq);
    gSentRecords += records;
    ++gSentBatches;
    gBackoffMs = 0;
    gNextAttemptMs = 0;
    gLastSuccessMs = millis();
    gBackendReachable = true;

    if (gFailureLogged)
    {
      const uint32_t outage = millis() - gFailingSinceMs;
      gFailureLogged = false;
      RLOGI(TAG, "backend reachable again after %lu ms, %lu batches had failed",
            (unsigned long)outage, (unsigned long)gFailedBatches);
    }
  }

  bool sendOneBatch(const char *url)
  {
    String body;
    uint16_t count = 0;
    uint32_t lastSeq = 0;
    buildBody(body, count, lastSeq);
    if (count == 0)
      return false;

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(LOG_HTTP_CONNECT_TIMEOUT_MS);
    http.setTimeout(LOG_HTTP_TIMEOUT_MS);
    http.setReuse(false);

    if (!http.begin(client, url))
    {
      noteFailure(0, "bad_url");
      return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device", HOST_NAME);

    const int code = http.POST(body);
    http.end();

    if (code < 200 || code >= 300)
    {
      noteFailure(code, code < 0 ? "transport" : "http");
      return false;
    }

    gLastHttpCode = code;
    noteSuccess(count, lastSeq);
    logWriteSerialOnly(RLOG_DEBUG, TAG, "sent %u records (seq<=%lu, %u B)",
                       (unsigned)count, (unsigned long)lastSeq, (unsigned)body.length());

    return count >= LOG_UPLOAD_MAX_RECORDS;
  }

  void uploaderTask(void *)
  {
    for (;;)
    {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LOG_UPLOAD_INTERVAL_MS));

      char url[sizeof(gUrl)];
      copyUrl(url, sizeof(url));
      if (url[0] == '\0')
        continue;

      if (WiFi.status() != WL_CONNECTED)
      {
        gBackendReachable = false;
        continue;
      }

      const uint32_t now = millis();
      if (gNextAttemptMs != 0 && (int32_t)(now - gNextAttemptMs) < 0)
        continue;

      for (uint8_t i = 0; i < MAX_BATCHES_PER_WAKE; ++i)
      {
        if (!sendOneBatch(url))
          break;
      }
    }
  }
}

void logUploaderInit()
{
  gUrlMutex = xSemaphoreCreateMutex();
  if (gUrlMutex == nullptr)
  {
    RLOGE(TAG, "uploader mutex alloc failed — logs stay in RAM only");
    return;
  }
  strlcpy(gUrl, LOG_BACKEND_URL, sizeof(gUrl));

  if (xTaskCreatePinnedToCore(uploaderTask, "logup", LOG_UPLOADER_STACK, nullptr, 1, &gTask, 0) != pdPASS)
  {
    gTask = nullptr;
    RLOGE(TAG, "uploader task failed to start — logs stay in RAM only");
    return;
  }

  if (gUrl[0] == '\0')
    RLOGW(TAG, "no backend URL configured — logs stay in the RAM ring (set LOG_BACKEND_URL or use `url <addr>`)");
  else
    RLOGI(TAG, "uploader ready, backend=%s, every %lu ms", gUrl, (unsigned long)LOG_UPLOAD_INTERVAL_MS);
}

void logUploaderRequestFlush()
{
  if (gTask != nullptr)
    xTaskNotifyGive(gTask);
}

void logUploaderSetUrl(const char *url)
{
  if (gUrlMutex == nullptr)
    return;
  if (xSemaphoreTake(gUrlMutex, pdMS_TO_TICKS(200)) != pdTRUE)
    return;
  strlcpy(gUrl, url != nullptr ? url : "", sizeof(gUrl));
  xSemaphoreGive(gUrlMutex);

  gBackoffMs = 0;
  gNextAttemptMs = 0;
  gFailureLogged = false;
  RLOGI(TAG, "backend URL set to \"%s\"", url != nullptr && url[0] != '\0' ? url : "(disabled)");
  logUploaderRequestFlush();
}

void logUploaderGetUrl(char *out, size_t outSize)
{
  if (out == nullptr || outSize == 0)
    return;
  copyUrl(out, outSize);
}

LogUploadStats logUploaderStats()
{
  LogUploadStats s{};
  char url[sizeof(gUrl)];
  copyUrl(url, sizeof(url));
  s.enabled = url[0] != '\0';
  s.backendReachable = gBackendReachable;
  s.sentRecords = gSentRecords;
  s.sentBatches = gSentBatches;
  s.failedBatches = gFailedBatches;
  s.lastHttpCode = gLastHttpCode;
  s.lastSuccessMs = gLastSuccessMs;
  s.backoffMs = gBackoffMs;
  return s;
}
