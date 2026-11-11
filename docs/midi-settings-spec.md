# USB MIDI settings channel – implementation specification

Document version: 1 (2026-11-12). Status: **approved for implementation**, target firmware **CwKey 0.4.0**.
Audience: the session that implements it (firmware + test tools) and the application project (web side).
Requirements: H-05 (settings without a PC, remembered), H-21 (only local paddles key the transmitter), H-30/H-33 (HID
keyboard behaviour), H-34 (later optional USB MIDI for settings), open item F-10 in [firmware-flashing.md](firmware-flashing.md).

## 1. Goal

Add a second USB interface next to the existing HID keyboard so that a web application can, via **Web MIDI**:

1. read the device info (firmware version, chip, protocol version),
2. read and change settings: speed, keyer mode, tone pitch and volume, PTT times, USB mode, …,
3. store settings in the key (they survive power-off, H-05) – also when changed with the KEY button,
4. switch the key into the **UF2 bootloader** so the application can write a firmware update without any button presses.

Non-goals: keying anything from the computer (forbidden by H-21), sending text/macros (H-23), changing the HID keyboard
behaviour, changing the hardware.

## 2. Hard constraints

| # | Constraint |
|---|---|
| C-1 | **The HID keyboard must behave exactly as in 0.3.0**: boot keyboard, interrupt IN, `bInterval` 1 ms, F13 stream key, L/R Ctrl in mode C, one report per state change, no lost or merged events. Re-run acceptance H-42 (1). See [key-interface.md](key-interface.md). |
| C-2 | **H-21:** no MIDI message may key the transmitter, drive the keyer line, the sidetone as keying, KEY (B9) or PTT (B5). The MIDI code must have no call path to them. ENTER_BOOTLOADER forces KEY/PTT LOW before reset. |
| C-3 | **Never write option bytes, never mass-erase.** Settings use one flash sector only (section 6). |
| C-4 | The TinyUF2 bootloader (sectors 0–3) and the application layout (application at 0x08010000) stay as they are. Both chips (STM32F411CE, STM32F401CC/CD/CE) stay supported; one firmware drives both sidetone outputs (buzzer PB8, speaker PA8). |
| C-5 | Keyer timing, paddle filter and sidetone latency (H-13) must not get worse. Flash erase/write only while the keyer is idle. |
| C-6 | Firmware licence MIT; third-party code only with MIT/BSD/Apache-compatible licences (H-41). |

## 3. USB stack decision

The current firmware uses the stm32duino Arduino core. Its USB support is **either** HID (keyboard + mouse) **or** CDC –
no composite device with MIDI, and its ST USB library is under SLA0044.

**Decision D-1 (recommended):** move the firmware to **TinyUSB (MIT)** with the **STM32CubeF4 HAL/LL (BSD-3)** in a plain
**CMake** project, without the Arduino core.
- TinyUSB has the STM32F4 OTG FS driver (`dwc2`), HID and MIDI device classes and composite descriptors.
- Toolchain is already available: xPack `arm-none-eabi-gcc` 14.2 from the Arduino package, `cmake` + `ninja` (pip).
  A working example of exactly this stack for these boards is the TinyUF2 build (`ports/stm32f4`, board files for
  `stm32f411ce_blackpill` and `stm32f401_blackpill`) – reuse its clock setup and linker approach.
- Removes the SLA0044 licence issue of the binaries.
- Fallback D-1b, only if D-1 turns out impractical: stay on Arduino and add TinyUSB through a maintained library that
  supports stm32duino. Verify support first; do not patch the core.

**First step of the implementation is a spike:** a composite HID + MIDI device that enumerates on Windows, Android and
iPhone, with the HID latency unchanged (section 9, step S1).

## 4. USB device

