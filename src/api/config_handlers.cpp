#include "web_handlers.h"
#include "http_json.h"
#include "json_views.h"
#include "generator/generator_ops.h"
#include "services/EEPROMHandler.h"
#include "services/GenAuto.h"
#include "services/settings.h"
#include "services/WiFiHandler.h"

namespace
{
  void sendMode()
  {
    sendJson(200, [](JsonDocument &d) {
      d["ok"] = true;
      d["mode"] = genModeName(getGenMode());
      fillAuto(d["auto"].to<JsonObject>());
      fillSources(d["sources"].to<JsonObject>());
    });
  }

  void sendConfig(const char *status)
  {
    sendJson(200, [&](JsonDocument &d) {
      d["ok"] = true;
      if (status != nullptr)
        d["status"] = status;
      fillSources(d["sources"].to<JsonObject>());
      fillWifi(d["wifi"].to<JsonObject>());
    });
  }
}

void handleGetMode()
{
  sendMode();
}

// POST /mode  body: {"mode": "manual" | "local_voltage" | "remote_voltage" | "generator"}
void handleSetMode()
{
  static const char *hint = "{\"mode\":\"manual|local_voltage|remote_voltage|generator\"}";
  JsonDocument doc;
  if (!parseJsonBody(doc, hint))
    return;
  GenMode m;
  if (!doc["mode"].is<const char *>() || !parseGenMode(doc["mode"].as<const char *>(), m))
  {
    sendError(400, "invalid_request", "mode must be manual, local_voltage, remote_voltage or generator", hint);
    return;
  }
  if (!genModeAvailable(m))
  {
    sendError(409, "source_disabled",
              String("voltage source for ") + genModeName(m) + " is disabled, enable it via /config/sources");
    return;
  }
  setGenMode(m, "http");
  sendMode();
}

// POST /mode/thresholds  body: {"minV": float, "maxV": float}
// Пороги режимів local_voltage і remote_voltage, зберігаються в EEPROM.
void handleSetThresholds()
{
  static const char *hint = "{\"minV\":23.6,\"maxV\":27.0}";
  JsonDocument doc;
  if (!parseJsonBody(doc, hint))
    return;
  if (!doc["minV"].is<float>() || !doc["maxV"].is<float>())
  {
    sendError(400, "invalid_request", "minV and maxV must be numbers", hint);
    return;
  }
  if (!setGenAutoThresholds(doc["minV"].as<float>(), doc["maxV"].as<float>()))
  {
    sendError(400, "out_of_range", String("need 0 <= minV < maxV <= ") + String(GEN_AUTO_BAT_LIMIT_V, 0), hint);
    return;
  }
  sendMode();
}

// POST /config/sources  body: {"remote": bool}
// Local source (A0) is always on and cannot be disabled.
void handleSetSources()
{
  static const char *hint = "{\"remote\":true}";
  JsonDocument doc;
  if (!parseJsonBody(doc, hint))
    return;
  if (doc["local"].is<bool>() && !doc["local"].as<bool>())
  {
    sendError(400, "out_of_range", "local voltage source (A0) is always enabled", hint);
    return;
  }
  if (!doc["remote"].is<bool>())
  {
    sendError(400, "invalid_request", "remote must be true or false", hint);
    return;
  }
  setExtBatRemoteEnabled(doc["remote"].as<bool>());
  genModeSourcesChanged();
  sendMode();
}

void handleGetConfig()
{
  sendConfig(nullptr);
}

// POST /config/remote  body: {"ip": "192.168.3.235"}
void handleSetRemote()
{
  static const char *hint = "{\"ip\":\"192.168.3.235\"}";
  JsonDocument doc;
  if (!parseJsonBody(doc, hint))
    return;
  IPAddress ip;
  if (!doc["ip"].is<const char *>() || !ip.fromString(doc["ip"].as<const char *>()))
  {
    sendError(400, "invalid_request", "ip must be a dotted IPv4 string", hint);
    return;
  }
  setRemoteModuleIp(ip);
  sendConfig(nullptr);
}

// POST /config/wifi  body: {"ssid": "..", "password": ".."} or {"reset": true}
// Reconnects 0.5 s after the reply; rolls back if no IP within 60 s.
void handleSetWifi()
{
  static const char *hint = "{\"ssid\":\"MyNetwork\",\"password\":\"secret\"} or {\"reset\":true}";
  JsonDocument doc;
  if (!parseJsonBody(doc, hint))
    return;

  if (doc["reset"].is<bool>() && doc["reset"].as<bool>())
  {
    resetWifiCreds();
  }
  else
  {
    if (!doc["ssid"].is<const char *>() || !doc["password"].is<const char *>())
    {
      sendError(400, "invalid_request", "ssid and password must be strings", hint);
      return;
    }
    const char *ssid = doc["ssid"].as<const char *>();
    const char *pass = doc["password"].as<const char *>();
    if (strlen(ssid) == 0 || strlen(ssid) > WIFI_SSID_MAX || strlen(pass) > WIFI_PASS_MAX)
    {
      sendError(400, "out_of_range", "ssid 1..32 chars, password up to 64 chars", hint);
      return;
    }
    setWifiCreds(ssid, pass);
  }

  sendConfig("reconnecting");
  wifiApplyCreds();
}
