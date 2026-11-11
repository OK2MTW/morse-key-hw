// SPDX-License-Identifier: MIT
// Host unit tests for the hardware-independent modules: settings, SysEx protocol, keyer, paddle filter.
//   python -m ziglang cc -std=gnu11 -I src tests/test_host.c src/settings.c src/protocol.c src/keyer.c -o test.exe
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "keyer.h"
#include "protocol.h"
#include "settings.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// ---------------------------------------------------------------- protocol host stub
static uint8_t out[4096];
static uint32_t outLen;
static int frames;
static bool busyFlag, bootRequested;
static int saveRequests, changedCalls;

static void h_send(const uint8_t *d, uint32_t n) { memcpy(out + outLen, d, n); outLen += n; frames++; }
static uint8_t h_chip(void) { return 1; }
static bool h_busy(void) { return busyFlag; }
static void h_save(uint8_t seq) { (void)seq; saveRequests++; }
static void h_boot(void) { bootRequested = true; }
static void h_changed(uint8_t id) { (void)id; changedCalls++; }
static const ProtocolHost host = {h_send, h_chip, h_busy, h_save, h_boot, h_changed};

static void reset_out(void) { outLen = 0; frames = 0; }
static void feed(const uint8_t *b, uint32_t n) { for (uint32_t i = 0; i < n; i++) protocol_rx_byte(b[i]); }
static void cmd(uint8_t c, uint8_t seq, const uint8_t *p, uint8_t n) {
  uint8_t f[64] = {0xF0, 0x7D, 0x43, 0x4B, 0x01, c, seq};
  memcpy(f + 7, p, n);
  f[7 + n] = 0xF7;
  feed(f, 8 + n);
}
// Response i (0-based) in the output: pointer to its first byte (F0) or NULL.
static const uint8_t *frame_at(int i) {
  for (uint32_t k = 0; k < outLen; k++) {
    if (out[k] == 0xF0 && i-- == 0) return &out[k];
  }
  return NULL;
}

static void test_settings(void) {
  settings_init_defaults();
  CHECK(settings_get(SET_WPM) == 20);
  CHECK(settings_get(SET_STREAM_KEY) == 0x68);
  CHECK(settings_get(SET_PTT_HANG_MS) == 500);
  CHECK(settings_set(SET_WPM, 4) == SETTINGS_ERR_RANGE);
  CHECK(settings_set(SET_WPM, 61) == SETTINGS_ERR_RANGE);
  CHECK(settings_get(SET_WPM) == 20);
  CHECK(settings_set(SET_WPM, 25) == SETTINGS_OK && settings_get(SET_WPM) == 25);
  CHECK(settings_set(0x00, 1) == SETTINGS_ERR_UNKNOWN_ID);
  CHECK(settings_set(0x0F, 1) == SETTINGS_ERR_UNKNOWN_ID);
  CHECK(settings_next_button_wpm(20) == 30);
  CHECK(settings_next_button_wpm(25) == 30);
  CHECK(settings_next_button_wpm(5) == 20);
  CHECK(settings_next_button_wpm(60) == 20);
  uint32_t g = settings_generation();
  settings_set(SET_WPM, 25);            // same value: no change
  CHECK(settings_generation() == g);
  settings_set(SET_WPM, 26);
  CHECK(settings_generation() != g);
}

