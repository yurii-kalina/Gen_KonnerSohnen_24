#include "settings.h"
#include "config/config.h"
#include "services/EEPROMHandler.h"
#include "services/logger.h"

static const char *const TAG = "cfg";

namespace
{
  bool gRemote = EXT_BAT_REMOTE_DEFAULT;
  IPAddress gRemoteIp;
  char gSsid[WIFI_SSID_MAX + 1] = {};
  char gPass[WIFI_PASS_MAX + 1] = {};
  bool gWifiFromEeprom = false;
  bool gWifiPending = false;

  void loadWifiDefaults()
  {
    strlcpy(gSsid, DEFAULT_WIFI_SSID, sizeof(gSsid));
    strlcpy(gPass, DEFAULT_WIFI_PASS, sizeof(gPass));
    gWifiFromEeprom = false;
  }
}

void settingsInit()
{
  if (!loadEepromRemoteEnabled(gRemote))
    gRemote = EXT_BAT_REMOTE_DEFAULT;

  uint8_t ip[4];
  if (loadEepromRemoteIp(ip))
    gRemoteIp = IPAddress(ip[0], ip[1], ip[2], ip[3]);
  else
    gRemoteIp.fromString(EXT_BAT_REMOTE_DEFAULT_IP);

  const WifiSlot slot = loadEepromWifi(gSsid, gPass);
  gWifiFromEeprom = slot != WifiSlot::Empty;
  gWifiPending = slot == WifiSlot::Pending;
  if (!gWifiFromEeprom)
    loadWifiDefaults();

  RLOGI(TAG, "settings: remote module %s %s, wifi \"%s\" from %s%s",
        gRemote ? "enabled" : "disabled", gRemoteIp.toString().c_str(), gSsid,
        gWifiFromEeprom ? "EEPROM" : "config.h", gWifiPending ? " (NEW, not verified yet)" : "");
}

bool extBatRemoteEnabled() { return gRemote; }

void setExtBatRemoteEnabled(bool enabled)
{
  if (enabled == gRemote)
    return;
  RLOGI(TAG, "remote module %s", enabled ? "ENABLED" : "DISABLED");
  gRemote = enabled;
  saveEepromRemoteEnabled(enabled);
}

IPAddress remoteModuleIp() { return gRemoteIp; }

void setRemoteModuleIp(const IPAddress &ip)
{
  if (ip == gRemoteIp)
    return;
  RLOGI(TAG, "remote module IP: %s -> %s", gRemoteIp.toString().c_str(), ip.toString().c_str());
  gRemoteIp = ip;
  const uint8_t b[4] = {ip[0], ip[1], ip[2], ip[3]};
  saveEepromRemoteIp(b);
}

const char *wifiSsid() { return gSsid; }
const char *wifiPass() { return gPass; }
bool wifiFromEeprom() { return gWifiFromEeprom; }

void setWifiCreds(const char *ssid, const char *pass)
{
  if (!gWifiPending)
  {
    if (gWifiFromEeprom)
      saveEepromWifiPrev(gSsid, gPass);
    else
      clearEepromWifiPrev();
  }
  strlcpy(gSsid, ssid, sizeof(gSsid));
  strlcpy(gPass, pass, sizeof(gPass));
  gWifiFromEeprom = true;
  gWifiPending = true;
  saveEepromWifi(gSsid, gPass, true);
  RLOGW(TAG, "WiFi credentials changed to \"%s\" — reconnecting, rollback if it does not connect in 60 s", gSsid);
}

void resetWifiCreds()
{
  clearEepromWifi();
  loadWifiDefaults();
  gWifiPending = false;
  RLOGW(TAG, "WiFi credentials reset to config.h (\"%s\")", gSsid);
}

bool wifiPending() { return gWifiPending; }

void wifiConfirm()
{
  if (!gWifiPending)
    return;
  gWifiPending = false;
  markEepromWifiVerified();
  RLOGI(TAG, "new WiFi credentials \"%s\" verified", gSsid);
}

void wifiRevert()
{
  if (!gWifiPending)
    return;
  const String failed = gSsid;
  char ssid[WIFI_SSID_MAX + 1], pass[WIFI_PASS_MAX + 1];
  if (loadEepromWifiPrev(ssid, pass))
  {
    strlcpy(gSsid, ssid, sizeof(gSsid));
    strlcpy(gPass, pass, sizeof(gPass));
    gWifiFromEeprom = true;
    saveEepromWifi(gSsid, gPass, false);
    clearEepromWifiPrev();
  }
  else
  {
    clearEepromWifi();
    loadWifiDefaults();
  }
  gWifiPending = false;
  RLOGE(TAG, "new WiFi \"%s\" did not connect — rolled back to \"%s\" (%s)",
        failed.c_str(), gSsid, gWifiFromEeprom ? "EEPROM" : "config.h");
}
