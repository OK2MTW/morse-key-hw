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

**Status:** prototype, firmware **0.4.0**. Working: iambic A/B keyer (5–60 WPM), paddle filter, buzzer and speaker sidetone outputs,
speed/mode button, USB keyboard, KEY/PTT outputs, settings stored in the key and adjustable from the application over
USB MIDI (0.4.0, not yet tested on hardware). Planned: headphone output with PC audio mix, isolated KEY/PTT
([stage plan](docs/stage-plan.md)).

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
| [docs/firmware-update-check.md](docs/firmware-update-check.md) | how an application checks for, downloads and offers new firmware (on every login) |
| [docs/stage1-blackpill.md](docs/stage1-blackpill.md) | current stage: pins, controls, deviations from the specification |
| [docs/midi-settings-spec.md](docs/midi-settings-spec.md) | next step: USB MIDI settings channel and bootloader entry from the application (implementation spec) |
| [docs/stage-plan.md](docs/stage-plan.md), [docs/architecture.md](docs/architecture.md) | development plan, block diagram, open decisions |
| [docs/mcu-selection.md](docs/mcu-selection.md), [docs/test-h31-stream-key.md](docs/test-h31-stream-key.md) | MCU choice, stream key test on iPhone |
| [tools/keytest.html](tools/keytest.html) | test page: key events, element lengths, lost events |
| [tools/miditest.html](tools/miditest.html) | test page for the USB MIDI settings channel (Chrome/Edge/Firefox) |
| [tools/cwkey_midi.py](tools/cwkey_midi.py) | Windows command-line tool: MIDI info/settings and a full firmware update over MIDI (bootloader, copy, verify) |

**Building** (Windows; arm-none-eabi-gcc, `python -m pip install cmake ninja`): `firmware/build.ps1 [-Test]`
(downloads the pinned TinyUSB / ST sources with `firmware/tools/get_deps.py`, builds both chips, `-Test` runs the host unit
tests with `python -m pip install ziglang`); whole release package: `firmware/package.ps1`.

## Licence

- Firmware, tools and documentation: **MIT**, see [LICENSE](LICENSE).
- Hardware (schematics, PCB when available): **CERN-OHL-P v2**, see [LICENSE-HW](LICENSE-HW).

Third-party components (downloaded at build time by `firmware/tools/get_deps.py`, pinned versions):

| Component | Where | Licence |
|---|---|---|
| TinyUSB (Ha Thach) | USB stack, compiled into the firmware | MIT |
| STM32CubeF4 HAL driver (STMicroelectronics) | clock, GPIO, flash, compiled into the firmware | BSD-3-Clause |
| CMSIS device F4 (STMicroelectronics), CMSIS core (Arm) | startup code and register headers | Apache-2.0 |
| TinyUF2 0.36.0 (Ha Thach / Adafruit), built with `TINYUF2_PROTECT_BOOTLOADER=0` | `firmware/bootloader/`, inside `-full.bin` | MIT, [LICENSE-tinyuf2.txt](firmware/bootloader/LICENSE-tinyuf2.txt) |
| uf2conv.py (Microsoft) | `tools/uf2/` | MIT, [LICENSE.txt](tools/uf2/LICENSE.txt) |

Since 0.4.0 the binaries contain no code under restrictive licences (the earlier Arduino builds included the ST USB
library under SLA0044).

---
Martin OK2MTW
