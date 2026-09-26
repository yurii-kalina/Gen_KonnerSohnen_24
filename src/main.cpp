#include <esp_task_wdt.h>
#include <ElegantOTA.h>
#include <WiFi.h>
#include "config/pins.h"
#include "config/config.h"
#include "generator/generator_state.h"
#include "generator/generator_ops.h"
#include "services/WiFiHandler.h"
#include "services/EEPROMHandler.h"
#include "services/FuelPump.h"
#include "services/GenAuto.h"
#include "services/settings.h"
#include "services/diagnostics.h"
#include "services/log_uploader.h"
#include "services/logger.h"
#include "services/serial_console.h"
#include "api/web_server.h"
#include "api/routes.h"
#include "drivers/relays.h"
#include "drivers/lamps.h"
#include "drivers/ks24_bus.h"
#include "drivers/sensor_cache.h"
#include "drivers/ext_bat_remote.h"

namespace
{
    // R1 is left alone: the generator keeps running or stays stopped through the update
    void setupOtaCallbacks()
    {
        ElegantOTA.onStart([]()
                           { RLOGC("ota", "OTA update started — firmware will reboot on success");
                             pumpAbort("ota"); });
        ElegantOTA.onProgress([](size_t current, size_t total)
                              { esp_task_wdt_reset();
                                RLOG_EVERY(2000, RLOG_DEBUG, "ota", "OTA progress %u/%u B (%u%%)",
                                           (unsigned)current, (unsigned)total,
                                           (unsigned)(total > 0 ? (current * 100U) / total : 0U)); });
        ElegantOTA.onEnd([](bool success)
                         {
            if (success)
                RLOGC("ota", "OTA update finished OK — rebooting into the new firmware");
            else
                RLOGE("ota", "OTA update FAILED — staying on the current firmware"); });
    }
}

void setup()
{
    initRelays();
    // Everything back to defaults: GEN_MODE_DEFAULT (manual), R1 and R2 open
    initGeneratorOps();
    Serial.begin(115200);

    logInit();
    RLOGI("boot", "relays released first thing (R1, R2, R3 open) at %lu ms", (unsigned long)millis());

    // EEPROM before everything that reads settings from it
    initEEPROM();
    diagInit();
    diagLogBootReport();
    settingsInit();

    esp_task_wdt_init(LOOP_WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);
    RLOGI("boot", "task WDT armed: %lu s, panic on timeout, loop() subscribed",
          (unsigned long)LOOP_WDT_TIMEOUT_S);

    initLamps();
    initKs24Bus();
    initSensors();
    initFuelPump();
    initGenAuto();
    // After initGenAuto(): restoring a voltage mode re-evaluates the battery with the saved thresholds
    restoreGenModeFromEeprom();

    RLOGI("wifi", "connecting to \"%s\"...", wifiSsid());
    initWiFi();
    if (WiFi.status() == WL_CONNECTED)
    {
        RLOGI("wifi", "SSID=\"%s\" BSSID=%s ch=%d RSSI=%d dBm",
              WiFi.SSID().c_str(), WiFi.BSSIDstr().c_str(), WiFi.channel(), WiFi.RSSI());
        RLOGI("wifi", "IP=%s GW=%s mask=%s host=%s",
              WiFi.localIP().toString().c_str(),
              WiFi.gatewayIP().toString().c_str(),
              WiFi.subnetMask().toString().c_str(),
              WiFi.getHostname());
    }
    else
    {
        RLOGW("wifi", "not connected within the boot window — retrying in background");
    }

    if (!initExtBatRemote())
        RLOGC("boot", "remote module task FAILED to start — remote_voltage mode will have no data");

    initGeneratorState();
    RLOGI("boot", "generator state initialized, runtime counter=%lu h",
          (unsigned long)(gTotalRuntimeMs / 3600000ULL));

    logUploaderInit();

    setupOtaCallbacks();
    ElegantOTA.begin(&server);
    setupRoutes();
    RLOGI("boot", "HTTP routes + OTA ready on port 80");

    serialConsoleInit();
    RLOGI("boot", "setup done in %lu ms", (unsigned long)millis());
}

void loop()
{
    esp_task_wdt_reset();
    server.handleClient();
    ElegantOTA.loop();
    handleWiFiReconnect();
    updateLamps();
    updateKs24Bus();
    updateGenAuto();
    updateFuelPump();
    updateGeneratorState();
    diagUpdate();
    serialConsoleUpdate();
}
