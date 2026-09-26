#pragma once
#include "web_server.h"

void handleStart();
void handleStop();
void handleStatus();
void handleSetUptime();
void handlePumpStart();
void handlePumpStop();
void handlePumpAuto();
void handleLogs();
void handleDiagSys();
void handleNotFound();

void handleGetMode();
void handleSetMode();
void handleSetThresholds();
void handleGetConfig();
void handleSetSources();
void handleSetRemote();
void handleSetWifi();
