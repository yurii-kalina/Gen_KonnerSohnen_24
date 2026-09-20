#pragma once
#include <stdint.h>

extern bool generatorRun;
extern uint64_t gTotalRuntimeMs;

void initGeneratorState();
void updateGeneratorState(bool force = false);
// Generator runs if the RUN lamp is lit OR the KS24 bus confirms it
bool pollGeneratorRun();
void setTotalRuntimeMs(uint64_t runtimeMs);
