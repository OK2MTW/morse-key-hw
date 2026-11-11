# Staged development plan

> **2026-11-09: development on the Black Pill F411** (the author's decision). Stage 1 is narrowed to paddles, keyer, piezo and
> the on-board button, see [stage1-blackpill.md](stage1-blackpill.md). The shopping lists with the Pico no longer apply. For stage 1 a
> 3.5 mm stereo jack, a passive piezo, 100 Ω, 2× 1 kΩ, 2× 10 nF and a breadboard are enough. Further stages will be revised when they are opened.

Status: **draft** (2026-11-08). It assumed the RP2040 ([mcu-selection.md](mcu-selection.md)) and the architecture from [architecture.md](architecture.md).
Principle: **every stage ends with a usable device.** Already after stage 1 the keyer can be used with a web trainer.
The acceptance criteria refer to items H-42 (1)–(5) in the specification. Prices are indicative.

Rules for working with boards (apply to all stages): before the first write, read and back up the board (`picotool info`,
`picotool save`), every flashing only with consent, one attempt, stop on error. The RP2040 is flashed by copying a UF2 to
the RPI-RP2 drive (BOOTSEL); there are no option bytes and no mass erase.

---

## Stage 0: preparation and H-31 pre-test (no purchase, no flashing)

**Goal:** environment prepared, first data on iPhone behaviour before there is any HW.
- Install pico-sdk (Raspberry Pi Pico extension for VS Code), compile an empty project with TinyUSB HID (build only).
- Write host tests for the keyer and the keyer itself (pure logic, on the PC).
- Test page [tools/keytest.html](../tools/keytest.html) and the H-31 pre-test with an **ordinary USB keyboard** on iPhone and
  Android ([test-h31-stream-key.md](test-h31-stream-key.md), step A).

**Acceptance:** the firmware compiles. Host tests of the keyer (iambic A/B, single-lever, memory, weight, ratio) pass. The table of
H-31 pre-test results filled in for the available platforms.

**Buy:** nothing. An iPhone with Lightning needs the "Lightning to USB 3 Camera Adapter"; with USB-C a USB-C → USB-A adapter is enough.

---

## Stage 1: paddles + keyer + stream B (+ C) + buzzer on a breadboard

**Goal:** a keyer usable with the application (the "straight key" mode already accepts one key = tone on/off, see status 0.13.10)
and with paddle state C (L-Ctrl/R-Ctrl) also with web trainers such as Vail/VBand/morsecode.world.

Content:
- Paddle input with protection (pull-up 10k, 1k, diodes, 10 nF), debouncing 3 ms (H-01, H-04).
- Keyer iambic A/B, single-lever, pass-through. Speed by potentiometer, other parameters in code for now (H-02, H-03).
- USB HID boot keyboard, `bInterval` 1 ms, stream B always, C optional (H-30–H-33). Stream key according to the result of the
  pre-test, as a constant in code.
- Buzzer (passive) switched off by a jumper (H-10, part).
- Latency measurement in firmware (event → report taken over by the host).
- Generator for acceptance: a second Pico "presses the paddles" via an optocoupler according to a script (100 elements, known lengths).

**Acceptance:**
- **H-42 (1):** 100 elements from the generator → in [tools/keytest.html](../tools/keytest.html) (until the application has the
  Settings → Key page): no lost or duplicated events, element lengths with jitter ≤ 1 ms, both paddles at once handled correctly
  (C: both Ctrl pressed). The firmware measures a device-side delay ≤ 2 ms. The total USB latency ≤ 5 ms is confirmed
  with a logic analyser or oscilloscope if available, otherwise only with the application.
- **H-42 (5), part:** PC Chrome + Firefox, Android Chrome, iPhone Safari (according to the H-31 test, step B).
- Keyer: speed 5–40 WPM within ±1 % tolerance, iambic A vs. B differ correctly (squeeze test).

**Buy (≈ 600–900 CZK):**
| Item | Qty | Note |
|---|---|---|
| Raspberry Pi Pico (LaskaKit ≈ 138 CZK) | 2 | one keyer, the other generator / SWD probe (Debugprobe) |
| Pin headers 2×20 (if not included) | 2 | |
| Breadboard 830 points + jumper wires | 1 | |
| 3.5 mm stereo jack socket for PCB / breakout module | 2 | paddles, spare |
| Resistors 1k, 10k, capacitors 10 nF, 100 nF (kit) | | |
| BAT54S or 1N4148 (input protection) | 4 | TVS array on the PCB later |
| Passive buzzer (piezo) + transistor BC547 / 2N7000 | 1 | |
| Potentiometer 10k lin | 1 | speed |
| Optocoupler PC817 | 2 | generator → paddles (galvanically like a real paddle) |
| Micro-USB cable (data), USB-C ↔ micro-B adapter | 1+1 | phone |
| *Optional:* logic analyser 8 ch / 24 MHz (≈ 200–300 CZK, sigrok/PulseView) | 1 | latency and timing measurement, highly recommended |

---

## Stage 2: headphones and line input mix

**Goal:** sidetone into headphones with shaped edges, mixed with PC/phone audio → one pair of headphones for CW Chat and receiving lessons.

Content: DDS sine on core 1, 5 ms envelope, pitch 400–1000 Hz, volume (H-12). 2nd-order filter, summing amplifier, headphone amplifier
(H-10, H-11). Analog supply via an LC filter. Ground loop experiments (R-07).

**Acceptance:**
- **H-42 (2):** paddle → tone start at the headphone output ≤ 2 ms (pass-through mode) / from element start (keyer). Measured
  with an oscilloscope, or with a logic analyser on the paddle and on the output via a comparator.
- **H-42 (3):** line from the PC (same USB) and from a phone (powered from a power bank) without audible hum. Tone without clicks
  (listening + capture of the envelope waveform).
- Buzzer and headphones can be switched off independently.

**Buy (≈ 300–500 CZK):**
| Item | Qty | Note |
|---|---|---|
| TS922IN (DIP-8), or MCP6002 | 2 | headphone driver / summing amplifier |
| 3.5 mm stereo jack socket | 2 | headphones, line input |
| Dual-gang potentiometer 10k log | 1 | volume |
| Capacitors: film 10–47 nF (filter), electrolytic 220 µF, 10 µF | kit | |
| Choke / ferrite + 100 µF (LC filter for the analog supply) | 1 | |
| 3.5 mm stereo jack–jack cable | 1 | line from PC/phone |
| *Optional:* 1:1 isolation transformer (600 Ω) | 2 | ground loop |
| *Optional:* PCM5102A module (I²S DAC) | 1 | only if PWM is not clean enough (R-05) |

---

## Stage 3: KEY / PTT outputs for the transmitter

**Goal:** safe keying of the transmitter from the local paddles.

Content: optocouplers, RF filtering, KEY and PTT jacks (H-20). Double-pole training/transmitter switch with HW interlock and LED (H-21).
PTT sequencer: lead time, hang time, PTT disable (H-22). Maximum key-down time safeguard (R-13). Documentation of polarity and voltage.

**Acceptance (H-42 (4)):**
- With a dummy load on the transmitter: PTT lead and hang times measured with the analyser match the settings within ±1 ms.
- Training mode: KEY/PTT open (ohmmeter) even while keying and **even with deliberately faulty FW** (HW interlock test: a test
  firmware that tries to close KEY, and the output must stay open).
- Keying from USB is impossible: code review (no path USB → keyline/txout) + a test where the computer sends HID output reports
  (keyboard LEDs) and later MIDI. The output must not move.
- Reset and power disconnection during keying → KEY open.
- RF: keying while transmitting into a load at full power does not cause false elements or a reset.

**Buy (≈ 200–400 CZK, without transmitter and load):**
| Item | Qty | Note |
|---|---|---|
| Optocoupler PC817 / 4N25 (or PhotoMOS TLP222A) | 2–4 | R-08 |
| Toggle switch 2 × changeover (DPDT) | 1 | training / transmitter |
| Red LED + resistor | 1 | "TRANSMITTER" |
| 3.5 mm jack socket | 2 | KEY, PTT |
| Ferrites (rings / beads), ceramic 1 nF, bidirectional TVS 15–24 V | 4+4+2 | RF and overvoltage |
| Cables to the transmitter | 2 | depending on the rig type (which rigs? to be added) |

---

## Stage 4: control without PC, settings storage, enclosure

**Goal:** a standalone device that can be configured and works without a computer (from a power bank).

Content: encoder + menu announced in Morse (speed, mode, tone, tone volume, ratio, weight, memory, paddle swap,
paddle state C, `[` `]` key, PTT lead/hang time, buzzer). Storage in flash, factory reset (H-05). Move to
RP2040-Zero (USB-C), perfboard or first custom PCB, enclosure.

**Acceptance:**
- H-05: every setting can be changed without a PC and survives power disconnection. Repeated power-off during saving does not corrupt
  the settings (fallback to the last valid copy).
- Writing settings during keying does not cause a tone or USB dropout (write only when idle, verified with the analyser).
- H-35: current draw < 100 mA (USB meter), operation from a power bank for 1 h without switching off.
- **H-42 (1)–(5) complete** on the final HW, including iPhone Safari.

**Buy (≈ 400–800 CZK):**
| Item | Qty | Note |
|---|---|---|
| Waveshare RP2040-Zero | 2 | final board + spare |
| Encoder EC11 with push button + knob | 1 | |
| Push button | 1 | spare / function |
| Perfboard or PCB manufacturing (JLCPCB/PCBWay ≈ 5 pcs) | | PCB in KiCad, CERN-OHL-P |
| Enclosure (plastic/aluminium) | 1 | |
| USB current meter (if not available) | 1 | H-35 |

---

## Stage 5: USB MIDI for settings from the application

**Goal:** composite device HID + MIDI. The application can set speed/mode/tone via Web MIDI (H-34).

Content: MIDI class in TinyUSB, settings protocol (Control Change or SysEx messages, inspired by Vail adapter), new
`bcdDevice`/PID. No MIDI message can key (H-21).

**Acceptance:**
- H-34: settings from Chrome (PC, Android) and Firefox (PC) via Web MIDI work. On iPhone the device still works as a
  keyboard (iOS accepts the composite device), settings stay on the HW.
- **H-42 (1) and (5) again:** adding MIDI did not worsen HID latency or jitter.
- H-21: MIDI fuzz test (random messages) → neither KEY/PTT nor the stream moves, code review.

**Buy:** nothing.

---

## After the stages
- Documentation (cs + en): wiring, KEY/PTT polarity, iPhone, window focus (spec chapter 5, item 8).
- Publishing the repository on GitHub (H-40) **only after the author's consent** and a check that it contains no personal data.
- Tasks for the application (spec chapter 5) follow on from stage 1 ("HW keyer (USB)" type, event log for H-42).
