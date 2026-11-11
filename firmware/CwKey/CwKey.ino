// SPDX-License-Identifier: MIT
//
// CW Key – firmware for WeAct Black Pill STM32F411 / STM32F401
// Spec: docs/specification.md, stage description: docs/stage1-blackpill.md
//
//   PB6  input  dit paddle (jack tip),  to GND when closed, internal pull-up
//   PB7  input  dah paddle (jack ring), to GND when closed, internal pull-up
//   PB8  output sidetone for an ACTIVE BUZZER: HIGH while the tone sounds
//   PA8  output sidetone for a SPEAKER / passive transducer (TIM1_CH1 PWM) via an NPN transistor
//               Both outputs always run together; connect the part you have.
//   PB9  output KEY  – HIGH while an element is sent (drives an NPN transistor)
//   PB5  output PTT  – HIGH from the first element until PTT_HANG_MS after the last one
//   PA0  input  on-board KEY button (active low)
//   PC13 output on-board LED (active low), lit while the keyer sends an element
//
// Button: short press = next speed 20/30/40/50/60 WPM (announced in Morse on
// the sidetone), long press (1 s) = USB mode B <-> C (announced "B" / "C").
// KEY and PTT follow only the local keyer (never USB, never the announcer, H-21).
//
// USB HID boot keyboard, polled every 1 ms (-DHID_FS_BINTERVAL=1, see build.ps1):
//   mode B: stream key down while the keyer sends an element (H-31)
//   mode C: stream key + dit paddle = Left Ctrl, dah paddle = Right Ctrl (H-32)
// Nothing received over USB can drive the keyer or any output (H-21).

#include "usbd_def.h"
#include "usbd_hid_composite_if.h"
#include "types.h"

// The stock Keyboard library waits 20 ms after every report and ignores the
// "endpoint busy" result, so reports go through the USB stack directly.
extern "C" {
extern USBD_HandleTypeDef hUSBD_Device_HID;
uint8_t USBD_HID_KEYBOARD_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len);
}

// ---------------------------------------------------------------- settings

// Firmware version; also used by firmware/package.ps1 for the release file names.
extern "C" __attribute__((used)) const char CWKEY_VERSION[] = "CwKey 0.3.0";

const int DIT_PIN = PB6;
const int DAH_PIN = PB7;
const int BUZZER_PIN = PB8;
const int SPEAKER_PIN = PA8;   // floating input after reset (no pull-up), so the speaker stays off in the bootloader
const int KEY_OUT_PIN = PB9;
const int PTT_OUT_PIN = PB5;
const int BTN_PIN = USER_BTN;  // PA0 on the Black Pill
const int LED_PIN = LED_BUILTIN;  // PC13, active low

const uint8_t SPEEDS_WPM[] = {20, 30, 40, 50, 60};
const uint8_t SPEED_COUNT = sizeof(SPEEDS_WPM) / sizeof(SPEEDS_WPM[0]);

const KeyerMode KEYER_MODE = IAMBIC_B;
const uint32_t DAH_RATIO_X10 = 30;   // dah = 3.0 dits (H-03)

// Transmitter outputs (H-22). PTT goes on with the first element and stays on until
// PTT_HANG_MS after the end of the last element. PTT_LEAD_MS > 0 switches PTT on that
// much before the first element and delays the whole keyer output (sidetone, USB, KEY)
// by the same time, so what you hear is what is transmitted.
const uint32_t PTT_LEAD_MS = 0;
const uint32_t PTT_HANG_MS = 500;

// Sidetone, both outputs driven at the same time (one firmware for both kinds of hardware):
//   PB8 active buzzer (own oscillator, e.g. 32SC4120LF ~4 kHz): HIGH while sounding.
//       (PWM to lower its volume was tested and does not work; use a series resistor.)
//   PA8 passive transducer / small speaker driven by an NPN transistor: square wave of
//       SPEAKER_TONE_HZ, volume = duty cycle, soft edges (H-12).
const uint32_t SPEAKER_TONE_HZ = 700;     // pitch (H-12: 400–1000 Hz)
const uint32_t SPEAKER_VOLUME_PCT = 50;   // duty cycle at full level: 50 = loudest, lower = quieter
const uint32_t SPEAKER_RAMP_US = 4000;    // rise/fall time of the tone, avoids clicks

