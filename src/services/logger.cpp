#include "logger.h"

#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace
{
  constexpr bool LOG_SERIAL_COLOR = true;

  LogRecord gRing[LOG_RING_SLOTS];
  uint16_t gHead = 0;
  uint16_t gStored = 0;
  uint32_t gSeq = 0;
  uint32_t gSentSeq = 0;

  volatile uint32_t gDroppedOverflow = 0;
  volatile uint32_t gDroppedLock = 0;
  volatile uint32_t gEvictedUnsent = 0;

  SemaphoreHandle_t gRingMutex = nullptr;
  SemaphoreHandle_t gSerialMutex = nullptr;

  uint8_t gSerialLevel = LOG_DEFAULT_SERIAL_LEVEL;
  uint8_t gNetLevel = LOG_DEFAULT_NET_LEVEL;
  bool gReady = false;

  const char *const kLevelNames[RLOG_LEVEL_COUNT] = {"DEBUG", "INFO", "WARN", "ERROR", "CRIT"};
  const char kLevelChars[RLOG_LEVEL_COUNT] = {'D', 'I', 'W', 'E', 'C'};
  const char *const kLevelColors[RLOG_LEVEL_COUNT] = {
      "\033[90m", "\033[0m", "\033[33m", "\033[31m", "\033[97;41m"};

  inline uint16_t tailIndex()
  {
    return (uint16_t)((gHead + LOG_RING_SLOTS - gStored) % LOG_RING_SLOTS);
  }

  inline bool takeRing()
  {
    return gRingMutex != nullptr &&
           xSemaphoreTake(gRingMutex, pdMS_TO_TICKS(LOG_MUTEX_WAIT_MS)) == pdTRUE;
  }

  inline void giveRing()
  {
    if (gRingMutex != nullptr)
      xSemaphoreGive(gRingMutex);
  }

  inline uint32_t nowEpoch()
  {
    const time_t t = time(nullptr);
    return (t > 1700000000) ? (uint32_t)t : 0u;
  }

  void serialEmit(uint8_t level, const char *tag, const char *msg, uint32_t uptimeMs)
  {
#ifdef SERIAL_LOG_ENABLED
    const bool locked =
        gSerialMutex != nullptr &&
        xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(LOG_MUTEX_WAIT_MS)) == pdTRUE;

    if (LOG_SERIAL_COLOR)
      Serial.print(kLevelColors[level]);
    Serial.printf("[%6lu.%03lu] %c %-6s %s",
                  (unsigned long)(uptimeMs / 1000UL),
                  (unsigned long)(uptimeMs % 1000UL),
                  kLevelChars[level],
                  tag,
                  msg);
    if (LOG_SERIAL_COLOR)
      Serial.print("\033[0m");
    Serial.println();

    if (locked)
      xSemaphoreGive(gSerialMutex);
#else
    (void)level; (void)tag; (void)msg; (void)uptimeMs;
#endif
  }

  void appendJsonEscaped(String &out, const char *s)
  {
    for (const char *p = s; *p != '\0'; ++p)
    {
      const char c = *p;
      switch (c)
      {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20)
        {
          char esc[7];
          snprintf(esc, sizeof(esc), "\\u%04X", (unsigned)(uint8_t)c);
          out += esc;
        }
        else
        {
          out += c;
        }
      }
    }
  }

  void appendRecordJson(String &out, const LogRecord &r)
  {
    char head[96];
    snprintf(head, sizeof(head), "{\"seq\":%lu,\"up\":%lu,\"lvl\":\"%s\",\"tag\":\"%s\",\"msg\":\"",
             (unsigned long)r.seq,
             (unsigned long)r.uptimeMs,
             kLevelNames[r.level < RLOG_LEVEL_COUNT ? r.level : RLOG_INFO],
             r.tag != nullptr ? r.tag : "?");
    out += head;
    appendJsonEscaped(out, r.msg);
    out += '"';
    if (r.epoch != 0)
    {
      char ts[32];
      snprintf(ts, sizeof(ts), ",\"ts\":%lu", (unsigned long)r.epoch);
      out += ts;
    }
    out += '}';
  }

  void ringPush(uint8_t level, const char *tag, const char *msg, uint32_t uptimeMs, uint32_t epoch)
  {
    if (gStored == LOG_RING_SLOTS)
    {
      const LogRecord &oldest = gRing[tailIndex()];
      const bool oldestIsUnsentAlarm = (oldest.seq > gSentSeq) && (oldest.level >= RLOG_WARN);

      if (oldestIsUnsentAlarm && level < RLOG_WARN)
      {
        ++gDroppedOverflow;
        return;
      }
      if (oldest.seq > gSentSeq)
        ++gEvictedUnsent;
      --gStored;
    }

    LogRecord &slot = gRing[gHead];
    slot.seq = ++gSeq;
    slot.uptimeMs = uptimeMs;
    slot.epoch = epoch;
    slot.tag = tag;
    slot.level = level;
    strlcpy(slot.msg, msg, sizeof(slot.msg));

    gHead = (uint16_t)((gHead + 1) % LOG_RING_SLOTS);
    ++gStored;
  }

  void writeImpl(uint8_t level, const char *tag, bool toRing, const char *fmt, va_list ap)
  {
    if (level >= RLOG_LEVEL_COUNT)
      level = RLOG_INFO;
    if (tag == nullptr)
      tag = "?";

    const bool wantSerial = level >= gSerialLevel;
    const bool wantRing = toRing && level >= gNetLevel;
    if (!wantSerial && !wantRing)
      return;

    char msg[LOG_MSG_MAX];
    vsnprintf(msg, sizeof(msg), fmt, ap);

    const uint32_t uptimeMs = millis();

    if (wantSerial)
      serialEmit(level, tag, msg, uptimeMs);

    if (!wantRing)
      return;

    if (!takeRing())
    {
      ++gDroppedLock;
      return;
    }
    ringPush(level, tag, msg, uptimeMs, nowEpoch());
    giveRing();
  }
}

