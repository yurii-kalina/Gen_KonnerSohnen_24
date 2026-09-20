#include "generator_ops.h"
#include <Arduino.h>
#include <string.h>
#include "config/config.h"
#include "config/pins.h"
#include "generator/generator_state.h"
#include "drivers/lamps.h"
#include "drivers/relays.h"
#include "drivers/sensor_cache.h"
#include "services/GenAuto.h"

namespace
{
  GenMode mode = GEN_MODE_DEFAULT;
  bool runCommanded = false;
}

void initGeneratorOps()
{
  setRunCommand(false);
  setGenMode(GEN_MODE_DEFAULT);
}

StartStopResult startGenerator()
{
  if (mode != GenMode::Manual)
  {
    return {false, generatorRun, "wrong_mode"};
  }
  if (runCommanded)
  {
    return {true, generatorRun, "already_on"};
  }
  setRunCommand(true);
  return {true, generatorRun, "started"};
}

StartStopResult stopGenerator()
{
  const bool wasOff = mode == GenMode::Manual && !runCommanded;
  setGenMode(GenMode::Manual);
  setRunCommand(false);
  return {true, generatorRun, wasOff ? "already_off" : "stopped"};
}

void setGenMode(GenMode newMode)
{
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
  if (newMode == GenMode::Voltage)
  {
    resetGenAuto(); // evaluate the current voltage afresh
  }
}

GenMode getGenMode()
{
  return mode;
}

const char *genModeName(GenMode m)
{
  switch (m)
  {
  case GenMode::Manual:
    return "manual";
  case GenMode::Voltage:
    return "voltage";
  case GenMode::Generator:
    return "generator";
  }
  return "unknown";
}

bool parseGenMode(const char *name, GenMode &out)
{
  if (name == nullptr)
    return false;
  for (GenMode m : {GenMode::Manual, GenMode::Voltage, GenMode::Generator})
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
  setRelay(RELAY_CONTROL_PIN, on);
  runCommanded = on;
}

StatusData readStatus()
{
  StatusData s{};
  s.generatorRun = pollGeneratorRun();
  s.mode = mode;
  s.lampRun = getRunLampState();
  s.lampOil = isLampOilOn();
  s.lampOverload = isLampOverloadOn();
  s.bus = getKs24Data();

  // Cached copy from the background task — no I2C in the request path.
  const SensorSnapshot sn = getSensorSnapshot();
  const uint32_t now = millis();
  s.analogBattery = sn.batRaw;
  s.voltageBattery = sn.batVoltage;
  s.batValid = sensorChannelValid(sn.batLastSuccessMs, now);
  s.fuelExtRaw = sn.fuelExtRaw;
  const bool fuelExtOk = sensorChannelValid(sn.fuelExtLastSuccessMs, now) && fuelSensorInRange(sn.fuelExtRaw);
  s.fuelExtPercent = fuelExtOk ? fuelLevelPercent(sn.fuelExtRaw, FUEL_EXT_EMPTY_ADC, FUEL_EXT_FULL_ADC) : 0;
  s.pump = getFuelPumpStatus();
  return s;
}
