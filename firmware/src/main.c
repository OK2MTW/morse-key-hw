// SPDX-License-Identifier: MIT
// CW Key firmware 0.4.0 – WeAct Black Pill STM32F411 / STM32F401
// Spec: docs/specification.md, USB/MIDI: docs/midi-settings-spec.md, flashing: docs/firmware-flashing.md
//
// USB composite device:
//   HID boot keyboard (1 ms): stream key while an element sounds (mode B), plus L/R Ctrl = paddles (mode C)
//   MIDI: SysEx settings channel (read/change/store settings, enter the UF2 bootloader)
// KEY/PTT outputs follow only the local keyer – nothing received over USB can key them (H-21).
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "board.h"
#include "config.h"
#include "keyer.h"
#include "protocol.h"
#include "settings.h"
#include "storage.h"
#include "tusb.h"

// Keep the version string in the image (package.ps1 checks for it).
__attribute__((used)) const char cwkey_version[] = CWKEY_VERSION_STRING;

// ---------------------------------------------------------------- settings applied to the hardware

static bool modeC, paddleSwap, sidetoneOn;
static uint8_t streamKey;
static uint32_t speakerMaxCompare;
static uint32_t appliedGeneration;

static void apply_settings(void) {
  KeyerParams k = {
    .wpm = settings_get(SET_WPM),
    .iambicB = settings_get(SET_KEYER_MODE) == 1,
    .dahRatioX10 = settings_get(SET_DAH_RATIO_X10),
    .weightPct = settings_get(SET_WEIGHT_PCT),
    .elementMemory = settings_get(SET_ELEMENT_MEMORY) != 0,
    .pttEnabled = settings_get(SET_PTT_ENABLED) != 0,
    .pttLeadMs = settings_get(SET_PTT_LEAD_MS),
    .pttHangMs = settings_get(SET_PTT_HANG_MS),
  };
  keyer_set_params(&k);
  modeC = settings_get(SET_USB_MODE) == 1;
  paddleSwap = settings_get(SET_PADDLE_SWAP) != 0;
  sidetoneOn = settings_get(SET_SIDETONE_ON) != 0;
  streamKey = (uint8_t)settings_get(SET_STREAM_KEY);
  board_speaker_set_tone(settings_get(SET_TONE_HZ));
  // 100 % volume = 50 % duty (loudest square wave)
  speakerMaxCompare = board_speaker_period() / 2 * settings_get(SET_VOLUME_PCT) / 100;
  appliedGeneration = settings_generation();
}

// ---------------------------------------------------------------- autosave

static bool dirty;
static uint32_t lastChangeMs;

static void mark_dirty(void) {
  dirty = true;
  lastChangeMs = board_millis();
}

// ---------------------------------------------------------------- sidetone (buzzer on PB8 + speaker on PA8)

static bool soundOn;
static uint32_t soundEdgeUs, soundLevelAtEdge, soundLevel;   // level 0..1000

static void sound_update(bool on, uint32_t now) {
  if (on != soundOn) {
    soundOn = on;
    board_buzzer(on);
    soundEdgeUs = now;
    soundLevelAtEdge = soundLevel;
  }
  uint32_t elapsed = now - soundEdgeUs;
  uint32_t step = elapsed >= SPEAKER_RAMP_US ? 1000 : elapsed * 1000 / SPEAKER_RAMP_US;
  uint32_t level = soundOn ? (soundLevelAtEdge + step > 1000 ? 1000 : soundLevelAtEdge + step)
                           : (soundLevelAtEdge > step ? soundLevelAtEdge - step : 0);
  soundLevel = level;
  board_speaker_set_compare(speakerMaxCompare * level / 1000);
}

// ---------------------------------------------------------------- HID keyboard queue
// Every state change is queued and sent in its own 1 ms frame: no event is merged or lost (H-33, H-42).

typedef struct { uint8_t mods, key; } HidState;
#define HID_FIFO 16
static HidState fifo[HID_FIFO];
static uint8_t fifoHead, fifoCount;
static HidState lastQueued;
static bool haveQueued;

static void hid_queue(HidState s) {
  if (!tud_mounted()) {
    fifoCount = 0;
    haveQueued = false;          // resend the current state once the host configures us
    return;
  }
  if (haveQueued && s.mods == lastQueued.mods && s.key == lastQueued.key) return;
  if (fifoCount == HID_FIFO) {
    fifo[(fifoHead + fifoCount - 1) % HID_FIFO] = s;   // overflow: keep the newest state
  } else {
    fifo[(fifoHead + fifoCount) % HID_FIFO] = s;
    fifoCount++;
  }
  lastQueued = s;
  haveQueued = true;
}

