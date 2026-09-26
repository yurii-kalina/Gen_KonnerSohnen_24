#include "serial_console.h"

#include <Arduino.h>
#include <time.h>

#include "config/config.h"
#include "services/diagnostics.h"
#include "services/log_uploader.h"
#include "services/logger.h"
#include "services/settings.h"
#include "api/json_views.h"
#include "services/WiFiHandler.h"

#if defined(SERIAL_CONSOLE_ENABLED) && defined(SERIAL_LOG_ENABLED)

namespace
{
  constexpr size_t CONSOLE_LINE_MAX = 96;
  char gLine[CONSOLE_LINE_MAX];
  size_t gLen = 0;

  uint8_t tokenize(char *line, char **argv, uint8_t maxArgs)
  {
    uint8_t argc = 0;
    char *p = line;
    while (*p != '\0' && argc < maxArgs)
    {
      while (*p == ' ' || *p == '\t')
        ++p;
      if (*p == '\0')
        break;
      argv[argc++] = p;
      while (*p != '\0' && *p != ' ' && *p != '\t')
        ++p;
      if (*p != '\0')
        *p++ = '\0';
    }
    return argc;
  }

  void printHelp()
  {
    Serial.println(F(
        "commands:\r\n"
        "  tail [n] [level]     last n ring records (default 30, level debug)\r\n"
        "  stats                ring / uploader / heap / clock counters\r\n"
        "  status               same as GET /status\r\n"
        "  level serial <lvl>   what reaches USB      (debug|info|warn|error|crit)\r\n"
        "  level net <lvl>      what enters the ring and goes to the backend\r\n"
        "  url [addr]           show or set the backend URL (until reboot)\r\n"
        "  flush                try to upload the pending queue now\r\n"
        "  clear                drop the ring contents\r\n"
        "  wifi                 show WiFi credentials source\r\n"
        "  wifi reset           back to config.h credentials and reconnect\r\n"
        "  reboot               restart the board"));
  }

  void cmdStats()
  {
    const LogRingStats ls = logGetStats();
    const LogUploadStats us = logUploaderStats();

    Serial.printf("ring     : stored=%u/%u pending=%u produced=%lu\r\n",
                  (unsigned)ls.stored, (unsigned)ls.capacity, (unsigned)ls.pending,
                  (unsigned long)ls.produced);
    Serial.printf("ring lost: overflow=%lu lock=%lu evicted_unsent=%lu (sent up to seq %lu)\r\n",
                  (unsigned long)ls.droppedOverflow, (unsigned long)ls.droppedLock,
                  (unsigned long)ls.evictedUnsent, (unsigned long)ls.lastSentSeq);

    char url[160];
    logUploaderGetUrl(url, sizeof(url));
    Serial.printf("uploader : %s reachable=%d batches=%lu ok / %lu failed, records=%lu\r\n",
                  url[0] != '\0' ? url : "(disabled)",
                  (int)us.backendReachable,
                  (unsigned long)us.sentBatches, (unsigned long)us.failedBatches,
                  (unsigned long)us.sentRecords);
    Serial.printf("uploader : lastHttpCode=%d backoff=%lu ms lastSuccess=%s\r\n",
                  us.lastHttpCode, (unsigned long)us.backoffMs,
                  us.lastSuccessMs != 0 ? String((millis() - us.lastSuccessMs) / 1000UL).c_str() : "never");

    Serial.printf("levels   : serial=%s net=%s\r\n",
                  logLevelName(logGetSerialLevel()), logLevelName(logGetNetLevel()));
    Serial.printf("board    : boot #%lu id=%08lX reset=%s (prev %s)\r\n",
                  (unsigned long)diagBootCount(), (unsigned long)diagBootId(),
                  diagResetReasonName(), diagPrevResetReasonName());
    Serial.printf("memory   : heap=%lu B min=%lu B uptime=%lu s\r\n",
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)diagMinFreeHeap(),
                  (unsigned long)(millis() / 1000UL));

