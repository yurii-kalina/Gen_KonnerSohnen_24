#include "json_views.h"

#include <math.h>
#include <WiFi.h>
#include "api/http_json.h"
#include "api/web_server.h"
#include "config/config.h"
#include "drivers/ext_bat_remote.h"
#include "drivers/ks24_bus.h"
#include "drivers/lamps.h"
#include "drivers/sensor_cache.h"
#include "generator/generator_ops.h"
#include "generator/generator_state.h"
#include "services/FuelPump.h"
#include "services/GenAuto.h"
#include "services/diagnostics.h"
#include "services/log_uploader.h"
#include "services/logger.h"
#include "services/settings.h"

namespace
{
  void setFloat(JsonObject o, const char *key, float value, bool valid = true)
  {
    if (!valid || isnan(value) || isinf(value))
      o[key] = nullptr;
    else
      o[key] = value;
  }

  void setStr(JsonObject o, const char *key, const char *value)
  {
    if (value == nullptr || value[0] == '\0')
      o[key] = nullptr;
    else
      o[key] = value;
  }

  void setLevel(JsonObject o, uint8_t level, uint16_t raw, bool valid)
  {
    if (valid)
      o["level"] = level;
    else
      o["level"] = nullptr;
    o["raw"] = raw;
    o["valid"] = valid;
  }
}

void setRuntimeHours(JsonObject o)
{
  // Десяті години, відкинуті вниз: 42.8 = 42 год 48..53 хв.
  const uint64_t tenths = gTotalRuntimeMs / 360000ULL;
  char buf[24];
  snprintf(buf, sizeof(buf), "%llu.%u", (unsigned long long)(tenths / 10ULL), (unsigned)(tenths % 10ULL));
  o["runtimeHours"] = serialized(String(buf));
}

void fillGenerator(JsonObject o, const StatusData &s)
{
  o["running"] = s.generatorRun;
  o["runLamp"] = s.lampRun == RunLampState::On;
  o["runLampState"] = runLampStateName(s.lampRun); // on | blinking | off
  o["oilAlert"] = s.oilAlert;
  o["overload"] = s.overload;
  setRuntimeHours(o);
  o["mode"] = genModeName(s.mode);
  o["runCommanded"] = s.runCommanded; // R1 closed
}

// Generator output = the KS24 inverter -> display bus
void fillOutput(JsonObject o, const StatusData &s)
{
  const bool v = s.bus.valid;
  o["valid"] = v;
  o["running"] = s.bus.running;
  setFloat(o, "voltage", s.bus.voltage, v);
  setFloat(o, "current", s.bus.current, v);
  setFloat(o, "power", s.bus.voltage * s.bus.current, v);
  if (v)
  {
    o["rpm"] = s.bus.rpm;
    o["tempGuess"] = s.bus.tempGuess;
  }
  else
  {
    o["rpm"] = nullptr;
    o["tempGuess"] = nullptr;
  }
}

void fillBattery(JsonObject o, const StatusData &s)
{
  JsonObject ext = o["external"].to<JsonObject>(); // A0
  setFloat(ext, "voltage", s.batVoltage, s.batValid);
  ext["raw"] = s.batRaw;
  ext["valid"] = s.batValid;

  const RemoteBatReading r = getRemoteBatReading();
  JsonObject rem = o["remote"].to<JsonObject>();
  setFloat(rem, "voltage", r.voltage, r.valid);
  rem["valid"] = r.valid;
}

void fillFuel(JsonObject o, const StatusData &s)
{
  setLevel(o["generator"].to<JsonObject>(), s.fuelGenPercent, s.fuelGenRaw, s.fuelGenValid); // A1
  setLevel(o["external"].to<JsonObject>(), s.fuelExtPercent, s.fuelExtRaw, s.fuelExtValid);  // A2
}

void fillAuto(JsonObject o)
{
  const GenAutoStatus a = getGenAutoStatus();
  o["minV"] = a.minV;
  o["maxV"] = a.maxV;
  if (!a.active)
  {
    o["voltage"] = nullptr;
    o["voltageValid"] = false;
    o["zone"] = nullptr;
    o["dataLostSec"] = 0;
    return;
  }
  setFloat(o, "voltage", a.voltage, a.voltageValid);
  o["voltageValid"] = a.voltageValid;
  o["zone"] = a.zone;
  o["dataLostSec"] = a.dataLostSec;
}

void fillSources(JsonObject o)
{
  o["local"] = true; // A0, always on
  o["remote"] = extBatRemoteEnabled();
  o["remoteIp"] = remoteModuleIp().toString();
}

void fillPump(JsonObject o)
{
  const FuelPumpStatus p = getFuelPumpStatus();
  o["running"] = p.running;
  o["manual"] = p.manualRun;
  o["runtimeSec"] = p.runtimeSec;
  o["limitSec"] = p.limitSec;
  o["maxRuntimeSec"] = PUMP_MAX_RUNTIME_SEC;
  o["autoEnabled"] = p.autoEnabled;
  setStr(o, "lastStopReason", p.lastStopReason);
  o["overflowAlarm"] = p.overflowAlarm;
}