static void hid_pump(void) {
  if (fifoCount == 0 || !tud_mounted() || !tud_hid_ready()) return;
  uint8_t keys[6] = {0};
  keys[0] = fifo[fifoHead].key;
  if (tud_hid_keyboard_report(0, fifo[fifoHead].mods, keys)) {
    fifoHead = (fifoHead + 1) % HID_FIFO;
    fifoCount--;
  }
}

// ---------------------------------------------------------------- MIDI transmit buffer

#define MIDI_TX 1024
static uint8_t midiTx[MIDI_TX];
static uint16_t midiHead, midiCount;

static void midi_send(const uint8_t *data, uint32_t len) {
  if (!tud_midi_mounted()) return;
  if (midiCount + len > MIDI_TX) return;           // host is not reading: drop whole frames only
  for (uint32_t i = 0; i < len; i++) midiTx[(midiHead + midiCount + i) % MIDI_TX] = data[i];
  midiCount += (uint16_t)len;
}

static void midi_pump(void) {
  if (!tud_midi_mounted()) {
    midiCount = 0;
    return;
  }
  while (midiCount) {
    uint16_t chunk = midiCount;
    if (midiHead + chunk > MIDI_TX) chunk = MIDI_TX - midiHead;   // contiguous part
    uint32_t written = tud_midi_stream_write(0, &midiTx[midiHead], chunk);
    if (written == 0) break;
    midiHead = (uint16_t)((midiHead + written) % MIDI_TX);
    midiCount -= (uint16_t)written;
  }
  uint8_t rx[64];
  while (tud_midi_available()) {
    uint32_t n = tud_midi_stream_read(rx, sizeof rx);
    if (n == 0) break;
    for (uint32_t i = 0; i < n; i++) protocol_rx_byte(rx[i]);
  }
}

// ---------------------------------------------------------------- protocol host

static bool saveRequested;
static uint8_t saveSeq;
static uint32_t saveRequestMs;
static bool bootloaderRequested;
static uint32_t bootloaderRequestMs;

static bool host_busy(void) { return !keyer_idle() || keyer_ptt(); }
static uint8_t host_chip(void) { return board_chip(); }

static void host_request_save(uint8_t seq) {
  saveRequested = true;
  saveSeq = seq;
  saveRequestMs = board_millis();
}

static void host_request_bootloader(void) {
  bootloaderRequested = true;
  bootloaderRequestMs = board_millis();
}

static void host_settings_changed(uint8_t id) {
  (void)id;
  apply_settings();
  mark_dirty();
}

static const ProtocolHost protocolHost = {
  .send = midi_send,
  .chip = host_chip,
  .busy = host_busy,
  .request_save = host_request_save,
  .request_bootloader = host_request_bootloader,
  .settings_changed = host_settings_changed,
};

static void service_save(void) {
  uint32_t now = board_millis();
  bool idle = keyer_idle() && !keyer_ptt();
  if (saveRequested) {
    if (idle) {
      bool ok = storage_save();
      if (ok) dirty = false;
      if (ok) protocol_send_ack(CMD_SAVE, saveSeq); else protocol_send_nak(CMD_SAVE, saveSeq, ERR_FLASH);
      saveRequested = false;
    } else if (now - saveRequestMs > 5000) {
      protocol_send_nak(CMD_SAVE, saveSeq, ERR_BUSY);
      saveRequested = false;
    }
    return;
  }
  if (dirty && idle && now - lastChangeMs >= AUTOSAVE_DELAY_MS) {
    if (storage_save()) dirty = false;
    else lastChangeMs = now;          // try again later
  }
}

static void service_bootloader(void) {
  if (!bootloaderRequested) return;
  if (board_millis() - bootloaderRequestMs < 30) return;   // let the ACK leave the device
  board_key_out(false);
  board_ptt_out(false);
  tud_disconnect();
  uint32_t t0 = board_millis();
  while (board_millis() - t0 < 50) tud_task();
  board_reboot_to_bootloader();
}

// ---------------------------------------------------------------- KEY button

static bool btnPressed, btnLongHandled;
static uint32_t btnChangeMs, btnPressedMs;

