# Stage 1 on the Black Pill: paddles, keyer, piezo, button

Status: **firmware written and compiled, not yet flashed** (2026-11-09).
Decision 2026-11-09: **development stays on the WeAct Black Pill STM32F411CEU6**; the module is not changing. The board's USB is used
to connect to a phone or PC. Since 0.2.0 also KEY/PTT outputs (NPN), since 0.3.0 a speaker output on PA8; headphones and controls remain open.

Firmware: [firmware/src](../firmware/src) (since 0.4.0 TinyUSB + STM32 HAL + CMake; up to 0.3.0 an Arduino sketch), build: [firmware/build.ps1](../firmware/build.ps1).

## Pins

| Pin | Direction | Function | Wiring | Requirement |
|---|---|---|---|---|
| **PB6** | input | **dit** paddle (jack tip) | contact to GND, internal pull-up (≈ 40 kΩ → contact current ≈ 80 µA) | H-01 |
| **PB7** | input | **dah** paddle (jack ring) | ditto, jack sleeve = GND | H-01 |
| **PB8** | output | sidetone for an **active buzzer** (HIGH = tone, ≈ 4 kHz) | PB8 → (+) 32SC4120LF (−) → GND | H-10 |
| **PA8** | output | sidetone for a **speaker / passive transducer** (since 0.3.0): PWM 700 Hz, volume, soft edges; runs together with PB8 | NPN, base 1 kΩ + 10 kΩ pull-down, see [firmware-flashing.md](firmware-flashing.md) 3.5 | H-10, H-12 |
| **PB9** | output | **KEY** to the transmitter (since 0.2.0): HIGH during each element | NPN, base 2.2 kΩ + 47 kΩ pull-down, open collector to the rig | H-20 (part) |
| **PB5** | output | **PTT** to the transmitter (since 0.2.0): HIGH from the first element until 500 ms after the last | ditto | H-22 |
| PA0 | input | **KEY** button on the board | on the board, active low | H-05 (part) |
| PC13 | output | blue LED on the board | lit during an element | – |

Why these pins: PB6/PB7 already worked in the previous converter and are 5 V tolerant. PB8 has a hardware timer, so the tone runs without
CPU load. None of them conflicts with USB (PA11/PA12), SWD (PA13/PA14) or BOOT1 (PB2).

On the breadboard I recommend a 1 kΩ series resistor and 10 nF to GND on each paddle (protection and RF). Full ESD protection
(H-01) will come with the PCB. The buzzer used, 32SC4120LF, is **active** (own oscillator ≈ 4 kHz, 3–16 V); mind the + / − polarity. A passive transducer or speaker goes to PA8 via a transistor (same firmware, since 0.3.0).

## Control with the KEY button

| Action | Result | Confirmation in the piezo |
|---|---|---|
| short press | speed 20 → 30 → 40 → 50 → 60 → 20 WPM | new speed in Morse (e.g. "30"), sent at that speed |
| long press (1 s) | USB mode **B ↔ C** | "B" or "C" (sounds while still holding) |

The announcement goes **only to the piezo**, never to USB. Pressing a paddle interrupts the announcement.
After power-up: the stored settings (since 0.4.0; factory defaults 20 WPM, mode B – hold KEY while plugging in).

## USB modes (H-30 to H-33)

USB HID keyboard (boot protocol), 1 ms polling. Every state change goes in its own USB frame, so nothing
gets merged or lost.

| Mode | What is sent | Use |
|---|---|---|
| **B** (default) | **stream key** held down for the duration of each element (dit/dah) from the keyer | applications, "straight key" mode (H-31) |
| **C** | stream key + **Left Ctrl = dit paddle, Right Ctrl = dah paddle** (actual paddle state, independent of the keyer) | Vail, VBand, morsecode.world and fist analysis (H-32, Z-24) |

Per the specification, mode C sends the stream **in addition** to Ctrl. Web trainers ignore an unknown key.

**The stream key is F13 for now (provisional).** The final choice will be made based on the iPhone test (H-31,
[test-h31-stream-key.md](test-h31-stream-key.md)). In the firmware it is a single constant `STREAM_KEY`.

## Keyer

- Iambic B (constant `KEYER_MODE`, also supports A). A single-lever paddle works on the same inputs, since both levers
  cannot be pressed at once.
- Element memory: the opposite paddle pressed during an element is remembered. The paddles are not read in the gap after an element (since 0.1.1): the next element is determined only at the end of the gap from memory and from the paddles held at that moment. Ratio 3:1, weight 50 %, paddle filter (see below).
- Paddle filter (H-04): sampling every 100 µs into an integrator with hysteresis. A press is valid after ≥ 1 ms of clean closure, a release after ≥ 3 ms of clean opening, a new press no earlier than 8 ms after release (contact bounce on release). Short spikes (interference) are discarded. This delays a press by about 1 ms and a release by about 3 ms.
- Timing from the end of the previous element (no drift), 1 µs resolution.
- Tone delay: the active buzzer is switched directly by the pin (the oscillator starts practically immediately). A passive piezo starts with the next PWM period (≤ 1.43 ms at 700 Hz) → H-13 met.

## Deviations from the specification and open items

| # | Item | Proposal |
|---|---|---|
| O-1 | Speeds 20–60 WPM vs. H-03 (min. 5–40 WPM). Beginners lack a pace below 20; 50–60 are above the spec range. | Keep as requested, possibly add 10/15 later. I take "wps" to mean WPM (words per minute, PARIS). |
| O-2 | Settings are not remembered after power-off (H-05). | **Done in 0.4.0:** flash sector 5 (0x08020000), autosave 5 s after a change while idle. |
| O-3 | Stream key (H-31) | F13 provisionally, verify with an iPhone |
| O-4 | Iambic A/B and single-lever "last press wins" mode cannot be switched from the button | A constant for now, a menu later |
| O-5 | License: the ST USB library in the stm32duino core is under SLA0044 | **Done in 0.4.0:** firmware moved to TinyUSB + STM32CubeF4 HAL, no SLA0044 code. |
| O-6 | Classic (pass-through) mode for a straight key (H-02) | Next step, if needed |

## Bring-up procedure (only with consent)

1. Wire up on the breadboard: paddle jack (tip PB6, ring PB7, sleeve GND), piezo on PB8.
2. Flash the firmware according to [firmware-flashing.md](firmware-flashing.md): the first time via the factory DFU with the combined image `…-full.bin`,
   then updates via UF2. Before the first write, back up the flash contents; write in a single attempt with read-back
   verification, never mass erase or option bytes. The image must match the chip (F411 × F401).
3. NRST without BOOT0 → 1 long LED blink (crystal OK).
4. Test: paddles → tone in the piezo and the LED blinks. Button → "30", long press → "C".
   In [tools/keytest.html](../tools/keytest.html), mode B shows `F13`, mode C additionally `ControlLeft`/`ControlRight`.
5. Phone: Android via OTG, iPhone via an adapter (H-42 (5), H-31).

## Stage 1 acceptance (from H-42)
- (1) 100 elements: no lost or duplicated events in keytest.html (column "errors" = 0), element durations according to speed
  with a spread of ≤ 1 ms. In mode C both paddles at once = both Ctrl.
- (5) PC Chrome + Firefox, Android Chrome, iPhone Safari.