void logInit()
{
#ifdef SERIAL_LOG_ENABLED
  Serial.begin(115200);
  delay(200);
  Serial.println();
#endif
  gRingMutex = xSemaphoreCreateMutex();
  gSerialMutex = xSemaphoreCreateMutex();
  gSerialLevel = LOG_DEFAULT_SERIAL_LEVEL;
  gNetLevel = LOG_DEFAULT_NET_LEVEL;
  gReady = gRingMutex != nullptr;

#ifdef SERIAL_LOG_ENABLED
  if (!gReady)
  {
    Serial.println("[log] FATAL: ring mutex alloc failed, USB-only logging");
  }
#endif
}

bool logIsReady() { return gReady; }

void logWrite(uint8_t level, const char *tag, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  writeImpl(level, tag, true, fmt, ap);
  va_end(ap);
}

void logWriteSerialOnly(uint8_t level, const char *tag, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  writeImpl(level, tag, false, fmt, ap);
  va_end(ap);
}

void logSetSerialLevel(uint8_t level)
{
  if (level < RLOG_LEVEL_COUNT)
    gSerialLevel = level;
}

void logSetNetLevel(uint8_t level)
{
  if (level < RLOG_LEVEL_COUNT)
    gNetLevel = level;
}

uint8_t logGetSerialLevel() { return gSerialLevel; }
uint8_t logGetNetLevel() { return gNetLevel; }

const char *logLevelName(uint8_t level)
{
  return level < RLOG_LEVEL_COUNT ? kLevelNames[level] : "?";
}

bool logLevelFromName(const char *name, uint8_t &out)
{
  if (name == nullptr || name[0] == '\0')
    return false;
  if (name[1] == '\0' && name[0] >= '0' && name[0] <= '4')
  {
    out = (uint8_t)(name[0] - '0');
    return true;
  }
  const char c = (char)tolower((unsigned char)name[0]);
  switch (c)
  {
  case 'd': out = RLOG_DEBUG; return true;
  case 'i': out = RLOG_INFO; return true;
  case 'w': out = RLOG_WARN; return true;
  case 'e': out = RLOG_ERROR; return true;
  case 'c': out = RLOG_CRIT; return true;
  default: return false;
  }
}

