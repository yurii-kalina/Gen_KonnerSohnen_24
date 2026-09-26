#include "FuelPump.h"
#include "config/config.h"
#include "config/pins.h"
#include "drivers/sensor_cache.h"
#include "drivers/relays.h"
#include "generator/generator_state.h"
#include "services/EEPROMHandler.h"
#include "services/log_uploader.h"
#include "services/logger.h"

static const char *const TAG = "pump";

static constexpr uint32_t PUMP_TICK_MS = 1000;

namespace
{
  bool pumpOn = false;
  bool autoEnabled = PUMP_AUTO_DEFAULT;
  bool manualRun = false;
  uint32_t runLimitSec = PUMP_MAX_RUNTIME_SEC;
  unsigned long pumpStartedAtMs = 0;
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

  uint16_t fuelRaw()
  {
    return getSensorSnapshot().fuelGenRaw;
  }

  void setAutoMode(bool enabled)
  {
    if (enabled == autoEnabled)
      return;
    autoEnabled = enabled;
    saveEepromPumpAuto(enabled);
  }

  void startPump(bool manual, uint32_t limitSec)
  {
    const unsigned long now = millis();
    const uint16_t adc = fuelRaw();
    pumpOn = true;
    manualRun = manual;
    runLimitSec = limitSec;
    pumpStartedAtMs = now;
    dryRunStartMs = now;
    dryRunStartAdc = adc;
    setRelay(RELAY_PUMP_PIN, true);

    RLOGI(TAG, "pump ON (%s, max %lu s), fuelRaw=%u (low=%u high=%u), gen=%d",
          manual ? "manual" : "auto", (unsigned long)limitSec,
          (unsigned)adc, (unsigned)FUEL_LEVEL_LOW_ADC, (unsigned)FUEL_LEVEL_HIGH_ADC, (int)generatorRun);
  }

  // lockoutAuto: protection trip, auto stays off until POST /pump/auto {"enabled":true}
  void stopPump(const char *reason, bool lockoutAuto)
  {
    const unsigned long ranSec = pumpOn ? (millis() - pumpStartedAtMs) / 1000UL : 0UL;
    const bool wasManual = manualRun;

    pumpOn = false;
    manualRun = false;
    setRelay(RELAY_PUMP_PIN, false);
    lastStopReason = reason;
    if (lockoutAuto)
    {
      setAutoMode(false);
    }

    logWrite(lockoutAuto ? RLOG_ERROR : RLOG_INFO, TAG,
             "pump OFF (%s) after %lu s, reason=%s, fuelRaw=%u%s",
             wasManual ? "manual" : "auto", ranSec, reason, (unsigned)fuelRaw(),
             lockoutAuto ? " — AUTO LOCKED OUT until POST /pump/auto {\"enabled\":true}" : "");

    if (lockoutAuto)
      logUploaderRequestFlush();
  }

