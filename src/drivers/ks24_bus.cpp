#include "ks24_bus.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include "config/config.h"
#include "config/pins.h"
#include "ks24_frame.h"

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
    haveLatest = false;
    histCount = 0;
    histPos = 0;
    confirmedRunning = false;
  }

  void onAccepted(uint32_t now)
  {
    const bool run = frame.running();
    // The first frame after a gap only arms the check; it never confirms alone
    if (haveLatest && run == prevFrameRunning)
      confirmedRunning = run;
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

  // Rejected frames are printed, not hidden: a new genuine state shows up here
  // and the template in ks24_frame.h then needs updating.
  void logRejected(ks24::Result r)
  {
    Serial.print(r == ks24::Result::RejectedTemplate ? "KS24 rejected (template):" : "KS24 rejected (range):");
    for (uint8_t i = 0; i < ks24::FRAME_LEN; ++i)
      Serial.printf(" %02X", frame.raw[i]);
    Serial.println();
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
