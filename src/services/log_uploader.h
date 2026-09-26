#pragma once
#include <Arduino.h>

struct LogUploadStats
{
  bool enabled;
  bool backendReachable;
  uint32_t sentRecords;
  uint32_t sentBatches;
  uint32_t failedBatches;
  int lastHttpCode;
  uint32_t lastSuccessMs;
  uint32_t backoffMs;
};

void logUploaderInit();

void logUploaderRequestFlush();

void logUploaderSetUrl(const char *url);
void logUploaderGetUrl(char *out, size_t outSize);

LogUploadStats logUploaderStats();
