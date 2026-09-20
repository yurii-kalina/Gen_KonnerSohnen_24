#include "routes.h"
#include "api/web_handlers.h"

void setupRoutes() {
  server.on("/start",  HTTP_POST, handleStart);
  server.on("/stop",   HTTP_POST, handleStop);
  server.on("/status", HTTP_GET,  handleStatus);
  server.on("/gen/thresholds", HTTP_POST, handleGenThresholds);
  server.on("/gen/mode", HTTP_POST, handleGenMode);
  server.on("/uptime", HTTP_GET,  handleUptime);
  server.on("/uptime", HTTP_POST, handleSetUptime);
  server.on("/pump", HTTP_GET,  handlePumpStatus);
  server.on("/pump/start", HTTP_POST, handlePumpStart);
  server.on("/pump/stop",  HTTP_POST, handlePumpStop);
  server.on("/pump/auto",  HTTP_POST, handlePumpAuto);

  server.begin();
}
