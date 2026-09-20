#include "web_handlers.h"
#include "http_json.h"
#include <math.h>
#include "generator/generator_ops.h"
#include "generator/generator_state.h"
#include "config/config.h"
#include "services/FuelPump.h"
#include "services/GenAuto.h"

static constexpr double MAX_RUNTIME_HOURS = 1000000.0;

static void sendStartStop(const StartStopResult &r)
{
  sendJson(r.ok ? 200 : 409, [&](JsonDocument &d)
           {
    d["ok"] = r.ok;
    d["status"] = r.status;
    d["generatorRun"] = r.generatorRun;
    d["mode"] = genModeName(getGenMode()); });
}

// POST /start - лише в ручному режимі: замкнути R1 і тримати
void handleStart()
{
  sendStartStop(startGenerator());
}

// POST /stop - з будь-якого режиму: перейти в ручний і розімкнути R1
void handleStop()
{
  sendStartStop(stopGenerator());
}

// POST /gen/mode  body: {"mode": "manual" | "voltage" | "generator"}
void handleGenMode()
{
  const String body = server.arg("plain");

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  GenMode mode;
  if (err != DeserializationError::Ok || !doc["mode"].is<const char *>() ||
      !parseGenMode(doc["mode"].as<const char *>(), mode))
  {
    const String reason = err != DeserializationError::Ok
                              ? String("json_parse_error: ") + err.c_str()
                              : String("mode_missing_or_unknown");
    sendJson(400, [&](JsonDocument &d)
             {
      d["ok"] = false;
      d["status"] = "invalid_request";
      d["reason"] = reason;
      d["hint"] = "use JSON body: {\"mode\":\"manual\"|\"voltage\"|\"generator\"}"; });
    return;
  }

  setGenMode(mode);
  sendJson(200, [&](JsonDocument &d)
           {
    d["ok"] = true;
    d["mode"] = genModeName(getGenMode()); });
}

// GET /status
void handleStatus()
{
  const auto s = readStatus();
  sendJson(200, [&](JsonDocument &d)
           {
    d["host"] = HOST_NAME;

    JsonObject gen = d["generator"].to<JsonObject>();
    gen["mode"] = genModeName(s.mode);
    gen["run"] = s.generatorRun;
    gen["runtimeHours"] = (uint32_t)(gTotalRuntimeMs / 3600000ULL);
    gen["runtimeMs"] = gTotalRuntimeMs;

    JsonObject lamps = d["lamps"].to<JsonObject>();
    lamps["run"] = runLampStateName(s.lampRun); // on | blinking | off
    lamps["oil"] = s.lampOil;
    lamps["overload"] = s.lampOverload;

    JsonObject bat = d["battery"].to<JsonObject>(); // A0
    bat["voltage"] = s.voltageBattery;
    bat["raw"] = s.analogBattery;
    bat["valid"] = s.batValid;

    // Шина KS24; значення null, поки кадрів немає або вони застаріли
    JsonObject bus = d["bus"].to<JsonObject>();
    bus["valid"] = s.bus.valid;
    bus["running"] = s.bus.running;
    if (s.bus.valid) {
      bus["voltage"] = s.bus.voltage;
      bus["current"] = s.bus.current;
      bus["rpm"] = s.bus.rpm;
      bus["tempGuess"] = s.bus.tempGuess;
    } else {
      bus["voltage"] = nullptr;
      bus["current"] = nullptr;
      bus["rpm"] = nullptr;
      bus["tempGuess"] = nullptr;
    }

    JsonObject fuel = d["fuel"].to<JsonObject>();
    JsonObject fuelGen = fuel["generatorTank"].to<JsonObject>(); // A1
    fuelGen["percent"] = s.pump.levelPercent;
    fuelGen["raw"] = s.pump.levelAdc;
    fuelGen["overflowAlarm"] = s.pump.overflowAlarm;
    JsonObject fuelExt = fuel["externalTank"].to<JsonObject>(); // A2
    fuelExt["percent"] = s.fuelExtPercent;
    fuelExt["raw"] = s.fuelExtRaw;

    JsonObject pump = d["pump"].to<JsonObject>();
    pump["running"] = s.pump.running;
    pump["runtimeSec"] = s.pump.runtimeSec;
    pump["autoEnabled"] = s.pump.autoEnabled;
    pump["lastStopReason"] = s.pump.lastStopReason;

    JsonObject cfg = d["config"].to<JsonObject>();
    JsonObject cfgBat = cfg["battery"].to<JsonObject>();
    cfgBat["minV"] = getGenAutoMinV();
    cfgBat["maxV"] = getGenAutoMaxV();
    JsonObject cfgGen = cfg["generatorTank"].to<JsonObject>();
    cfgGen["lowAdc"] = FUEL_LEVEL_LOW_ADC;
    cfgGen["highAdc"] = FUEL_LEVEL_HIGH_ADC;
    JsonObject cfgExt = cfg["externalTank"].to<JsonObject>();
    cfgExt["emptyAdc"] = FUEL_EXT_EMPTY_ADC;
    cfgExt["fullAdc"] = FUEL_EXT_FULL_ADC;
    cfg["pumpMaxRuntimeSec"] = PUMP_MAX_RUNTIME_SEC; });
}

