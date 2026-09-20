#pragma once
#include <ArduinoJson.h>

void sendJson(int httpCode, const std::function<void(JsonDocument&)>& fill);
