#include "sensor_cache.h"
#include <Arduino.h>
#include <Wire.h>
#include "config/config.h"
#include "config/pins.h"

static bool adsAvailable = false;

static SensorSnapshot cache;
static SemaphoreHandle_t cacheMutex = nullptr;
static uint8_t consecFullFails = 0;

// ---- raw ADS1115 access -----------------------------------------------------
// Every Wire transaction's result is checked; any failure fails the sample
// explicitly (the Adafruit driver would surface stale register contents).

static constexpr uint8_t REG_CONVERSION = 0x00;
static constexpr uint8_t REG_CONFIG = 0x01;
// OS=start | PGA=±6.144V (0.1875 mV/LSB) | MODE=single-shot | DR=128SPS | comparator disabled
static constexpr uint16_t CFG_BASE = 0x8000 | 0x0100 | 0x0080 | 0x0003;
// One conversion at 128 SPS takes ~7.8 ms
static constexpr uint32_t ADS_CONVERSION_MS = 8;
static inline uint16_t cfgFor(uint8_t ch)
{
  return CFG_BASE | 0x4000 | (static_cast<uint16_t>(ch) << 12); // single-ended AINx
}

static bool writeReg(uint8_t reg, uint16_t val)
{
  Wire.beginTransmission(ADS_I2C_ADDR);
  Wire.write(reg);
  Wire.write(static_cast<uint8_t>(val >> 8));
  Wire.write(static_cast<uint8_t>(val & 0xFF));
  return Wire.endTransmission() == 0;
}

static bool readReg(uint8_t reg, uint16_t &val)
{
  Wire.beginTransmission(ADS_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0)
    return false;
  if (Wire.requestFrom(ADS_I2C_ADDR, static_cast<uint8_t>(2)) != 2)
    return false;
  val = (static_cast<uint16_t>(Wire.read()) << 8) | Wire.read();
  return true;
}

static bool probeAddr(uint8_t addr)
{
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static bool tryFindAds()
{
  adsAvailable = probeAddr(ADS_I2C_ADDR);
  return adsAvailable;
}

static void beginBus()
{
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);
  Wire.setTimeOut(WIRE_TIMEOUT_MS);
}

