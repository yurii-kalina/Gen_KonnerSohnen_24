#include <ElegantOTA.h>
#include "config/pins.h"
#include "generator/generator_state.h"
#include "generator/generator_ops.h"
#include "services/WiFiHandler.h"
#include "api/web_server.h"
#include <WiFi.h>
#include "services/EEPROMHandler.h"
#include "api/routes.h"
#include "services/FuelPump.h"
#include "services/GenAuto.h"
#include "drivers/relays.h"
#include "drivers/lamps.h"
#include "drivers/ks24_bus.h"
#include "drivers/sensor_cache.h"
#include "config/config.h"

void setup()
{
    initRelays();
    // Everything back to defaults: GEN_MODE_DEFAULT (manual), R1 and R2 open
    initGeneratorOps();
    Serial.begin(115200);

    // EEPROM before the sensor task: GenAuto reads its thresholds from it
    initEEPROM();

    initLamps();
    initKs24Bus();
    initSensors();
    initFuelPump();
    initGenAuto();
    initWiFi();
    initGeneratorState();

    ElegantOTA.begin(&server);
    setupRoutes();
}

void loop()
{
    server.handleClient();
    ElegantOTA.loop();
    handleWiFiReconnect();
    updateLamps();
    updateKs24Bus();
    updateGenAuto();
    updateFuelPump();
    updateGeneratorState();
}
