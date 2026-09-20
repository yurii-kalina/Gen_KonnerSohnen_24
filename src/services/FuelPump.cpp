#include "FuelPump.h"
#include "config/config.h"
#include "config/pins.h"
#include "drivers/sensor_cache.h"
#include "drivers/relays.h"

namespace
{
  // RAM only: after reboot auto mode is back to default and lockout is cleared
  bool pumpOn = false;
  bool autoEnabled = PUMP_AUTO_DEFAULT;
  unsigned long pumpStartedAtMs = 0;
  bool hasManualDeadline = false;
  unsigned long manualStopAtMs = 0;
  unsigned long lastTickMs = 0;
  bool tickedOnce = false;
  uint16_t dryRunStartAdc = 0;
  unsigned long dryRunStartMs = 0;
  const char *lastStopReason = "";

  bool overflowAlarm = false;     // debounced
  bool overflowPinActive = false; // raw, since overflowPinSinceMs
  unsigned long overflowPinSinceMs = 0;

  constexpr bool SENSOR_INVERTED = FUEL_LEVEL_HIGH_ADC < FUEL_LEVEL_LOW_ADC;

  // Higher value = more fuel, regardless of sensor direction
  inline int32_t fullness(uint16_t adc, bool inverted = SENSOR_INVERTED)
  {
    return inverted ? -static_cast<int32_t>(adc) : static_cast<int32_t>(adc);
  }

  inline bool isLevelLow(uint16_t adc)
  {
    return fullness(adc) <= fullness(FUEL_LEVEL_LOW_ADC);
  }

  inline bool isLevelHigh(uint16_t adc)
  {
    return fullness(adc) >= fullness(FUEL_LEVEL_HIGH_ADC);
  }

  bool isSensorOk(const SensorSnapshot &sn)
  {
    return sensorChannelValid(sn.fuelGenLastSuccessMs, millis()) && fuelSensorInRange(sn.fuelGenRaw);
  }

  void startPump(bool withManualDeadline, unsigned long deadlineMs, uint16_t adc)
  {
    const unsigned long now = millis();
    pumpOn = true;
    pumpStartedAtMs = now;
    hasManualDeadline = withManualDeadline;
    manualStopAtMs = deadlineMs;
    dryRunStartMs = now;
    dryRunStartAdc = adc;
    setRelay(RELAY_PUMP_PIN, true);
  }

  // lockoutAuto: protection trip, auto stays off until POST /pump/auto {"enabled":true}
  void stopPump(const char *reason, bool lockoutAuto)
  {
    pumpOn = false;
    setRelay(RELAY_PUMP_PIN, false);
    lastStopReason = reason;
    hasManualDeadline = false;
    manualStopAtMs = 0;
    if (lockoutAuto)
    {
      autoEnabled = false;
    }
  }

  // Runs on every loop() pass: the emergency sensor must not wait for a tick.
  void checkOverflowAlarm(unsigned long now)
  {
    const bool active = digitalRead(FUEL_OVERFLOW_ALARM_PIN) == FUEL_OVERFLOW_ALARM_LEVEL;
    if (!active)
    {
      overflowPinActive = false;
      overflowAlarm = false; // auto stays locked out until re-enabled
      return;
    }
    if (!overflowPinActive)
    {
      overflowPinActive = true;
      overflowPinSinceMs = now;
    }
    if (overflowAlarm || now - overflowPinSinceMs < FUEL_OVERFLOW_DEBOUNCE_MS)
      return;

    overflowAlarm = true;
    autoEnabled = false; // A1 missed a full tank: don't trust it until checked
    // Stops the pump in every mode, a manual run included
    if (pumpOn)
      stopPump("overflow_alarm", true);
  }
}

uint8_t fuelLevelPercent(uint16_t adc, uint16_t emptyAdc, uint16_t fullAdc)
{
  const bool inverted = fullAdc < emptyAdc;
  const int32_t span = fullness(fullAdc, inverted) - fullness(emptyAdc, inverted);
  if (span <= 0)
    return 0;
  const int32_t pct = (fullness(adc, inverted) - fullness(emptyAdc, inverted)) * 100 / span;
  return static_cast<uint8_t>(constrain(pct, 0, 100));
}

