#include "ext_bat_remote.h"

#include <math.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "services/logger.h"
#include "services/settings.h"

static const char *const TAG = "remote";

static constexpr uint32_t REMOTE_POLL_MS = 5000;
static constexpr uint32_t REMOTE_STALE_MS = 30000;
static constexpr uint16_t REMOTE_CONNECT_TIMEOUT_MS = 2000;
static constexpr uint16_t REMOTE_TIMEOUT_MS = 3000;
static constexpr uint16_t REMOTE_TASK_STACK = 6144;

namespace
{
  SemaphoreHandle_t gMutex = nullptr;
  float gVoltage = 0.0f;
  uint32_t gLastSuccessMs = 0;
  int gLastHttpCode = 0;

  // GET http://<IP>/status -> {"voltage": 25.3, ...}; other fields are ignored
  bool poll(float &voltage, int &code)
  {
    char url[48];
    snprintf(url, sizeof(url), "http://%s/status", remoteModuleIp().toString().c_str());

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(REMOTE_CONNECT_TIMEOUT_MS);
    http.setTimeout(REMOTE_TIMEOUT_MS);
    if (!http.begin(client, url))
    {
      code = -1;
      return false;
    }
    code = http.GET();
    if (code != HTTP_CODE_OK)
    {
      http.end();
      return false;
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err || !doc["voltage"].is<float>())
    {
      code = -100;
      return false;
    }
    voltage = doc["voltage"].as<float>();
    return isfinite(voltage);
  }

  void remoteTask(void *)
  {
    bool loggedUp = false;
    bool loggedDown = false;
    for (;;)
    {
      if (!extBatRemoteEnabled() || WiFi.status() != WL_CONNECTED)
      {
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }

      float v = 0.0f;
      int code = 0;
      const bool ok = poll(v, code);
      const uint32_t now = millis();

      xSemaphoreTake(gMutex, portMAX_DELAY);
      gLastHttpCode = code;
      if (ok)
      {
        gVoltage = v;
        gLastSuccessMs = now;
      }
      xSemaphoreGive(gMutex);

      if (ok && !loggedUp)
      {
        RLOGI(TAG, "remote module %s answers: %.2f V", remoteModuleIp().toString().c_str(), v);
        loggedUp = true;
        loggedDown = false;
      }
      else if (!ok && !loggedDown)
      {
        RLOGW(TAG, "remote module %s not answering (code %d)", remoteModuleIp().toString().c_str(), code);
        loggedDown = true;
        loggedUp = false;
      }

      vTaskDelay(pdMS_TO_TICKS(REMOTE_POLL_MS));
    }
  }
}

bool initExtBatRemote()
{
  gMutex = xSemaphoreCreateMutex();
  if (gMutex == nullptr)
    return false;
  // Core 0 next to the sensor task: a slow HTTP reply never blocks loop()
  if (xTaskCreatePinnedToCore(remoteTask, "remote", REMOTE_TASK_STACK, nullptr, 1, nullptr, 0) != pdPASS)
  {
    vSemaphoreDelete(gMutex);
    gMutex = nullptr;
    return false;
  }
  return true;
}

RemoteBatReading getRemoteBatReading()
{
  RemoteBatReading r{false, 0.0f, 0, 0};
  if (gMutex == nullptr)
    return r;
  xSemaphoreTake(gMutex, portMAX_DELAY);
  r.voltage = gVoltage;
  r.lastSuccessMs = gLastSuccessMs;
  r.lastHttpCode = gLastHttpCode;
  xSemaphoreGive(gMutex);
  r.valid = extBatRemoteEnabled() && r.lastSuccessMs != 0 && millis() - r.lastSuccessMs < REMOTE_STALE_MS;
  return r;
}
