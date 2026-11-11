// SPDX-License-Identifier: MIT
#include "settings.h"

typedef struct {
  uint16_t min, max, def;
} SettingDef;

static const SettingDef DEFS[SET_COUNT] = {
  [SET_WPM - SET_FIRST]            = {5, 60, 20},
  [SET_KEYER_MODE - SET_FIRST]     = {0, 1, 1},
  [SET_DAH_RATIO_X10 - SET_FIRST]  = {28, 35, 30},
  [SET_WEIGHT_PCT - SET_FIRST]     = {25, 75, 50},
  [SET_ELEMENT_MEMORY - SET_FIRST] = {0, 1, 1},
  [SET_PADDLE_SWAP - SET_FIRST]    = {0, 1, 0},
  [SET_USB_MODE - SET_FIRST]       = {0, 1, 0},
  [SET_SIDETONE_ON - SET_FIRST]    = {0, 1, 1},
  [SET_TONE_HZ - SET_FIRST]        = {300, 1200, 700},
  [SET_VOLUME_PCT - SET_FIRST]     = {0, 100, 100},
  [SET_PTT_ENABLED - SET_FIRST]    = {0, 1, 1},
  [SET_PTT_LEAD_MS - SET_FIRST]    = {0, 100, 0},
  [SET_PTT_HANG_MS - SET_FIRST]    = {50, 3000, 500},
  [SET_STREAM_KEY - SET_FIRST]     = {0x04, 0x73, 0x68},   // HID usage, default F13 (H-31)
};

static uint16_t values[SET_COUNT];
static uint32_t generation;

bool settings_known(uint8_t id) { return id >= SET_FIRST && id <= SET_LAST; }

void settings_init_defaults(void) {
  for (int i = 0; i < SET_COUNT; i++) values[i] = DEFS[i].def;
  generation++;
}

uint16_t settings_default(uint8_t id) { return settings_known(id) ? DEFS[id - SET_FIRST].def : 0; }

uint16_t settings_get(uint8_t id) { return settings_known(id) ? values[id - SET_FIRST] : 0; }

uint8_t settings_set(uint8_t id, uint16_t value) {
  if (!settings_known(id)) return SETTINGS_ERR_UNKNOWN_ID;
  const SettingDef *d = &DEFS[id - SET_FIRST];
  if (value < d->min || value > d->max) return SETTINGS_ERR_RANGE;
  if (values[id - SET_FIRST] != value) {
    values[id - SET_FIRST] = value;
    generation++;
  }
  return SETTINGS_OK;
}

uint16_t settings_next_button_wpm(uint16_t wpm) {
  static const uint16_t steps[] = {20, 30, 40, 50, 60};
  for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; i++) {
    if (steps[i] > wpm) return steps[i];
  }
  return steps[0];
}

uint32_t settings_generation(void) { return generation; }
