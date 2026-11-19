// SPDX-License-Identifier: MIT
#include <math.h>
#include "board.h"
#include "config.h"
#include "diag.h"
#include "stm32f4xx_hal.h"
#include "tusb.h"

static bool clockOnHsi = false;

// HSE 25 MHz preferred (USB needs its accuracy), HSI 16 MHz only as a fallback.
// USB always gets 48 MHz:  F411 96 MHz (VCO 192 / 2, USB /4);  F401 84 MHz (VCO 336 / 4, USB /7).
static void clock_init(void) {
#if defined(STM32F401xE) || defined(STM32F401xC)
  const uint32_t n = 336, p = RCC_PLLP_DIV4, q = 7, scale = PWR_REGULATOR_VOLTAGE_SCALE2, latency = FLASH_LATENCY_2;
#else
  const uint32_t n = 192, p = RCC_PLLP_DIV2, q = 4, scale = PWR_REGULATOR_VOLTAGE_SCALE1, latency = FLASH_LATENCY_3;
#endif
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

#ifdef CWKEY_DIAG
  DIAG_INFO(0x01, RCC->CR);
  DIAG_INFO(0x02, RCC->CFGR);
  DIAG_INFO(0x03, RCC->PLLCFGR);
  DIAG_INFO(0x04, __get_PRIMASK());
  DIAG_INFO(0x05, SysTick->CTRL);
  DIAG_INFO(0x06, SCB->VTOR);
  {  // is the HAL tick running?
    uint32_t t0 = HAL_GetTick();
    for (volatile uint32_t i = 0; i < 200000u; i++) {}
    DIAG_INFO(0x07, HAL_GetTick() - t0);
  }
#endif
  // TinyUF2 may start the application with the system clock still on its own PLL (seen: HSE PLL 84 MHz).
  // HAL refuses to reconfigure a PLL that drives SYSCLK, so go back to HSI and stop the PLL first.
  RCC->CR |= RCC_CR_HSION;
  while (!(RCC->CR & RCC_CR_HSIRDY)) {}
  RCC->CFGR &= ~RCC_CFGR_SW;                                  // SYSCLK = HSI
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {}
  RCC->CFGR = 0;                                              // all prescalers /1
  RCC->CR &= ~RCC_CR_PLLON;
  while (RCC->CR & RCC_CR_PLLRDY) {}
  SystemCoreClock = HSI_VALUE;
  HAL_InitTick(TICK_INT_PRIORITY);                            // SysTick for 16 MHz again
  DIAG_INFO(0x08, RCC->CFGR);

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(scale);

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_ON;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = 25;
  osc.PLL.PLLN = n;
  osc.PLL.PLLP = p;
  osc.PLL.PLLQ = q;
  HAL_StatusTypeDef st = HAL_RCC_OscConfig(&osc);
  DIAG_INFO(0x10, st);       // HSE + PLL
  DIAG_INFO(0x11, RCC->CR);
  if (st != HAL_OK) {
    clockOnHsi = true;
    RCC_OscInitTypeDef hsi = {0};
    hsi.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI;
    hsi.HSEState = RCC_HSE_OFF;
    hsi.HSIState = RCC_HSI_ON;
    hsi.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    hsi.PLL.PLLState = RCC_PLL_ON;
    hsi.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    hsi.PLL.PLLM = 16;
    hsi.PLL.PLLN = n;
    hsi.PLL.PLLP = p;
    hsi.PLL.PLLQ = q;
    st = HAL_RCC_OscConfig(&hsi);
    DIAG_INFO(0x20, st);       // HSI fallback
    DIAG_INFO(0x21, RCC->CR);
    if (st != HAL_OK) {
      for (;;) {}  // no clock at all: nothing sensible left to do
    }
  }
  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  st = HAL_RCC_ClockConfig(&clk, latency);  // also re-programs SysTick for 1 ms
  DIAG_INFO(0x30, st);
  DIAG_INFO(0x31, RCC->CFGR);
  (void)st;
}