// Paddle filter (H-04). The contacts are sampled every SAMPLE_US into an
// integrator that counts up while closed and down while open (saturating at
// LEVEL_MAX). Bounce and short spikes cancel out instead of toggling the state.
//   press   = integrator reaches PRESS_LEVEL   (>= 1 ms net closed)
//   release = integrator falls back to 0       (>= 3 ms net open after a press)
//   a new press is not accepted sooner than REPRESS_HOLDOFF_US after a release
//   (release bounce); a press held through the holdoff is taken when it ends.
const uint32_t SAMPLE_US = 100;
const uint8_t PRESS_LEVEL = 10;
const uint8_t LEVEL_MAX = 30;
const uint32_t REPRESS_HOLDOFF_US = 8000;
const uint32_t BTN_DEBOUNCE_MS = 20;
const uint32_t LONG_PRESS_MS = 1000;

// HID usage of the stream key. Provisional until H-31 is verified on a phone.
const uint8_t STREAM_KEY = 0x68;     // F13
const uint8_t DIT_MODIFIER = 0x01;   // Left Ctrl  (H-32)
const uint8_t DAH_MODIFIER = 0x10;   // Right Ctrl (H-32)

// ---------------------------------------------------------------- state

UsbMode usbMode = USB_MODE_B;
uint8_t speedIndex = 0;

inline uint32_t ditUs() { return 1200000UL / SPEEDS_WPM[speedIndex]; }

// Signed difference so comparisons survive the 71-minute micros() wrap.
inline bool reached(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }

// ---------------------------------------------------------------- clock

// Set when the 25 MHz crystal did not start and the clock fell back to HSI.
bool clockOnHsi = false;

// Replaces the core's weak Black Pill clock setup, which hangs in
// Error_Handler() when the HSE crystal fails to start. HSE 25 MHz is still
// preferred (USB needs its accuracy); HSI is only a fallback so the board
// keeps running and can report the problem. USB always gets 48 MHz.
//   STM32F411: SYSCLK 96 MHz (VCO 192 MHz / 2), USB VCO / 4
//   STM32F401: SYSCLK 84 MHz (its maximum; VCO 336 MHz / 4), USB VCO / 7
#if defined(STM32F401xC) || defined(STM32F401xE)
const uint32_t PLL_N = 336, PLL_P = RCC_PLLP_DIV4, PLL_Q = 7;
const uint32_t VOLTAGE_SCALE = PWR_REGULATOR_VOLTAGE_SCALE2;
const uint32_t FLASH_WAIT = FLASH_LATENCY_2;
#else
const uint32_t PLL_N = 192, PLL_P = RCC_PLLP_DIV2, PLL_Q = 4;
const uint32_t VOLTAGE_SCALE = PWR_REGULATOR_VOLTAGE_SCALE1;
const uint32_t FLASH_WAIT = FLASH_LATENCY_3;
#endif

extern "C" void SystemClock_Config(void) {
  RCC_OscInitTypeDef osc = {};
  RCC_ClkInitTypeDef clk = {};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(VOLTAGE_SCALE);

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_ON;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = 25;
  osc.PLL.PLLN = PLL_N;
  osc.PLL.PLLP = PLL_P;
  osc.PLL.PLLQ = PLL_Q;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
    clockOnHsi = true;
    osc = {};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI;
    osc.HSEState = RCC_HSE_OFF;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM = 16;
    osc.PLL.PLLN = PLL_N;
    osc.PLL.PLLP = PLL_P;
    osc.PLL.PLLQ = PLL_Q;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
      Error_Handler();
    }
  }

  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_WAIT) != HAL_OK) {
    Error_Handler();
  }
}

