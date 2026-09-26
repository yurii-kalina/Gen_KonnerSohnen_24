#pragma once
#include <Arduino.h>

struct FuelPumpStatus {
    bool        running;
    bool        manualRun;      // поточний запуск ручний
    uint32_t    runtimeSec;
    uint32_t    limitSec;       // ліміт поточного запуску (N ручного або PUMP_MAX_RUNTIME_SEC авто), 0 якщо стоїть
    bool        autoEnabled;
    const char *lastStopReason; // "" / "reached_full" / "gen_stopped" / "max_runtime" / "dry_run" / "fuel_sensor_lost" / "overflow_alarm" / "manual_stop" / "ota"
    bool        overflowAlarm;  // GPIO34 в аварійному рівні
};

// Auto mode comes from EEPROM (PUMP_AUTO_DEFAULT if never saved). Call after initEEPROM().
void initFuelPump();
// Call from loop(); runs the level control and protections. The overflow
// alarm is checked on every call, the rest once per PUMP_TICK_MS.
// Auto start needs the generator running; auto run stops when it stops.
void updateFuelPump();

// Manual run for N seconds (1..PUMP_MAX_RUNTIME_SEC), counted from now even if
// the pump was already running. Disables auto mode. Only the time limit and the
// overflow alarm apply. false while the overflow alarm is active or N is out of range.
bool requestPumpStart(uint32_t seconds);
// Also disables auto mode (re-enable with setPumpAutoEnabled(true))
void requestPumpStop();
// Stops a running pump without touching auto mode (OTA)
void pumpAbort(const char *reason);
// Enabling also clears the lockout after a protection trip
void setPumpAutoEnabled(bool enabled);

FuelPumpStatus getFuelPumpStatus();

// Raw ADC -> 0..100 %. fullAdc < emptyAdc means an inverted sender.
uint8_t fuelLevelPercent(uint16_t adc, uint16_t emptyAdc, uint16_t fullAdc);
// Outside FUEL_SENSOR_MIN_ADC..MAX_ADC = open or shorted sender
bool fuelSensorInRange(uint16_t adc);
