#include "http_json.h"
#include "web_server.h"


void sendJson(int httpCode, const std::function<void(JsonDocument&)>& fill) {
  JsonDocument d;

  fill(d);

  String out;
  serializeJson(d, out);
  server.send(httpCode, "application/json", out);
}