static void test_protocol(void) {
  settings_init_defaults();
  protocol_init(&host);

  reset_out();
  cmd(CMD_GET_INFO, 5, NULL, 0);
  const uint8_t *f = frame_at(0);
  CHECK(frames == 1 && f && f[1] == 0x7D && f[2] == 0x43 && f[3] == 0x4B && f[4] == 1);
  CHECK(f[5] == RSP_INFO && f[6] == 5);
  CHECK(f[7] == 1 && f[8] == CWKEY_VERSION_MAJOR && f[9] == CWKEY_VERSION_MINOR && f[10] == CWKEY_VERSION_PATCH);
  CHECK(f[11] == 1 /* chip F401 from stub */ && f[13] == 0x07 && f[14] == 5 && memcmp(&f[15], "CwKey", 5) == 0);
  CHECK(out[outLen - 1] == 0xF7);

  // SET tone 1000 Hz = 0x3E8 -> LSB7 0x68, MSB7 0x07
  reset_out(); changedCalls = 0;
  uint8_t setTone[] = {SET_TONE_HZ, 0x68, 0x07};
  cmd(CMD_SET, 9, setTone, 3);
  f = frame_at(0);
  CHECK(f && f[5] == RSP_ACK && f[6] == 9 && f[7] == CMD_SET);
  CHECK(settings_get(SET_TONE_HZ) == 1000 && changedCalls == 1);

  reset_out();
  uint8_t badRange[] = {SET_WPM, 70, 0};
  cmd(CMD_SET, 10, badRange, 3);
  f = frame_at(0);
  CHECK(f && f[5] == RSP_NAK && f[7] == CMD_SET && f[8] == ERR_RANGE && settings_get(SET_WPM) == 20);

  reset_out();
  uint8_t badId[] = {0x30, 1, 0};
  cmd(CMD_SET, 11, badId, 3);
  f = frame_at(0);
  CHECK(f && f[5] == RSP_NAK && f[8] == ERR_UNKNOWN_ID);

  reset_out();
  uint8_t getWpm[] = {SET_WPM};
  cmd(CMD_GET, 12, getWpm, 1);
  f = frame_at(0);
  CHECK(f && f[5] == RSP_VALUE && f[6] == 12 && f[7] == SET_WPM && f[8] == 20 && f[9] == 0);

  reset_out();
  cmd(CMD_GET_ALL, 13, NULL, 0);
  CHECK(frames == SET_COUNT + 1);
  f = frame_at(SET_COUNT);
  CHECK(f && f[5] == RSP_ACK && f[7] == CMD_GET_ALL);

  reset_out();
  cmd(0x20, 14, NULL, 0);
  f = frame_at(0);
  CHECK(f && f[5] == RSP_NAK && f[8] == ERR_UNKNOWN_CMD);

  reset_out();                                       // wrong protocol version
  uint8_t v2[] = {0xF0, 0x7D, 0x43, 0x4B, 0x02, CMD_GET_INFO, 1, 0xF7};
  feed(v2, sizeof v2);
  f = frame_at(0);
  CHECK(f && f[5] == RSP_NAK && f[8] == ERR_BAD_FRAME);

  reset_out();                                       // other 0x7D traffic, notes, real-time bytes: ignored
  uint8_t foreign[] = {0xF0, 0x7D, 0x11, 0x22, 0x01, 0x01, 0x00, 0xF7, 0x90, 0x40, 0x7F, 0xF8};
  feed(foreign, sizeof foreign);
  CHECK(frames == 0);
  uint8_t withClock[] = {0xF0, 0x7D, 0x43, 0xF8, 0x4B, 0x01, CMD_GET_INFO, 2, 0xF7};
  feed(withClock, sizeof withClock);
  CHECK(frames == 1);

  reset_out(); bootRequested = false;
  uint8_t wrongConfirm[] = {0x55, 0x46, 0x33};
  cmd(CMD_ENTER_BOOTLOADER, 20, wrongConfirm, 3);
  f = frame_at(0);
  CHECK(f && f[5] == RSP_NAK && f[8] == ERR_BAD_CONFIRM && !bootRequested);

  reset_out(); busyFlag = true;
  uint8_t confirm[] = {0x55, 0x46, 0x32};
  cmd(CMD_ENTER_BOOTLOADER, 21, confirm, 3);
  f = frame_at(0);
  CHECK(f && f[5] == RSP_NAK && f[8] == ERR_BUSY && !bootRequested);

  reset_out(); busyFlag = false;
  cmd(CMD_ENTER_BOOTLOADER, 22, confirm, 3);
  f = frame_at(0);
  CHECK(f && f[5] == RSP_ACK && f[7] == CMD_ENTER_BOOTLOADER && bootRequested);

  reset_out(); saveRequests = 0;
  cmd(CMD_SAVE, 23, NULL, 0);
  CHECK(saveRequests == 1 && frames == 0);         // answered later by the firmware

  settings_set(SET_WPM, 33);
  reset_out();
  cmd(CMD_RESET_DEFAULTS, 24, NULL, 0);
  CHECK(settings_get(SET_WPM) == 20 && frame_at(0)[5] == RSP_ACK);

  reset_out();
  protocol_notify_changed(SET_USB_MODE);
  f = frame_at(0);
  CHECK(f && f[5] == RSP_CHANGED && f[6] == 0 && f[7] == SET_USB_MODE);
}

// ---------------------------------------------------------------- keyer simulation

static Paddle pd, pa;
static KeyerParams kp;

typedef struct { uint32_t on, off; } Element;
static Element els[64];
static int nEls;

// Runs the keyer for durMs with the given paddle states (already filtered), records elements.
static void run(uint32_t *t, uint32_t durMs, bool dit, bool dah, bool *wasDown) {
  for (uint32_t i = 0; i < durMs * 10; i++) {   // 100 us steps
    if (dit && !pd.pressed) pd.pressedAtUs = *t;
    if (dah && !pa.pressed) pa.pressedAtUs = *t;
    pd.pressed = dit;
    pa.pressed = dah;
    keyer_step(*t, &pd, &pa);
    bool down = keyer_key_down();
    if (down && !*wasDown && nEls < 64) els[nEls++].on = *t;
    if (!down && *wasDown) els[nEls - 1].off = *t;
    *wasDown = down;
    *t += 100;
  }
}

