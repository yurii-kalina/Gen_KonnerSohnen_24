#include "generator_ops.h"
#include <Arduino.h>
#include <string.h>
#include "config/config.h"
#include "config/pins.h"
#include "generator/generator_state.h"
#include "drivers/lamps.h"
#include "drivers/relays.h"
#include "drivers/sensor_cache.h"
#include "services/FuelPump.h"
#include "services/GenAuto.h"
#include "services/EEPROMHandler.h"
#include "services/log_uploader.h"
#include "services/logger.h"
#include "services/settings.h"

static const char *const TAG = "gen";

namespace
{
  GenMode mode = GEN_MODE_DEFAULT;
  bool runCommanded = false;
  // EEPROM and the logger are not ready during initGeneratorOps(); writes start after the restore
  bool persistMode = false;
}

void initGeneratorOps()
{
  setRunCommand(false);
  setGenMode(GEN_MODE_DEFAULT, "boot");
}

// Generator is restored too: it is the operator's choice, and the generator
// keeps the voltage itself, same as before the power loss.
void restoreGenModeFromEeprom()
{
  const uint8_t stored = loadEepromGenMode();
  GenMode restored = GEN_MODE_DEFAULT;
  switch (stored)
  {
  case static_cast<uint8_t>(GenMode::Manual):
  case static_cast<uint8_t>(GenMode::Local):
  case static_cast<uint8_t>(GenMode::Generator):
  case static_cast<uint8_t>(GenMode::Remote):
    restored = static_cast<GenMode>(stored);
    break;
  }
  const bool sourceOff = !genModeAvailable(restored);
  if (sourceOff)
    restored = GenMode::Manual;
  setGenMode(restored, "eeprom");
  persistMode = true;
  if (sourceOff)
  {
    RLOGW(TAG, "stored mode remote_voltage has the remote module disabled — starting in manual");
    saveEepromGenMode(static_cast<uint8_t>(restored));
  }
  RLOGI(TAG, "mode restored: %s (EEPROM byte 0x%02X)", genModeName(mode), (unsigned)stored);
}

StartStopResult startGenerator()
{
  if (mode != GenMode::Manual)
  {
    RLOGW(TAG, "start rejected: mode is %s, not manual", genModeName(mode));
    return {false, "wrong_mode"};
  }
  if (runCommanded)
  {
    return {false, "already_running"};
  }
  RLOGI(TAG, "start requested (generator %s)", generatorRun ? "already running" : "stopped");
  setRunCommand(true);
  return {true, "started"};
}

// Resetting the mode to Manual is intentional: a stop in a voltage mode would
// otherwise be undone by GenAuto the next time the battery drops to MIN.
StartStopResult stopGenerator(const char *why)
{
  const bool wasOff = mode == GenMode::Manual && !runCommanded;
  if (wasOff)
  {
    return {false, "already_stopped"};
  }
  RLOGI(TAG, "stop requested (%s), mode %s -> manual", why, genModeName(mode));
  setGenMode(GenMode::Manual, why);
  setRunCommand(false);
  return {true, "stopped"};
}

void setGenMode(GenMode newMode, const char *why)
{
  const bool changed = newMode != mode;
  if (changed && persistMode)
  {
    RLOGI(TAG, "mode %s -> %s (%s)", genModeName(mode), genModeName(newMode), why);
  }
  if (newMode == GenMode::Generator)
  {
    // R2 first: the generator takes over before the control contact opens
    setRelay(RELAY_MODE_PIN, true);
    setRunCommand(false);
  }
  else
  {
    setRelay(RELAY_MODE_PIN, false);
  }
  mode = newMode;
  if (changed && persistMode)
  {
    saveEepromGenMode(static_cast<uint8_t>(newMode));
    logUploaderRequestFlush();
  }
  if (newMode == GenMode::Local || newMode == GenMode::Remote)
  {
    resetGenAuto(); // evaluate the current voltage afresh
  }
}

GenMode getGenMode()
{
  return mode;
}

bool genModeAvailable(GenMode m)
{
  return m != GenMode::Remote || extBatRemoteEnabled();
}

void genModeSourcesChanged()
{
  if (genModeAvailable(mode))
    return;
  RLOGW(TAG, "remote module disabled while in %s — switching to manual, R1 stays %s",
        genModeName(mode), runCommanded ? "closed" : "open");
  setGenMode(GenMode::Manual, "source_disabled");
}

const char *genModeName(GenMode m)
{
  switch (m)
  {
  case GenMode::Manual:
    return "manual";
  case GenMode::Local:
    return "local_voltage";
  case GenMode::Remote:
    return "remote_voltage";
  case GenMode::Generator:
    return "generator";
  }
  return "unknown";
}

bool parseGenMode(const char *name, GenMode &out)
{
  if (name == nullptr)
    return false;
  for (GenMode m : {GenMode::Manual, GenMode::Local, GenMode::Remote, GenMode::Generator})
  {
    if (strcmp(name, genModeName(m)) == 0)
    {
      out = m;
      return true;
    }
  }
  return false;
}

void setRunCommand(bool on)
{
  if (on != runCommanded && persistMode)
  {
    RLOGI(TAG, "R1 %s (mode %s, generator %s)", on ? "CLOSED -> run" : "OPEN -> stop",
          genModeName(mode), generatorRun ? "running" : "stopped");
  }
  setRelay(RELAY_CONTROL_PIN, on);
  runCommanded = on;
}

bool isRunCommanded()
{
  return runCommanded;
}

StatusData readStatus()
{
  StatusData s{};
  s.generatorRun = pollGeneratorRun();
  s.mode = mode;
  s.runCommanded = runCommanded;
  s.lampRun = getRunLampState();
  s.oilAlert = oilAlertOn();
  s.overload = overloadOn();
  s.bus = getKs24Data();

  // Cached copy from the background task — no I2C in the request path.
  const SensorSnapshot sn = getSensorSnapshot();
  const uint32_t now = millis();
  s.batRaw = sn.batRaw;
  s.batVoltage = sn.batVoltage;
  s.batValid = sensorChannelValid(sn.batLastSuccessMs, now);

  s.fuelGenRaw = sn.fuelGenRaw;
  s.fuelGenValid = sensorChannelValid(sn.fuelGenLastSuccessMs, now) && fuelSensorInRange(sn.fuelGenRaw);
  s.fuelGenPercent = s.fuelGenValid ? fuelLevelPercent(sn.fuelGenRaw, FUEL_LEVEL_LOW_ADC, FUEL_LEVEL_HIGH_ADC) : 0;

  s.fuelExtRaw = sn.fuelExtRaw;
  s.fuelExtValid = sensorChannelValid(sn.fuelExtLastSuccessMs, now) && fuelSensorInRange(sn.fuelExtRaw);
  s.fuelExtPercent = s.fuelExtValid ? fuelLevelPercent(sn.fuelExtRaw, FUEL_EXT_EMPTY_ADC, FUEL_EXT_FULL_ADC) : 0;
  return s;
}
