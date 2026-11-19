// SPDX-License-Identifier: MIT
// WeAct Black Pill (STM32F411CE / STM32F401Cx) hardware layer.
//
//   PB6  in   dit paddle (to GND)          PB9  out  KEY  (NPN, active high)
//   PB7  in   dah paddle (to GND)          PB5  out  PTT  (NPN, active high)
//   PB8  out  active buzzer (high = on)    PA0  in   KEY button (active low)
//   PA8  out  speaker sine PWM (TIM1_CH1)     PC13 out  LED (active low)
//   PA11/PA12 USB OTG FS
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { CHIP_F411 = 0, CHIP_F401 = 1 };

void board_init(void);                 // clocks, SysTick, GPIO, timers, USB pins
bool board_clock_on_hsi(void);         // true when the 25 MHz crystal did not start
uint8_t board_chip(void);              // CHIP_F411 / CHIP_F401
uint32_t board_millis(void);
uint32_t board_micros(void);           // 1 MHz free-running 32-bit counter (TIM2)
void board_delay_ms(uint32_t ms);

bool board_dit_closed(void);           // raw paddle contacts (true = closed)
bool board_dah_closed(void);
bool board_button_pressed(void);       // raw KEY button

void board_led(bool on);
void board_key_out(bool on);
void board_ptt_out(bool on);
void board_buzzer(bool on);

void board_speaker_set_tone(uint32_t hz);      // sine frequency
void board_speaker_set_volume(uint32_t pct);   // 0..100 % amplitude, up to 300 % soft-clipped (louder)
void board_speaker(bool on);                   // start/stop with a soft rise/fall

void board_reboot_to_bootloader(void) __attribute__((noreturn));
