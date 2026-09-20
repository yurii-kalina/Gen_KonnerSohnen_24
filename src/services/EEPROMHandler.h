#pragma once
#include <stdint.h>

// Карта EEPROM (емуляція поверх NVS):
//   0..7    uint64  мотогодини, мс
//   8..11   float   поріг старту по АКБ, В
//   12..15  float   поріг стопу по АКБ, В
//   16..31          резерв
// Незаписані байти = 0xFF (після розширення блоку старі дані зберігаються).
#define EEPROM_SIZE                  32
#define EEPROM_TOTAL_RUNTIME_MS_ADDR 0
#define EEPROM_GEN_BAT_MIN_V_ADDR    8
#define EEPROM_GEN_BAT_MAX_V_ADDR    12

void initEEPROM();

void     saveEepromTotalRuntimeMs(uint64_t ms);
uint64_t loadEepromTotalRuntimeMs();

void saveEepromGenBatThresholds(float minV, float maxV);
// false, якщо пороги ще не записувались або пошкоджені
bool loadEepromGenBatThresholds(float &minV, float &maxV);
