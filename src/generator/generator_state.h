#pragma once
#include <stdint.h>

extern bool generatorRun;
extern uint64_t gTotalRuntimeMs;

void initGeneratorState();
// Call from loop(): alarm lamps every call, run state / runtime / net-loss once per STATE_POLL_PERIOD_MS
void updateGeneratorState(bool force = false);
// Generator runs if the RUN lamp is lit OR the KS24 bus confirms it
bool pollGeneratorRun();
// OIL / OVERLOAD lamps, debounced
bool oilAlertOn();
bool overloadOn();
void setTotalRuntimeMs(uint64_t runtimeMs);
