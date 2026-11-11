// SPDX-License-Identifier: MIT
// Settings table (docs/midi-settings-spec.md, section 5.3). Pure logic, no hardware access.
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
  SET_WPM = 0x01,
  SET_KEYER_MODE = 0x02,      // 0 iambic A, 1 iambic B
  SET_DAH_RATIO_X10 = 0x03,
  SET_WEIGHT_PCT = 0x04,
  SET_ELEMENT_MEMORY = 0x05,
  SET_PADDLE_SWAP = 0x06,
  SET_USB_MODE = 0x07,        // 0 B, 1 C
  SET_SIDETONE_ON = 0x08,
  SET_TONE_HZ = 0x09,
  SET_VOLUME_PCT = 0x0A,
  SET_PTT_ENABLED = 0x0B,
  SET_PTT_LEAD_MS = 0x0C,
  SET_PTT_HANG_MS = 0x0D,
  SET_STREAM_KEY = 0x0E,
  SET_FIRST = SET_WPM,
  SET_LAST = SET_STREAM_KEY,
  SET_COUNT = SET_LAST - SET_FIRST + 1,
};

// Result codes, equal to the protocol NAK error codes.
enum { SETTINGS_OK = 0, SETTINGS_ERR_UNKNOWN_ID = 0x02, SETTINGS_ERR_RANGE = 0x03 };

void settings_init_defaults(void);
bool settings_known(uint8_t id);
uint16_t settings_get(uint8_t id);                 // 0 for an unknown id
uint8_t settings_set(uint8_t id, uint16_t value);  // SETTINGS_OK or an error, nothing changes on error
uint16_t settings_default(uint8_t id);

// Next speed for the KEY button: the first of 20/30/40/50/60 above the current value, after 60 -> 20.
uint16_t settings_next_button_wpm(uint16_t wpm);

// Incremented on every successful change, lets other modules notice changes cheaply.
uint32_t settings_generation(void);
