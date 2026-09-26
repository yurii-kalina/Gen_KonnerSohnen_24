#pragma once
#include <Arduino.h>

// Налаштування, що змінюються в рантаймі (EEPROM, значення за замовчуванням — config.h)
void settingsInit();

// Local source (A0) is always on; the remote module may be absent
bool extBatRemoteEnabled();
void setExtBatRemoteEnabled(bool enabled);

IPAddress remoteModuleIp();
void setRemoteModuleIp(const IPAddress &ip);

const char *wifiSsid();
const char *wifiPass();
bool wifiFromEeprom();
// Нові дані вважаються неперевіреними, поки плата не отримає з ними IP
void setWifiCreds(const char *ssid, const char *pass);
void resetWifiCreds();

bool wifiPending();
void wifiConfirm();
// Назад до попередніх перевірених даних (або config.h)
void wifiRevert();
