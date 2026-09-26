#include "EEPROMHandler.h"
#include <EEPROM.h>
#include <math.h>
#include <string.h>

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

void saveEepromGenMode(uint8_t mode) {
    EEPROM.write(EEPROM_GEN_MODE_ADDR, mode);
    EEPROM.commit();
}

uint8_t loadEepromGenMode() {
    return EEPROM.read(EEPROM_GEN_MODE_ADDR);
}

void saveEepromPumpAuto(bool enabled) {
    EEPROM.write(EEPROM_PUMP_AUTO_ADDR, enabled ? 1 : 0);
    EEPROM.commit();
}

bool loadEepromPumpAuto(bool &enabled) {
    const uint8_t v = EEPROM.read(EEPROM_PUMP_AUTO_ADDR);
    if (v != 0 && v != 1) return false;
    enabled = v == 1;
    return true;
}

void bumpEepromBootRecord(uint8_t resetReason, uint32_t &bootCountOut, uint8_t &prevResetOut) {
    uint32_t count = 0;
    for (int i = 0; i < 4; ++i) {
        count |= (uint32_t)EEPROM.read(EEPROM_BOOT_COUNT_ADDR + i) << (8 * i);
    }
    if (count == 0xFFFFFFFFUL) count = 0;

    prevResetOut = EEPROM.read(EEPROM_LAST_RESET_ADDR);
    if (prevResetOut == 0xFF) prevResetOut = 0;

    ++count;
    for (int i = 0; i < 4; ++i) {
        EEPROM.write(EEPROM_BOOT_COUNT_ADDR + i, (uint8_t)((count >> (8 * i)) & 0xFF));
    }
    EEPROM.write(EEPROM_LAST_RESET_ADDR, resetReason);
    EEPROM.commit();

    bootCountOut = count;
}

void saveEepromRemoteEnabled(bool enabled) {
    EEPROM.write(EEPROM_REMOTE_ENABLED_ADDR, enabled ? 1 : 0);
    EEPROM.commit();
}

bool loadEepromRemoteEnabled(bool &enabled) {
    const uint8_t v = EEPROM.read(EEPROM_REMOTE_ENABLED_ADDR);
    if (v != 0 && v != 1) return false;
    enabled = v == 1;
    return true;
}

void saveEepromRemoteIp(const uint8_t ip[4]) {
    for (int i = 0; i < 4; ++i) EEPROM.write(EEPROM_REMOTE_IP_ADDR + i, ip[i]);
    EEPROM.commit();
}

bool loadEepromRemoteIp(uint8_t ip[4]) {
    uint8_t b[4];
    bool blank = true;
    for (int i = 0; i < 4; ++i) {
        b[i] = EEPROM.read(EEPROM_REMOTE_IP_ADDR + i);
        if (b[i] != 0xFF) blank = false;
    }
    if (blank) return false;
    memcpy(ip, b, 4);
    return true;
}

static constexpr uint8_t WIFI_VERIFIED = 0xA5;
static constexpr uint8_t WIFI_PENDING = 0x5A;

static void writeStr(int addr, const char *s, size_t maxLen) {
    size_t i = 0;
    for (; i < maxLen && s[i] != '\0'; ++i) EEPROM.write(addr + i, (uint8_t)s[i]);
    for (; i <= maxLen; ++i) EEPROM.write(addr + i, 0);
}

static void readStr(int addr, char *out, size_t maxLen) {
    for (size_t i = 0; i < maxLen; ++i) out[i] = (char)EEPROM.read(addr + i);
    out[maxLen] = '\0';
}

void saveEepromWifi(const char *ssid, const char *pass, bool pending) {
    writeStr(EEPROM_WIFI_SSID_ADDR, ssid, WIFI_SSID_MAX);
    writeStr(EEPROM_WIFI_PASS_ADDR, pass, WIFI_PASS_MAX);
    EEPROM.write(EEPROM_WIFI_MARKER_ADDR, pending ? WIFI_PENDING : WIFI_VERIFIED);
    EEPROM.commit();
}

WifiSlot loadEepromWifi(char *ssid, char *pass) {
    const uint8_t m = EEPROM.read(EEPROM_WIFI_MARKER_ADDR);
    if (m != WIFI_VERIFIED && m != WIFI_PENDING) return WifiSlot::Empty;
    readStr(EEPROM_WIFI_SSID_ADDR, ssid, WIFI_SSID_MAX);
    readStr(EEPROM_WIFI_PASS_ADDR, pass, WIFI_PASS_MAX);
    if (ssid[0] == '\0') return WifiSlot::Empty;
    return m == WIFI_PENDING ? WifiSlot::Pending : WifiSlot::Verified;
}

void markEepromWifiVerified() {
    EEPROM.write(EEPROM_WIFI_MARKER_ADDR, WIFI_VERIFIED);
    EEPROM.write(EEPROM_WIFI_PREV_MARKER_ADDR, 0xFF);
    EEPROM.commit();
}

void clearEepromWifi() {
    EEPROM.write(EEPROM_WIFI_MARKER_ADDR, 0xFF);
    EEPROM.write(EEPROM_WIFI_PREV_MARKER_ADDR, 0xFF);
    EEPROM.commit();
}

void saveEepromWifiPrev(const char *ssid, const char *pass) {
    writeStr(EEPROM_WIFI_PREV_SSID_ADDR, ssid, WIFI_SSID_MAX);
    writeStr(EEPROM_WIFI_PREV_PASS_ADDR, pass, WIFI_PASS_MAX);
    EEPROM.write(EEPROM_WIFI_PREV_MARKER_ADDR, WIFI_VERIFIED);
    EEPROM.commit();
}

bool loadEepromWifiPrev(char *ssid, char *pass) {
    if (EEPROM.read(EEPROM_WIFI_PREV_MARKER_ADDR) != WIFI_VERIFIED) return false;
    readStr(EEPROM_WIFI_PREV_SSID_ADDR, ssid, WIFI_SSID_MAX);
    readStr(EEPROM_WIFI_PREV_PASS_ADDR, pass, WIFI_PASS_MAX);
    return ssid[0] != '\0';
}

void clearEepromWifiPrev() {
    EEPROM.write(EEPROM_WIFI_PREV_MARKER_ADDR, 0xFF);
    EEPROM.commit();
}