| Item | Value |
|---|---|
| Interfaces | 0: HID boot keyboard (same report descriptor as today, **no mouse interface**); 1: Audio Control; 2: MIDI Streaming (1 cable, bulk IN/OUT 64 B) |
| HID endpoint | interrupt IN, 8 B, `bInterval` 1 (full speed) – unchanged |
| Strings | manufacturer `morse-key-hw`, product **`CW Key`** (H-30), MIDI port name `CW Key` |
| Serial number | none, or a fixed string – no chip UID (H-30) |
| VID:PID | **open decision D-2.** Today the core's ST `0483:5740` is used, which must not be used for an own device. Proposal: request a free PID at [pid.codes](https://pid.codes) (VID 0x1209, for open-source hardware – this project qualifies). Until assigned use pid.codes' test PID (check their current rules) and do not publish binaries with it. |
| Power | bus powered, max 100 mA |
| `bcdDevice` | 0x0300 for 0.3.0 (Windows caches descriptors per VID/PID/bcdDevice) |

The keyboard events seen by the browser do not depend on VID/PID or names, so the application's key handling is unaffected.

## 5. Protocol (MIDI System Exclusive)

All messages are SysEx with the **non-commercial manufacturer ID `0x7D`**:

```
F0 7D 43 4B <ver> <cmd> <seq> <payload ...> F7
         'C' 'K'
```

| Field | Meaning |
|---|---|
| `43 4B` | "CK" – identifies CW Key messages (ignore other 0x7D traffic) |
| `ver` | protocol version, **0x01** |
| `cmd` | command (host → device `0x01–0x3F`), response/notification (device → host `0x40–0x7F`) |
| `seq` | 0–127, chosen by the host, echoed in the response (device notifications use 0) |
| payload | 7-bit bytes only. Numbers are **14-bit: LSB7, MSB7** (0–16383). Strings: ASCII, length byte first. |

Everything else (notes, CC, clock, other SysEx) is ignored. The device never sends anything except responses and
notifications described here.

### 5.1 Commands (host → device)

| cmd | Name | Payload | Response |
|---|---|---|---|
| `01` | GET_INFO | – | `41` INFO |
| `02` | GET_ALL | – | one `42` VALUE per setting, then `43` ACK |
| `03` | GET | `id` | `42` VALUE |
| `04` | SET | `id`, value (14-bit) | `43` ACK (value applied in RAM) or `44` NAK |
| `05` | SAVE | – | `43` ACK after the settings are stored in flash (waits until the keyer is idle, max 5 s) or `44` NAK |
| `06` | RESET_DEFAULTS | – | all settings to defaults (RAM), `43` ACK; stored by the next save |
| `07` | ENTER_BOOTLOADER | confirmation `55 46 32` ("UF2") | `43` ACK, then the device resets into TinyUF2 (section 7) |

### 5.2 Responses and notifications (device → host)

| cmd | Name | Payload |
|---|---|---|
| `41` | INFO | protocol version; firmware version major, minor, patch; chip (`0` F411, `1` F401); reserved byte `0`; capabilities bitmask (bit0 tone settings, bit1 PTT outputs, bit2 bootloader entry); firmware name string ("CwKey") |
| `42` | VALUE | `id`, value (14-bit) |
| `43` | ACK | echoed `cmd` |
| `44` | NAK | echoed `cmd`, error code |
| `45` | CHANGED | `id`, value – sent whenever a setting changes **on the device** (KEY button), so the app UI stays in sync |

Error codes: `01` unknown command, `02` unknown setting id, `03` value out of range, `04` busy (keyer active / PTT on),
`05` not supported in this build (e.g. tone on the buzzer build), `06` flash error, `07` bad confirmation, `08` bad frame.

### 5.3 Settings

