// SPDX-License-Identifier: MIT
// Paddle filter, iambic keyer, PTT sequencer and Morse announcer. Pure logic: time and raw
// contact states come in as arguments, the outputs are read with the getters.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint16_t wpm;            // 5..60
  bool iambicB;            // false = iambic A
  uint16_t dahRatioX10;    // 28..35
  uint16_t weightPct;      // 25..75, 50 = neutral
  bool elementMemory;
  bool pttEnabled;
  uint16_t pttLeadMs;
  uint16_t pttHangMs;
} KeyerParams;

// ---- paddle filter (H-04): integrator with hysteresis, sampled every PADDLE_SAMPLE_US
typedef struct {
  bool pressed;
  uint8_t level;
  uint32_t lastReleaseUs;
  uint32_t pressedAtUs;
} Paddle;

void paddle_init(Paddle *p, uint32_t nowUs);
void paddle_sample(Paddle *p, bool closed, uint32_t nowUs);   // call once per sample period

// ---- keyer
void keyer_init(void);
void keyer_set_params(const KeyerParams *params);
void keyer_step(uint32_t nowUs, const Paddle *dit, const Paddle *dah);
bool keyer_key_down(void);   // an element is being sent (sidetone, USB stream, KEY output)
bool keyer_idle(void);       // no element, gap or PTT lead in progress
bool keyer_ptt(void);        // PTT output state
uint32_t keyer_dit_us(void);

// ---- announcer: short Morse messages on the sidetone only (never USB, never KEY/PTT)
void announce_start(const char *text, uint32_t nowUs);   // digits and A–Z
void announce_step(uint32_t nowUs);                     // cancelled while the keyer is busy
bool announce_tone(void);
