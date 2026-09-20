#pragma once
#include <Arduino.h>

// Лампа RUN: моргає ~1 Гц, коли генератор стоїть; світить постійно, коли працює.
enum class RunLampState : uint8_t
{
  Off,
  Blinking, // генератор стоїть
  On        // світить без перерви довше за RUN_LAMP_STEADY_MS — генератор працює
};

void initLamps();
// Call from loop(): samples the RUN lamp every RUN_LAMP_POLL_MS
void updateLamps();
// Samples the pin LAMP_SAMPLES times (lamps may be PWM-driven)
bool readLampStable(uint8_t pin);

RunLampState getRunLampState();
const char *runLampStateName(RunLampState s); // "on" | "blinking" | "off"
// true only for a steadily lit RUN lamp (not blinking)
bool isLampRunOn();
bool isLampOilOn();
bool isLampOverloadOn();
