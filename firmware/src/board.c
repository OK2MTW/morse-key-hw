// SPDX-License-Identifier: MIT
#include "board.h"
#include "config.h"
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
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
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
    if (HAL_RCC_OscConfig(&hsi) != HAL_OK) {
      for (;;) {}  // no clock at all: nothing sensible left to do
    }
  }
  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  HAL_RCC_ClockConfig(&clk, latency);  // also re-programs SysTick for 1 ms
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

// TIM1 channel 1: speaker PWM, 1 MHz tick, period = tone, duty = loudness.
static void speaker_init(void) {
  __HAL_RCC_TIM1_CLK_ENABLE();
  uint32_t timclk = HAL_RCC_GetPCLK2Freq();      // APB2 prescaler 1
  TIM1->CR1 = 0;
  TIM1->PSC = timclk / 1000000u - 1;
  TIM1->ARR = 1000000u / 700u - 1;
  TIM1->CCR1 = 0;
  TIM1->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;  // PWM mode 1, preload
  TIM1->CCER = TIM_CCER_CC1E;
  TIM1->BDTR = TIM_BDTR_MOE;                     // main output enable (advanced timer)
  TIM1->EGR = TIM_EGR_UG;
  TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
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
  clock_init();
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
  TIM1->ARR = 1000000u / hz - 1;                 // preloaded: takes effect at the next period
}
uint32_t board_speaker_period(void) { return TIM1->ARR + 1; }
void board_speaker_set_compare(uint32_t ticks) { TIM1->CCR1 = ticks; }

void board_reboot_to_bootloader(void) {
  board_key_out(false);
  board_ptt_out(false);
  board_buzzer(false);
  board_speaker_set_compare(0);
  __disable_irq();
  *(volatile uint32_t *)BOOTLOADER_MAGIC_ADDR = BOOTLOADER_MAGIC;
  __DSB();
  NVIC_SystemReset();
  for (;;) {}
}

// ---------------------------------------------------------------- interrupt handlers

void SysTick_Handler(void) { HAL_IncTick(); }
void OTG_FS_IRQHandler(void) { tud_int_handler(0); }
