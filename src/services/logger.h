#pragma once
#include <Arduino.h>
#include "config/config.h"

constexpr uint8_t LOG_MSG_MAX = 140;
constexpr uint16_t LOG_RING_SLOTS = 200;
constexpr uint32_t LOG_MUTEX_WAIT_MS = 50;
static_assert(LOG_MSG_MAX >= 64, "LOG_MSG_MAX under 64 truncates most diagnostic lines");

enum RLogLevel : uint8_t
{
  RLOG_DEBUG = 0,
  RLOG_INFO = 1,
  RLOG_WARN = 2,
  RLOG_ERROR = 3,
  RLOG_CRIT = 4,
  RLOG_LEVEL_COUNT = 5
};

struct LogRecord
{
  uint32_t seq;
  uint32_t uptimeMs;
  uint32_t epoch;
  const char *tag;
  uint8_t level;
  char msg[LOG_MSG_MAX];
};

struct LogRingStats
{
  uint16_t stored;
  uint16_t pending;
  uint16_t capacity;
  uint32_t produced;
  uint32_t droppedOverflow;
  uint32_t droppedLock;
  uint32_t evictedUnsent;
  uint32_t lastSentSeq;
};

void logInit();
bool logIsReady();

void logWrite(uint8_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

void logWriteSerialOnly(uint8_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

void logSetSerialLevel(uint8_t level);
void logSetNetLevel(uint8_t level);
uint8_t logGetSerialLevel();
uint8_t logGetNetLevel();
const char *logLevelName(uint8_t level);
bool logLevelFromName(const char *name, uint8_t &out);

LogRingStats logGetStats();
void logClear();

uint16_t logAppendPendingJson(String &out, uint16_t maxRecords, uint16_t maxBytes,
                              uint32_t &lastSeqOut);
void logMarkSent(uint32_t seq);

uint16_t logAppendRecentJson(String &out, uint16_t maxRecords, uint8_t minLevel);
void logPrintRecent(uint16_t maxRecords, uint8_t minLevel);

#define RLOGD(tag, fmt, ...) logWrite(RLOG_DEBUG, tag, fmt, ##__VA_ARGS__)
#define RLOGI(tag, fmt, ...) logWrite(RLOG_INFO, tag, fmt, ##__VA_ARGS__)
#define RLOGW(tag, fmt, ...) logWrite(RLOG_WARN, tag, fmt, ##__VA_ARGS__)
#define RLOGE(tag, fmt, ...) logWrite(RLOG_ERROR, tag, fmt, ##__VA_ARGS__)
#define RLOGC(tag, fmt, ...) logWrite(RLOG_CRIT, tag, fmt, ##__VA_ARGS__)

#define RLOG_EVERY(periodMs, level, tag, fmt, ...)                    \
  do                                                                  \
  {                                                                   \
    static uint32_t _rlogLastMs = 0;                                  \
    static bool _rlogFired = false;                                   \
    const uint32_t _rlogNow = millis();                               \
    if (!_rlogFired || (_rlogNow - _rlogLastMs) >= (uint32_t)(periodMs)) \
    {                                                                 \
      _rlogFired = true;                                              \
      _rlogLastMs = _rlogNow;                                         \
      logWrite((level), (tag), fmt, ##__VA_ARGS__);                   \
    }                                                                 \
  } while (0)
