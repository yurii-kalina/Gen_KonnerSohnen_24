#pragma once
#include <Arduino.h>

constexpr const char *HOST_NAME = "KonnerSohnen24";
constexpr const char *DEFAULT_WIFI_SSID = "Home";
constexpr const char *DEFAULT_WIFI_PASS = "31023102";

// -----------------------------------------------------------------------------
// Реле
// -----------------------------------------------------------------------------
// Рівень на GPIO, при якому реле замкнене
constexpr uint8_t RELAY_ACTIVE_LEVEL = LOW;       // R1 CONTROL TERMINAL, R2 режим
constexpr uint8_t RELAY_PUMP_ACTIVE_LEVEL = HIGH; // R3 насос

// -----------------------------------------------------------------------------
// Режими роботи
// -----------------------------------------------------------------------------
//   Manual    - R2 розімкнене. R1 замикається/розмикається командами /start, /stop.
//   Voltage   - R2 розімкнене. ESP сама керує R1 по напрузі АКБ (A0), пороги MIN/MAX.
//   Generator - R2 замкнене, R1 розімкнене. Генератор сам підтримує напругу, ESP не втручається.
enum class GenMode : uint8_t
{
  Manual,
  Voltage,
  Generator
};
// Після будь-якого перезавантаження ESP — цей режим
constexpr GenMode GEN_MODE_DEFAULT = GenMode::Manual;

// R1 тримається замкненим, поки генератор має працювати. Після будь-якого
// перезавантаження ESP R1 розімкнене (генератор зупиняється).

// -----------------------------------------------------------------------------
// Режим Voltage: керування по АКБ (A0, у вольтах після калібрування)
// -----------------------------------------------------------------------------
// Напруга <= MIN -> старт (R1 замкнути), >= MAX -> стоп (R1 розімкнути).
// Одна команда на кожен вхід у зону.
// MIN/MAX змінюються запитом POST /gen/thresholds і зберігаються в EEPROM;
// значення нижче — лише початкові, поки в EEPROM нічого не записано.
constexpr float GEN_AUTO_BAT_MIN_V = 23.6f;
constexpr float GEN_AUTO_BAT_MAX_V = 27.0f;
constexpr float GEN_AUTO_BAT_LIMIT_V = 100.0f; // допустимий діапазон порогів у запиті: 0..LIMIT
constexpr uint32_t GEN_AUTO_CONFIRM_MS = 10000; // скільки напруга має триматись у зоні
constexpr uint32_t GEN_AUTO_TICK_MS = 1000;

static_assert(GEN_AUTO_BAT_MIN_V < GEN_AUTO_BAT_MAX_V, "GEN_AUTO_BAT_MIN_V must be below GEN_AUTO_BAT_MAX_V");

// -----------------------------------------------------------------------------
// Лампи панелі (RUN, OIL, OVERLOAD)
// -----------------------------------------------------------------------------
// Лампи можуть бути під ШІМ, тому читаємо серію семплів і рахуємо активні.
constexpr uint8_t LAMP_ACTIVE_LEVEL = HIGH;
constexpr uint8_t LAMP_SAMPLES = 10;
constexpr uint16_t LAMP_SAMPLE_GAP_US = 0;
constexpr uint8_t LAMP_ACTIVE_THRESHOLD = 5;

static_assert(LAMP_ACTIVE_THRESHOLD <= LAMP_SAMPLES,
              "LAMP_ACTIVE_THRESHOLD must be <= LAMP_SAMPLES, otherwise every lamp reads OFF");
static_assert(LAMP_SAMPLES > 0,
              "LAMP_SAMPLES must be > 0, otherwise readLampStable always returns false");

// Лампа RUN моргає раз на секунду, коли генератор стоїть, і світить постійно,
// коли працює. "Працює" = світить без жодної перерви довше за STEADY_MS
// (більше за період моргання).
constexpr uint32_t RUN_LAMP_POLL_MS = 20;            // як часто опитуємо лампу
constexpr uint32_t RUN_LAMP_MAX_GAP_MS = 100;        // пропуск опитування довший -> відлік STEADY заново
constexpr uint32_t RUN_LAMP_STEADY_MS = 2000;        // безперервно світить стільки -> генератор працює
constexpr uint32_t RUN_LAMP_BLINK_TIMEOUT_MS = 2500; // не світила стільки -> лампа вимкнена (не моргає)

static_assert(RUN_LAMP_STEADY_MS > 1000, "RUN_LAMP_STEADY_MS must exceed the 1 s blink period");
static_assert(RUN_LAMP_BLINK_TIMEOUT_MS > 1000, "RUN_LAMP_BLINK_TIMEOUT_MS must exceed the 1 s blink period");
static_assert(RUN_LAMP_MAX_GAP_MS > RUN_LAMP_POLL_MS, "RUN_LAMP_MAX_GAP_MS must exceed RUN_LAMP_POLL_MS");

// -----------------------------------------------------------------------------
// Шина KS 24VS-DC (інвертор -> дисплей), див. drivers/ks24_frame.h
// -----------------------------------------------------------------------------
constexpr uint32_t KS24_BAUD = 2400;
// Кадр іде 141 мс, пауза між кадрами ~860 мс. UART віддає байти пачками після
// кінця кадру, тому поріг тиші має бути довшим за кадр і коротшим за паузу.
constexpr uint32_t KS24_SILENCE_RESET_MS = 400;
// Дані валідні, поки останній прийнятий кадр молодший за це (кадр раз на секунду)
constexpr uint32_t KS24_STALE_MS = 5000;

