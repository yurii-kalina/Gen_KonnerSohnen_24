#include "ks24_bus.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include "config/config.h"
#include "config/pins.h"
#include "ks24_frame.h"
#include "services/logger.h"

static const char *const TAG = "ks24";

static constexpr uint32_t KS24_BAUD = 2400;
// A frame takes 141 ms, the gap between frames ~860 ms. The UART hands bytes
// over in bursts, so the silence threshold sits between the two.
static constexpr uint32_t KS24_SILENCE_RESET_MS = 400;
// Data is valid while the last accepted frame is younger than this (one frame per second)
static constexpr uint32_t KS24_STALE_MS = 5000;

static_assert(KS24_SILENCE_RESET_MS > 141 && KS24_SILENCE_RESET_MS < 860,
              "KS24_SILENCE_RESET_MS must be longer than a frame and shorter than the gap");

namespace
{
  HardwareSerial &bus = Serial2;
  ks24::Parser parser;
  ks24::Frame frame;

  uint32_t lastByteMs = 0;
  uint32_t lastAcceptedMs = 0; // 0 = no frame since boot
  bool haveLatest = false;     // there is a live (not stale) frame
  uint8_t tempGuess = 0;

  // Last accepted frames, for the median
  constexpr uint8_t HISTORY = 3;
  float histV[HISTORY];
  float histA[HISTORY];
  uint16_t histRpm[HISTORY];
  uint8_t histCount = 0;
  uint8_t histPos = 0; // next slot to write

  // A state change needs two consecutive accepted frames that agree
  bool prevFrameRunning = false;
  bool confirmedRunning = false;

  template <typename T>
  T median3(const T *v)
  {
    const T a = v[0], b = v[1], c = v[2];
    return max(min(a, b), min(max(a, b), c));
  }

  // Median once three frames are in, the latest frame before that
  template <typename T>
  T filtered(const T *v)
  {
    if (histCount >= HISTORY)
      return median3(v);
    return v[(histPos + HISTORY - 1) % HISTORY];
  }

  void forget()
  {
    RLOGW(TAG, "bus silent for %lu ms — data dropped, run state from the RUN lamp only",
          (unsigned long)KS24_STALE_MS);
    haveLatest = false;
    histCount = 0;
    histPos = 0;
    confirmedRunning = false;
  }

  void onAccepted(uint32_t now)
  {
    const bool run = frame.running();
    if (!haveLatest)
      RLOGI(TAG, "bus frames received: %.1f V %.1f A %u rpm, status 0x%02X",
            frame.voltage(), frame.current(), (unsigned)frame.rpm(), (unsigned)frame.status());
    if (!frame.statusKnown())
      RLOG_EVERY(60000, RLOG_WARN, TAG, "unknown status byte 0x%02X (treated as not running)",
                 (unsigned)frame.status());
    // The first frame after a gap only arms the check; it never confirms alone
    if (haveLatest && run == prevFrameRunning && run != confirmedRunning)
    {
      confirmedRunning = run;
      RLOGI(TAG, "engine %s (bus, 2 frames): %.1f V %.1f A %u rpm", run ? "RUNNING" : "stopped",
            frame.voltage(), frame.current(), (unsigned)frame.rpm());
    }
    prevFrameRunning = run;

    histV[histPos] = frame.voltage();
    histA[histPos] = frame.current();
    histRpm[histPos] = frame.rpm();
    histPos = (histPos + 1) % HISTORY;
    if (histCount < HISTORY)
      ++histCount;

    tempGuess = frame.temp_guess();
    lastAcceptedMs = now;
    haveLatest = true;
  }

  // Rejected frames are logged, not hidden: a new genuine state shows up here
  // and the template in ks24_frame.h then needs updating. Every frame goes to
  // USB, one per 10 min to the backend so a steady new state cannot flood the ring.
  void logRejected(ks24::Result r)
  {
    char hex[ks24::FRAME_LEN * 2 + 1];
    for (uint8_t i = 0; i < ks24::FRAME_LEN; ++i)
      snprintf(hex + i * 2, 3, "%02X", frame.raw[i]);
    const char *why = r == ks24::Result::RejectedTemplate ? "template" : "range";
    logWriteSerialOnly(RLOG_WARN, TAG, "rejected (%s): %s", why, hex);
    RLOG_EVERY(600000, RLOG_WARN, TAG, "rejected (%s, %lu total): %s", why,
               (unsigned long)parser.rejected(), hex);
  }
}

void initKs24Bus()
{
  bus.begin(KS24_BAUD, SERIAL_8N1, KS24_RX_PIN, -1);
  // The line idles LOW: pull-down keeps a disconnected wire quiet
  gpio_pullup_dis(static_cast<gpio_num_t>(KS24_RX_PIN));
  gpio_pulldown_en(static_cast<gpio_num_t>(KS24_RX_PIN));
  lastByteMs = millis();
}

void updateKs24Bus()
{
  const uint32_t now = millis();
  bool gotByte = false;

  while (bus.available() > 0)
  {
    const int b = bus.read();
    if (b < 0)
      break;
    gotByte = true;
    const ks24::Result r = parser.push(static_cast<uint8_t>(b), frame);
    if (r == ks24::Result::Accepted)
      onAccepted(now);
    else if (r == ks24::Result::RejectedTemplate || r == ks24::Result::RejectedRange)
      logRejected(r);
  }

  if (gotByte)
    lastByteMs = now;
  else if (parser.pending() && now - lastByteMs > KS24_SILENCE_RESET_MS)
    parser.reset(); // the tail of that frame was lost

  if (haveLatest && now - lastAcceptedMs >= KS24_STALE_MS)
    forget();
}

Ks24Data getKs24Data()
{
  const uint32_t now = millis();
  Ks24Data d{};
  d.valid = haveLatest && now - lastAcceptedMs < KS24_STALE_MS;
  d.running = d.valid && confirmedRunning;
  if (d.valid)
  {
    d.voltage = filtered(histV);
    d.current = filtered(histA);
    d.rpm = filtered(histRpm);
    d.tempGuess = tempGuess;
  }
  return d;
}

Ks24Stats getKs24Stats()
{
  Ks24Stats st{};
  st.good = parser.good();
  st.rejectedTemplate = parser.rejectedTemplate();
  st.rejectedRange = parser.rejectedRange();
  st.bad = parser.bad();
  st.dropped = parser.dropped();
  st.truncated = parser.truncated();
  return st;
}