static void gpio_out(GPIO_TypeDef *port, uint32_t pin, GPIO_PinState initial) {
  HAL_GPIO_WritePin(port, pin, initial);
  GPIO_InitTypeDef g = {0};
  g.Pin = pin;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(port, &g);
}

static void gpio_in_pullup(GPIO_TypeDef *port, uint32_t pin) {
  GPIO_InitTypeDef g = {0};
  g.Pin = pin;
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(port, &g);
}

static void gpio_init(void) {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  // Transmitter outputs first, off (the transistor bases also need pull-down resistors).
  gpio_out(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);   // KEY
  gpio_out(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);   // PTT
  gpio_out(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);   // buzzer
  gpio_out(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);    // LED off (active low)

  gpio_in_pullup(GPIOB, GPIO_PIN_6);             // dit
  gpio_in_pullup(GPIOB, GPIO_PIN_7);             // dah
  gpio_in_pullup(GPIOA, GPIO_PIN_0);             // KEY button

  // Speaker: PA8 = TIM1_CH1 (AF1).
  GPIO_InitTypeDef g = {0};
  g.Pin = GPIO_PIN_8;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Alternate = GPIO_AF1_TIM1;
  HAL_GPIO_Init(GPIOA, &g);
}

// TIM2: 32-bit free-running microsecond counter.
static void micros_init(void) {
  __HAL_RCC_TIM2_CLK_ENABLE();
  uint32_t timclk = HAL_RCC_GetPCLK1Freq() * 2;  // APB1 prescaler is 2 -> timers run at 2x PCLK1
  TIM2->CR1 = 0;
  TIM2->PSC = timclk / 1000000u - 1;
  TIM2->ARR = 0xFFFFFFFFu;
  TIM2->EGR = TIM_EGR_UG;
  TIM2->CR1 = TIM_CR1_CEN;
}

// ---------------------------------------------------------------- speaker: sine tone by PWM (PA8, TIM1_CH1)
// Carrier SPEAKER_RATE_HZ (inaudible, filtered by the speaker itself); every carrier period the TIM1 update interrupt
// writes the next sample: duty = envelope * (1 + wave) / 2. Silent = duty 0 (no DC through the speaker).
// wave = volume * sin up to 100 %; above (up to 300 %) the sine is soft-clipped, towards a square wave with round
// edges at 300 % (the loudness of the former square-wave firmware): louder, peak current unchanged.
// The envelope is a raised cosine of SPEAKER_RAMP_US for rise and fall: no key clicks.

#define SINE_BITS 8
#define SINE_SIZE (1u << SINE_BITS)
#define RAMP_SAMPLES (SPEAKER_RAMP_US * (SPEAKER_RATE_HZ / 1000u) / 1000u)

static int16_t waveTable[SINE_SIZE];                 // Q15, volume included
static uint32_t waveVolume = 0xFFFFFFFFu;
static uint16_t rampTable[RAMP_SAMPLES + 1];         // Q15, 0 .. 32768
static uint32_t pwmSteps;                            // ARR + 1
static volatile uint32_t phase, phaseStep;
static volatile bool speakerOn;
static uint32_t rampPos;

