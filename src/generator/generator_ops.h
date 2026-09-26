#pragma once
#include <Arduino.h>
#include "config/config.h"
#include "drivers/ks24_bus.h"
#include "drivers/lamps.h"

struct StartStopResult
{
  bool ok;
  const char *status; // ok: "started" | "stopped"; error: "wrong_mode" | "already_running" | "already_stopped"
};

struct StatusData
{
  bool generatorRun;
  GenMode mode;
  bool runCommanded; // R1 замкнене
  RunLampState lampRun;
  bool oilAlert;     // з дебаунсом
  bool overload;     // з дебаунсом
  Ks24Data bus;

  uint16_t batRaw; // A0 - АКБ
  float batVoltage;
  bool batValid;

  uint16_t fuelGenRaw;    // A1 - бак генератора
  uint8_t fuelGenPercent; // 0% = FUEL_LEVEL_LOW_ADC, 100% = FUEL_LEVEL_HIGH_ADC
  bool fuelGenValid;

  uint16_t fuelExtRaw;    // A2 - зовнішній бак
  uint8_t fuelExtPercent;
  bool fuelExtValid;
};

// Defaults after any reset: GEN_MODE_DEFAULT, R1 open (stopped).
// Call right after initRelays().
void initGeneratorOps();
// Mode saved in EEPROM (Manual if missing or invalid). Call after initGenAuto().
// From here on every mode change is saved.
void restoreGenModeFromEeprom();

// Manual mode only: R1 closed and held, the generator runs while it stays closed.
StartStopResult startGenerator();
// Any mode: switches to Manual (R2 open) and opens R1, the generator stops.
StartStopResult stopGenerator(const char *why);

// Manual/Local/Remote: R2 open, R1 left as it is.
// Generator: R2 closed, R1 open; the generator keeps the voltage by itself.
void setGenMode(GenMode mode, const char *why);
GenMode getGenMode();
// remote_voltage needs the remote module enabled; the other modes are always available
bool genModeAvailable(GenMode mode);
// Call after the remote module was enabled/disabled: leaves remote_voltage for manual if needed
void genModeSourcesChanged();
const char *genModeName(GenMode mode); // "manual" | "local_voltage" | "remote_voltage" | "generator"
bool parseGenMode(const char *name, GenMode &out);

// R1 without mode checks — for the voltage automation only.
void setRunCommand(bool on);
bool isRunCommanded();

StatusData readStatus();