static void announce_wpm(uint32_t nowUs) {
  char text[4];
  uint16_t w = settings_get(SET_WPM);
  text[0] = (char)('0' + w / 10);
  text[1] = (char)('0' + w % 10);
  text[2] = 0;
  announce_start(w >= 10 ? text : text + 1, nowUs);
}

static void button_update(uint32_t nowMs, uint32_t nowUs) {
  bool raw = board_button_pressed();
  if (raw != btnPressed && nowMs - btnChangeMs >= BUTTON_DEBOUNCE_MS) {
    btnPressed = raw;
    btnChangeMs = nowMs;
    if (raw) {
      btnPressedMs = nowMs;
      btnLongHandled = false;
    } else if (!btnLongHandled) {                         // short press: next speed
      settings_set(SET_WPM, settings_next_button_wpm(settings_get(SET_WPM)));
      apply_settings();
      mark_dirty();
      announce_wpm(nowUs);
      protocol_notify_changed(SET_WPM);
    }
  }
  if (btnPressed && !btnLongHandled && nowMs - btnPressedMs >= BUTTON_LONG_MS) {   // long: USB mode
    btnLongHandled = true;
    settings_set(SET_USB_MODE, settings_get(SET_USB_MODE) ? 0 : 1);
    apply_settings();
    mark_dirty();
    announce_start(settings_get(SET_USB_MODE) ? "C" : "B", nowUs);
    protocol_notify_changed(SET_USB_MODE);
  }
}

// ---------------------------------------------------------------- start-up

static void signal_clock_source(void) {
  // 1 long flash = crystal OK, 3 short = HSI fallback (USB may be unreliable)
  int count = board_clock_on_hsi() ? 3 : 1;
  uint32_t onMs = board_clock_on_hsi() ? 150 : 600;
  for (int i = 0; i < count; i++) {
    board_led(true);
    board_delay_ms(onMs);
    board_led(false);
    board_delay_ms(250);
  }
}

// KEY held while plugging in = factory reset (defaults stored, "R" announced).
static bool factory_reset_requested(void) {
  if (!board_button_pressed()) return false;
  uint32_t t0 = board_millis();
  while (board_millis() - t0 < 100) {
    if (!board_button_pressed()) return false;
  }
  return true;
}

int main(void) {
  __asm volatile("" : : "r"(cwkey_version));   // keep the version string in the image
  board_init();

  settings_init_defaults();
  bool reset = factory_reset_requested();
  if (reset) {
    storage_save();
    btnPressed = true;            // the held button must not count as a short press
    btnLongHandled = true;
  } else {
    storage_load();
  }
  keyer_init();
  apply_settings();
  signal_clock_source();

  protocol_init(&protocolHost);
  tusb_init();

  Paddle dit, dah;
  uint32_t lastSampleUs = board_micros();
  paddle_init(&dit, lastSampleUs);
  paddle_init(&dah, lastSampleUs);
  if (reset) announce_start("R", lastSampleUs);

  for (;;) {
    tud_task();
    uint32_t now = board_micros();

    // Paddles sampled at a fixed rate; a held-up loop does not replay old samples.
    if (now - lastSampleUs > 10 * PADDLE_SAMPLE_US) lastSampleUs = now - PADDLE_SAMPLE_US;
    while (now - lastSampleUs >= PADDLE_SAMPLE_US) {
      lastSampleUs += PADDLE_SAMPLE_US;
      bool a = board_dit_closed(), b = board_dah_closed();
      paddle_sample(&dit, paddleSwap ? b : a, lastSampleUs);
      paddle_sample(&dah, paddleSwap ? a : b, lastSampleUs);
    }

    button_update(board_millis(), now);
    if (appliedGeneration != settings_generation()) apply_settings();
    keyer_step(now, &dit, &dah);
    announce_step(now);

    bool keyDown = keyer_key_down();
    board_key_out(keyDown);
    board_ptt_out(keyer_ptt());
    sound_update((keyDown && sidetoneOn) || announce_tone(), now);
    board_led(keyDown);

    HidState s = {0, keyDown ? streamKey : 0};
    if (modeC) s.mods = (dit.pressed ? 0x01 : 0) | (dah.pressed ? 0x10 : 0);   // Left Ctrl, Right Ctrl
    hid_queue(s);
    hid_pump();

    midi_pump();
    service_save();
    service_bootloader();
  }
}