| id | Name | Range | Default | Notes |
|---|---|---|---|---|
| `01` | wpm | 5–60 | 20 | The KEY button keeps cycling 20/30/40/50/60: from any value it goes to the next list value above (60 → 20). |
| `02` | keyer_mode | 0 iambic A, 1 iambic B | 1 | Single-lever "last pressed wins" (H-02) can be added later as 2. |
| `03` | dah_ratio_x10 | 28–35 | 30 | H-03 |
| `04` | weight_pct | 25–75 | 50 | H-03, 50 = neutral |
| `05` | element_memory | 0/1 | 1 | |
| `06` | paddle_swap | 0/1 | 0 | left-handed (H-03) |
| `07` | usb_mode | 0 B, 1 C | 0 | same as the long KEY press |
| `08` | sidetone_on | 0/1 | 1 | announcements (speed/mode) still sound |
| `09` | tone_hz | 300–1200 | 700 | speaker output A8 (the active buzzer on B8 has a fixed pitch) |
| `0A` | volume_pct | 0–100 | 100 | speaker output A8 (100 % = 50 % duty) |
| `0B` | ptt_enabled | 0/1 | 1 | 0 = PTT output never closes (rigs with QSK/VOX, H-22) |
| `0C` | ptt_lead_ms | 0–100 | 0 | delays the first element, see `PTT_LEAD_MS` |
| `0D` | ptt_hang_ms | 50–3000 | 500 | `PTT_HANG_MS` |
| `0E` | stream_key | HID usage 0x04–0x73 | 0x68 (F13) | H-31; must differ from Ctrl |

New ids may be added later; the application must ignore unknown ids in GET_ALL and keep working.

### 5.4 Behaviour rules
- A SET is applied immediately in RAM (from the next element / next tone) and acknowledged. Out-of-range → NAK `03`, nothing changes.
- **Autosave:** 5 s after the last change (from MIDI **or** the KEY button) and only while the keyer is idle, the settings are
  written to flash (H-05). SAVE forces it now (still waiting for idle).
- Changes made with the KEY button are sent as `45` CHANGED.
- Settings are loaded at start-up; an invalid/missing record → defaults.
- **Factory reset without a PC:** hold KEY while plugging in USB → defaults, stored, announced in Morse ("R").
- The MIDI handling runs in the main loop with lower priority than paddle sampling, the keyer and the HID queue (C-5).

## 6. Settings storage (flash)

- **Sector 5, 0x08020000–0x0803FFFF (128 KB).** It exists on all supported chips (F401CC 256 KB, F401CD 384 KB,
  F401CE/F411CE 512 KB) and lies behind the application. Consequence: **the application must stay ≤ 64 KB** (sector 4,
  0x08010000–0x0801FFFF); the build must fail if it does not (linker `LD_MAX_SIZE`/`MEMORY` limit).
- TinyUF2 erases only the sectors it writes, so UF2 updates and the first installation (`-full.bin` ends in sector 4)
  **keep the settings**.
- Format: append-only records (e.g. 64 B: magic, layout version, sequence number, settings, CRC-32). The newest valid record
  wins. Erase the sector only when it is full (≈ 2000 saves per erase).
- An erase of a 128 KB sector blocks the CPU for ~1–2 s (single flash bank). Only when the keyer is idle and PTT is off;
  HID reports wait (no key changes happen meanwhile because nobody is keying). Writes of one record take < 1 ms.
- Never touch other sectors, never option bytes (C-3).

## 7. Entering the bootloader

TinyUF2 (our build, see [firmware-flashing.md](firmware-flashing.md)) checks a RAM word at start-up:

| Item | Value |
|---|---|
| address | **0x2000FFFC** (`_board_dfu_dbl_tap`, same for the F411 and F401 builds) |
| value "stay in bootloader" | **0xF01669EF** (`DBL_TAP_MAGIC`) |
| do not use | 0xF5E80AB4 (`DBL_TAP_MAGIC_ERASE_APP` – erases the application) |

