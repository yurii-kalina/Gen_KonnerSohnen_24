#include <Arduino.h>

#include "generator_state.h"
#include "drivers/ks24_bus.h"
#include "drivers/lamps.h"
#include "config/config.h"
#include "config/pins.h"
#include "services/EEPROMHandler.h"

bool generatorRun = false;            // публічний прапорець "генератор працює"
static bool lastGeneratorRun = false; // попередній стан для детекції фронтів
static unsigned long runtimeLastUpdateMs = 0; // останній момент донарахування runtime
static unsigned long runtimeLastSaveMs = 0;   // останній момент checkpoint у EEPROM
static bool runtimeDirty = false;             // є ще не збережені в EEPROM мс
static unsigned long lastStateUpdateMs = 0;   // тротлінг updateGeneratorState
uint64_t gTotalRuntimeMs = 0ULL;

namespace
{
  inline bool elapsed(unsigned long since, unsigned long period)
  {
    return static_cast<long>(millis() - since) >= static_cast<long>(period);
  }

  inline void markRuntimeCheckpoint(unsigned long now)
  {
    saveEepromTotalRuntimeMs(gTotalRuntimeMs);
    runtimeLastSaveMs = now;
    runtimeDirty = false;
  }

  inline void accumulateRuntime(unsigned long now)
  {
    const uint32_t deltaMs = now - runtimeLastUpdateMs;
    if (deltaMs == 0)
      return;

    gTotalRuntimeMs += static_cast<uint64_t>(deltaMs);
    runtimeLastUpdateMs = now;
    runtimeDirty = true;
  }
}

void initGeneratorState()
{
  generatorRun = false;
  lastGeneratorRun = false;
  runtimeLastUpdateMs = millis();
  runtimeLastSaveMs = runtimeLastUpdateMs;
  runtimeDirty = false;
  gTotalRuntimeMs = loadEepromTotalRuntimeMs();
  pollGeneratorRun();
}

// Генератор працює, якщо світить лампа RUN АБО шина KS24 двома кадрами
// поспіль показує статус "працює". Поки шина мовчить, лишається лише лампа.
bool pollGeneratorRun()
{
  generatorRun = isLampRunOn() || getKs24Data().running;
  return generatorRun;
}

void updateGeneratorState(bool force)
{
  const unsigned long now = millis();
  if (!force && lastStateUpdateMs != 0 && (now - lastStateUpdateMs) < STATE_POLL_PERIOD_MS)
    return;
  lastStateUpdateMs = now;

  const bool currentRun = pollGeneratorRun();

  if (!lastGeneratorRun && currentRun)
  {
    runtimeLastUpdateMs = now;
    runtimeLastSaveMs = now;
    runtimeDirty = false;
  }
  else if (lastGeneratorRun)
  {
    accumulateRuntime(now);
  }

  if (currentRun && runtimeDirty && elapsed(runtimeLastSaveMs, RUNTIME_CHECKPOINT_MS))
  {
    markRuntimeCheckpoint(now);
  }

  // Фронт вимкнення: 1 -> 0
  if (lastGeneratorRun && !currentRun)
  {
    if (runtimeDirty)
    {
      markRuntimeCheckpoint(now);
    }
  }

  lastGeneratorRun = currentRun;
}

void setTotalRuntimeMs(uint64_t runtimeMs)
{
  gTotalRuntimeMs = runtimeMs;
  saveEepromTotalRuntimeMs(gTotalRuntimeMs);
  runtimeDirty = false;

  const unsigned long now = millis();
  runtimeLastSaveMs = now;
  runtimeLastUpdateMs = now;
}
