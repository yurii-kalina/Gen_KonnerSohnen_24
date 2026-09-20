#include "lamps.h"
#include "config/config.h"
#include "config/pins.h"

namespace
{
  // RUN lamp tracking: blinks ~1 Hz while the generator is stopped, lit
  // steadily while it runs. Only a long unbroken lit stretch counts as "on".
  bool runSampledOnce = false;
  bool runLit = false;              // last sample
  unsigned long runLastSampleMs = 0;
  unsigned long runLitSinceMs = 0;  // start of the current unbroken lit stretch
  bool runEverLit = false;
  unsigned long runLastLitMs = 0;
}

void initLamps()
{
  pinMode(LED_RUN_PIN, INPUT);
  pinMode(LED_OIL_PIN, INPUT);
  pinMode(LED_OVERLOAD_PIN, INPUT);
  runSampledOnce = false;
  runLit = false;
  runEverLit = false;
}

bool readLampStable(uint8_t pin)
{
  uint8_t activeCount = 0;
  for (uint8_t i = 0; i < LAMP_SAMPLES; ++i)
  {
    if (digitalRead(pin) == LAMP_ACTIVE_LEVEL)
      ++activeCount;
    if (LAMP_SAMPLE_GAP_US > 0)
      delayMicroseconds(LAMP_SAMPLE_GAP_US);
  }
  return activeCount >= LAMP_ACTIVE_THRESHOLD;
}

void updateLamps()
{
  const unsigned long now = millis();
  if (runSampledOnce && now - runLastSampleMs < RUN_LAMP_POLL_MS)
    return;

  const bool lit = readLampStable(LED_RUN_PIN);
  // Unsampled time could hide a dark phase, so a gap restarts the lit stretch
  const bool gap = runSampledOnce && now - runLastSampleMs > RUN_LAMP_MAX_GAP_MS;
  if (lit)
  {
    if (!runLit || gap || !runSampledOnce)
      runLitSinceMs = now;
    runEverLit = true;
    runLastLitMs = now;
  }
  runLit = lit;
  runLastSampleMs = now;
  runSampledOnce = true;
}

RunLampState getRunLampState()
{
  const unsigned long now = millis();
  if (runLit && now - runLitSinceMs >= RUN_LAMP_STEADY_MS)
    return RunLampState::On;
  if (runEverLit && now - runLastLitMs < RUN_LAMP_BLINK_TIMEOUT_MS)
    return RunLampState::Blinking;
  return RunLampState::Off;
}

const char *runLampStateName(RunLampState s)
{
  switch (s)
  {
  case RunLampState::On:
    return "on";
  case RunLampState::Blinking:
    return "blinking";
  case RunLampState::Off:
    return "off";
  }
  return "unknown";
}

bool isLampRunOn()
{
  return getRunLampState() == RunLampState::On;
}

bool isLampOilOn()
{
  return readLampStable(LED_OIL_PIN);
}

bool isLampOverloadOn()
{
  return readLampStable(LED_OVERLOAD_PIN);
}