// Startup LED signal: one long flash = crystal OK, three short = HSI fallback.
void signalClockSource() {
  int count = clockOnHsi ? 3 : 1;
  int onMs = clockOnHsi ? 150 : 600;
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(onMs);
    digitalWrite(LED_PIN, HIGH);
    delay(250);
  }
}

// ---------------------------------------------------------------- sidetone

HardwareTimer *soundTimer = nullptr;
uint32_t soundChannel = 0;
uint32_t soundPeriodTicks = 0;   // timer ticks per tone period (speaker)
bool soundOn = false;            // requested state
uint32_t soundEdgeUs = 0;        // when the request last changed
uint32_t soundLevelAtEdge = 0;   // level (0..1000) at that moment
uint32_t soundLevel = 0;         // current level 0..1000 (speaker)
uint32_t soundLastUpdateUs = 0;

void soundInit() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  PinName pin = digitalPinToPinName(SPEAKER_PIN);
  TIM_TypeDef *instance = (TIM_TypeDef *)pinmap_peripheral(pin, PinMap_PWM);
  soundChannel = STM_PIN_CHANNEL(pinmap_function(pin, PinMap_PWM));
  soundTimer = new HardwareTimer(instance);
  // The timer runs continuously at the tone frequency; loudness is the duty cycle (0 = silent).
  soundTimer->setPWM(soundChannel, SPEAKER_PIN, SPEAKER_TONE_HZ, 0);
  soundPeriodTicks = soundTimer->getOverflow(TICK_FORMAT);
}

// Speaker: the level ramps linearly over SPEAKER_RAMP_US. The ramp starts at once
// (next tone period, < 1.5 ms at 700 Hz), so the sidetone latency stays within H-13.
void soundUpdate(uint32_t now) {
  if (now - soundLastUpdateUs < 250 && soundLevel != 0 && soundLevel != 1000) {
    return;  // ~4 kHz update rate is plenty for a 4 ms ramp
  }
  soundLastUpdateUs = now;
  uint32_t elapsed = now - soundEdgeUs;
  uint32_t step = elapsed >= SPEAKER_RAMP_US ? 1000 : elapsed * 1000 / SPEAKER_RAMP_US;
  uint32_t level;
  if (soundOn) {
    level = soundLevelAtEdge + step > 1000 ? 1000 : soundLevelAtEdge + step;
  } else {
    level = soundLevelAtEdge > step ? soundLevelAtEdge - step : 0;
  }
  if (level == soundLevel) {
    return;
  }
  soundLevel = level;
  uint32_t compare = (uint64_t)soundPeriodTicks * SPEAKER_VOLUME_PCT * level / 100000;
  soundTimer->setCaptureCompare(soundChannel, compare, TICK_COMPARE_FORMAT);
}

void soundSet(bool on, uint32_t now) {
  if (on == soundOn) {
    return;
  }
  soundOn = on;
  digitalWrite(BUZZER_PIN, on ? HIGH : LOW);
  soundEdgeUs = now;
  soundLevelAtEdge = soundLevel;
  soundLastUpdateUs = now - 1000;  // update immediately
  soundUpdate(now);
}

// ---------------------------------------------------------------- inputs

Input dit = {DIT_PIN, false, 0, 0, 0};
Input dah = {DAH_PIN, false, 0, 0, 0};
uint32_t lastSampleUs = 0;

void integrate(Input &in, uint32_t now) {
  bool closed = digitalRead(in.pin) == LOW;
  if (closed) {
    if (in.level < LEVEL_MAX) in.level++;
  } else {
    if (in.level > 0) in.level--;
  }
  if (!in.pressed) {
    if (in.level >= PRESS_LEVEL && now - in.lastReleaseUs >= REPRESS_HOLDOFF_US) {
      in.pressed = true;
      in.pressedAtUs = now;
      in.level = LEVEL_MAX;  // hysteresis: releasing now needs 3 ms net open
    }
  } else if (in.level == 0) {
    in.pressed = false;
    in.lastReleaseUs = now;
  }
}

