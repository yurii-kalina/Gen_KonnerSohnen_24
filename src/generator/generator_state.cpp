#include <Arduino.h>
#include <WiFi.h>

#include "generator_state.h"
#include "drivers/ks24_bus.h"
#include "drivers/lamps.h"
#include "config/config.h"
#include "config/pins.h"
#include "generator/generator_ops.h"
#include "services/EEPROMHandler.h"
#include "services/log_uploader.h"
#include "services/logger.h"

static const char *const TAG = "gen";

static constexpr uint32_t RUNTIME_CHECKPOINT_MS = 5 * 60 * 1000;
static constexpr uint32_t STATE_POLL_PERIOD_MS = 10000;

bool generatorRun = false;            // публічний прапорець "генератор працює"
static bool lastGeneratorRun = false; // попередній стан для детекції фронтів
static unsigned long runtimeLastUpdateMs = 0; // останній момент донарахування runtime
static unsigned long runtimeLastSaveMs = 0;   // останній момент checkpoint у EEPROM
static bool runtimeDirty = false;             // є ще не збережені в EEPROM мс
static unsigned long lastStateUpdateMs = 0;   // тротлінг updateGeneratorState
static unsigned long sessionStartMs = 0;
static unsigned long noInternetSince = 0;
uint64_t gTotalRuntimeMs = 0ULL;

namespace
{
  constexpr uint32_t ALARM_LAMP_POLL_MS = 100;
  constexpr uint32_t ALARM_LAMP_DEBOUNCE_MS = 2000;

  inline bool elapsed(unsigned long since, unsigned long period)
  {
    return static_cast<long>(millis() - since) >= static_cast<long>(period);
  }

  inline void markRuntimeCheckpoint(unsigned long now)
  {
    saveEepromTotalRuntimeMs(gTotalRuntimeMs);
    RLOGD(TAG, "runtime checkpoint saved: %lu h (%llu ms)",
          (unsigned long)(gTotalRuntimeMs / 3600000ULL), (unsigned long long)gTotalRuntimeMs);
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

  // OIL / OVERLOAD: state flips only after the lamp holds the new level for
  // ALARM_LAMP_DEBOUNCE_MS; only the flips are logged.
  struct AlarmLamp
  {
    const char *name;
    bool (*read)();
    bool on;
    unsigned long changeSinceMs;
  };

  AlarmLamp oilLamp{"OIL (low oil pressure)", isLampOilOn, false, 0};
  AlarmLamp overloadLamp{"OVERLOAD", isLampOverloadOn, false, 0};
  unsigned long lastAlarmPollMs = 0;

  void pollAlarmLamp(AlarmLamp &l, unsigned long now)
  {
    if (l.read() == l.on)
    {
      l.changeSinceMs = 0;
      return;
    }
    if (l.changeSinceMs == 0)
      l.changeSinceMs = now;
    if (now - l.changeSinceMs < ALARM_LAMP_DEBOUNCE_MS)
      return;
    l.on = !l.on;
    l.changeSinceMs = 0;
    if (l.on)
    {
      const Ks24Data bus = getKs24Data();
      RLOGW(TAG, "%s lamp ON (run=%d, bus %.1f V %.1f A)", l.name, (int)generatorRun,
            bus.valid ? bus.voltage : 0.0f, bus.valid ? bus.current : 0.0f);
      logUploaderRequestFlush();
    }
    else
    {
      RLOGI(TAG, "%s lamp off", l.name);
    }
  }

  // Manual mode only: the operator started it and nobody can stop it remotely
  void checkNetLoss(bool currentRun)
  {
    if (!currentRun || getGenMode() != GenMode::Manual)
    {
      noInternetSince = 0;
      return;
    }
    if (WiFi.status() == WL_CONNECTED)
    {
      if (noInternetSince != 0)
      {
        RLOGI(TAG, "WiFi back while running — auto-stop countdown cleared after %lu min",
              (unsigned long)((millis() - noInternetSince) / 60000UL));
      }
      noInternetSince = 0;
      return;
    }
    if (noInternetSince == 0)
    {
      noInternetSince = millis();
      RLOGW(TAG, "generator running without WiFi — auto-stop countdown started (%lu min)",
            (unsigned long)(NET_LOSS_AUTO_STOP_MS / 60000UL));
    }
    else if (elapsed(noInternetSince, NET_LOSS_AUTO_STOP_MS))
    {
      RLOGC(TAG, "NET-LOSS AUTO-STOP: no WiFi for %lu min while running — stopping generator",
            (unsigned long)((millis() - noInternetSince) / 60000UL));
      noInternetSince = 0;
      stopGenerator("net_loss");
    }
  }
}

void initGeneratorState()
{
  generatorRun = false;
  lastGeneratorRun = false;
  runtimeLastUpdateMs = millis();
  runtimeLastSaveMs = runtimeLastUpdateMs;
  runtimeDirty = false;
  noInternetSince = 0;
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

bool oilAlertOn()
{
  return oilLamp.on;
}

bool overloadOn()
{
  return overloadLamp.on;
}

void updateGeneratorState(bool force)
{
  const unsigned long now = millis();
  if (lastAlarmPollMs == 0 || now - lastAlarmPollMs >= ALARM_LAMP_POLL_MS)
  {
    lastAlarmPollMs = now;
    pollAlarmLamp(oilLamp, now);
    pollAlarmLamp(overloadLamp, now);
  }

  if (!force && lastStateUpdateMs != 0 && (now - lastStateUpdateMs) < STATE_POLL_PERIOD_MS)
    return;
  lastStateUpdateMs = now;

  const bool currentRun = pollGeneratorRun();

  if (!lastGeneratorRun && currentRun)
  {
    sessionStartMs = now;
    const Ks24Data bus = getKs24Data();
    RLOGI(TAG, "RUN 0->1: generator is running (lamp=%s, bus=%s, R1=%s, total %lu h)",
          runLampStateName(getRunLampState()), bus.valid ? (bus.running ? "running" : "stopped") : "no_data",
          isRunCommanded() ? "closed" : "open", (unsigned long)(gTotalRuntimeMs / 3600000ULL));
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
    RLOGI(TAG, "RUN 1->0: generator stopped after %lu s this session (R1=%s, total %lu h)",
          (unsigned long)((now - sessionStartMs) / 1000UL), isRunCommanded() ? "closed" : "open",
          (unsigned long)(gTotalRuntimeMs / 3600000ULL));
    if (runtimeDirty)
    {
      markRuntimeCheckpoint(now);
    }
  }

  checkNetLoss(currentRun);

  lastGeneratorRun = currentRun;
}

void setTotalRuntimeMs(uint64_t runtimeMs)
{
  RLOGW(TAG, "runtime counter overwritten: %lu h -> %lu h",
        (unsigned long)(gTotalRuntimeMs / 3600000ULL), (unsigned long)(runtimeMs / 3600000ULL));
  gTotalRuntimeMs = runtimeMs;
  saveEepromTotalRuntimeMs(gTotalRuntimeMs);
  runtimeDirty = false;

  const unsigned long now = millis();
  runtimeLastSaveMs = now;
  runtimeLastUpdateMs = now;
}
