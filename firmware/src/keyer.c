// SPDX-License-Identifier: MIT
#include "keyer.h"
#include "config.h"

// Signed difference so comparisons survive the 71-minute wrap of the microsecond counter.
static inline bool reached(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }

// ---------------------------------------------------------------- paddle filter
// Bounce and short spikes cancel out in the integrator instead of toggling the state.
//   press   = level reaches PADDLE_PRESS_LEVEL (>= 1 ms net closed)
//   release = level falls back to 0 (>= 3 ms net open after a press, hysteresis)
//   a new press is not accepted sooner than PADDLE_REPRESS_HOLD_US after a release;
//   a press held through the hold-off is taken when it ends.

void paddle_init(Paddle *p, uint32_t nowUs) {
  p->pressed = false;
  p->level = 0;
  p->lastReleaseUs = nowUs - PADDLE_REPRESS_HOLD_US;
  p->pressedAtUs = nowUs;
}

void paddle_sample(Paddle *p, bool closed, uint32_t nowUs) {
  if (closed) {
    if (p->level < PADDLE_LEVEL_MAX) p->level++;
  } else if (p->level > 0) {
    p->level--;
  }
  if (!p->pressed) {
    if (p->level >= PADDLE_PRESS_LEVEL && nowUs - p->lastReleaseUs >= PADDLE_REPRESS_HOLD_US) {
      p->pressed = true;
      p->pressedAtUs = nowUs;
      p->level = PADDLE_LEVEL_MAX;
    }
  } else if (p->level == 0) {
    p->pressed = false;
    p->lastReleaseUs = nowUs;
  }
}

// ---------------------------------------------------------------- keyer

typedef enum { K_IDLE, K_LEAD, K_ELEMENT, K_GAP } KState;
typedef enum { EL_NONE, EL_DIT, EL_DAH } Element;

static KeyerParams prm = {20, true, 30, 50, true, true, 0, 500};
static KState state = K_IDLE;
static Element last = EL_NONE, pending = EL_NONE;
static uint32_t endUs;
static bool ditMemory, dahMemory;
static bool pttOn;
static uint32_t lastKeyUpUs;

void keyer_init(void) {
  state = K_IDLE;
  last = pending = EL_NONE;
  ditMemory = dahMemory = pttOn = false;
}

void keyer_set_params(const KeyerParams *p) { prm = *p; }

uint32_t keyer_dit_us(void) { return 1200000u / (prm.wpm ? prm.wpm : 20); }

// Weight shifts time from the gap to the element: 50 % = neutral, 75 % = +0.5 dit on, -0.5 dit off.
static int32_t weight_adj(void) { return (int32_t)keyer_dit_us() * ((int32_t)prm.weightPct - 50) / 50; }

static uint32_t element_us(Element e) {
  int32_t base = (int32_t)(e == EL_DIT ? keyer_dit_us() : keyer_dit_us() * prm.dahRatioX10 / 10);
  int32_t v = base + weight_adj();
  int32_t minV = (int32_t)keyer_dit_us() / 4;
  return (uint32_t)(v < minV ? minV : v);
}

static uint32_t gap_us(void) {
  int32_t v = (int32_t)keyer_dit_us() - weight_adj();
  int32_t minV = (int32_t)keyer_dit_us() / 4;
  return (uint32_t)(v < minV ? minV : v);
}

// Iambic alternation: after a dit prefer a dah and vice versa; one paddle repeats its element.
static Element next_element(const Paddle *dit, const Paddle *dah) {
  bool wantDit = ditMemory || dit->pressed;
  bool wantDah = dahMemory || dah->pressed;
  if (last == EL_DIT) return wantDah ? EL_DAH : (wantDit ? EL_DIT : EL_NONE);
  if (last == EL_DAH) return wantDit ? EL_DIT : (wantDah ? EL_DAH : EL_NONE);
  if (wantDit && wantDah) return (int32_t)(dit->pressedAtUs - dah->pressedAtUs) <= 0 ? EL_DIT : EL_DAH;
  return wantDit ? EL_DIT : (wantDah ? EL_DAH : EL_NONE);
}