    const time_t t = time(nullptr);
    if (t > 1700000000)
    {
      struct tm tmUtc;
      gmtime_r(&t, &tmUtc);
      char buf[32];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmUtc);
      Serial.printf("clock    : %s UTC\r\n", buf);
    }
    else
    {
      Serial.println(F("clock    : not synced (records carry uptime only)"));
    }
  }

  void cmdStatus()
  {
    JsonDocument doc;
    fillStatus(doc);
    serializeJsonPretty(doc, Serial);
    Serial.println();
  }

  void cmdLevel(uint8_t argc, char **argv)
  {
    if (argc < 3)
    {
      Serial.println(F("usage: level serial|net <debug|info|warn|error|crit>"));
      return;
    }
    uint8_t lvl;
    if (!logLevelFromName(argv[2], lvl))
    {
      Serial.printf("unknown level \"%s\"\r\n", argv[2]);
      return;
    }
    if (strcmp(argv[1], "serial") == 0)
    {
      logSetSerialLevel(lvl);
      Serial.printf("serial level = %s\r\n", logLevelName(lvl));
    }
    else if (strcmp(argv[1], "net") == 0)
    {
      logSetNetLevel(lvl);
      Serial.printf("net level = %s\r\n", logLevelName(lvl));
    }
    else
    {
      Serial.println(F("usage: level serial|net <level>"));
    }
  }

  void cmdUrl(uint8_t argc, char **argv)
  {
    if (argc < 2)
    {
      char url[160];
      logUploaderGetUrl(url, sizeof(url));
      Serial.printf("backend url = %s\r\n", url[0] != '\0' ? url : "(disabled)");
      return;
    }
    const bool disable = strcmp(argv[1], "off") == 0 || strcmp(argv[1], "-") == 0;
    logUploaderSetUrl(disable ? "" : argv[1]);
  }

  void cmdTail(uint8_t argc, char **argv)
  {
    uint16_t n = 30;
    uint8_t minLevel = RLOG_DEBUG;
    if (argc >= 2)
    {
      const long parsed = strtol(argv[1], nullptr, 10);
      if (parsed > 0)
        n = (uint16_t)min<long>(parsed, LOG_RING_SLOTS);
    }
    if (argc >= 3 && !logLevelFromName(argv[2], minLevel))
    {
      Serial.printf("unknown level \"%s\"\r\n", argv[2]);
      return;
    }
    logPrintRecent(n, minLevel);
  }

  void dispatch(char *line)
  {
    char *argv[4];
    const uint8_t argc = tokenize(line, argv, 4);
    if (argc == 0)
      return;

    if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0)
      printHelp();
    else if (strcmp(argv[0], "tail") == 0)
      cmdTail(argc, argv);
    else if (strcmp(argv[0], "stats") == 0)
      cmdStats();
    else if (strcmp(argv[0], "status") == 0)
      cmdStatus();
    else if (strcmp(argv[0], "level") == 0)
      cmdLevel(argc, argv);
    else if (strcmp(argv[0], "url") == 0)
      cmdUrl(argc, argv);
    else if (strcmp(argv[0], "flush") == 0)
    {
      logUploaderRequestFlush();
      Serial.println(F("flush requested"));
    }
    else if (strcmp(argv[0], "clear") == 0)
    {
      logClear();
      Serial.println(F("ring cleared"));
    }
    else if (strcmp(argv[0], "wifi") == 0)
    {
      if (argc >= 2 && strcmp(argv[1], "reset") == 0)
      {
        resetWifiCreds();
        wifiApplyCreds();
        Serial.println(F("WiFi reset to config.h — reconnecting"));
        return;
      }
      Serial.printf("wifi: \"%s\" from %s\r\n", wifiSsid(), wifiFromEeprom() ? "EEPROM" : "config.h");
    }
    else if (strcmp(argv[0], "reboot") == 0)
    {
      RLOGW("console", "reboot requested over USB console");
      Serial.flush();
      delay(100);
      ESP.restart();
    }
    else
    {
      Serial.printf("unknown command \"%s\" — type `help`\r\n", argv[0]);
    }
  }
}

void serialConsoleInit()
{
  gLen = 0;
  Serial.println(F("[console] USB console ready — type `help`"));
}

void serialConsoleUpdate()
{
  while (Serial.available() > 0)
  {
    const int c = Serial.read();
    if (c < 0)
      break;

    if (c == '\r' || c == '\n')
    {
      if (gLen > 0)
      {
        gLine[gLen] = '\0';
        gLen = 0;
        dispatch(gLine);
      }
      continue;
    }
    if (c == 8 || c == 127)
    {
      if (gLen > 0)
        --gLen;
      continue;
    }
    if (gLen < CONSOLE_LINE_MAX - 1)
      gLine[gLen++] = (char)c;
  }
}

#else

void serialConsoleInit() {}
void serialConsoleUpdate() {}

#endif
