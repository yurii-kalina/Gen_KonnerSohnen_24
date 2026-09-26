#pragma once
#include <stdint.h>

// Режими local_voltage (АКБ на A0) і remote_voltage (віддалений модуль):
// керування R1 по напрузі АКБ. Пороги MIN/MAX спільні, зберігаються в EEPROM
// (initGenAuto() викликати після initEEPROM()). Замикає R1 при вході напруги
// в зону MIN і розмикає при вході в зону MAX. В інших режимах нічого не робить.
// Немає даних — R1 лишається як є, режим не змінюється.
struct GenAutoStatus
{
  bool active;       // режим local_voltage або remote_voltage
  float minV;
  float maxV;
  bool voltageValid;
  float voltage;     // з джерела поточного режиму
  const char *zone;  // "low" | "normal" | "high" | "unknown"
  uint32_t dataLostSec;
};

void initGenAuto();
void updateGenAuto();
// Забути, на яку зону вже відреагували — поточна напруга оцінюється заново
void resetGenAuto();

// false — пороги відхилено (не числа, поза 0..GEN_AUTO_BAT_LIMIT_V або min >= max)
bool setGenAutoThresholds(float minV, float maxV);
float getGenAutoMinV();
float getGenAutoMaxV();
GenAutoStatus getGenAutoStatus();
