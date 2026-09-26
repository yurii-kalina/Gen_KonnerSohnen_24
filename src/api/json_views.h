#pragma once
#include <ArduinoJson.h>
#include "generator/generator_ops.h"

// JSON groups shared by /status and the command responses (same shape as WalkenPower)
void setRuntimeHours(JsonObject o);
void fillGenerator(JsonObject o, const StatusData &s);
void fillOutput(JsonObject o, const StatusData &s);
void fillBattery(JsonObject o, const StatusData &s);
void fillFuel(JsonObject o, const StatusData &s);
void fillAuto(JsonObject o);
void fillSources(JsonObject o);
void fillPump(JsonObject o);
void fillWifi(JsonObject o);

void fillStatus(JsonDocument &d);
void fillDiag(JsonDocument &d);

// {"ok":false,"error":..,"message":..,"hint":..}
void sendError(int httpCode, const char *error, const String &message, const char *hint = nullptr);
// false — the error response is already sent
bool parseJsonBody(JsonDocument &doc, const char *hint);
