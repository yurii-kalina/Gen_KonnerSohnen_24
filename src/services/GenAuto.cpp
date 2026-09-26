#include "GenAuto.h"
#include <Arduino.h>
#include "config/config.h"
#include "drivers/ext_bat_remote.h"
#include "drivers/sensor_cache.h"
#include "generator/generator_ops.h"
#include "generator/generator_state.h"
#include "services/EEPROMHandler.h"
#include "services/logger.h"
#include <math.h>

static const char *const TAG = "auto";

static constexpr uint32_t GEN_AUTO_TICK_MS = 1000;
// How long the voltage must stay in a zone before R1 reacts
static constexpr uint32_t GEN_AUTO_CONFIRM_MS = 10000;

namespace
{
  enum class Zone : uint8_t
  {
    Unknown,
    Low,
    Normal,
    High
  };

  float batMinV = GEN_AUTO_BAT_MIN_V;
  float batMaxV = GEN_AUTO_BAT_MAX_V;
  Zone zone = Zone::Unknown;      // зона, на яку вже відреагували
  Zone candidate = Zone::Unknown; // зона, в якій напруга зараз
  unsigned long candidateSinceMs = 0;
  unsigned long dataLostSinceMs = 0;
  unsigned long lastTickMs = 0;
  bool tickedOnce = false;

  Zone zoneFor(float v)
  {
    if (v <= batMinV)
      return Zone::Low;
    if (v >= batMaxV)
      return Zone::High;
    return Zone::Normal;
  }

  const char *zoneName(Zone z)
  {
    switch (z)
    {
    case Zone::Low: return "low";
    case Zone::Normal: return "normal";
    case Zone::High: return "high";
    default: return "unknown";
    }
  }

  bool isAutoMode(GenMode m)
  {
    return m == GenMode::Local || m == GenMode::Remote;
  }

  bool readVoltage(GenMode m, float &v)
  {
    if (m == GenMode::Local)
    {
      const SensorSnapshot sn = getSensorSnapshot();
      v = sn.batVoltage;
      return sensorChannelValid(sn.batLastSuccessMs, millis());
    }
    if (m == GenMode::Remote)
    {
      const RemoteBatReading r = getRemoteBatReading();
      v = r.voltage;
      return r.valid;
    }
    return false;
  }
}

void initGenAuto()
{
  if (!loadEepromGenBatThresholds(batMinV, batMaxV))
  {
    batMinV = GEN_AUTO_BAT_MIN_V;
    batMaxV = GEN_AUTO_BAT_MAX_V;
  }
  resetGenAuto();
  RLOGI(TAG, "thresholds min=%.2f V, max=%.2f V", batMinV, batMaxV);
}

void resetGenAuto()
{
  zone = Zone::Unknown;
  candidate = Zone::Unknown;
  dataLostSinceMs = 0;
}

void updateGenAuto()
{
  const unsigned long now = millis();
  if (tickedOnce && (now - lastTickMs) < GEN_AUTO_TICK_MS)
    return;
  lastTickMs = now;
  tickedOnce = true;

  const GenMode mode = getGenMode();
  if (!isAutoMode(mode))
    return;

  float v = 0.0f;
  if (!readVoltage(mode, v))
  {
    candidate = Zone::Unknown;
    if (dataLostSinceMs == 0)
    {
      dataLostSinceMs = now;
      RLOGW(TAG, "no voltage data from %s source — waiting, R1 stays %s (generator %s)",
            genModeName(mode), isRunCommanded() ? "closed" : "open", generatorRun ? "running" : "stopped");
    }
    return;
  }
  if (dataLostSinceMs != 0)
  {
    RLOGI(TAG, "voltage data back after %lu s: %.2f V", (unsigned long)((now - dataLostSinceMs) / 1000UL), v);
    dataLostSinceMs = 0;
  }

  const Zone current = zoneFor(v);
  if (current != candidate)
  {
    candidate = current;
    candidateSinceMs = now;
    return;
  }
  if (current == zone || now - candidateSinceMs < GEN_AUTO_CONFIRM_MS)
    return;

  // Напруга стабільно в новій зоні — реагуємо один раз
  if (current == Zone::Low)
  {
    RLOGI(TAG, "%.2f V <= min %.2f V -> start (R1 close)", v, batMinV);
    setRunCommand(true);
  }
  else if (current == Zone::High)
  {
    RLOGI(TAG, "%.2f V >= max %.2f V -> stop (R1 open)", v, batMaxV);
    setRunCommand(false);
  }
  else
  {
    RLOGD(TAG, "%.2f V between thresholds, R1 unchanged", v);
  }
  zone = current;
}

bool setGenAutoThresholds(float minV, float maxV)
{
  if (!isfinite(minV) || !isfinite(maxV) || minV < 0.0f || maxV > GEN_AUTO_BAT_LIMIT_V || minV >= maxV)
    return false;
  if (minV == batMinV && maxV == batMaxV)
    return true;
  RLOGI(TAG, "thresholds %.2f/%.2f V -> %.2f/%.2f V", batMinV, batMaxV, minV, maxV);
  batMinV = minV;
  batMaxV = maxV;
  saveEepromGenBatThresholds(minV, maxV);
  resetGenAuto(); // зони змінились — оцінити напругу заново
  return true;
}

float getGenAutoMinV()
{
  return batMinV;
}

float getGenAutoMaxV()
{
  return batMaxV;
}

GenAutoStatus getGenAutoStatus()
{
  const GenMode mode = getGenMode();
  GenAutoStatus s{};
  s.active = isAutoMode(mode);
  s.minV = batMinV;
  s.maxV = batMaxV;
  s.voltageValid = s.active && readVoltage(mode, s.voltage);
  if (!s.voltageValid)
    s.voltage = 0.0f;
  s.zone = zoneName(zone);
  s.dataLostSec = dataLostSinceMs != 0 ? (millis() - dataLostSinceMs) / 1000UL : 0;
  return s;
}