static void speaker_init(void) {
  board_speaker_set_volume(100);
  for (uint32_t i = 0; i <= RAMP_SAMPLES; i++) {
    float s = sinf(1.5707963f * (float)i / RAMP_SAMPLES);
    rampTable[i] = (uint16_t)lrintf(32768.0f * s * s);
  }
  __HAL_RCC_TIM1_CLK_ENABLE();
  uint32_t timclk = HAL_RCC_GetPCLK2Freq();      // APB2 prescaler 1
  pwmSteps = timclk / SPEAKER_RATE_HZ;           // F411 96 MHz: 2000, F401 84 MHz: 1750
  TIM1->CR1 = 0;
  TIM1->PSC = 0;
  TIM1->ARR = pwmSteps - 1;
  TIM1->CCR1 = 0;
  TIM1->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;  // PWM mode 1, preload
  TIM1->CCER = TIM_CCER_CC1E;
  TIM1->BDTR = TIM_BDTR_MOE;                     // main output enable (advanced timer)
  TIM1->EGR = TIM_EGR_UG;
  TIM1->SR = 0;
  TIM1->DIER = TIM_DIER_UIE;
  NVIC_SetPriority(TIM1_UP_TIM10_IRQn, 1);       // above USB: an even sample rate
  NVIC_EnableIRQ(TIM1_UP_TIM10_IRQn);
  TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

void TIM1_UP_TIM10_IRQHandler(void) {
  TIM1->SR = ~TIM_SR_UIF;
  if (speakerOn) {
    if (rampPos < RAMP_SAMPLES) rampPos++;
  } else if (rampPos > 0) {
    rampPos--;
  } else {
    TIM1->CCR1 = 0;
    phase = 0;                                   // next tone starts at zero phase
    return;
  }
  phase += phaseStep;
  uint32_t unipolar = (uint32_t)(32768 + waveTable[phase >> (32 - SINE_BITS)]);  // Q16, 1 .. 65535
  TIM1->CCR1 = (((uint32_t)rampTable[rampPos] * unipolar) >> 15) * pwmSteps >> 16;
}

static void usb_init(void) {
  GPIO_InitTypeDef g = {0};
  g.Pin = GPIO_PIN_11 | GPIO_PIN_12;             // DM, DP
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF10_OTG_FS;
  HAL_GPIO_Init(GPIOA, &g);

  g.Pin = GPIO_PIN_10;                           // ID
  g.Mode = GPIO_MODE_AF_OD;
  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &g);

  __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
  USB_OTG_FS->GCCFG |= USB_OTG_GCCFG_NOVBUSSENS;  // the Black Pill does not route VBUS to PA9
  NVIC_SetPriority(OTG_FS_IRQn, 2);
}

void board_init(void) {
  // TinyUF2 jumps here with VTOR already set; set it again for a bare ST-Link start.
  SCB->VTOR = APP_START_ADDR;
  HAL_Init();
  DIAG(2);
  clock_init();
  DIAG(3);
  gpio_init();
  micros_init();
  speaker_init();
  usb_init();
}

bool board_clock_on_hsi(void) { return clockOnHsi; }

// Chosen at build time (one image per chip). DBGMCU_IDCODE is avoided: on some STM32F4 revisions it
// reads as 0 without a debugger attached.
uint8_t board_chip(void) {
#if defined(STM32F401xE) || defined(STM32F401xC)
  return CHIP_F401;
#else
  return CHIP_F411;
#endif
}

uint32_t board_millis(void) { return HAL_GetTick(); }
uint32_t board_micros(void) { return TIM2->CNT; }
void board_delay_ms(uint32_t ms) { HAL_Delay(ms); }

bool board_dit_closed(void) { return (GPIOB->IDR & GPIO_PIN_6) == 0; }
bool board_dah_closed(void) { return (GPIOB->IDR & GPIO_PIN_7) == 0; }
bool board_button_pressed(void) { return (GPIOA->IDR & GPIO_PIN_0) == 0; }

static inline void pin_write(GPIO_TypeDef *port, uint32_t pin, bool high) {
  port->BSRR = high ? pin : (pin << 16);
}
void board_led(bool on) { pin_write(GPIOC, GPIO_PIN_13, !on); }
void board_key_out(bool on) { pin_write(GPIOB, GPIO_PIN_9, on); }
void board_ptt_out(bool on) { pin_write(GPIOB, GPIO_PIN_5, on); }
void board_buzzer(bool on) { pin_write(GPIOB, GPIO_PIN_8, on); }

