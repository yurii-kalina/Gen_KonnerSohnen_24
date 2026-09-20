#include "GenAuto.h"
#include <Arduino.h>
#include "config/config.h"
#include "drivers/sensor_cache.h"
#include "generator/generator_ops.h"
#include "services/EEPROMHandler.h"
#include <math.h>

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
}

void initGenAuto()
{
  if (!loadEepromGenBatThresholds(batMinV, batMaxV))
  {
    batMinV = GEN_AUTO_BAT_MIN_V;
    batMaxV = GEN_AUTO_BAT_MAX_V;
  }
  resetGenAuto();
}

void resetGenAuto()
{
  zone = Zone::Unknown;
  candidate = Zone::Unknown;
}

void updateGenAuto()
{
  const unsigned long now = millis();
  if (tickedOnce && (now - lastTickMs) < GEN_AUTO_TICK_MS)
    return;
  lastTickMs = now;
  tickedOnce = true;

  if (getGenMode() != GenMode::Voltage)
    return;

  const SensorSnapshot sn = getSensorSnapshot();
  if (!sensorChannelValid(sn.batLastSuccessMs, now))
  {
    candidate = Zone::Unknown;
    return;
  }

  const Zone current = zoneFor(sn.batVoltage);
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
    setRunCommand(true); // R1 замкнути й тримати
  else if (current == Zone::High)
    setRunCommand(false); // R1 розімкнути
  zone = current;
}

bool setGenAutoThresholds(float minV, float maxV)
{
  if (!isfinite(minV) || !isfinite(maxV) || minV < 0.0f || maxV > GEN_AUTO_BAT_LIMIT_V || minV >= maxV)
    return false;
  if (minV == batMinV && maxV == batMaxV)
    return true;
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