  // Runs on every loop() pass: the emergency sensor must not wait for a tick.
  void checkOverflowAlarm(unsigned long now)
  {
    const bool active = digitalRead(FUEL_OVERFLOW_ALARM_PIN) == FUEL_OVERFLOW_ALARM_LEVEL;
    if (!active)
    {
      overflowPinActive = false;
      if (overflowAlarm)
      {
        overflowAlarm = false; // auto stays locked out until re-enabled
        RLOGI(TAG, "overflow alarm cleared (auto stays %s)", autoEnabled ? "on" : "locked out");
      }
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
    RLOGC(TAG, "FUEL OVERFLOW ALARM: emergency sensor active, fuelRaw=%u (high=%u) — fuel sender missed a full tank?",
          (unsigned)fuelRaw(), (unsigned)FUEL_LEVEL_HIGH_ADC);
    // Stops the pump in every mode, a manual run included
    if (pumpOn)
      stopPump("overflow_alarm", true);
    setAutoMode(false); // A1 missed a full tank: don't trust it until checked
    logUploaderRequestFlush();
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
  manualRun = false;
  if (!loadEepromPumpAuto(autoEnabled))
    autoEnabled = PUMP_AUTO_DEFAULT;
  tickedOnce = false;
  lastStopReason = "";
  overflowAlarm = false;
  overflowPinActive = false;
  setRelay(RELAY_PUMP_PIN, false);
  RLOGI(TAG, "pump initialized (relay off, mode=%s)", autoEnabled ? "auto" : "manual");
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

  const bool genRunning = pollGeneratorRun();
  const SensorSnapshot sn = getSensorSnapshot();
  const bool sensorOk = isSensorOk(sn);
  const uint16_t adc = sn.fuelGenRaw;

  if (!pumpOn)
  {
    if (autoEnabled && genRunning && !overflowAlarm && sensorOk && isLevelLow(adc))
    {
      RLOGI(TAG, "auto-start: fuelRaw=%u reached low=%u, generator running",
            (unsigned)adc, (unsigned)FUEL_LEVEL_LOW_ADC);
      startPump(false, PUMP_MAX_RUNTIME_SEC);
    }
    return;
  }

  if (now - pumpStartedAtMs >= runLimitSec * 1000UL)
  {
    // The manual run simply ran out; for auto it is a protection trip
    stopPump("max_runtime", !manualRun);
    return;
  }

  // Ручний режим: крім заданого часу й аварійного переливу жодних захистів
  if (manualRun)
    return;

  if (!genRunning)
  {
    stopPump("gen_stopped", false);
    return;
  }

  if (!sensorOk)
  {
    RLOGE(TAG, "fuel sender not readable or out of range (raw=%u) while pumping", (unsigned)adc);
    stopPump("fuel_sensor_lost", true);
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
    const int32_t rise = fullness(adc) - fullness(dryRunStartAdc);
    if (rise < FUEL_DRY_RUN_MIN_DELTA_ADC)
    {
      RLOGE(TAG, "dry-run detected: fuelRaw moved %ld toward full in %lu s while pumping (need %u)",
            (long)rise, (unsigned long)((now - dryRunStartMs) / 1000UL), (unsigned)FUEL_DRY_RUN_MIN_DELTA_ADC);
      stopPump("dry_run", true);
      return;
    }
    RLOGD(TAG, "fuel rising: fuelRaw %u, +%ld in %lu s, dry-run window restarted",
          (unsigned)adc, (long)rise, (unsigned long)((now - dryRunStartMs) / 1000UL));
    dryRunStartMs = now;
    dryRunStartAdc = adc;
  }
}

bool requestPumpStart(uint32_t seconds)
{
  if (seconds < 1 || seconds > PUMP_MAX_RUNTIME_SEC)
  {
    RLOGW(TAG, "manual start rejected: %lu s outside 1..%lu",
          (unsigned long)seconds, (unsigned long)PUMP_MAX_RUNTIME_SEC);
    return false;
  }
  if (overflowAlarm)
  {
    RLOGW(TAG, "manual start rejected: overflow alarm is active");
    return false;
  }

  if (autoEnabled)
    RLOGI(TAG, "manual start: auto mode DISABLED");
  setAutoMode(false);

  if (pumpOn)
  {
    RLOGI(TAG, "pump already running %lu s (%s) — now a manual run for %lu s from now",
          (unsigned long)((millis() - pumpStartedAtMs) / 1000UL), manualRun ? "manual" : "auto",
          (unsigned long)seconds);
    manualRun = true;
    runLimitSec = seconds;
    pumpStartedAtMs = millis();
  }
  else
  {
    startPump(true, seconds);
  }
  return true;
}

void requestPumpStop()
{
  // Auto is disabled either way, otherwise it restarts the pump on the next tick while level is low
  if (autoEnabled)
    RLOGI(TAG, "manual stop: auto mode DISABLED");
  setAutoMode(false);

  if (pumpOn)
  {
    stopPump("manual_stop", false);
  }
  else
  {
    RLOGD(TAG, "manual stop requested but pump already off (last reason=%s)",
          lastStopReason[0] != '\0' ? lastStopReason : "-");
  }
}

void pumpAbort(const char *reason)
{
  if (pumpOn)
    stopPump(reason, false);
}

void setPumpAutoEnabled(bool enabled)
{
  if (enabled != autoEnabled)
  {
    RLOGI(TAG, "auto mode %s (pump currently %s, last stop reason=%s)",
          enabled ? "ENABLED — lockout cleared" : "DISABLED",
          pumpOn ? (manualRun ? "running manual" : "running auto") : "off",
          lastStopReason[0] != '\0' ? lastStopReason : "-");
  }
  // A running pump is not stopped here; use /pump/stop for that
  setAutoMode(enabled);
}

FuelPumpStatus getFuelPumpStatus()
{
  FuelPumpStatus s{};
  s.running = pumpOn;
  s.manualRun = pumpOn && manualRun;
  s.runtimeSec = pumpOn ? (millis() - pumpStartedAtMs) / 1000UL : 0;
  s.limitSec = pumpOn ? runLimitSec : 0;
  s.autoEnabled = autoEnabled;
  s.lastStopReason = lastStopReason;
  s.overflowAlarm = overflowAlarm;
  return s;
}