static void start_element(Element e, uint32_t startUs) {
  state = K_ELEMENT;
  last = e;
  endUs = startUs + element_us(e);
  if (e == EL_DIT) ditMemory = false; else dahMemory = false;
}

void keyer_step(uint32_t now, const Paddle *dit, const Paddle *dah) {
  switch (state) {
    case K_IDLE: {
      Element e = next_element(dit, dah);
      if (e == EL_NONE) break;
      if (prm.pttEnabled && !pttOn && prm.pttLeadMs > 0) {
        pttOn = true;                       // PTT first, the element follows after the lead time
        pending = e;
        state = K_LEAD;
        endUs = now + prm.pttLeadMs * 1000u;
        break;
      }
      if (prm.pttEnabled) pttOn = true;
      start_element(e, now);
      break;
    }
    case K_LEAD:
      if (reached(now, endUs)) start_element(pending, endUs);
      break;
    case K_ELEMENT:
      // The opposite paddle closed during an element is remembered (squeeze / memory).
      if (prm.elementMemory) {
        if (last == EL_DIT && dah->pressed) dahMemory = true;
        if (last == EL_DAH && dit->pressed) ditMemory = true;
      }
      if (reached(now, endUs)) {
        lastKeyUpUs = endUs;
        state = K_GAP;
        endUs += gap_us();                  // scheduled from the deadline: no drift
      }
      break;
    case K_GAP:
      // The paddles are NOT read during the gap: a paddle released while the gap runs must not
      // add another element. The next element is decided at the end of the gap only.
      if (reached(now, endUs)) {
        if (!prm.iambicB && !dit->pressed && !dah->pressed) ditMemory = dahMemory = false;  // mode A
        Element e = next_element(dit, dah);
        if (e != EL_NONE) {
          start_element(e, endUs);
        } else {
          state = K_IDLE;
          last = EL_NONE;
        }
      }
      break;
  }
  // PTT drops PTT hang time after the last element once the keyer is idle.
  if (pttOn && state == K_IDLE && reached(now, lastKeyUpUs + prm.pttHangMs * 1000u)) pttOn = false;
  if (!prm.pttEnabled) pttOn = false;
}

bool keyer_key_down(void) { return state == K_ELEMENT; }
bool keyer_idle(void) { return state == K_IDLE; }
bool keyer_ptt(void) { return pttOn; }

// ---------------------------------------------------------------- announcer

static char annPattern[40];
static uint8_t annLen, annPos;
static bool annActive, annTone;
static uint32_t annEndUs;

static const char *morse_for(char c) {
  static const char *digits[10] = {"-----", ".----", "..---", "...--", "....-",
                                   ".....", "-....", "--...", "---..", "----."};
  static const char *letters[26] = {".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
                                    "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
                                    "..-", "...-", ".--", "-..-", "-.--", "--.."};
  if (c >= '0' && c <= '9') return digits[c - '0'];
  if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
  return "";
}

void announce_start(const char *text, uint32_t nowUs) {
  annLen = 0;
  for (const char *t = text; *t; t++) {
    for (const char *m = morse_for(*t); *m && annLen < sizeof annPattern - 1; m++) annPattern[annLen++] = *m;
    if (annLen < sizeof annPattern - 1) annPattern[annLen++] = ' ';
  }
  annPos = 0;
  annTone = false;
  annEndUs = nowUs;
  annActive = true;
}

void announce_step(uint32_t now) {
  if (!annActive) return;
  if (state != K_IDLE) {                    // paddles take priority
    annActive = annTone = false;
    return;
  }
  if (!reached(now, annEndUs)) return;
  uint32_t unit = keyer_dit_us();
  if (annTone) {                            // gap after every element
    annTone = false;
    annEndUs = now + unit;
    return;
  }
  if (annPos >= annLen) {
    annActive = false;
    return;
  }
  char c = annPattern[annPos++];
  if (c == '.') { annTone = true; annEndUs = now + unit; }
  else if (c == '-') { annTone = true; annEndUs = now + 3 * unit; }
  else annEndUs = now + 2 * unit;           // letter gap = 3 units, 1 already elapsed
}

bool announce_tone(void) { return annTone; }