// Samples both paddles at a fixed rate independent of how fast loop() runs.
void updateInputs(uint32_t now) {
  if (now - lastSampleUs > 10 * SAMPLE_US) {
    lastSampleUs = now - SAMPLE_US;  // loop was held up: do not replay old samples
  }
  while (now - lastSampleUs >= SAMPLE_US) {
    lastSampleUs += SAMPLE_US;
    integrate(dit, lastSampleUs);
    integrate(dah, lastSampleUs);
  }
}

// ---------------------------------------------------------------- keyer

struct Keyer {
  KeyerState state = K_IDLE;
  Element last = EL_NONE;
  Element pending = EL_NONE;   // element waiting for the PTT lead time
  uint32_t endUs = 0;
  bool ditMemory = false;
  bool dahMemory = false;
} keyer;

// Transmitter outputs, driven only from the keyer (H-21).
bool pttOn = false;
uint32_t lastKeyUpUs = 0;      // end of the last element, for the PTT hang time

// Iambic alternation: after a dit prefer a dah and vice versa; a single
// paddle (or a single-lever key) repeats its own element.
Element nextElement() {
  bool wantDit = keyer.ditMemory || dit.pressed;
  bool wantDah = keyer.dahMemory || dah.pressed;
  if (keyer.last == EL_DIT) {
    return wantDah ? EL_DAH : (wantDit ? EL_DIT : EL_NONE);
  }
  if (keyer.last == EL_DAH) {
    return wantDit ? EL_DIT : (wantDah ? EL_DAH : EL_NONE);
  }
  if (wantDit && wantDah) {  // both from idle: the paddle closed first starts
    return (int32_t)(dit.pressedAtUs - dah.pressedAtUs) <= 0 ? EL_DIT : EL_DAH;
  }
  return wantDit ? EL_DIT : (wantDah ? EL_DAH : EL_NONE);
}

void startElement(Element e, uint32_t startUs) {
  keyer.state = K_ELEMENT;
  keyer.last = e;
  keyer.endUs = startUs + (e == EL_DIT ? ditUs() : ditUs() * DAH_RATIO_X10 / 10);
  if (e == EL_DIT) {
    keyer.ditMemory = false;
  } else {
    keyer.dahMemory = false;
  }
}

void updateKeyer(uint32_t now) {
  switch (keyer.state) {
    case K_IDLE: {
      Element e = nextElement();
      if (e == EL_NONE) {
        break;
      }
      if (!pttOn && PTT_LEAD_MS > 0) {
        // PTT first, the element follows after the lead time.
        pttOn = true;
        keyer.pending = e;
        keyer.state = K_LEAD;
        keyer.endUs = now + PTT_LEAD_MS * 1000;
        break;
      }
      pttOn = true;
      startElement(e, now);
      break;
    }
    case K_LEAD:
      if (reached(now, keyer.endUs)) {
        startElement(keyer.pending, keyer.endUs);
      }
      break;
    case K_ELEMENT:
      // Opposite paddle closed during an element is remembered (squeeze / memory).
      if (keyer.last == EL_DIT && dah.pressed) keyer.dahMemory = true;
      if (keyer.last == EL_DAH && dit.pressed) keyer.ditMemory = true;
      if (reached(now, keyer.endUs)) {
        lastKeyUpUs = keyer.endUs;
        keyer.state = K_GAP;
        keyer.endUs += ditUs();  // scheduled from the deadline, so timing does not drift
      }
      break;
    case K_GAP:
      // The paddles are NOT read during the gap: a paddle that is released
      // while the gap runs must not add another element. The next element is
      // decided only at the end of the gap, from the memory latched during the
      // element (opposite paddle) and from the paddles still held at that moment.
      if (reached(now, keyer.endUs)) {
        // Mode A: releasing both paddles cancels the remembered element.
        if (KEYER_MODE == IAMBIC_A && !dit.pressed && !dah.pressed) {
          keyer.ditMemory = keyer.dahMemory = false;
        }
        Element e = nextElement();
        if (e != EL_NONE) {
          startElement(e, keyer.endUs);
        } else {
          keyer.state = K_IDLE;
          keyer.last = EL_NONE;
        }
      }
      break;
  }
}