void board_speaker_set_tone(uint32_t hz) {
  if (hz < 100) hz = 100;
  phaseStep = (uint32_t)(((uint64_t)hz << 32) / SPEAKER_RATE_HZ);
}
void board_speaker_set_volume(uint32_t pct) {
  if (pct > 300) pct = 300;
  if (pct == waveVolume) return;
  waveVolume = pct;
  const float knee = 0.7f;                       // soft clipping starts at 70 % of full swing
  float g = (float)pct / 100.0f;
  for (uint32_t i = 0; i < SINE_SIZE; i++) {
    float x = g * sinf(6.2831853f * (float)i / SINE_SIZE);
    float a = fabsf(x);
    if (pct > 100 && a > knee) a = knee + (1.0f - knee) * tanhf((a - knee) / (1.0f - knee));
    if (a > 1.0f) a = 1.0f;
    waveTable[i] = (int16_t)lrintf(copysignf(a, x) * 32767.0f);
  }
}
void board_speaker(bool on) { speakerOn = on; }

void board_reboot_to_bootloader(void) {
  board_key_out(false);
  board_ptt_out(false);
  board_buzzer(false);
  NVIC_DisableIRQ(TIM1_UP_TIM10_IRQn);
  TIM1->CCR1 = 0;
  __disable_irq();
  *(volatile uint32_t *)BOOTLOADER_MAGIC_ADDR = BOOTLOADER_MAGIC;
  __DSB();
  NVIC_SystemReset();
  for (;;) {}
}

// ---------------------------------------------------------------- interrupt handlers

void SysTick_Handler(void) { HAL_IncTick(); }

#ifdef CWKEY_DIAG
static inline void diag_flash_word(uint32_t addr, uint32_t v) {
  if (FLASH->CR & FLASH_CR_LOCK) {
    FLASH->KEYR = 0x45670123u;
    FLASH->KEYR = 0xCDEF89ABu;
  }
  while (FLASH->SR & FLASH_SR_BSY) {}
  FLASH->SR = FLASH_SR_EOP | FLASH_SR_SOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;
  FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_CR_PSIZE_1 | FLASH_CR_PG;   // 32-bit programming
  *(volatile uint32_t *)addr = v;
  __DSB();
  while (FLASH->SR & FLASH_SR_BSY) {}
  FLASH->CR &= ~FLASH_CR_PG;
  FLASH->CR |= FLASH_CR_LOCK;
}

// Appends one diagnostic log entry (diag.h). The free position is found once per boot (before the data cache can hold stale words).
void diag_log(uint32_t tag, uint32_t value) {
  static int32_t next = -1;
  volatile const uint32_t *log = (volatile const uint32_t *)DIAG_LOG_ADDR;
  if (next < 0) {
    next = 0;
    while ((uint32_t)next < DIAG_LOG_WORDS && log[next] != 0xFFFFFFFFu) next += 2;
  }
  if ((uint32_t)next + 2 > DIAG_LOG_WORDS) return;   // full
  diag_flash_word(DIAG_LOG_ADDR + 4u * (uint32_t)next, tag);
  diag_flash_word(DIAG_LOG_ADDR + 4u * (uint32_t)next + 4u, value);
  next += 2;
}

#endif

#ifdef CWKEY_DIAG
// Any CPU fault: log the faulting PC/LR and the fault status, then flicker the LED.
void diag_fault(uint32_t *frame) {
  diag_log(DIAG_TAG_FAULT | 1, frame[6]);   // PC
  diag_log(DIAG_TAG_FAULT | 2, frame[5]);   // LR
  diag_log(DIAG_TAG_FAULT | 3, SCB->CFSR);
  diag_log(DIAG_TAG_FAULT | 4, SCB->HFSR);
  diag_log(DIAG_TAG_FAULT | 5, SCB->BFAR);
  diag_hw_init();
  for (;;) {
    diag_led(true);
    for (volatile uint32_t i = 0; i < 100000u; i++) {}
    diag_led(false);
    for (volatile uint32_t i = 0; i < 100000u; i++) {}
  }
}
__attribute__((naked)) void HardFault_Handler(void) {
  __asm volatile(
      "tst lr, #4      \n"
      "ite eq          \n"
      "mrseq r0, msp   \n"
      "mrsne r0, psp   \n"
      "b diag_fault    \n");
}
#endif
void OTG_FS_IRQHandler(void) { tud_int_handler(0); }