// One conversion. Budget counts from the config write; each Wire transaction
// is additionally bounded by WIRE_TIMEOUT_MS.
static bool readOneSample(uint8_t ch, int16_t &out)
{
  const uint32_t t0 = millis();
  if (!writeReg(REG_CONFIG, cfgFor(ch)))
    return false;
  // Polling OS right after the write can still see "idle" from before the
  // start and return the previous conversion; wait out one conversion first.
  vTaskDelay(pdMS_TO_TICKS(ADS_CONVERSION_MS));
  for (;;)
  {
    uint16_t cfg;
    if (!readReg(REG_CONFIG, cfg))
      return false;
    if (cfg & 0x8000) // OS bit: conversion finished
      break;
    if (millis() - t0 > ADS_SAMPLE_BUDGET_MS)
      return false;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  uint16_t raw;
  if (!readReg(REG_CONVERSION, raw))
    return false;
  out = static_cast<int16_t>(raw);
  return true;
}

// N samples -> trimmed mean (min and max dropped). Publishes only when enough
// samples were individually valid; otherwise the previous value stays cached.
static bool readChannel(uint8_t ch, float &avgOut)
{
  int32_t sum = 0;
  int16_t mn = INT16_MAX, mx = INT16_MIN;
  uint8_t good = 0;

  // First conversion after switching the mux still carries the previous
  // channel's charge — discard it.
  int16_t settle;
  readOneSample(ch, settle);

  for (uint8_t i = 0; i < ADC_SAMPLES; ++i)
  {
    int16_t v;
    if (readOneSample(ch, v))
    {
      sum += v;
      if (v < mn) mn = v;
      if (v > mx) mx = v;
      ++good;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }

  if (good < ADS_MIN_VALID_SAMPLES)
    return false;

  sum -= mn;
  sum -= mx;
  avgOut = static_cast<float>(sum) / (good - 2);
  return true;
}

// A locked bus never self-recovers: clock out up to 9 pulses so a slave stuck
// mid-byte releases SDA, then re-init the bus and re-probe.
static void recoverBus()
{
  Wire.end();
  pinMode(I2C_SCL_PIN, OUTPUT_OPEN_DRAIN);
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  for (uint8_t i = 0; i < 9 && digitalRead(I2C_SDA_PIN) == LOW; ++i)
  {
    digitalWrite(I2C_SCL_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(I2C_SCL_PIN, HIGH);
    delayMicroseconds(5);
  }
  beginBus();
  tryFindAds();
}

static uint16_t clampRaw(float v)
{
  if (v < 0) return 0;
  if (v > 65535.0f) return 65535;
  return static_cast<uint16_t>(v);
}

static constexpr uint8_t CHANNELS[] = {ADS_CH_BAT, ADS_CH_FUEL_GEN, ADS_CH_FUEL_EXT};
static constexpr uint8_t CHANNEL_COUNT = sizeof(CHANNELS) / sizeof(CHANNELS[0]);

// Stores one channel into the cache right away, so /status never waits for
// the rest of the cycle.
static void publishChannel(uint8_t idx, float v)
{
  const uint32_t now = millis();
  xSemaphoreTake(cacheMutex, portMAX_DELAY);
  switch (idx)
  {
  case 0:
    cache.batRaw = clampRaw(v);
    cache.batVoltage = v * CALIBRATE_VOLTAGE_BAT;
    cache.batLastSuccessMs = now; // success time, NOT attempt time
    break;
  case 1:
    cache.fuelGenRaw = clampRaw(v);
    cache.fuelGenLastSuccessMs = now;
    break;
  case 2:
    cache.fuelExtRaw = clampRaw(v);
    cache.fuelExtLastSuccessMs = now;
    break;
  }
  xSemaphoreGive(cacheMutex);
}

static void sensorTask(void *)
{
  for (;;)
  {
    const uint32_t cycleStart = millis();

    // Модуль міг з'явитись пізніше (живлення піднялось після ESP32)
    if (!adsAvailable)
      tryFindAds();

    if (adsAvailable)
    {
      bool anyOk = false;
      for (uint8_t i = 0; i < CHANNEL_COUNT; ++i)
      {
        float v;
        if (readChannel(CHANNELS[i], v))
        {
          publishChannel(i, v);
          anyOk = true;
        }
      }

      if (!anyOk)
      {
        if (++consecFullFails >= ADS_FAILS_BEFORE_RECOVERY)
        {
          recoverBus();
          consecFullFails = 0;
        }
      }
      else
      {
        consecFullFails = 0;
      }
    }

    const uint32_t spent = millis() - cycleStart;
    if (spent < SENSOR_CYCLE_MS)
      vTaskDelay(pdMS_TO_TICKS(SENSOR_CYCLE_MS - spent));
  }
}

bool initSensors()
{
  beginBus();

  for (uint8_t attempt = 0; attempt < ADS_BEGIN_ATTEMPTS && !tryFindAds(); ++attempt)
    delay(ADS_BEGIN_RETRY_MS);

  cacheMutex = xSemaphoreCreateMutex();
  if (cacheMutex == nullptr)
    return false;
  // Core 0, priority 1: yields to WiFi/system tasks, isolated from loop() on core 1.
  if (xTaskCreatePinnedToCore(sensorTask, "sensors", 5120, nullptr, 1, nullptr, 0) != pdPASS)
  {
    vSemaphoreDelete(cacheMutex);
    cacheMutex = nullptr;
    return false;
  }
  return true;
}

SensorSnapshot getSensorSnapshot()
{
  if (cacheMutex == nullptr)
    return SensorSnapshot{};
  SensorSnapshot out;
  xSemaphoreTake(cacheMutex, portMAX_DELAY);
  out = cache;
  xSemaphoreGive(cacheMutex);
  return out;
}
