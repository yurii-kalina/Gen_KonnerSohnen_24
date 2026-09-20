#pragma once
#include <Arduino.h>

struct FuelPumpStatus {
    bool        running;
    uint32_t    runtimeSec;
    bool        autoEnabled;
    const char *lastStopReason; // "" / "reached_full" / "max_runtime" / "dry_run" / "sensor_fault" / "overflow_alarm" / "manual_stop" / "manual_duration_done"
    bool        overflowAlarm;  // GPIO34 в аварійному рівні
    bool        sensorOk;       // датчик A1
    uint16_t    levelAdc;
    bool        levelLow;
    bool        levelHigh;
    uint8_t     levelPercent; // 0% = FUEL_LEVEL_LOW_ADC, 100% = FUEL_LEVEL_HIGH_ADC
};

void initFuelPump();
// Call from loop(); runs the level control and protections. The overflow
// alarm is checked on every call, the rest once per PUMP_TICK_MS.
void updateFuelPump();

// Manual run for N seconds (1..PUMP_MAX_RUNTIME_SEC); max_runtime and the
// overflow alarm still apply. false while the overflow alarm is active.
bool requestPumpStart(uint16_t seconds);
// Also disables auto mode (re-enable with setPumpAutoEnabled(true))
void requestPumpStop();
// Enabling also clears the lockout after a protection trip
void setPumpAutoEnabled(bool enabled);

FuelPumpStatus getFuelPumpStatus();

// Raw ADC -> 0..100 %. fullAdc < emptyAdc means an inverted sender.
uint8_t fuelLevelPercent(uint16_t adc, uint16_t emptyAdc, uint16_t fullAdc);
// Outside FUEL_SENSOR_MIN_ADC..MAX_ADC = open or shorted sender
bool fuelSensorInRange(uint16_t adc);
