#pragma once
#include <stdint.h>
#include <stddef.h>

// Карта EEPROM (емуляція поверх NVS):
//   0..7     uint64  мотогодини, мс
//   8..11    float   поріг старту по АКБ, В
//   12..15   float   поріг стопу по АКБ, В
//   16       uint8   режим (GenMode), 0xFF = не записувався
//   17       uint8   авто-режим насоса: 1 авто, 0 ручний, 0xFF = не записувався
//   18..21   uint32  лічильник завантажень
//   22       uint8   причина попереднього скидання
//   23       uint8   віддалений модуль: 1 увімкнений, 0 вимкнений, 0xFF = не записувався
//   24..27   uint8×4 IP віддаленого модуля напруги
//   28       uint8   WiFi: 0xA5 перевірені, 0x5A нові (не перевірені)
//   29..61   char    SSID
//   62..126  char    пароль
//   127      uint8   0xA5 — є попередні WiFi-дані для відкату
//   128..160 char    попередній SSID
//   161..225 char    попередній пароль
//   226..255         резерв
// Незаписані байти = 0xFF (після розширення блоку старі дані зберігаються).
#define EEPROM_SIZE                  256
#define EEPROM_TOTAL_RUNTIME_MS_ADDR 0
#define EEPROM_GEN_BAT_MIN_V_ADDR    8
#define EEPROM_GEN_BAT_MAX_V_ADDR    12
#define EEPROM_GEN_MODE_ADDR         16
#define EEPROM_PUMP_AUTO_ADDR        17
#define EEPROM_BOOT_COUNT_ADDR       18
#define EEPROM_LAST_RESET_ADDR       22
#define EEPROM_REMOTE_ENABLED_ADDR   23
#define EEPROM_REMOTE_IP_ADDR        24
#define EEPROM_WIFI_MARKER_ADDR      28
#define EEPROM_WIFI_SSID_ADDR        29
#define EEPROM_WIFI_PASS_ADDR        62
#define EEPROM_WIFI_PREV_MARKER_ADDR 127
#define EEPROM_WIFI_PREV_SSID_ADDR   128
#define EEPROM_WIFI_PREV_PASS_ADDR   161

constexpr size_t WIFI_SSID_MAX = 32;
constexpr size_t WIFI_PASS_MAX = 64;

void initEEPROM();

void     saveEepromTotalRuntimeMs(uint64_t ms);
uint64_t loadEepromTotalRuntimeMs();

void saveEepromGenBatThresholds(float minV, float maxV);
// false, якщо пороги ще не записувались або пошкоджені
bool loadEepromGenBatThresholds(float &minV, float &maxV);

// Сире значення GenMode; перевірку діапазону робить викликач
void    saveEepromGenMode(uint8_t mode);
uint8_t loadEepromGenMode(); // 0xFF, якщо не записувався

void saveEepromPumpAuto(bool enabled);
// false, якщо ще не записувався
bool loadEepromPumpAuto(bool &enabled);

// +1 до лічильника завантажень і запис причини поточного скидання
void bumpEepromBootRecord(uint8_t resetReason, uint32_t &bootCountOut, uint8_t &prevResetOut);

void saveEepromRemoteEnabled(bool enabled);
// false, якщо ще не записувався
bool loadEepromRemoteEnabled(bool &enabled);

void saveEepromRemoteIp(const uint8_t ip[4]);
bool loadEepromRemoteIp(uint8_t ip[4]);

enum class WifiSlot : uint8_t { Empty, Verified, Pending };
void     saveEepromWifi(const char *ssid, const char *pass, bool pending);
WifiSlot loadEepromWifi(char *ssid, char *pass);
void     markEepromWifiVerified();
void     clearEepromWifi();

void saveEepromWifiPrev(const char *ssid, const char *pass);
bool loadEepromWifiPrev(char *ssid, char *pass);
void clearEepromWifiPrev();