static_assert(KS24_SILENCE_RESET_MS > 141 && KS24_SILENCE_RESET_MS < 860,
              "KS24_SILENCE_RESET_MS must be longer than a frame and shorter than the gap");

// -----------------------------------------------------------------------------
// ADS1115
// -----------------------------------------------------------------------------
constexpr uint8_t ADC_SAMPLES = 10;
// Сирий АЦП множиться на цей коефіцієнт. Підбирається мультиметром:
// CALIBRATE = реальна_напруга / raw  (АЦП працює в діапазоні ±6.144 В, 0.1875 мВ/LSB)
// Відкалібровано по шині KS24: raw 13428 при 27.4 В (генератор працював, 38 А).
constexpr float CALIBRATE_VOLTAGE_BAT = 0.0020405f;

// Модуль може підніматись пізніше за ESP32 — пробуємо кілька разів.
constexpr uint8_t ADS_BEGIN_ATTEMPTS = 10;
constexpr uint16_t ADS_BEGIN_RETRY_MS = 200;

constexpr uint8_t ADS_I2C_ADDR = 0x48;

// Фонова задача сенсорів (після старту — єдиний власник шини I2C).
constexpr uint32_t SENSOR_CYCLE_MS = 1000;
constexpr uint32_t ADS_SAMPLE_BUDGET_MS = 60;
constexpr uint8_t ADS_MIN_VALID_SAMPLES = 6;     // публікуємо, якщо стільки семплів успішні
constexpr uint8_t ADS_FAILS_BEFORE_RECOVERY = 3; // провалених циклів до відновлення шини
constexpr uint16_t WIRE_TIMEOUT_MS = 50;

// Канал валідний, поки останній УСПІХ молодший за це. Має перекривати повний
// цикл відновлення шини.
constexpr uint32_t ADS_STALE_MS = 15000;

static_assert(ADS_STALE_MS > SENSOR_CYCLE_MS * (ADS_FAILS_BEFORE_RECOVERY + 1),
              "ADS_STALE_MS must outlast a full bus-recovery cycle, otherwise the "
              "valid flags drop during recoveries the firmware handles by itself");

// -----------------------------------------------------------------------------
// Датчики рівня палива (сирий АЦП)
// -----------------------------------------------------------------------------
// Показ поза цим діапазоном = обрив/КЗ датчика. ADS не виміряє більше за своє
// живлення (3.3 В ≈ 17600, 5 В ≈ 26600), тому MAX треба поставити трохи нижче
// за показ при відключеному датчику.
constexpr uint16_t FUEL_SENSOR_MIN_ADC = 100;
constexpr uint16_t FUEL_SENSOR_MAX_ADC = 32000;

// A2 зовнішній бак — лише показ. 0% = EMPTY, 100% = FULL.
// Якщо FULL < EMPTY — датчик інверсний (менше АЦП = більше палива).
// EMPTY виміряно: порожній бак (поплавок унизу) = 26984.
// TODO: FULL поки приблизний, виміряти на повному баку.
constexpr uint16_t FUEL_EXT_EMPTY_ADC = 26900;
constexpr uint16_t FUEL_EXT_FULL_ADC = 11000;

// -----------------------------------------------------------------------------
// Перекачка палива в бак генератора (A1 + R3)
// -----------------------------------------------------------------------------
constexpr bool PUMP_AUTO_DEFAULT = false; // після перезавантаження ESP — ручний режим
constexpr uint32_t PUMP_TICK_MS = 1000;
// Сирий АЦП каналу A1. Авто-насос вмикається при <= LOW і вимикається при >= HIGH.
// Якщо HIGH < LOW — датчик вважається інверсним (менше АЦП = більше палива).
constexpr uint16_t FUEL_LEVEL_LOW_ADC = 2000;
constexpr uint16_t FUEL_LEVEL_HIGH_ADC = 11000;
// Захисти (спрацювання -> насос стоп, авто блокується до POST /pump/auto)
constexpr uint32_t PUMP_MAX_RUNTIME_SEC = 360;
constexpr uint32_t PUMP_DRY_RUN_WINDOW_MS = 100 * 1000;
constexpr uint16_t FUEL_DRY_RUN_MIN_DELTA_ADC = 50; // мін. зростання рівня за вікно

// Аварійний перелив (GPIO34): рівень, при якому аварія, і скільки він має
// триматись, щоб відсіяти завади. Спрацювання зупиняє насос у будь-якому
// режимі й блокує авто; поки аварія активна, насос не вмикається взагалі.
constexpr uint8_t FUEL_OVERFLOW_ALARM_LEVEL = LOW;
constexpr uint32_t FUEL_OVERFLOW_DEBOUNCE_MS = 100;

// -----------------------------------------------------------------------------
// Стан генератора
// -----------------------------------------------------------------------------
constexpr int RUNTIME_CHECKPOINT_MS = 5 * 60 * 1000;      // 5 хвилин
constexpr uint32_t STATE_POLL_PERIOD_MS = 10000;