inline bool keyerKeyDown() { return keyer.state == K_ELEMENT; }

// PTT drops PTT_HANG_MS after the last element once the keyer is idle.
void updateTxOutputs(uint32_t now) {
  if (pttOn && keyer.state == K_IDLE && reached(now, lastKeyUpUs + PTT_HANG_MS * 1000)) {
    pttOn = false;
  }
  digitalWrite(KEY_OUT_PIN, keyerKeyDown() ? HIGH : LOW);
  digitalWrite(PTT_OUT_PIN, pttOn ? HIGH : LOW);
}

// ---------------------------------------------------------------- announcer
// Plays short Morse messages (speed, USB mode) on the piezo only. It never
// reaches the USB stream. Paddles take priority and cancel it.

char annPattern[32];
uint8_t annLength = 0;
uint8_t annPos = 0;
bool annActive = false;
bool annToneOn = false;
uint32_t annEndUs = 0;

const char *morseFor(char c) {
  switch (c) {
    case '0': return "-----";
    case '1': return ".----";
    case '2': return "..---";
    case '3': return "...--";
    case '4': return "....-";
    case '5': return ".....";
    case '6': return "-....";
    case '7': return "--...";
    case '8': return "---..";
    case '9': return "----.";
    case 'B': return "-...";
    case 'C': return "-.-.";
    default:  return "";
  }
}

void announce(const char *text) {
  annLength = 0;
  for (const char *t = text; *t; t++) {
    for (const char *m = morseFor(*t); *m && annLength < sizeof(annPattern) - 1; m++) {
      annPattern[annLength++] = *m;
    }
    if (annLength < sizeof(annPattern) - 1) {
      annPattern[annLength++] = ' ';  // letter gap
    }
  }
  annPos = 0;
  annToneOn = false;
  annEndUs = micros();
  annActive = true;
}

void updateAnnouncer(uint32_t now) {
  if (!annActive) {
    return;
  }
  if (keyer.state != K_IDLE) {
    annActive = false;
    annToneOn = false;
    return;
  }
  if (!reached(now, annEndUs)) {
    return;
  }
  uint32_t unit = ditUs();
  if (annToneOn) {  // gap after every element
    annToneOn = false;
    annEndUs = now + unit;
    return;
  }
  if (annPos >= annLength) {
    annActive = false;
    return;
  }
  char c = annPattern[annPos++];
  if (c == '.') {
    annToneOn = true;
    annEndUs = now + unit;
  } else if (c == '-') {
    annToneOn = true;
    annEndUs = now + 3 * unit;
  } else {
    annEndUs = now + 2 * unit;  // letter gap = 3 units, 1 already elapsed
  }
}

// ---------------------------------------------------------------- button

struct Button {
  bool pressed;
  uint32_t lastChangeMs;
  uint32_t pressedAtMs;
  bool longHandled;
} btn = {false, 0, 0, false};

void onShortPress() {
  speedIndex = (speedIndex + 1) % SPEED_COUNT;
  char text[4];
  snprintf(text, sizeof(text), "%u", SPEEDS_WPM[speedIndex]);
  announce(text);
}

void onLongPress() {
  usbMode = (usbMode == USB_MODE_B) ? USB_MODE_C : USB_MODE_B;
  announce(usbMode == USB_MODE_B ? "B" : "C");
}

void updateButton(uint32_t nowMs) {
  bool raw = digitalRead(BTN_PIN) == LOW;
  if (raw != btn.pressed && nowMs - btn.lastChangeMs >= BTN_DEBOUNCE_MS) {
    btn.pressed = raw;
    btn.lastChangeMs = nowMs;
    if (raw) {
      btn.pressedAtMs = nowMs;
      btn.longHandled = false;
    } else if (!btn.longHandled) {
      onShortPress();
    }
  }
  // Long press acts while still held, so the user hears it and can release.
  if (btn.pressed && !btn.longHandled && nowMs - btn.pressedAtMs >= LONG_PRESS_MS) {
    btn.longHandled = true;
    onLongPress();
  }
}

