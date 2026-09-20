#pragma once
#include <Arduino.h>
#include <stdint.h>
#include "config/config.h"

// ADS1115 (I2C)
constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;

// ADS1115 channels
constexpr uint8_t ADS_CH_BAT = 0;      // A0 - напруга АКБ
constexpr uint8_t ADS_CH_FUEL_GEN = 1; // A1 - рівень палива в баку генератора (по ньому працює насос)
constexpr uint8_t ADS_CH_FUEL_EXT = 2; // A2 - рівень палива в зовнішньому баку

// Relays
constexpr uint8_t RELAY_CONTROL_PIN = 18;     // R1 - CONTROL TERMINAL: замкнено = генератор працює
constexpr uint8_t RELAY_MODE_PIN = 19;        // R2 - режим генератора: замкнено = авто, розімкнено = ручний
constexpr uint8_t RELAY_PUMP_PIN = 23;        // R3 - насос перекачки палива

// Generator panel LEDs (inputs, HIGH = світить)
constexpr uint8_t LED_RUN_PIN = 25;
constexpr uint8_t LED_OIL_PIN = 26;
constexpr uint8_t LED_OVERLOAD_PIN = 27;

// Аварійний датчик переливу (LOW = аварія). GPIO34 лише вхід і без внутрішніх
// підтяжок — потрібен зовнішній резистор до 3.3 В.
constexpr uint8_t FUEL_OVERFLOW_ALARM_PIN = 34;

// Лінія інвертор -> дисплей KS 24VS-DC (UART2, лише прийом)
constexpr uint8_t KS24_RX_PIN = 16;
