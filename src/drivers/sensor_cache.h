#pragma once
#include <stdint.h>
#include "config/config.h" // ADS_STALE_MS

// Snapshot of the last sensor acquisition cycle. Handlers copy this under a
// mutex instead of touching the I2C bus — after initSensors() the background
// task is the ONLY code allowed to use Wire.
//
// The success timestamp is the single source of truth for validity
// (see sensorChannelValid below).
struct SensorSnapshot
{
  uint16_t batRaw = 0;           // A0 - АКБ
  float batVoltage = 0.0f;
  uint32_t batLastSuccessMs = 0; // 0 = never succeeded since boot

  uint16_t fuelGenRaw = 0;       // A1 - рівень палива в баку генератора
  uint32_t fuelGenLastSuccessMs = 0;

  uint16_t fuelExtRaw = 0;       // A2 - рівень палива в зовнішньому баку
  uint32_t fuelExtLastSuccessMs = 0;
};

// Cached value is trustworthy while its last SUCCESS is younger than
// ADS_STALE_MS. lastSuccessMs == 0 is the "never succeeded" sentinel.
// Pass the same `now` for all channels so age and validity cannot disagree.
inline bool sensorChannelValid(uint32_t lastSuccessMs, uint32_t now)
{
  return lastSuccessMs != 0 && (now - lastSuccessMs) < ADS_STALE_MS;
}

// Starts I2C, probes the ADS1115 (with retries) and launches the sensor task.
// Returns false if the task could not be created — then getSensorSnapshot()
// serves a zeroed snapshot (every channel reads invalid).
bool initSensors();
SensorSnapshot getSensorSnapshot();