bool fuelSensorInRange(uint16_t adc)
{
  return adc >= FUEL_SENSOR_MIN_ADC && adc <= FUEL_SENSOR_MAX_ADC;
}

void initFuelPump()
{
  pinMode(FUEL_OVERFLOW_ALARM_PIN, INPUT);
  pumpOn = false;
  autoEnabled = PUMP_AUTO_DEFAULT;
  hasManualDeadline = false;
  manualStopAtMs = 0;
  tickedOnce = false;
  lastStopReason = "";
  overflowAlarm = false;
  overflowPinActive = false;
  setRelay(RELAY_PUMP_PIN, false);
}

void updateFuelPump()
{
  const unsigned long now = millis();
  checkOverflowAlarm(now);

  if (tickedOnce && (now - lastTickMs) < PUMP_TICK_MS)
  {
    return;
  }
  lastTickMs = now;
  tickedOnce = true;

  const SensorSnapshot sn = getSensorSnapshot();
  const bool sensorOk = isSensorOk(sn);
  const uint16_t adc = sn.fuelGenRaw;

  if (!pumpOn)
  {
    if (autoEnabled && !overflowAlarm && sensorOk && isLevelLow(adc))
    {
      startPump(false, 0, adc);
    }
    return;
  }

  if (now - pumpStartedAtMs >= PUMP_MAX_RUNTIME_SEC * 1000UL)
  {
    stopPump("max_runtime", true);
    return;
  }

  // Ручний режим: крім max_runtime і аварійного переливу жодних захистів, тільки заданий час
  if (hasManualDeadline)
  {
    if (static_cast<long>(now - manualStopAtMs) >= 0)
    {
      stopPump("manual_duration_done", false);
    }
    return;
  }

  if (!sensorOk)
  {
    stopPump("sensor_fault", true);
    return;
  }

  if (isLevelHigh(adc))
  {
    stopPump("reached_full", false);
    return;
  }

  // Dry run: level must rise by at least DELTA within each window
  if (now - dryRunStartMs >= PUMP_DRY_RUN_WINDOW_MS)
  {
    if (fullness(adc) - fullness(dryRunStartAdc) < FUEL_DRY_RUN_MIN_DELTA_ADC)
    {
      stopPump("dry_run", true);
      return;
    }
    dryRunStartMs = now;
    dryRunStartAdc = adc;
  }
}

bool requestPumpStart(uint16_t seconds)
{
  if (overflowAlarm || seconds < 1 || seconds > PUMP_MAX_RUNTIME_SEC)
  {
    return false;
  }
  const unsigned long deadline = millis() + static_cast<unsigned long>(seconds) * 1000UL;
  if (pumpOn)
  {
    // Takes over the running pump; the max_runtime clock keeps running
    hasManualDeadline = true;
    manualStopAtMs = deadline;
  }
  else
  {
    startPump(true, deadline, getSensorSnapshot().fuelGenRaw);
  }
  return true;
}

void requestPumpStop()
{
  if (pumpOn)
  {
    stopPump("manual_stop", true);
  }
  // Auto is disabled either way, otherwise it restarts the pump on the next tick while level is low
  autoEnabled = false;
}

void setPumpAutoEnabled(bool enabled)
{
  autoEnabled = enabled;
  // A running pump is not stopped here; use /pump/stop for that
}

FuelPumpStatus getFuelPumpStatus()
{
  const SensorSnapshot sn = getSensorSnapshot();
  const bool ok = isSensorOk(sn);
  FuelPumpStatus s{};
  s.running = pumpOn;
  s.runtimeSec = pumpOn ? (millis() - pumpStartedAtMs) / 1000UL : 0;
  s.autoEnabled = autoEnabled;
  s.lastStopReason = lastStopReason;
  s.overflowAlarm = overflowAlarm;
  s.sensorOk = ok;
  s.levelAdc = sn.fuelGenRaw;
  s.levelLow = ok && isLevelLow(sn.fuelGenRaw);
  s.levelHigh = ok && isLevelHigh(sn.fuelGenRaw);
  s.levelPercent = ok ? fuelLevelPercent(sn.fuelGenRaw, FUEL_LEVEL_LOW_ADC, FUEL_LEVEL_HIGH_ADC) : 0;
  return s;
}
