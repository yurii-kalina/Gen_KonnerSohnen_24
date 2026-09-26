#include "http_json.h"
#include "web_server.h"
#include "services/logger.h"

static const char *methodName(HTTPMethod m)
{
  switch (m)
  {
  case HTTP_GET: return "GET";
  case HTTP_POST: return "POST";
  case HTTP_PUT: return "PUT";
  case HTTP_PATCH: return "PATCH";
  case HTTP_DELETE: return "DELETE";
  case HTTP_OPTIONS: return "OPTIONS";
  default: return "?";
  }
}

void sendJson(int httpCode, const std::function<void(JsonDocument&)>& fill) {
  JsonDocument d;

  fill(d);

  String out;
  serializeJson(d, out);
  server.send(httpCode, "application/json", out);

  logWrite(httpCode >= 400 ? RLOG_WARN : RLOG_DEBUG, "http",
           "%s %s -> %d (%s, %u B)",
           methodName(server.method()),
           server.uri().c_str(),
           httpCode,
           server.client().remoteIP().toString().c_str(),
           (unsigned)out.length());
}
