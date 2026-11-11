# morse-key-hw – CW Key, a USB keyer for Morse paddles

A small hardware keyer for iambic or single-lever paddles, built on a cheap **WeAct Black Pill** board
(STM32F411 or STM32F401). The keyer forms dits and dahs with machine timing, plays its own sidetone without delay
and sends the keying to a computer or phone as a **USB keyboard**: no drivers, any browser.

**What it is for**

- Practising Morse sending with a web trainer (the Morse Trainer project): the application receives clean, machine-timed
  elements and can decode and score them reliably.
- Zero-latency sidetone from the key itself, instead of a phone or Bluetooth audio path with 50–150 ms delay.
- Compatible with other web trainers: in mode C it also reports the paddles as Left/Right Ctrl, like the Vail adapter
  (Vail, VBand, morsecode.world).
- Keying a transmitter: KEY and PTT outputs.

**Status:** prototype, firmware **0.3.0**. Working: iambic B keyer (20–60 WPM), paddle filter, buzzer and speaker sidetone outputs,
speed/mode button, USB keyboard, KEY/PTT outputs. Planned: headphone output with PC audio mix, isolated KEY/PTT,
settings stored and adjustable from the application ([stage plan](docs/stage-plan.md)).

## Get started → [Wiki](https://github.com/OK2MTW/morse-key-hw/wiki)

| | |
|---|---|
| [Hardware and wiring](https://github.com/OK2MTW/morse-key-hw/wiki/Hardware-and-Wiring) | parts, pins, paddles, buzzer or speaker, KEY/PTT |
| [First firmware installation](https://github.com/OK2MTW/morse-key-hw/wiki/First-Firmware-Installation) | a new board from the shop, once, on a computer |
| [Firmware update](https://github.com/OK2MTW/morse-key-hw/wiki/Firmware-Update) | copy a `.uf2` file to the board's USB drive |
| [Usage](https://github.com/OK2MTW/morse-key-hw/wiki/Usage) | button, speeds, USB modes, LED |
| [Troubleshooting](https://github.com/OK2MTW/morse-key-hw/wiki/Troubleshooting) | |

## Firmware downloads

- Latest version, stable file names: [`firmware/latest/`](firmware/latest/) – also the permanent links for applications.
- All versions: [GitHub Releases](https://github.com/OK2MTW/morse-key-hw/releases) and [`firmware/dist/`](firmware/dist/).

Four files per release: for each chip (F411, F401) a `-full.bin` for the first installation over the factory DFU and a
`.uf2` for updates. One firmware serves both a buzzer (B8) and a speaker (A8).

## Documentation for developers

| Document | Content |
|---|---|
| [docs/specification.md](docs/specification.md) | requirements (H-xx hardware/firmware, A-xx application) |
| [docs/key-interface.md](docs/key-interface.md) | what the key sends over USB (keys, timing) – for application developers |
| [docs/firmware-flashing.md](docs/firmware-flashing.md) | package format, `manifest.json`, permanent links, DFU and UF2 flashing incl. from a web application |
| [docs/stage1-blackpill.md](docs/stage1-blackpill.md) | current stage: pins, controls, deviations from the specification |
| [docs/midi-settings-spec.md](docs/midi-settings-spec.md) | next step: USB MIDI settings channel and bootloader entry from the application (implementation spec) |
| [docs/stage-plan.md](docs/stage-plan.md), [docs/architecture.md](docs/architecture.md) | development plan, block diagram, open decisions |
| [docs/mcu-selection.md](docs/mcu-selection.md), [docs/test-h31-stream-key.md](docs/test-h31-stream-key.md) | MCU choice, stream key test on iPhone |
| [tools/keytest.html](tools/keytest.html) | test page: key events, element lengths, lost events |

**Building** (Windows, arduino-cli with the STMicroelectronics:stm32 core):
`firmware/build.ps1 -Mcu F411|F401 [-Uf2]`, whole release package: `firmware/package.ps1`.

## Licence

- Firmware, tools and documentation: **MIT**, see [LICENSE](LICENSE).
- Hardware (schematics, PCB when available): **CERN-OHL-P v2**, see [LICENSE-HW](LICENSE-HW).

Third-party components:

| Component | Where | Licence |
|---|---|---|
| TinyUF2 0.36.0 (Ha Thach / Adafruit), built with `TINYUF2_PROTECT_BOOTLOADER=0` | `firmware/bootloader/`, inside `-full.bin` | MIT, [LICENSE-tinyuf2.txt](firmware/bootloader/LICENSE-tinyuf2.txt) |
| uf2conv.py (Microsoft) | `tools/uf2/` | MIT, [LICENSE.txt](tools/uf2/LICENSE.txt) |
| STM32 Arduino core (stm32duino), ST HAL and USB library | compiled into the CwKey binaries | BSD-3-Clause (HAL, CMSIS), **SLA0044** (ST USB Device library, ST microcontrollers only) |

The source code in this repository is MIT. The Black Pill binaries contain the ST USB library under SLA0044, which
permits use with ST microcontrollers only – true for the Black Pill. A move to TinyUSB (MIT) is considered.

---
Martin OK2MTW
