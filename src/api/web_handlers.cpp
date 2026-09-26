#include "web_handlers.h"
#include "http_json.h"
#include "json_views.h"
#include <math.h>
#include "config/config.h"
#include "generator/generator_ops.h"
#include "generator/generator_state.h"
#include "services/FuelPump.h"
#include "services/diagnostics.h"
#include "services/log_uploader.h"
#include "services/logger.h"

static constexpr double MAX_RUNTIME_HOURS = 1000000.0;

namespace
{
  const char *resultMessage(const char *status)
  {
    if (strcmp(status, "started") == 0)
      return "R1 closed and held, generator runs while it stays closed";
    if (strcmp(status, "stopped") == 0)
      return "mode switched to manual, R1 open";
    if (strcmp(status, "wrong_mode") == 0)
      return "start works in manual mode only, switch via POST /mode";
    if (strcmp(status, "already_running") == 0)
      return "R1 is already closed";
    return "already in manual mode with R1 open";
  }

  void sendStartStop(const StartStopResult &r)
  {
    sendJson(r.ok ? 200 : 409, [&](JsonDocument &d) {
      d["ok"] = r.ok;
      if (r.ok)
        d["status"] = r.status;
      else
        d["error"] = r.status;
      d["message"] = resultMessage(r.status);
      fillGenerator(d["generator"].to<JsonObject>(), readStatus());
    });
  }

  void sendPump(const char *status)
  {
    sendJson(200, [&](JsonDocument &d) {
      d["ok"] = true;
      if (status != nullptr)
        d["status"] = status;
      fillPump(d["pump"].to<JsonObject>());
    });
  }
}

// POST /start - лише в ручному режимі: замкнути R1 і тримати
void handleStart()
{
  sendStartStop(startGenerator());
}

// POST /stop - з будь-якого режиму: перейти в ручний і розімкнути R1
void handleStop()
{
  sendStartStop(stopGenerator("http"));
}

void handleStatus()
{
  sendJson(200, [](JsonDocument &d) { fillStatus(d); });
}

// POST /uptime  body: {"runtimeHours": 123.5}
void handleSetUptime()
{
  static const char *hint = "{\"runtimeHours\":123.5}";
  JsonDocument doc;
  if (!parseJsonBody(doc, hint))
    return;
  if (!doc["runtimeHours"].is<double>())
  {
    sendError(400, "invalid_request", "runtimeHours must be a number", hint);
    return;
  }
  const double hours = doc["runtimeHours"].as<double>();
  if (!isfinite(hours) || hours < 0.0 || hours > MAX_RUNTIME_HOURS)
  {
    sendError(400, "out_of_range", String("runtimeHours must be 0..") + (unsigned long)MAX_RUNTIME_HOURS, hint);
    return;
  }

  setTotalRuntimeMs(static_cast<uint64_t>(hours * 3600000.0));
  sendJson(200, [](JsonDocument &d) {
    d["ok"] = true;
    setRuntimeHours(d.as<JsonObject>());
  });
}

// POST /pump/start  body: {"seconds": N}  (N in [1..PUMP_MAX_RUNTIME_SEC])
void handlePumpStart()
{
  static const char *hint = "{\"seconds\":60}";
  JsonDocument doc;
  if (!parseJsonBody(doc, hint))
    return;
  if (!doc["seconds"].is<uint32_t>())
  {
    sendError(400, "invalid_request", "seconds must be a positive integer", hint);
    return;
  }
  const uint32_t seconds = doc["seconds"].as<uint32_t>();
  if (seconds < 1 || seconds > PUMP_MAX_RUNTIME_SEC)
  {
    sendError(400, "out_of_range", String("seconds must be 1..") + (unsigned long)PUMP_MAX_RUNTIME_SEC, hint);
    return;
  }
  if (!requestPumpStart(seconds))
  {
    sendError(409, "overflow_alarm", "emergency overflow sensor is active, pump start blocked");
    return;
  }
  sendPump("started");
}

// POST /pump/stop - hard stop + auto off, no body
void handlePumpStop()
{
  const bool wasRunning = getFuelPumpStatus().running;
  requestPumpStop();
  sendPump(wasRunning ? "stopped" : "already_stopped");
}

// POST /pump/auto  body: {"enabled": true/false}
void handlePumpAuto()
{
  static const char *hint = "{\"enabled\":true}";
  JsonDocument doc;
  if (!parseJsonBody(doc, hint))
    return;
  if (!doc["enabled"].is<bool>())
  {
    sendError(400, "invalid_request", "enabled must be true or false", hint);
    return;
  }
  setPumpAutoEnabled(doc["enabled"].as<bool>());
  sendPump(nullptr);
}

// GET /logs?n=50&level=info
void handleLogs()
{
  constexpr uint16_t LOGS_MAX_N = 150;
  constexpr uint16_t LOGS_DEFAULT_N = 50;

  uint16_t n = LOGS_DEFAULT_N;
  if (server.hasArg("n"))
  {
    const long parsed = server.arg("n").toInt();
    if (parsed > 0)
      n = (uint16_t)min<long>(parsed, LOGS_MAX_N);
  }

  uint8_t minLevel = RLOG_DEBUG;
  if (server.hasArg("level") && !logLevelFromName(server.arg("level").c_str(), minLevel))
  {
    sendError(400, "invalid_request", "level must be one of debug|info|warn|error|crit");
    return;
  }

  const LogRingStats ls = logGetStats();
  const LogUploadStats us = logUploaderStats();

  // Built by hand: the records are already JSON in the ring, a JsonDocument would copy them all
  String out;
  out.reserve(1024);
  char head[400];
  snprintf(head, sizeof(head),
           "{\"ok\":true,"
           "\"board\":{\"host\":\"%s\",\"bootId\":\"%08lX\",\"bootCount\":%lu,\"uptimeSec\":%lu},"
           "\"ring\":{\"stored\":%u,\"capacity\":%u,\"pending\":%u,\"produced\":%lu,"
           "\"droppedOverflow\":%lu,\"droppedLock\":%lu,\"evictedUnsent\":%lu},"
           "\"uploader\":{\"enabled\":%s,\"backendReachable\":%s,\"failedBatches\":%lu},"
           "\"records\":[",
           HOST_NAME, (unsigned long)diagBootId(), (unsigned long)diagBootCount(),
           (unsigned long)(millis() / 1000UL),
           (unsigned)ls.stored, (unsigned)ls.capacity, (unsigned)ls.pending,
           (unsigned long)ls.produced, (unsigned long)ls.droppedOverflow,
           (unsigned long)ls.droppedLock, (unsigned long)ls.evictedUnsent,
           us.enabled ? "true" : "false",
           us.backendReachable ? "true" : "false",
           (unsigned long)us.failedBatches);
  out = head;
  const uint16_t sent = logAppendRecentJson(out, n, minLevel);
  out += "]}";

  server.send(200, "application/json", out);
  RLOGD("http", "GET /logs -> 200 (%u records, %u B)", (unsigned)sent, (unsigned)out.length());
}

void handleDiagSys()
{
  sendJson(200, [](JsonDocument &d) { fillDiag(d); });
}

void handleNotFound()
{
  sendError(404, "not_found", String("no route for ") + server.uri());
}
