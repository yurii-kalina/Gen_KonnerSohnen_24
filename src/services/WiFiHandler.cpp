#include <WiFi.h>
#include <esp_wifi.h>
#include "WiFiHandler.h"
#include "config/config.h"

static constexpr uint32_t WIFI_RECONNECT_MIN_MS = 10000;
static constexpr uint32_t WIFI_RECONNECT_MAX_MS = 60000;

static unsigned long lastAttemptMs = 0;
static uint32_t backoffMs = WIFI_RECONNECT_MIN_MS;

static void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      backoffMs = WIFI_RECONNECT_MIN_MS;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      lastAttemptMs = millis();
      break;
    default: break;
  }
}

void initWiFi() {
  WiFi.setHostname(HOST_NAME);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // modem sleep adds hundreds of ms to every HTTP reply
  WiFi.persistent(false);

  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onWiFiEvent);

  WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
  lastAttemptMs = millis();

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 3000) {
    delay(100);
  }
}

void handleWiFiReconnect() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  if (millis() - lastAttemptMs < backoffMs) {
    return;
  }

  WiFi.reconnect();
  lastAttemptMs = millis();
  backoffMs = min<uint32_t>(backoffMs * 2, WIFI_RECONNECT_MAX_MS);
}
