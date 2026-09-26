#pragma once
#include <stdint.h>

// Дані з лінії інвертор -> дисплей KS 24VS-DC (UART2 RX, 2400 8N1).
// Кадри без контрольної суми (див. ks24_frame.h), тому стан "працює"
// змінюється лише коли два прийняті кадри поспіль згодні, а значення —
// медіана трьох останніх кадрів.
struct Ks24Data
{
  bool valid;         // останній прийнятий кадр молодший за KS24_STALE_MS
  bool running;       // двигун працює, підтверджено двома кадрами (лише коли valid)
  float voltage;      // В, напруга шини
  float current;      // А, струм заряду
  uint16_t rpm;
  uint8_t tempGuess;  // байт 29, можливо температура (не перевірено)
};

void initKs24Bus();
// Call from loop(): drains the UART and parses frames
void updateKs24Bus();
Ks24Data getKs24Data();

// Parser counters since boot, for /diag/sys
struct Ks24Stats
{
  uint32_t good;
  uint32_t rejectedTemplate;
  uint32_t rejectedRange;
  uint32_t bad;
  uint32_t dropped;
  uint32_t truncated;
};
Ks24Stats getKs24Stats();