void handleUptime()
{
  sendJson(200, [&](JsonDocument &d)
           { d["runtimeHours"] = (uint32_t)(gTotalRuntimeMs / 3600000ULL); });
}

void handleSetUptime()
{
  bool ok = false;
  String reason = "empty_body";
  uint64_t newRuntimeMs = 0ULL;
  const String body = server.arg("plain");

  if (body.length() > 0)
  {
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, body);
    if (err == DeserializationError::Ok)
    {
      if (doc["runtimeHours"].is<float>() || doc["runtimeHours"].is<double>() || doc["runtimeHours"].is<uint32_t>())
      {
        const double runtimeHours = doc["runtimeHours"].as<double>();
        if (!isfinite(runtimeHours))
        {
          reason = "runtimeHours_invalid";
        }
        else if (runtimeHours < 0.0)
        {
          reason = "runtimeHours_negative";
        }
        else if (runtimeHours > MAX_RUNTIME_HOURS)
        {
          reason = "runtimeHours_too_large";
        }
        else
        {
          newRuntimeMs = static_cast<uint64_t>(runtimeHours * 3600000.0);
          ok = true;
        }
      }
      else
      {
        reason = "runtimeHours_missing";
      }
    }
    else
    {
      reason = String("json_parse_error: ") + err.c_str();
    }
  }

  if (ok)
  {
    setTotalRuntimeMs(newRuntimeMs);
  }

  sendJson(ok ? 200 : 400, [&](JsonDocument &d)
           {
    d["ok"] = ok;
    d["runtimeHours"] = (uint32_t)(gTotalRuntimeMs / 3600000ULL);
    d["runtimeMs"] = gTotalRuntimeMs;
    if (!ok) {
      d["status"] = "invalid_request";
      d["reason"] = reason;
      d["hint"] = "use JSON body: {\"runtimeHours\":123.5}";
    } });
}

// GET /pump
void handlePumpStatus()
{
  const auto p = getFuelPumpStatus();
  sendJson(200, [&](JsonDocument &d)
           {
    d["ok"] = true;
    d["pumpRunning"] = p.running;
    d["pumpRuntimeSec"] = p.runtimeSec;
    d["pumpAutoEnabled"] = p.autoEnabled;
    d["pumpLastStopReason"] = p.lastStopReason;
    d["fuelOverflowAlarm"] = p.overflowAlarm;
    d["fuelSensorOk"] = p.sensorOk;
    d["adcFuelLevel"] = p.levelAdc;
    d["fuelLevelLow"] = p.levelLow;
    d["fuelLevelHigh"] = p.levelHigh;
    d["fuelPercent"] = p.levelPercent;
    d["lowAdc"] = FUEL_LEVEL_LOW_ADC;
    d["highAdc"] = FUEL_LEVEL_HIGH_ADC;
    d["maxRuntimeSec"] = PUMP_MAX_RUNTIME_SEC; });
}

