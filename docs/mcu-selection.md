# Microcontroller and board selection

> **Decision 2026-11-09: development stays on the WeAct Black Pill STM32F411CEU6; the module is not changing.**
> The original comparison is kept below for the record. The Black Pill risks (ST USB library license SLA0044, missing MIDI in the
> stm32duino core, careful flashing via DFU) remain and are being addressed step by step. Stage 1: [stage1-blackpill.md](stage1-blackpill.md).
> **Update 0.4.0:** the firmware moved to TinyUSB + STM32CubeF4 HAL (no Arduino): MIDI is available and SLA0044 is gone;
> flashing uses our TinyUF2 bootloader ([firmware-flashing.md](firmware-flashing.md)).

Status: original draft (2026-11-08). Addresses the first open question of the specification ([specification.md](specification.md), section 6).
The author decides. Prices are indicative (November 2026) and availability must be checked before buying.

## Criteria (from the specification)

| Criterion | Requirements | What it means for the MCU |
|---|---|---|
| USB HID + later MIDI | H-30, H-34 | Native USB Full-Speed and a USB stack that supports a **composite device** (HID boot keyboard + MIDI) without writing a stack from scratch. |
| USB latency ≤ 5 ms, jitter ≤ 1 ms | H-33 | Interrupt endpoint with `bInterval = 1`, report sent immediately after the event, no hidden `delay()` in the library (see the experience with the Keyboard library for STM32: 20 ms after every report). |
| Tone with shaped edges, sidetone ≤ 2 ms | H-12, H-13 | DAC, or PWM with a sufficient carrier, and a deterministic interrupt for samples (≈ 32 kHz). |
| Settings without a PC, remembered | H-05 | Safe write to flash or EEPROM that does not disturb USB or the tone while writing. |
| MIT license, reused code | H-41 | SDK and USB stack under a license compatible with MIT (BSD/MIT/Apache), no proprietary terms. |
| Easy and safe flashing without an ST-Link | (brief) | A bootloader that cannot be overwritten or "bricked", flashing without special tools. |
| Availability in the Czech Republic | (brief) | In stock at a Czech shop, replacement unit within a week. |
| Draw < 100 mA, power from a power bank | H-35 | No radio, low consumption. |

## Candidates

### A) WeAct Black Pill STM32F411CEU6 (we have 1 working unit)
- **USB:** OTG FS, the HW is fine. The stm32duino core, however, offers only HID (keyboard + mouse) or CDC, **not MIDI**. A composite device would
  mean modifying the descriptors in the ST USB library, or leaving Arduino (CMake + HAL + TinyUSB; a `dwc2` port for F4 exists).
- **License:** the ST USB Device library (`stm32_mw_usb_device`) is under **SLA0044** (proprietary "Ultimate Liberty", restricted to ST chips).
  For a public MIT project this is inconvenient at the very least. HAL is under BSD-3 and with TinyUSB the licensing would come out clean, but that is
  already a different project from today's sketch.
- **Tone:** the F411 **has no DAC** → PWM + filter (same as RP2040). Performance (100 MHz, FPU) is more than enough.
- **Settings:** the internal flash has large sectors (16/64/128 kB). Erasing a 16 kB sector takes hundreds of ms and the CPU stalls meanwhile
  (single bank) → write only when idle. Or an external I²C EEPROM.
- **Flashing:** ROM DFU (BOOT0). On the F411 Black Pill, DFU is known to be unreliable (dependence on the 25 MHz crystal, timing)
  and **two units are already dead**. Without an SWD probe there is no way to rescue the board.
- **Availability:** common (AliExpress, Czech e-shops), clone quality varies.
- **Verdict:** workable, but the USB stack license, missing MIDI in Arduino and above all the flashing risk go against the brief.

### B) Raspberry Pi RP2040: Raspberry Pi Pico / Waveshare RP2040-Zero ⭐ recommended
- **USB:** native FS; the official USB stack of pico-sdk is **TinyUSB (MIT)**. HID boot keyboard and MIDI are standard classes;
  a composite device is defined by descriptors only. The Vail adapter with MIDI solves the same thing (on SAMD21) and can serve as a reference.
