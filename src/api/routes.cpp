#include "routes.h"
#include "api/web_handlers.h"

void setupRoutes() {
  server.on("/start",  HTTP_POST, handleStart);
  server.on("/stop",   HTTP_POST, handleStop);
  server.on("/status", HTTP_GET,  handleStatus);
  server.on("/uptime", HTTP_POST, handleSetUptime);
  server.on("/pump/start", HTTP_POST, handlePumpStart);
  server.on("/pump/stop",  HTTP_POST, handlePumpStop);
  server.on("/pump/auto",  HTTP_POST, handlePumpAuto);
  server.on("/mode",             HTTP_GET,  handleGetMode);
  server.on("/mode",             HTTP_POST, handleSetMode);
  server.on("/mode/thresholds",  HTTP_POST, handleSetThresholds);
  server.on("/config",           HTTP_GET,  handleGetConfig);
  server.on("/config/sources",   HTTP_POST, handleSetSources);
  server.on("/config/remote",    HTTP_POST, handleSetRemote);
  server.on("/config/wifi",      HTTP_POST, handleSetWifi);
  server.on("/diag/sys", HTTP_GET, handleDiagSys);
  server.on("/logs",     HTTP_GET, handleLogs);
  server.onNotFound(handleNotFound);

  server.begin();
}
