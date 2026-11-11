// SPDX-License-Identifier: MIT
// Start-up diagnostics (build with -DCWKEY_DIAG=ON).
// Every checkpoint appends a (tag, value) word pair to a log in unused flash at DIAG_LOG_ADDR. The log survives
// resets and can be read back from the bootloader drive (CURRENT.UF2) after a double-tap of NRST.
// The flash area must be erased beforehand: the diagnostic UF2 contains an 0xFF block for it.
// Uses only registers, so it works before the clock, SysTick or interrupts are set up.
#pragma once
#ifdef CWKEY_DIAG
#include <stdbool.h>
#include "stm32f4xx.h"

#define DIAG_LOG_ADDR 0x08070000u
#define DIAG_LOG_WORDS 1024u

#define DIAG_TAG_BOOT 0xB0070000u      // value: RCC->CSR (reset cause)
#define DIAG_TAG_STAGE 0x57A60000u     // | stage; value: SystemCoreClock
#define DIAG_TAG_INFO 0x1AF00000u      // | id;    value: anything
#define DIAG_TAG_FAULT 0xFA170000u     // | id;    value: PC, LR, CFSR, HFSR

static inline void diag_hw_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
  (void)RCC->AHB1ENR;
  GPIOC->MODER = (GPIOC->MODER & ~(3u << 26)) | (1u << 26);   // PC13 output
}

static inline void diag_led(bool on) { GPIOC->BSRR = on ? (1u << (13 + 16)) : (1u << 13); }  // active low

void diag_log(uint32_t tag, uint32_t value);   // board.c (one shared write position)

static inline void diag_stage(int n) {
  diag_hw_init();
  diag_led((n & 1) != 0);   // the LED toggles with each stage
  if (n == 1) diag_log(DIAG_TAG_BOOT, RCC->CSR);
  diag_log(DIAG_TAG_STAGE | (uint32_t)n, SystemCoreClock);
}
#define DIAG(n) diag_stage(n)
#define DIAG_INFO(id, v) diag_log(DIAG_TAG_INFO | (uint32_t)(id), (uint32_t)(v))
#else
#define DIAG(n) ((void)0)
#define DIAG_INFO(id, v) ((void)0)
#endif