static void keyer_reset(void) {
  keyer_init();
  KeyerParams p = {20, true, 30, 50, true, true, 0, 500};
  kp = p;
  keyer_set_params(&kp);
  memset(&pd, 0, sizeof pd);
  memset(&pa, 0, sizeof pa);
  nEls = 0;
}

static void test_keyer(void) {
  uint32_t t = 1000000;
  bool down = false;

  // A short dit tap (40 ms, i.e. still held at the start of the gap) gives exactly one dit.
  keyer_reset();
  run(&t, 40, true, false, &down);
  run(&t, 300, false, false, &down);
  CHECK(nEls == 1);
  CHECK(els[0].off - els[0].on >= 59900 && els[0].off - els[0].on <= 60100);   // 20 WPM dit = 60 ms

  // Held dit for 500 ms: dits every 120 ms, all 60 ms long.
  keyer_reset(); down = false;
  run(&t, 500, true, false, &down);
  run(&t, 300, false, false, &down);
  CHECK(nEls >= 4 && nEls <= 5);
  for (int i = 1; i < nEls; i++) CHECK(els[i].on - els[i - 1].on >= 119900 && els[i].on - els[i - 1].on <= 120100);

  // Squeeze from idle, dit first, release both during the 2nd element (dah): iambic B adds one dit.
  keyer_reset(); down = false;
  run(&t, 5, true, false, &down);          // dit first
  run(&t, 150, true, true, &down);         // squeeze: dit(0-60) gap(60-120) dah(120-300)
  run(&t, 400, false, false, &down);
  CHECK(nEls == 3);
  CHECK(els[1].off - els[1].on >= 179900 && els[1].off - els[1].on <= 180100);  // dah 180 ms
  CHECK(els[2].off - els[2].on >= 59900 && els[2].off - els[2].on <= 60100);    // extra dit

  // Same in iambic A: no extra element.
  keyer_reset(); down = false;
  kp.iambicB = false;
  keyer_set_params(&kp);
  run(&t, 5, true, false, &down);
  run(&t, 150, true, true, &down);
  run(&t, 400, false, false, &down);
  CHECK(nEls == 2);

  // PTT on with the first element, off 500 ms after the last one.
  keyer_reset(); down = false;
  run(&t, 10, true, false, &down);
  CHECK(keyer_ptt());
  run(&t, 300, false, false, &down);       // dit ends at ~60 ms
  CHECK(keyer_ptt());
  run(&t, 300, false, false, &down);       // > 500 ms after the end of the dit
  CHECK(!keyer_ptt());

  // PTT disabled: never on.
  keyer_reset(); down = false;
  kp.pttEnabled = false;
  keyer_set_params(&kp);
  run(&t, 10, true, false, &down);
  CHECK(!keyer_ptt());

  // PTT lead 30 ms delays the first element.
  keyer_reset(); down = false;
  kp.pttLeadMs = 30;
  keyer_set_params(&kp);
  uint32_t start = t;
  run(&t, 10, true, false, &down);
  CHECK(keyer_ptt() && !keyer_key_down());
  run(&t, 300, false, false, &down);
  CHECK(nEls == 1 && els[0].on - start >= 29900 && els[0].on - start <= 30200);

  // Weight 75 %: dit 60 + 30 = 90 ms.
  keyer_reset(); down = false;
  kp.weightPct = 75;
  keyer_set_params(&kp);
  run(&t, 10, true, false, &down);
  run(&t, 300, false, false, &down);
  CHECK(nEls == 1 && els[0].off - els[0].on >= 89900 && els[0].off - els[0].on <= 90100);
}

static void test_paddle_filter(void) {
  Paddle p;
  uint32_t t = 0;
  paddle_init(&p, t);
  int presses = 0;
  bool last = false;
  // Contact closes with 3 ms of chatter, stays closed 20 ms, opens with 8 ms of chatter.
  for (uint32_t us = 0; us < 60000; us += PADDLE_SAMPLE_US) {
    bool closed;
    if (us < 5000) closed = false;
    else if (us < 8000) closed = (us / 300) % 2 == 0;
    else if (us < 28000) closed = true;
    else if (us < 36000) closed = (us / 300) % 2 == 1;
    else closed = false;
    paddle_sample(&p, closed, us);
    if (p.pressed && !last) presses++;
    last = p.pressed;
  }
  CHECK(presses == 1 && !p.pressed);

  // A 0.5 ms spike is ignored.
  paddle_init(&p, 100000);
  presses = 0; last = false;
  for (uint32_t us = 100000; us < 120000; us += PADDLE_SAMPLE_US) {
    paddle_sample(&p, us >= 110000 && us < 110500, us);
    if (p.pressed && !last) presses++;
    last = p.pressed;
  }
  CHECK(presses == 0);
}

int main(void) {
  test_settings();
  test_protocol();
  test_keyer();
  test_paddle_filter();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