Sequence for ENTER_BOOTLOADER:
1. Validate confirmation bytes (`55 46 32`), else NAK `07`. Keyer not idle or PTT on → NAK `04`.
2. Send ACK, wait ~20 ms so it leaves the device.
3. KEY and PTT LOW, sidetone off, stop paddle processing.
4. Disconnect USB (`tud_disconnect()`), wait ~50 ms.
5. Write 0xF01669EF to 0x2000FFFC (volatile 32-bit store), `__DSB()`, `NVIC_SystemReset()`.
6. TinyUF2 starts, sees the magic, clears it and shows the USB drive (`STMF411BOOT` / `BlackPill`).

The application then writes the `.uf2` as described in [firmware-flashing.md](firmware-flashing.md) section 5.3.

## 8. Application side (Web MIDI)

- `navigator.requestMIDIAccess({ sysex: true })` – needs a secure context (HTTPS) and a user permission prompt.
- Support: Chrome/Edge on PC and Chrome on Android – yes; Firefox on PC – yes, but SysEx access is gated by a site
  permission add-on prompt; Safari / iPhone / iPad – **no**. Without MIDI the application keeps working (keys only),
  settings stay on the KEY button.
- Find the port by name `CW Key` (input and output), send GET_INFO, then GET_ALL; listen for CHANGED.
- Timeout per request 500 ms, one retry; never retry SAVE or ENTER_BOOTLOADER automatically.
- The MIDI channel is unrelated to keyboard focus: settings work even when the trainer window is not focused.

## 9. Implementation steps and acceptance

| Step | Content | Acceptance |
|---|---|---|
| **S1** | New firmware project (CMake + HAL + TinyUSB) reproducing CwKey 0.3.0 behaviour exactly (keyer, filter, buzzer B8 + speaker A8, button, KEY/PTT, F411/F401 builds, UF2 offset). HID only first, then composite with an empty MIDI interface. | Same behaviour as 0.2.0; H-42 (1) with [tools/keytest.html](../tools/keytest.html): no lost/duplicated events, element jitter ≤ 1 ms; enumerates on Windows, Android, iPhone (keys still arrive). Binary ≤ 64 KB. |
| **S2** | SysEx parser, GET_INFO / GET / GET_ALL / SET in RAM, NAK handling. | Host unit tests of parser and validation; manual test page `tools/miditest.html` (new, Web MIDI) shows info and changes speed/tone live. |
| **S3** | Flash storage, autosave, SAVE, RESET_DEFAULTS, factory reset by button, CHANGED notifications. | Settings survive power-off and a UF2 update; 100 save cycles OK; keying during autosave not disturbed (logic analyser or keytest). |
| **S4** | ENTER_BOOTLOADER. | From `miditest.html`: drive appears within 2 s, KEY/PTT stay low throughout, copying a `.uf2` returns to the key. Refused while keying. |
| **S5** | `build.ps1` / `package.ps1` for the new build system (same 4 files + `latest/`), version 0.4.0, update [key-interface.md](key-interface.md) (USB identity, MIDI), [firmware-flashing.md](firmware-flashing.md) (bootloader entry from the app), wiki. | Package verified like 0.2.0 (addresses, stack pointer, SHA-256). |

## 10. Rules for working with the hardware (mandatory for the implementing session)

The author has lost two Black Pill boards to careless flashing before. Treat every board as the last one:
- **Read and back up before changing anything.** One attempt per write; on error stop and ask.
- **Never mass-erase, never write option bytes** (no `-e all`, no `-ob …`, no RDP/WRP changes). Sector erase only.
- Check the chip before flashing (STM32F411 Device ID 0x431, STM32F401 0x433) and use the image for that chip.
- UF2 copies only for images whose every block address is ≥ 0x08010000.
- Ask the user before every flash or publish step.

## 11. Open decisions

| # | Decision | Proposal |
|---|---|---|
| D-1 | USB stack / build system | TinyUSB + CubeF4 HAL + CMake (section 3) |
| D-2 | VID:PID | request at pid.codes |
| D-3 | Single-lever "last pressed wins" mode (keyer_mode 2) | later |
| D-4 | Should the app be able to disable the KEY button (lock)? | not in 0.3.0 |
