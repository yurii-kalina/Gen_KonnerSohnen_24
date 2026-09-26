#pragma once
#include <Arduino.h>


// #define SERIAL_LOG_ENABLED
// #define SERIAL_CONSOLE_ENABLED

constexpr const char *HOST_NAME = "KonnerSohnen24";
constexpr const char *DEFAULT_WIFI_SSID = "Home";
constexpr const char *DEFAULT_WIFI_PASS = "31023102";

// remote voltage module for remote_voltage mode (GET http://<IP>/status -> "voltage");
// local source (A0) is always on, the remote one is enabled via /config/sources
constexpr bool EXT_BAT_REMOTE_DEFAULT = false;
constexpr const char *EXT_BAT_REMOTE_DEFAULT_IP = "192.168.3.235";

// relay GPIO level that closes the contact: R1 control, R2 mode / R3 pump
constexpr uint8_t RELAY_ACTIVE_LEVEL = LOW;
constexpr uint8_t RELAY_PUMP_ACTIVE_LEVEL = HIGH;

// generator modes, stored in EEPROM: never renumber, only append
enum class GenMode : uint8_t
{
  Manual = 0,
  Local = 1,
  Generator = 2,
  Remote = 3
};
// mode until one is saved in EEPROM
constexpr GenMode GEN_MODE_DEFAULT = GenMode::Manual;

// voltage thresholds for generator auto-start/stop (V), until changed via /mode/thresholds
constexpr float GEN_AUTO_BAT_MIN_V = 23.6f;
constexpr float GEN_AUTO_BAT_MAX_V = 27.0f;
// accepted threshold range in requests: 0..LIMIT
constexpr float GEN_AUTO_BAT_LIMIT_V = 100.0f;

static_assert(GEN_AUTO_BAT_MIN_V < GEN_AUTO_BAT_MAX_V, "GEN_AUTO_BAT_MIN_V must be below GEN_AUTO_BAT_MAX_V");

// calibration constant for battery voltage (ADS1115 A0): real_voltage / raw
constexpr float CALIBRATE_VOLTAGE_BAT = 0.00204666f;

// panel lamps RUN / OIL / OVERLOAD: GPIO level while lit
constexpr uint8_t LAMP_ACTIVE_LEVEL = HIGH;

// ADS1115 settings
constexpr uint8_t ADC_SAMPLES = 10;
constexpr uint32_t ADS_STALE_MS = 15000;
constexpr uint8_t ADS_BEGIN_ATTEMPTS = 10;
constexpr uint16_t ADS_BEGIN_RETRY_MS = 200;
constexpr uint8_t ADS_I2C_ADDR = 0x48;

// fuel sender raw range; outside = open or shorted sender
constexpr uint16_t FUEL_SENSOR_MIN_ADC = 100;
constexpr uint16_t FUEL_SENSOR_MAX_ADC = 32000;

// fuel level sensor external tank (A2, display only); FULL < EMPTY = inverted sender
constexpr uint16_t FUEL_EXT_EMPTY_ADC = 14800;
constexpr uint16_t FUEL_EXT_FULL_ADC = 3800;

// generator run with no internet (manual mode only)
constexpr uint32_t NET_LOSS_AUTO_STOP_MS = 5UL * 60 * 60 * 1000;

// -----------------------------------------------------------------------------
// Насос переливу палива (A1 + R3)
// -----------------------------------------------------------------------------
// pump mode until one is saved in EEPROM
constexpr bool PUMP_AUTO_DEFAULT = false;
// fuel level sensor generator tank: pump starts at LOW, stops at HIGH
constexpr uint16_t FUEL_LEVEL_LOW_ADC = 8000;
constexpr uint16_t FUEL_LEVEL_HIGH_ADC = 14000;
constexpr uint32_t PUMP_MAX_RUNTIME_SEC = 360;
// dry run: the level must move toward full by DELTA within each window
constexpr uint32_t PUMP_DRY_RUN_WINDOW_MS = 100 * 1000;
constexpr uint16_t FUEL_DRY_RUN_MIN_DELTA_ADC = 50;
// emergency overflow sensor (GPIO34)
constexpr uint8_t FUEL_OVERFLOW_ALARM_LEVEL = LOW;
constexpr uint32_t FUEL_OVERFLOW_DEBOUNCE_MS = 100;

// Watchdog
constexpr uint32_t LOOP_WDT_TIMEOUT_S = 60;

// -----------------------------------------------------------------------------
// Логування
// -----------------------------------------------------------------------------
constexpr uint8_t LOG_DEFAULT_SERIAL_LEVEL = 0; // RLOG_DEBUG
constexpr uint8_t LOG_DEFAULT_NET_LEVEL = 1;    // RLOG_INFO

// Log backend URL
constexpr const char *LOG_BACKEND_URL = "http://192.168.121.252:5801/api/device-logs";