void fillWifi(JsonObject o)
{
  const bool connected = WiFi.status() == WL_CONNECTED;
  o["status"] = diagWifiStatusName();
  o["ssid"] = wifiSsid();
  o["source"] = wifiFromEeprom() ? "eeprom" : "config";
  o["pending"] = wifiPending();
  if (connected)
  {
    o["ip"] = WiFi.localIP().toString();
    o["rssi"] = WiFi.RSSI();
  }
  else
  {
    o["ip"] = nullptr;
    o["rssi"] = nullptr;
  }
}

void fillStatus(JsonDocument &d)
{
  d["ok"] = true;
  d["host"] = HOST_NAME;
  d["uptimeSec"] = millis() / 1000UL;
  const StatusData s = readStatus();
  fillGenerator(d["generator"].to<JsonObject>(), s);
  fillOutput(d["output"].to<JsonObject>(), s);
  fillBattery(d["battery"].to<JsonObject>(), s);
  fillFuel(d["fuel"].to<JsonObject>(), s);
  fillAuto(d["auto"].to<JsonObject>());
  fillPump(d["pump"].to<JsonObject>());
}

void fillDiag(JsonDocument &d)
{
  d["ok"] = true;

  JsonObject board = d["board"].to<JsonObject>();
  board["host"] = HOST_NAME;
  char bootId[9];
  snprintf(bootId, sizeof(bootId), "%08lX", (unsigned long)diagBootId());
  board["bootId"] = bootId;
  board["bootCount"] = diagBootCount();
  board["resetReason"] = diagResetReasonName();
  board["prevResetReason"] = diagPrevResetReasonName();
  board["uptimeSec"] = millis() / 1000UL;
  board["freeHeap"] = ESP.getFreeHeap();
  board["minFreeHeap"] = diagMinFreeHeap();
  board["firmware"] = __DATE__ " " __TIME__;

  fillWifi(d["wifi"].to<JsonObject>());

  JsonObject ads = d["ads"].to<JsonObject>();
  ads["available"] = sensorsAdsAvailable();
  ads["busRecoveries"] = sensorsBusRecoveries();

  const Ks24Data bus = getKs24Data();
  const Ks24Stats bs = getKs24Stats();
  JsonObject ks = d["bus"].to<JsonObject>();
  ks["valid"] = bus.valid;
  ks["good"] = bs.good;
  ks["rejectedTemplate"] = bs.rejectedTemplate;
  ks["rejectedRange"] = bs.rejectedRange;
  ks["bad"] = bs.bad;
  ks["dropped"] = bs.dropped;
  ks["truncated"] = bs.truncated;

  const RemoteBatReading r = getRemoteBatReading();
  JsonObject rem = d["remote"].to<JsonObject>();
  rem["enabled"] = extBatRemoteEnabled();
  rem["ip"] = remoteModuleIp().toString();
  rem["valid"] = r.valid;
  rem["lastHttpCode"] = r.lastHttpCode;
  if (r.lastSuccessMs != 0)
    rem["lastSuccessAgoSec"] = (millis() - r.lastSuccessMs) / 1000UL;
  else
    rem["lastSuccessAgoSec"] = nullptr;

  const LogRingStats ls = logGetStats();
  const LogUploadStats us = logUploaderStats();
  char url[160];
  logUploaderGetUrl(url, sizeof(url));
  JsonObject logs = d["logs"].to<JsonObject>();
  logs["stored"] = ls.stored;
  logs["capacity"] = ls.capacity;
  logs["pending"] = ls.pending;
  logs["dropped"] = ls.droppedOverflow + ls.droppedLock;
  logs["evictedUnsent"] = ls.evictedUnsent;
  logs["serialLevel"] = logLevelName(logGetSerialLevel());
  logs["netLevel"] = logLevelName(logGetNetLevel());
  setStr(logs, "backend", url);
  logs["backendReachable"] = us.backendReachable;
  logs["sentRecords"] = us.sentRecords;
  logs["sentBatches"] = us.sentBatches;
  logs["failedBatches"] = us.failedBatches;
  logs["lastHttpCode"] = us.lastHttpCode;
}

void sendError(int httpCode, const char *error, const String &message, const char *hint)
{
  sendJson(httpCode, [&](JsonDocument &d) {
    d["ok"] = false;
    d["error"] = error;
    d["message"] = message;
    if (hint != nullptr)
      d["hint"] = hint;
  });
}

bool parseJsonBody(JsonDocument &doc, const char *hint)
{
  const DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err == DeserializationError::Ok && doc.is<JsonObject>())
    return true;
  sendError(400, "invalid_json",
            err != DeserializationError::Ok ? String("JSON parse error: ") + err.c_str()
                                            : String("body must be a JSON object"),
            hint);
  return false;
}