// POST /pump/start  body: {"seconds": N}  (N in [1..PUMP_MAX_RUNTIME_SEC])
void handlePumpStart()
{
  const String body = server.arg("plain");

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err != DeserializationError::Ok)
  {
    sendJson(400, [&](JsonDocument &d)
             {
      d["ok"] = false;
      d["status"] = "invalid_request";
      d["reason"] = String("json_parse_error: ") + err.c_str();
      d["hint"] = "use JSON body: {\"seconds\":N}"; });
    return;
  }

  if (!doc["seconds"].is<int>())
  {
    sendJson(400, [&](JsonDocument &d)
             {
      d["ok"] = false;
      d["status"] = "invalid_request";
      d["reason"] = "seconds_missing_or_not_int";
      d["hint"] = "use JSON body: {\"seconds\":N}"; });
    return;
  }

  const int seconds = doc["seconds"].as<int>();
  if (seconds < 1 || seconds > (int)PUMP_MAX_RUNTIME_SEC)
  {
    sendJson(400, [&](JsonDocument &d)
             {
      d["ok"] = false;
      d["status"] = "seconds_out_of_range";
      d["reason"] = String("must be 1..") + (int)PUMP_MAX_RUNTIME_SEC; });
    return;
  }

  if (getFuelPumpStatus().overflowAlarm)
  {
    sendJson(409, [&](JsonDocument &d)
             {
      d["ok"] = false;
      d["status"] = "overflow_alarm";
      d["reason"] = "emergency overflow sensor is active"; });
    return;
  }

  const bool ok = requestPumpStart(static_cast<uint16_t>(seconds));
  const auto p = getFuelPumpStatus();
  sendJson(ok ? 200 : 400, [&](JsonDocument &d)
           {
    d["ok"] = ok;
    d["pumpRunning"] = p.running;
    d["pumpRuntimeSec"] = p.runtimeSec;
    d["pumpAutoEnabled"] = p.autoEnabled; });
}

// POST /pump/stop  - hard stop + auto off, no body
void handlePumpStop()
{
  const bool wasRunning = getFuelPumpStatus().running;
  requestPumpStop();
  const auto p = getFuelPumpStatus();
  sendJson(200, [&](JsonDocument &d)
           {
    d["ok"] = true;
    d["status"] = wasRunning ? "stopped" : "already_stopped";
    d["pumpRunning"] = p.running;
    d["pumpAutoEnabled"] = p.autoEnabled; });
}

// POST /pump/auto  body: {"enabled": true/false}
void handlePumpAuto()
{
  const String body = server.arg("plain");

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err != DeserializationError::Ok || !doc["enabled"].is<bool>())
  {
    const String reason = err != DeserializationError::Ok
                              ? String("json_parse_error: ") + err.c_str()
                              : String("enabled_missing_or_not_bool");
    sendJson(400, [&](JsonDocument &d)
             {
      d["ok"] = false;
      d["status"] = "invalid_request";
      d["reason"] = reason;
      d["hint"] = "use JSON body: {\"enabled\":true|false}"; });
    return;
  }

  setPumpAutoEnabled(doc["enabled"].as<bool>());
  const auto p = getFuelPumpStatus();
  sendJson(200, [&](JsonDocument &d)
           {
    d["ok"] = true;
    d["pumpAutoEnabled"] = p.autoEnabled;
    d["pumpRunning"] = p.running; });
}

// POST /gen/thresholds  body: {"minV": float, "maxV": float}
// Пороги режиму Voltage, зберігаються в EEPROM.
void handleGenThresholds()
{
  const String body = server.arg("plain");

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  String reason;
  if (err != DeserializationError::Ok)
    reason = String("json_parse_error: ") + err.c_str();
  else if (!doc["minV"].is<float>() || !doc["maxV"].is<float>())
    reason = "minV_and_maxV_required";
  else if (!setGenAutoThresholds(doc["minV"].as<float>(), doc["maxV"].as<float>()))
    reason = String("thresholds_invalid: need 0 <= minV < maxV <= ") + String(GEN_AUTO_BAT_LIMIT_V, 0);

  const bool ok = reason.isEmpty();
  sendJson(ok ? 200 : 400, [&](JsonDocument &d)
           {
    d["ok"] = ok;
    d["batMinV"] = getGenAutoMinV();
    d["batMaxV"] = getGenAutoMaxV();
    if (!ok) {
      d["status"] = "invalid_request";
      d["reason"] = reason;
      d["hint"] = "use JSON body: {\"minV\":23.6, \"maxV\":27.0}";
    } });
}
