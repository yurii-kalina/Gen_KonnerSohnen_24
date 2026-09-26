#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>
#include "WiFiHandler.h"
#include "config/config.h"
#include "services/log_uploader.h"
#include "services/logger.h"
#include "services/settings.h"

static const char *const TAG = "wifi";

static constexpr const char *NTP_SERVER_1 = "pool.ntp.org";
static constexpr const char *NTP_SERVER_2 = "time.google.com";
static constexpr uint32_t LOG_WIFI_OUTAGE_WARN_MS = 60000;

static constexpr uint32_t WIFI_RECONNECT_MIN_MS = 1000;
static constexpr uint32_t WIFI_RECONNECT_MAX_MS = 60000;
static constexpr uint32_t WIFI_NEW_CREDS_TIMEOUT_MS = 60000;
static constexpr uint32_t WIFI_APPLY_DELAY_MS = 500;

static unsigned long nextReconnectAt = 5000;
static uint32_t backoffMs = WIFI_RECONNECT_MIN_MS;

static unsigned long outageSinceMs = 0;
static uint32_t reconnectAttempts = 0;
static bool ntpStarted = false;

static uint32_t applyAtMs = 0;
static uint32_t newCredsSinceMs = 0;
static volatile bool gotIpSinceApply = false;

static const char *disconnectReasonName(uint8_t reason)
{
  switch (reason)
  {
  case 1: return "unspecified";
  case 2: return "auth_expire";
  case 3: return "auth_leave";
  case 4: return "assoc_expire";
  case 5: return "assoc_toomany";
  case 8: return "assoc_leave";
  case 15: return "4way_handshake_timeout";
  case 200: return "beacon_timeout";
  case 201: return "no_ap_found";
  case 202: return "auth_fail";
  case 203: return "assoc_fail";
  case 204: return "handshake_timeout";
  case 205: return "connection_fail";
  default: return "other";
  }
}

static void startNtpOnce()
{
  if (ntpStarted)
    return;
  ntpStarted = true;
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);
  RLOGD(TAG, "NTP requested from %s / %s", NTP_SERVER_1, NTP_SERVER_2);
}

static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
  switch (event)
  {
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
  {
    gotIpSinceApply = true;
    const unsigned long outage = outageSinceMs != 0 ? (millis() - outageSinceMs) : 0;
    outageSinceMs = 0;
    backoffMs = WIFI_RECONNECT_MIN_MS;

    if (outage > 0)
    {
      logWrite(outage >= LOG_WIFI_OUTAGE_WARN_MS ? RLOG_WARN : RLOG_INFO, TAG,
               "reconnected after %lu ms offline (%lu attempts), IP=%s RSSI=%d dBm",
               outage, (unsigned long)reconnectAttempts,
               WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
    else
    {
      RLOGI(TAG, "got IP=%s RSSI=%d dBm ch=%d", WiFi.localIP().toString().c_str(),
            WiFi.RSSI(), WiFi.channel());
    }
    reconnectAttempts = 0;

    startNtpOnce();
    logUploaderRequestFlush();
    break;
  }
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
  {
    const unsigned long now = millis();
    const uint8_t reason = info.wifi_sta_disconnected.reason;
    if (outageSinceMs == 0)
    {
      outageSinceMs = now;
      RLOGW(TAG, "disconnected: reason=%u (%s), retry in %lu ms",
            (unsigned)reason, disconnectReasonName(reason), (unsigned long)backoffMs);
    }
    else
    {
      RLOGD(TAG, "still disconnected: reason=%u (%s), offline %lu ms",
            (unsigned)reason, disconnectReasonName(reason), now - outageSinceMs);
    }
    nextReconnectAt = now + backoffMs + (now % 250);
    backoffMs = min<uint32_t>(backoffMs * 2, WIFI_RECONNECT_MAX_MS);
    break;
  }
  default:
    break;
  }
}

void initWiFi()
{
  WiFi.setHostname(HOST_NAME);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // modem sleep adds hundreds of ms to every HTTP reply
  WiFi.persistent(false);
  // Reconnects are driven from handleWiFiReconnect() with a backoff
  WiFi.setAutoReconnect(false);
  WiFi.onEvent(onWiFiEvent);

  WiFi.begin(wifiSsid(), wifiPass());

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 3000)
  {
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED)
    startNtpOnce();
  else
    outageSinceMs = millis();
}

static void reconnectWithCurrentCreds()
{
  gotIpSinceApply = false;
  WiFi.disconnect();
  WiFi.begin(wifiSsid(), wifiPass());
  nextReconnectAt = millis() + 5000;
}

void wifiApplyCreds()
{
  applyAtMs = millis() + WIFI_APPLY_DELAY_MS;
  if (applyAtMs == 0)
    applyAtMs = 1;
}

// New credentials become permanent once they get an IP; otherwise they are
// rolled back after WIFI_NEW_CREDS_TIMEOUT_MS.
static void checkNewCreds()
{
  if (applyAtMs != 0 && static_cast<long>(millis() - applyAtMs) >= 0)
  {
    applyAtMs = 0;
    RLOGW(TAG, "reconnecting with \"%s\"", wifiSsid());
    reconnectWithCurrentCreds();
    newCredsSinceMs = millis();
    return;
  }
  if (!wifiPending() || applyAtMs != 0)
    return;
  if (gotIpSinceApply && WiFi.status() == WL_CONNECTED)
  {
    wifiConfirm();
    return;
  }
  if (millis() - newCredsSinceMs < WIFI_NEW_CREDS_TIMEOUT_MS)
    return;
  wifiRevert();
  RLOGW(TAG, "connecting to \"%s\" after rollback", wifiSsid());
  reconnectWithCurrentCreds();
}

void handleWiFiReconnect()
{
  checkNewCreds();

  if (WiFi.status() == WL_CONNECTED || static_cast<long>(millis() - nextReconnectAt) < 0)
    return;

  ++reconnectAttempts;
  if (outageSinceMs == 0)
    outageSinceMs = millis();

  RLOGD(TAG, "reconnect attempt #%lu (offline %lu ms)",
        (unsigned long)reconnectAttempts, millis() - outageSinceMs);

  if ((millis() - outageSinceMs) >= LOG_WIFI_OUTAGE_WARN_MS)
  {
    RLOG_EVERY(300000, RLOG_WARN, TAG, "still offline for %lu s after %lu attempts",
               (unsigned long)((millis() - outageSinceMs) / 1000UL),
               (unsigned long)reconnectAttempts);
  }

  WiFi.reconnect();
  nextReconnectAt = millis() + backoffMs;
}
