#pragma once
#include <Arduino.h>
#include "config/config.h"
#include "drivers/ks24_bus.h"
#include "drivers/lamps.h"
#include "services/FuelPump.h"

struct StartStopResult
{
  bool ok;
  bool generatorRun;
  const char *status; // "started" | "already_on" | "stopped" | "already_off" | "wrong_mode"
};

struct StatusData
{
  bool generatorRun;
  GenMode mode;
  RunLampState lampRun;
  bool lampOil;
  bool lampOverload;
  Ks24Data bus;

  uint16_t analogBattery; // A0 - АКБ
  float voltageBattery;
  bool batValid;
  uint16_t fuelExtRaw;    // A2 - зовнішній бак
  uint8_t fuelExtPercent; // 0, якщо датчик несправний
  FuelPumpStatus pump;    // A1 - бак генератора + насос
};

// Defaults after any reset: GEN_MODE_DEFAULT, R1 open (stopped).
// Call right after initRelays().
void initGeneratorOps();

// Manual mode only: R1 closed and held, the generator runs while it stays closed.
StartStopResult startGenerator();
// Any mode: switches to Manual (R2 open) and opens R1, the generator stops.
StartStopResult stopGenerator();

// Manual/Voltage: R2 open, R1 left as it is.
// Generator: R2 closed, R1 open; the generator keeps the voltage by itself.
void setGenMode(GenMode mode);
GenMode getGenMode();
const char *genModeName(GenMode mode); // "manual" | "voltage" | "generator"
bool parseGenMode(const char *name, GenMode &out);

// R1 without mode checks — for the Voltage mode automation only.
void setRunCommand(bool on);

StatusData readStatus();
