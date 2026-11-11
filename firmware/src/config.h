// SPDX-License-Identifier: MIT
// CW Key firmware – build-wide constants.
#pragma once

#define CWKEY_VERSION_MAJOR 0
#define CWKEY_VERSION_MINOR 4
#define CWKEY_VERSION_PATCH 1
#define CWKEY_VERSION_STRING "CwKey 0.4.1"

// Application start behind the TinyUF2 bootloader (sectors 0–3).
#define APP_START_ADDR 0x08010000u

// Settings storage: flash sector 5, present on F401CC/CD/CE and F411CE (docs/midi-settings-spec.md, section 6).
#define SETTINGS_FLASH_ADDR   0x08020000u
#define SETTINGS_FLASH_SIZE   0x20000u
#define SETTINGS_FLASH_SECTOR 5u

// TinyUF2 "stay in the bootloader" request (RAM word read by the bootloader at start-up).
#define BOOTLOADER_MAGIC_ADDR 0x2000FFFCu
#define BOOTLOADER_MAGIC      0xF01669EFu

// Paddle filter (H-04), see input.c.
#define PADDLE_SAMPLE_US        100u
#define PADDLE_PRESS_LEVEL      10u    // >= 1 ms net closed
#define PADDLE_LEVEL_MAX        30u    // >= 3 ms net open to release
#define PADDLE_REPRESS_HOLD_US  8000u  // release bounce

#define BUTTON_DEBOUNCE_MS 20u
#define BUTTON_LONG_MS     1000u

#define SPEAKER_RAMP_US    4000u       // sidetone rise/fall on the speaker output (H-12)
#define AUTOSAVE_DELAY_MS  5000u
