#include "diagnostics.h"

#include <WiFi.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <time.h>

#include "config/config.h"
#include "services/EEPROMHandler.h"
#include "services/logger.h"

namespace
{
  const char *const TAG = "diag";

  constexpr uint32_t LOG_LOOP_STALL_WARN_MS = 3000;
  constexpr uint32_t LOG_HEAP_WARN_BYTES = 40000;
  constexpr uint32_t LOG_HEAP_CRIT_BYTES = 20000;
  constexpr uint32_t LOG_HEAP_RECOVER_BYTES = 6000;

  uint32_t gBootId = 0;
  uint32_t gBootCount = 0;
  uint8_t gResetReason = ESP_RST_UNKNOWN;
  uint8_t gPrevResetReason = ESP_RST_UNKNOWN;

  uint32_t gLastUpdateMs = 0;
  uint32_t gLastSlowCheckMs = 0;
  uint32_t gMinFreeHeap = UINT32_MAX;

  bool gHeapWarned = false;
  bool gHeapCritWarned = false;
  bool gTimeSynced = false;

  const char *resetReasonName(uint8_t reason)
  {
    switch (reason)
    {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_EXT: return "ext_pin";
    case ESP_RST_SW: return "sw_restart";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "other_wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
    }
  }

  bool isFaultReset(uint8_t reason)
  {
    return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
           reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
           reason == ESP_RST_BROWNOUT;
  }

  void watchHeap()
  {
    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < gMinFreeHeap)
      gMinFreeHeap = freeHeap;

    if (!gHeapCritWarned && freeHeap < LOG_HEAP_CRIT_BYTES)
    {
      gHeapCritWarned = true;
      gHeapWarned = true;
      RLOGC(TAG, "heap critical: %lu B free (largest block %lu B)",
            (unsigned long)freeHeap, (unsigned long)ESP.getMaxAllocHeap());
    }
    else if (!gHeapWarned && freeHeap < LOG_HEAP_WARN_BYTES)
    {
      gHeapWarned = true;
      RLOGW(TAG, "heap low: %lu B free (min since boot %lu B)",
            (unsigned long)freeHeap, (unsigned long)gMinFreeHeap);
    }
    else if (gHeapWarned && freeHeap > LOG_HEAP_WARN_BYTES + LOG_HEAP_RECOVER_BYTES)
    {
      gHeapWarned = false;
      gHeapCritWarned = false;
      RLOGI(TAG, "heap recovered: %lu B free", (unsigned long)freeHeap);
    }
  }

  void watchLoopStall(uint32_t now)
  {
    if (gLastUpdateMs != 0)
    {
      const uint32_t gap = now - gLastUpdateMs;
      if (gap > LOG_LOOP_STALL_WARN_MS)
      {
        RLOGW(TAG, "loop stalled %lu ms (budget %lu ms) — check blocking calls",
              (unsigned long)gap, (unsigned long)LOG_LOOP_STALL_WARN_MS);
      }
    }
    gLastUpdateMs = now;
  }

  void watchLogLoss()
  {
    static uint32_t lastReportedLoss = 0;
    static uint32_t lastReportMs = 0;

    const LogRingStats ls = logGetStats();
    const uint32_t lost = ls.droppedOverflow + ls.droppedLock + ls.evictedUnsent;
    if (lost == lastReportedLoss)
      return;

    const uint32_t now = millis();
    if (lastReportMs != 0 && (now - lastReportMs) < 300000UL)
      return;

    lastReportMs = now;
    lastReportedLoss = lost;
    RLOGW(TAG, "log ring losing records: dropped=%lu/%lu evicted_unsent=%lu (stored=%u pending=%u)",
          (unsigned long)ls.droppedOverflow, (unsigned long)ls.droppedLock,
          (unsigned long)ls.evictedUnsent,
          (unsigned)ls.stored, (unsigned)ls.pending);
  }

}

void diagInit()
{
  gResetReason = (uint8_t)esp_reset_reason();
  bumpEepromBootRecord(gResetReason, gBootCount, gPrevResetReason);

  gBootId = (gBootCount << 12) | (esp_random() & 0xFFF);
  if (gBootId == 0)
    gBootId = 1;

  gMinFreeHeap = ESP.getFreeHeap();
  gLastUpdateMs = 0;
  gLastSlowCheckMs = 0;
}

void diagLogBootReport()
{
  const bool fault = isFaultReset(gResetReason);
  logWrite(fault ? RLOG_CRIT : RLOG_INFO, TAG,
           "boot #%lu id=%08lX reset=%s (prev boot ended as %s)",
           (unsigned long)gBootCount,
           (unsigned long)gBootId,
           resetReasonName(gResetReason),
           resetReasonName(gPrevResetReason));

  if (gResetReason == ESP_RST_BROWNOUT)
  {
    RLOGC(TAG, "brownout reset — supply sagged below the detector threshold "
               "(check the controller supply)");
  }
  else if (gResetReason == ESP_RST_TASK_WDT || gResetReason == ESP_RST_INT_WDT)
  {
    RLOGC(TAG, "watchdog reset — loop() or an ISR did not yield in time");
  }
  else if (gResetReason == ESP_RST_PANIC)
  {
    RLOGC(TAG, "panic reset — check the core dump / decoded backtrace on USB");
  }

  RLOGI(TAG, "fw built %s %s, sdk=%s, chip=%s rev%d %luMHz cores=%d",
        __DATE__, __TIME__, ESP.getSdkVersion(), ESP.getChipModel(),
        (int)ESP.getChipRevision(), (unsigned long)ESP.getCpuFreqMHz(),
        (int)ESP.getChipCores());

  RLOGD(TAG, "heap=%lu B, flash=%luKB, sketch=%luKB/%luKB free, mac=%s",
        (unsigned long)ESP.getFreeHeap(),
        (unsigned long)(ESP.getFlashChipSize() / 1024UL),
        (unsigned long)(ESP.getSketchSize() / 1024UL),
        (unsigned long)(ESP.getFreeSketchSpace() / 1024UL),
        WiFi.macAddress().c_str());
}

void diagUpdate()
{
  const uint32_t now = millis();

  watchLoopStall(now);

  if (gLastSlowCheckMs != 0 && (now - gLastSlowCheckMs) < 1000UL)
    return;
  gLastSlowCheckMs = now;

  watchHeap();
  watchLogLoss();

  if (!gTimeSynced && time(nullptr) > 1700000000)
    diagOnTimeSynced();
}

void diagOnTimeSynced()
{
  if (gTimeSynced)
    return;
  gTimeSynced = true;

  const time_t t = time(nullptr);
  struct tm tmUtc;
  gmtime_r(&t, &tmUtc);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmUtc);
  RLOGI(TAG, "clock synced: %s UTC (epoch %lu) at uptime %lus",
        buf, (unsigned long)t, (unsigned long)(millis() / 1000UL));
}

uint32_t diagBootId() { return gBootId; }
uint32_t diagBootCount() { return gBootCount; }
const char *diagResetReasonName() { return resetReasonName(gResetReason); }
const char *diagPrevResetReasonName() { return resetReasonName(gPrevResetReason); }
uint32_t diagMinFreeHeap() { return gMinFreeHeap; }

const char *diagWifiStatusName()
{
  switch (WiFi.status())
  {
  case WL_CONNECTED: return "connected";
  case WL_NO_SSID_AVAIL: return "no_ssid";
  case WL_CONNECT_FAILED: return "failed";
  case WL_CONNECTION_LOST: return "lost";
  case WL_DISCONNECTED: return "disconnected";
  case WL_IDLE_STATUS: return "idle";
  default: return "unknown";
  }
}