// ---------------------------------------------------------------- USB HID

// Every state change is queued and sent in its own 1 ms USB frame, so short
// events are never merged or lost (H-33, H-42).
const uint8_t FIFO_SIZE = 16;
HidState fifo[FIFO_SIZE];
uint8_t fifoHead = 0;
uint8_t fifoCount = 0;
HidState lastQueued = {0, 0};
bool haveQueued = false;

// The stack transmits from the caller's buffer after SendReport returns, so
// two buffers alternate and the one in flight is never overwritten.
static uint8_t txBuffers[2][8];
uint8_t txNext = 0;

inline bool usbReady() { return hUSBD_Device_HID.dev_state == USBD_STATE_CONFIGURED; }

void queueState(const HidState &s) {
  if (!usbReady()) {
    fifoCount = 0;
    haveQueued = false;  // resend the current state once the host configures us
    return;
  }
  if (haveQueued && s == lastQueued) {
    return;
  }
  if (fifoCount == FIFO_SIZE) {
    fifo[(fifoHead + fifoCount - 1) % FIFO_SIZE] = s;  // overflow: keep the newest state
  } else {
    fifo[(fifoHead + fifoCount) % FIFO_SIZE] = s;
    fifoCount++;
  }
  lastQueued = s;
  haveQueued = true;
}

void pumpUsb() {
  if (fifoCount == 0 || !usbReady()) {
    return;
  }
  uint8_t *buf = txBuffers[txNext];
  memset(buf, 0, 8);
  buf[0] = fifo[fifoHead].modifiers;
  buf[2] = fifo[fifoHead].key;
  if (USBD_HID_KEYBOARD_SendReport(&hUSBD_Device_HID, buf, 8) == USBD_OK) {
    fifoHead = (fifoHead + 1) % FIFO_SIZE;
    fifoCount--;
    txNext ^= 1;
  }
  // USBD_BUSY: the previous report is still waiting for the host; retry next pass.
}

HidState currentHidState() {
  HidState s = {0, 0};
  if (keyerKeyDown()) {
    s.key = STREAM_KEY;
  }
  if (usbMode == USB_MODE_C) {
    s.modifiers = (dit.pressed ? DIT_MODIFIER : 0) | (dah.pressed ? DAH_MODIFIER : 0);
  }
  return s;
}

// ---------------------------------------------------------------- main

void setup() {
  asm volatile("" : : "r"(CWKEY_VERSION));  // keep the version string in the image
  // Transmitter outputs off first (the transistor bases also need pull-down resistors,
  // because the pins float during reset and in the bootloader).
  digitalWrite(KEY_OUT_PIN, LOW);
  digitalWrite(PTT_OUT_PIN, LOW);
  pinMode(KEY_OUT_PIN, OUTPUT);
  pinMode(PTT_OUT_PIN, OUTPUT);
  pinMode(DIT_PIN, INPUT_PULLUP);
  pinMode(DAH_PIN, INPUT_PULLUP);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  soundInit();
  signalClockSource();
  lastSampleUs = micros();
  dit.lastReleaseUs = dah.lastReleaseUs = lastSampleUs - REPRESS_HOLDOFF_US;
  HID_Composite_Init(HID_KEYBOARD);
}

void loop() {
  uint32_t now = micros();
  updateInputs(now);
  updateButton(millis());
  updateKeyer(now);
  updateAnnouncer(now);

  updateTxOutputs(now);
  soundSet(keyerKeyDown() || annToneOn, now);
  soundUpdate(now);
  digitalWrite(LED_PIN, keyerKeyDown() ? LOW : HIGH);

  queueState(currentHidState());
  pumpUsb();
}
