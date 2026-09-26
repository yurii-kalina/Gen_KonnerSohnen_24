#pragma once
#include <Arduino.h>

void diagInit();

void diagLogBootReport();

void diagUpdate();

void diagOnTimeSynced();

uint32_t diagBootId();
uint32_t diagBootCount();
const char *diagResetReasonName();
const char *diagPrevResetReasonName();
uint32_t diagMinFreeHeap();

const char *diagWifiStatusName();
