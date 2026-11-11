// SPDX-License-Identifier: MIT
// Types shared by CwKey.ino. Kept in a header because the Arduino builder
// inserts function prototypes above the first function of the sketch.
#pragma once
#include <stdint.h>

enum KeyerMode { IAMBIC_A, IAMBIC_B };
enum UsbMode { USB_MODE_B, USB_MODE_C };
enum Element : uint8_t { EL_NONE, EL_DIT, EL_DAH };
enum KeyerState : uint8_t { K_IDLE, K_LEAD, K_ELEMENT, K_GAP };

struct Input {
  int pin;
  bool pressed;           // filtered state
  uint8_t level;          // integrator: +1 per closed sample, -1 per open sample
  uint32_t lastReleaseUs; // when the filtered state last went to released
  uint32_t pressedAtUs;   // when the filtered state last went to pressed
};

struct HidState {
  uint8_t modifiers;
  uint8_t key;
  bool operator==(const HidState &o) const { return modifiers == o.modifiers && key == o.key; }
};
