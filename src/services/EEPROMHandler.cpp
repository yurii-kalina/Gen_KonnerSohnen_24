#include "EEPROMHandler.h"
#include <EEPROM.h>
#include <math.h>

void initEEPROM() {
    EEPROM.begin(EEPROM_SIZE);
}

void saveEepromTotalRuntimeMs(uint64_t ms) {
    for (int i = 0; i < 8; ++i) {
        EEPROM.write(EEPROM_TOTAL_RUNTIME_MS_ADDR + i, (uint8_t)((ms >> (8 * i)) & 0xFF));
    }
    EEPROM.commit();
}

uint64_t loadEepromTotalRuntimeMs() {
    uint64_t ms = 0;
    for (int i = 0; i < 8; ++i) {
        ms |= (uint64_t)EEPROM.read(EEPROM_TOTAL_RUNTIME_MS_ADDR + i) << (8 * i);
    }
    if (ms == 0xFFFFFFFFFFFFFFFFULL) return 0ULL;
    return ms;
}

void saveEepromGenBatThresholds(float minV, float maxV) {
    EEPROM.put(EEPROM_GEN_BAT_MIN_V_ADDR, minV);
    EEPROM.put(EEPROM_GEN_BAT_MAX_V_ADDR, maxV);
    EEPROM.commit();
}

bool loadEepromGenBatThresholds(float &minV, float &maxV) {
    float lo = NAN;
    float hi = NAN;
    EEPROM.get(EEPROM_GEN_BAT_MIN_V_ADDR, lo);
    EEPROM.get(EEPROM_GEN_BAT_MAX_V_ADDR, hi);
    // 0xFF..FF читається як NaN — порогів ще немає
    if (!isfinite(lo) || !isfinite(hi) || lo >= hi) return false;
    minV = lo;
    maxV = hi;
    return true;
}