LogRingStats logGetStats()
{
  LogRingStats s{};
  s.capacity = LOG_RING_SLOTS;
  s.droppedOverflow = gDroppedOverflow;
  s.droppedLock = gDroppedLock;
  s.evictedUnsent = gEvictedUnsent;

  if (!takeRing())
    return s;
  s.stored = gStored;
  s.produced = gSeq;
  s.lastSentSeq = gSentSeq;
  const uint32_t unsent = gSeq > gSentSeq ? (gSeq - gSentSeq) : 0;
  s.pending = (uint16_t)(unsent < gStored ? unsent : gStored);
  giveRing();
  return s;
}

void logClear()
{
  if (!takeRing())
    return;
  gStored = 0;
  gHead = 0;
  gSentSeq = gSeq;
  giveRing();
}

uint16_t logAppendPendingJson(String &out, uint16_t maxRecords, uint16_t maxBytes,
                              uint32_t &lastSeqOut)
{
  lastSeqOut = 0;
  if (!takeRing())
    return 0;

  const uint16_t startLen = (uint16_t)out.length();
  uint16_t appended = 0;
  uint16_t idx = tailIndex();
  for (uint16_t i = 0; i < gStored && appended < maxRecords; ++i, idx = (uint16_t)((idx + 1) % LOG_RING_SLOTS))
  {
    const LogRecord &r = gRing[idx];
    if (r.seq <= gSentSeq)
      continue;

    const uint16_t before = (uint16_t)out.length();
    if (appended > 0)
      out += ',';
    appendRecordJson(out, r);

    if (appended > 0 && (uint16_t)(out.length() - startLen) > maxBytes)
    {
      out.remove(before);
      break;
    }
    lastSeqOut = r.seq;
    ++appended;
  }

  giveRing();
  return appended;
}

void logMarkSent(uint32_t seq)
{
  if (!takeRing())
    return;
  if (seq > gSentSeq)
    gSentSeq = seq;
  giveRing();
}

uint16_t logAppendRecentJson(String &out, uint16_t maxRecords, uint8_t minLevel)
{
  if (!takeRing())
    return 0;

  uint16_t matching = 0;
  uint16_t idx = tailIndex();
  for (uint16_t i = 0; i < gStored; ++i, idx = (uint16_t)((idx + 1) % LOG_RING_SLOTS))
  {
    if (gRing[idx].level >= minLevel)
      ++matching;
  }
  uint16_t skip = matching > maxRecords ? (uint16_t)(matching - maxRecords) : 0;

  uint16_t appended = 0;
  idx = tailIndex();
  for (uint16_t i = 0; i < gStored; ++i, idx = (uint16_t)((idx + 1) % LOG_RING_SLOTS))
  {
    const LogRecord &r = gRing[idx];
    if (r.level < minLevel)
      continue;
    if (skip > 0)
    {
      --skip;
      continue;
    }
    if (appended > 0)
      out += ',';
    appendRecordJson(out, r);
    ++appended;
  }

  giveRing();
  return appended;
}

void logPrintRecent(uint16_t maxRecords, uint8_t minLevel)
{
#ifdef SERIAL_LOG_ENABLED
  if (!takeRing())
  {
    Serial.println("[log] ring busy, try again");
    return;
  }
  uint16_t matching = 0;
  uint16_t idx = tailIndex();
  for (uint16_t i = 0; i < gStored; ++i, idx = (uint16_t)((idx + 1) % LOG_RING_SLOTS))
  {
    if (gRing[idx].level >= minLevel)
      ++matching;
  }
  const uint16_t total = gStored;
  const uint16_t start = tailIndex();
  giveRing();

  uint16_t skip = matching > maxRecords ? (uint16_t)(matching - maxRecords) : 0;
  uint16_t printed = 0;

  for (uint16_t i = 0; i < total && printed < maxRecords; ++i)
  {
    LogRecord copy;
    if (!takeRing())
      break;
    copy = gRing[(start + i) % LOG_RING_SLOTS];
    giveRing();

    if (copy.level < minLevel)
      continue;
    if (skip > 0)
    {
      --skip;
      continue;
    }
    serialEmit(copy.level, copy.tag != nullptr ? copy.tag : "?", copy.msg, copy.uptimeMs);
    ++printed;
  }

  if (printed == 0)
    Serial.println("[log] no records match");
#else
  (void)maxRecords; (void)minLevel;
#endif
}