- **License:** pico-sdk **BSD-3-Clause**, TinyUSB **MIT** → firmware cleanly under MIT (it is enough to keep their copyright notice).
- **Tone:** no DAC, but 125 MHz PWM gives a carrier > 100 kHz at 10 bits. **The second core** can compute only the audio (DDS sine
  + envelope), so neither USB nor the keyer disturbs it. Tone latency is then on the order of tens of µs.
- **Settings:** external QSPI flash, last sector (4 kB). Write via `flash_safe_execute` (pauses the second core), only
  when idle → no impact on keying.
- **Flashing:** **UF2 bootloader in mask ROM** (BOOTSEL button). It cannot be overwritten or erased; the board shows up in Explorer
  as a drive and the firmware is copied onto it. No option bytes, no mass erase. Software practically cannot
  "kill" the board. `picotool` can read the flash (backup) and verify it. A second Pico with the *Debugprobe* firmware is also a cheap SWD probe.
- **Availability:** Raspberry Pi Pico **138 CZK, in stock** (LaskaKit, 2026-11-08). The RP2040-Zero (USB-C, 18 × 23 mm, RGB LED)
  is sold by Waveshare through European distributors (approx. 4.6 € excl. VAT, rlx.sk, Botland, TinyTronics). A specific Czech
  shop is still to be checked.
- **Weaknesses:** no DAC (solved by PWM + 2nd-order filter). The Pico has micro-USB (a phone needs a USB-C ↔ micro-B cable);
  the RP2040-Zero has USB-C. The Pico uses a switching 3.3 V regulator that is noisy (pin GP23 = PS switches it to PWM mode with lower
  ripple). Therefore power the analog section separately (see the architecture).
- **Caution:** the installed `arduino:mbed_rp2040` core (Mbed OS) is not suitable: Mbed is discontinued, it has its own USB stack and cannot
  be combined with TinyUSB. The recommended environment is **pico-sdk** (CMake, Raspberry Pi Pico extension for VS Code), or alternatively the
  arduino-pico core (E. Philhower) with the Adafruit TinyUSB library. Arduino-pico, however, is under LGPL-2.1, which brings obligations
  when distributing binaries → see decision R-02 in [architecture.md](architecture.md).

### C) RP2350: Raspberry Pi Pico 2 / RP2350-Zero
- Same SDK and bootloader as the RP2040, more RAM, FPU, more PIO. Firmware from pico-sdk just needs to be recompiled for it.
- We need nothing extra for the keyer. Older chips (stepping A2) have erratum E9 (GPIO leakage with the internal pull-down),
  but we use pull-ups. At LaskaKit the Pico 2 was sold out at the time of research, price around 110 CZK excl. VAT (Farnell).
- **Verdict:** an equivalent backup to the RP2040. If the RP2040 becomes unavailable, we take the RP2350 without changing the architecture.

### D) SAMD21: Seeed XIAO SAMD21 / Adafruit QT Py SAMD21
- The **Vail adapter** runs on this platform (the project explicitly states it does not support the RP2040). It is the closest as a reference.
- **Has a real 10-bit DAC** → the cleanest sine without PWM.
- The UF2 bootloader, however, is **in flash**: it is normally protected by a fuse, but if it gets corrupted it cannot be fixed without an SWD probe.
  It has only 11 pins (XIAO), which is tight for the encoder, KEY/PTT, switch and buzzer. The Arduino SAMD core is under LGPL.
- **Verdict:** a good choice for a "Vail adapter clone". We need more pins and more robust flashing.

### E) ESP32-S3 (e.g. ESP32-S3 Super Mini)
- It **has USB OTG** → TinyUSB in ESP-IDF, HID + MIDI work. The ROM download mode (BOOT) cannot be destroyed.
  Settings are stored in NVS. ESP-IDF is under Apache-2.0 (compatible with MIT).
- Against: we have no use for Wi-Fi/BLE (the spec does not want a radio), it runs on FreeRTOS where determinism is harder to guarantee, and when
  running TinyUSB OTG the built-in USB-JTAG console disappears. It is a more complex platform with no benefit for the brief.
