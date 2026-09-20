#include "relays.h"
#include "config/config.h"
#include "config/pins.h"

void setRelay(uint8_t pin, bool on)
{
    const uint8_t active = (pin == RELAY_PUMP_PIN) ? RELAY_PUMP_ACTIVE_LEVEL : RELAY_ACTIVE_LEVEL;
    digitalWrite(pin, on ? active : !active);
}

void initRelays()
{
    for (uint8_t pin : {RELAY_CONTROL_PIN, RELAY_MODE_PIN, RELAY_PUMP_PIN}) {
        // Level first, so the pin never drives the active level while switching to output
        setRelay(pin, false);
        pinMode(pin, OUTPUT);
    }
}
