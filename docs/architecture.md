# Hardware and firmware architecture

Status: **draft** (2026-11-08), written for the RP2040.
> **2026-11-09: the board is the Black Pill STM32F411** (the author's decision). The HW blocks and the firmware logic
> still apply. Parts tied to the RP2040 (second core, pico-sdk, pins in section 1.4) do not apply. Pins and the actual
> stage 1 firmware: [stage1-blackpill.md](stage1-blackpill.md). Other outputs are still open.

## 1. Hardware

### 1.1 Block diagram

```
                        +5 V USB (PC / charger / power bank, H-35)
                          |
                          +--[ferrite + LC]--> +5 V_A (analog) --> virtual ground 2.5 V
                          |
 PADDLES (TRS jack, H-01) |         +--------------------------------+
  tip   (dit)   --[1k]--+-|-------->| GP2  DIT                        |
  ring  (dah)   --[1k]--+-|-------->| GP3  DAH                        |    USB FS
  sleeve = GND          | |         |                                |<=========> PC / phone
  10k pull-up to 3V3,   | |         |        RP2040                  |  HID keyboard (+ MIDI, E5)
  TVS/diodes to 3V3/GND | |         |  core 0: inputs, keyer,        |
  10 nF to GND (RF)     | |         |          USB, UI, settings     |
                          |         |  core 1: DDS tone + envelope   |
 CONTROLS (H-05)          |         |                                |
  encoder A/B + button ------------>| GP11 GP12 GP13                 |
  (optional potentiometer) -------->| GP26 (ADC)                     |
                          |         |                                |
                          |         | GP4  PWM tone ---------+       |
                          |         | GP6  PWM buzzer --+    |       |
                          |         | GP8  KEY ---+     |    |       |
                          |         | GP9  PTT --+|     |    |       |
                          |         | GP10 <-- TX switch sense       |
                          |         | GP16 RGB LED (status, on Zero) |
                          |         +------------||-----|----|-------+
                          |                      ||     |    |
   BUZZER (H-10) <--[transistor]<----------------||-----+    |
   (passive, switchable)                         ||          v
                                                 ||   [2nd-order RC filter, fc ≈ 4 kHz]
 LINE INPUT (H-11)                               ||          |  tone (mono)
  L/R jack --[C]--[(1:1 transformer, opt.)]-+    ||          v
                                            +----||--> [L/R SUMMER + HEADPHONE AMPLIFIER]
                                                 ||     (rail-to-rail op-amp, e.g. TS922, +5 V_A)
                                                 ||          |
                                                 ||          +--> HEADPHONE jack (H-10) L = line L + tone
                                                 ||                                    R = line R + tone
   TRAINING / TRANSMITTER SWITCH (H-21), 2-pole: ||
    pole 1: in TRAINING position cuts power to   ||the optocoupler LEDs (HW interlock, independent of FW)
    pole 2: GP10 (FW knows which mode it is in) + red "TRANSMITTER" LED
                                                 ||
                 +-------------------------------+|
                 v                                v
        [optocoupler KEY]                 [optocoupler PTT]    galvanic isolation (H-20)
          | open collector                  | open collector
        [ferrite + 1 nF + TVS]            [ferrite + 1 nF + TVS]  RF filter
          v                                 v
        KEY jack                          PTT jack              → transmitter (keying to ground)
```

### 1.2 Blocks and recommended solution

| Block | Requirements | Design | Note |
|---|---|---|---|
| Paddle input | H-01, H-04 | External 10 kΩ pull-up to 3.3 V (contact current 0.33 mA ≤ 1 mA), 1 kΩ series resistor, ESD protection (TVS array, e.g. PESD/TPD series; on the breadboard 2× BAT54S to the supply rails), 10 nF to ground against RF. | An RC of 10 µs adds no delay and filters out RF from the transmitter. Contact bounce is handled by FW (H-04). |
| Straight key | H-02 | A mono jack (TS) shorts the ring to ground → dah would be permanently pressed. In pass-through mode the ring is ignored; a mono jack can be detected at power-up. | Decision R-10. |
| Tone | H-12, H-13 | PWM ≈ 122 kHz / 10 bits from GP4, samples at ≈ 32 kHz computed by core 1 (table sine × 5 ms raised-cosine envelope), passive 2nd-order RC filter. | Alternative: I²S DAC (PCM5102A) via PIO, R-05. |
| Mixing and headphones | H-10, H-11 | Analog summer (op-amp), line L/R + tone into both channels, rail-to-rail headphone driver on +5 V_A (TS922 can drive 32 Ω headphones; MCP6002 as a weaker substitute), 220 µF output capacitors. | The line input is mixed in the analog domain: no ADC and no delay. |
| Volume | H-12 | Tone volume digitally (DDS amplitude, setting). Overall volume with a 10k log dual-gang potentiometer. | R-06. |
| Ground loop | H-42 (3) | Try on the breadboard: (a) direct connection, (b) 1:1 isolation transformer on the line, (c) differential input. Leave room for the transformer on the PCB. | R-07. Analog supply through an LC filter. On the Pico switch the regulator to PWM mode (GP23 = 1). |
| Buzzer | H-10 | Passive piezo or magnetic transducer, transistor driven from GP6 (square wave at the tone pitch). Can be turned off in settings. | Shaped edges are not required for the buzzer. |
| KEY / PTT | H-20 | Optocoupler with transistor output (PC817/4N25: switches in µs, open collector to ground). Alternative PhotoMOS (TLP222A, CPC1017N): both polarities, but t_on ≈ 1 ms. RF: ferrite, 1 nF, TVS ≈ 15–24 V. | R-08. Document polarity and max. voltage (H-20). |
| Training / transmitter | H-21 | **Two-pole switch**: pole 1 physically cuts power to the optocoupler LEDs, pole 2 informs the FW and lights a red LED. In training mode KEY/PTT stay open even if the FW fails. | Safety in two layers: HW and FW. |
| Default state after reset | H-21 | GPIO in Hi-Z after reset + pull-down on the optocoupler drive → open. Watchdog. | A FW crash must not leave KEY closed. |
| Controls | H-05 | Encoder with push button (EC11): rotation = speed, press = menu announced in Morse into the sidetone. Optional potentiometer. | R-04. |
| Power | H-35 | USB 5 V only, estimated draw 30–60 mA (RP2040 ≈ 25 mA, amplifier, buzzer, LEDs). | Some power banks switch off at low load. R-14. |

### 1.3 Current draw (estimate)
RP2040 ≈ 25 mA, headphone amplifier 5–15 mA, buzzer ≤ 20 mA (only while the tone sounds), optocoupler LEDs 2 × 5 mA, LEDs 2–5 mA.
Total < 70 mA → meets H-35. To be verified by measurement in stage 4.

### 1.4 Pin assignment (Pico and RP2040-Zero, only GPIOs on the Zero's edge)

| GPIO | Function | GPIO | Function |
|---|---|---|---|
| GP2 | dit paddle (tip) | GP10 | TRAINING/TRANSMITTER switch sense |
| GP3 | dah paddle (ring) | GP11, GP12 | encoder A, B |
| GP4 | PWM tone (slice 2A) | GP13 | encoder button |
| GP6 | PWM buzzer (slice 3A) | GP14 | spare button |
| GP8 | KEY (optocoupler) | GP26 | ADC: potentiometer (optional) |
| GP9 | PTT (optocoupler) | GP16 | RGB LED on the Zero (WS2812), on the Pico GP25 LED |

## 2. Firmware

### 2.1 Environment
pico-sdk (BSD-3) + TinyUSB (MIT), C, CMake, Raspberry Pi Pico extension for VS Code (installs the toolchain). The keyer
logic is written as **plain C code with no HW dependency** and is tested on the PC (host tests), so it can be verified
before the board exists.

### 2.2 Structure

```
firmware/
  CMakeLists.txt
  src/
    main.c          initialization, core 0 main loop, core 1 start
    board.h         pins, board variant (Pico / Zero)
    hal_*.c         GPIO, PWM, timer, flash (the only chip-dependent place)
    input.c         paddle sampling at 10 kHz, debouncing, paddle swap (left-handed)
    keyer.c/.h      keyer: iambic A, iambic B, single-lever, pass-through (pure logic)
    timing.c        WPM → durations, ratio 2.8–3.5, weight
    keyline.c       "key line": the single source of tone on/off events, distributes them
    sidetone.c      core 1: DDS, envelope, volume, pitch
    buzzer.c        PWM buzzer
    hid.c           HID reports: stream B + paddle state C, report queue
    usb_desc.c      descriptors (HID boot keyboard; in E5 + MIDI)
    txout.c         KEY/PTT: lead time, hang time, interlock in training mode
    ui.c            encoder, buttons, menu
    announce.c      Morse announcements (sidetone only)
    settings.c      settings structure, CRC, storage in flash
    midi.c          (E5) settings via MIDI, with no ability to key
  test/             host tests of the keyer and timing (gcc + ctest)
```

### 2.3 Data flow

```
  paddles ──> input (debounce, swap) ───┬──> keyer ──> keyline ──┬──> sidetone (core 1)    ≤ 2 ms (H-13)
                                        │   (A/B/single/pass)    ├──> buzzer
                                        │                        ├──> hid: stream key (B, H-31)
                                        │                        └──> txout ─[TRANSMITTER mode only]─> KEY/PTT
                                        └──> hid: L-Ctrl / R-Ctrl (C, H-32, optional)

  ui / announce ──> sidetone (sound only; NEVER into the hid stream or txout)
  usb (HID output, MIDI in E5) ──> settings (speed, mode, tone). No path into keyline or txout (H-21, H-34)
```

Rules that keep it safe and correct:
1. **`txout` reads only `keyline`**, and `keyline` is fed only by `keyer` from the local paddles. No function in the code
   keys from USB or MIDI (H-21). Checked in review; every change to `txout.c` / `keyline.c` is reviewed separately.
2. **Menu announcements go only to the sidetone.** Announced letters must never reach the stream or KEY.
3. **No lost events (H-42):** `hid` has a report queue and each state is sent in its own USB frame (1 ms).
   A report always carries the complete state (stream + Ctrl), so nothing is lost or duplicated from the queue.
4. **No autorepeat (H-33)** from our device. OS autorepeat of a held key is filtered by the application
   (`event.repeat`, A-02).

### 2.4 Timing
- **Inputs and keyer:** periodic 10 kHz (100 µs) interrupt on core 0. Debouncing works so that **the first edge
  passes immediately** and further changes are ignored for 3 ms (as in the current firmware). Debouncing therefore adds no latency.
- **Tone:** core 1, ≈ 32 kHz interrupt, code in RAM (does not run from flash, so not even a settings write delays it). The
  envelope starts at the latest on the next sample (≈ 31 µs) after a `keyline` change → H-13 is met with a large margin.
- **USB:** `bInterval = 1`. A report is queued immediately on the event and the host collects it within 1 ms. On the
  device side the delay is therefore ≤ 1.1 ms and jitter ≤ 1 ms (frame quantization).
  *Variant R-12:* the keyer runs in 1 ms steps synchronized to USB SOF. Element durations then arrive at the browser
  exactly and device-side jitter is ≈ 0.
- **The firmware measures its own latency:** event time → `tud_hid_report_complete_cb` (host took the report); the histogram
  can be read out (in stage 1 by a button and Morse output, or via debug output over SWD).

### 2.5 Keyer
- Modes: iambic A, iambic B, single-lever (last press wins), pass-through (tip → directly to keyline). No bug (H-02).
- Parameters: 5–40 WPM (internally 5–50), ratio 2.8–3.5 (default 3.0), weight 25–75 % (default 50 %), element memory
  on/off (separately for dit/dah), paddle swap. Dit length = 1200 / WPM ms (PARIS).
- State machine: IDLE → ELEMENT (tone) → GAP (1 dit) → next element according to paddle state and memory. Iambic B additionally
  adds one opposite element after both paddles are released during a squeeze.
- Host tests: scenarios "paddle X pressed at time t" → expected sequence of elements (start, length) with 100 µs tolerance.

### 2.6 PTT (H-22)
- In transmitter mode: first element after idle → close PTT, wait the lead time (10–50 ms, default 25 ms), only then key.
  Hang time: PTT is held for `hang` after the last element (default e.g. 7 dits ≈ word space, adjustable); PTT can be turned off.
- Open (R-09): should the sidetone and stream also be delayed by the lead time so the sound matches the transmission, or only KEY?
  Recommendation: delay the whole keyer output (keyline). H-13 latency is measured "from the start of the element" and the element starts only after
  the lead time, so it is still met. The stream then matches what was actually transmitted.

### 2.7 Settings (H-05)
- `settings_t` structure with magic, version and CRC32, in the last two 4 kB flash sectors (alternating, for robustness against
  power loss during a write).
- Deferred write: 5 s after the last change and only while the keyer is idle. Via `flash_safe_execute` (pauses core 1; the tone
  is not playing at that moment because nothing is being keyed).
- Factory reset: hold the encoder button while connecting power.

### 2.8 USB identity (H-30)
- Product e.g. "CW Key (Morse)", manufacturer without personal data, **no serial number** (`iSerialNumber = 0`), or a fixed "1".
- VID/PID: for a public open-source project a PID from [pid.codes](https://pid.codes) (VID 0x1209, assigned via pull request
  = publication, only with consent). Their test PID for development. R-11.
- Adding MIDI (E5) changes the descriptors → increase `bcdDevice` or use a different PID so Windows does not use a stale cached entry.

### 2.9 License (H-41)
- Own code MIT. pico-sdk BSD-3 and TinyUSB MIT: keep their license headers and list them in `THIRD_PARTY.md`.
- Vail adapter (MIT) only as a reference. **The K3NG keyer is GPL-3 → do not take code from it** (it could not be MIT).
- From the earlier paddle converter (outside this repository) only ideas can be reused (debouncing, sending reports directly); the code is
  for STM32 anyway.

## 3. Open decisions

| # | Decision | Recommendation | When |
|---|---|---|---|
| R-01 | MCU and board | RP2040: Pico for development, RP2040-Zero for the enclosure ([mcu-selection.md](mcu-selection.md)) | now |
| R-02 | FW environment | pico-sdk + TinyUSB (clean license, full control over USB). Alternative arduino-pico + Adafruit TinyUSB: faster start, but LGPL core. | now |
| R-03 | Stream key (H-31) | according to the test [test-h31-stream-key.md](test-h31-stream-key.md), working candidate F13 | E0–E1 |
| R-04 | Control without a PC (H-05) | encoder with push button + menu announced in Morse. Speed directly by rotation. | before E4, ideally now |
| R-05 | Tone generation | PWM + 2nd-order RC filter. I²S DAC only if PWM is not clean enough. | E2 |
| R-06 | Amplifier and volume | TS922 (or MCP6002), tone digitally, overall with a dual-gang potentiometer | E2 |
| R-07 | Ground loop on the line | try direct connection, 1:1 transformer, differential input | E2 |
| R-08 | KEY/PTT element | optocoupler with transistor (fast). PhotoMOS as a variant for both polarities. | E3 |
| R-09 | PTT lead time and sidetone | delay the whole keyline | E3 |
| R-10 | Straight key with mono jack | ignore the ring in pass-through mode + detection at power-up | E1 |
| R-11 | VID/PID, strings, author name | pid.codes, "CW Key (Morse)", no serial number. Author in the license: OK2MTW? | before publication |
| R-12 | Keyer synchronized to USB SOF | try in E1 if jitter exceeds 1 ms | E1 |
| R-13 | Max. KEY closed time | safeguard e.g. 10 s (tuning the rig with a straight key?) | E3 |
| R-14 | Power bank switches off at low load | test with common power banks. Possibly a "low load" mode or a pulsed load. | E4 |
| R-15 | Spare Black Pill | spare / H-31 pre-test | E0 |
| R-16 | Alternative `[` `]` (H-32) and the Czech keyboard | the application must read `event.code`, not `event.key` (in the CZ layout `[` gives the character "ú") | E1 |
| R-17 | PCB and enclosure | KiCad (CERN-OHL-P), only after E3 | after E3 |