- **Verdict:** only as an emergency option.

### What to do with the boards we already have
- **Black Pill F411:** spare. It can be used for the H-31 pre-test (it can send any key). Flashing,
  however, means a DFU write and a risk → only with the author's consent, see [test-h31-stream-key.md](test-h31-stream-key.md).

## Comparison

| | Black Pill F411 | **RP2040 (Pico / Zero)** | RP2350 (Pico 2) | SAMD21 (XIAO) | ESP32-S3 |
|---|---|---|---|---|---|
| HID + MIDI composite | ⚠️ not in Arduino, different stack needed | ✅ TinyUSB | ✅ TinyUSB | ✅ (Vail adapter) | ✅ TinyUSB |
| USB latency/jitter | ✅ (without the Keyboard library) | ✅ | ✅ | ✅ | ✅ (watch the RTOS) |
| Tone | PWM | PWM + 2nd core | PWM + 2nd core | ✅ **DAC** | PWM / I²S |
| Settings | flash (large sectors) | flash 4 kB, `flash_safe_execute` | ditto | EEPROM emulation | NVS |
| SDK/USB license | ⚠️ SLA0044 (ST USB) | ✅ BSD-3 + MIT | ✅ BSD-3 + MIT | LGPL core / MIT TinyUSB | ✅ Apache-2.0 |
| Flashing without a probe | ⚠️ ROM DFU, 2 units dead | ✅ **UF2 in ROM, indestructible** | ✅ UF2 in ROM | ⚠️ UF2 in flash | ✅ ROM |
| Pins | enough | Pico enough, Zero 20 on the edge | enough | 11 (tight) | enough |
| Availability in CZ | common | ✅ Pico in stock 138 CZK | varies | EU distributors | common |

## Recommendation

**RP2040 with pico-sdk + TinyUSB.** For development on a breadboard, the **Raspberry Pi Pico** (2 units: one keyer, the other a test
generator and SWD probe); later, for the enclosure, the **RP2040-Zero** (USB-C, small). Both have the same chip and GPIO numbers, so there is
one firmware. The pin assignment in [architecture.md](architecture.md) uses only GPIOs brought out to the edge of the Zero board.

Main reasons:
1. **Flashing cannot be broken.** The bootloader in ROM cannot be overwritten. After two lost Black Pills this is decisive.
2. **Clean MIT license** (pico-sdk BSD-3, TinyUSB MIT). SLA0044 from the ST USB library goes away.
3. **HID + MIDI is standard in TinyUSB.** Stage 5 adds MIDI with just another class and descriptors.
4. **A second core for audio** gives a simple path to ≤ 2 ms latency and clean edges without competing with USB.
5. Cheap, in stock, with a large community and documentation.

If the author wanted to stay with the Black Pill, it would only be possible by leaving Arduino for CMake + HAL + TinyUSB, and ideally with an SWD
probe (a Pico with Debugprobe) so that the board can be rescued.

## Sources
- [Vail adapter: supported boards (SAMD21, not RP2040)](https://raw.githubusercontent.com/Vail-CW/vail-adapter/master/docs/advanced-install.md)
- [ST USB Device library: SLA0044 license](https://community.st.com/t5/stm32cubeide-mcus/compliance-to-use-modules-with-sla0044-license-under-a-mit/td-p/73257)
- [LaskaKit: Raspberry Pi Pico](https://www.laskakit.cz/raspberry-pi-pico), [LaskaKit: Pico 2](https://www.laskakit.cz/raspberry-pi-pico-2)
- [Farnell CZ: Pico 2](https://cz.farnell.com/raspberry-pi/raspberry-pi-pico-2/mcu-board-rpi-pico-2-520kb-150mhz/dp/4531086)
- [Waveshare RP2040-Zero](https://www.waveshare.com/RP2040-Zero.htm), [rlx.sk RP2040-Zero](https://rlx.sk/en/raspberry-pi-pico/8554-rp2040-zero-a-pico-like-mcu-board-based-on-raspberry-pi-mcu-rp2040-mini-ver-ws-20187.html)
