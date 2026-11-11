# CW Key firmware flashing: first installation (DFU) and update (UF2)

Document version: 4 (2026-11-12). Firmware: **CwKey 0.3.0**. Bootloader: **TinyUF2 0.36.0-morse-key-hw-nowrp**.
For: application development (web trainer) and user instructions. Describes (1) module wiring, (2) first installation of a new
module from an e-shop via the factory (ROM) USB DFU, (3) updates via the UF2 USB drive and (4) how to do both from the web application.
The key interface (keys, timing) is in [key-interface.md](key-interface.md).

## 1. Summary

- Module: **WeAct Black Pill**, two chip variants: **STM32F411CE** and **STM32F401CC/CD/CE**. Each needs **its own image**.
  The F411 image does not run on the F401: the stack is at 0x20020000, outside the F401's 96 kB RAM.
- **First installation (once, always on a computer):** the factory ROM bootloader (USB DFU, VID:PID `0483:DF11`) writes the
  **combined image** `…-full.bin` from 0x08000000: our TinyUF2 bootloader + CwKey. The recommended tool is
  **STM32CubeProgrammer** (section 4.2), on macOS/Linux also `dfu-util`. A phone is not sufficient for first installation.
- **Update (always afterwards):** 2× quick NRST → the module appears as a **USB drive** → copy `…​.uf2`. The module restarts by itself.
- **One firmware for both sidetone parts** (since 0.3.0): an **active buzzer on PB8** (fixed pitch) and a
  **speaker / passive transducer on PA8** via an NPN transistor (pitch, volume, soft edges) are driven at the same time.
  The user connects the part they have; there is nothing to choose when flashing.
- **4 files per release:** for each chip (F411, F401) `…-full.bin` (first installation) and `….uf2` (update).
- **Option bytes are never changed.** The official TinyUF2 for STM32F4 writes WRP protection, so only our build
  with `TINYUF2_PROTECT_BOOTLOADER=0` is used. Its binary does not contain the keys for unlocking the option bytes.

## 2. Firmware package (absolute links)

Repository: <https://github.com/OK2MTW/morse-key-hw> (public). Every release is published three ways:

| Where | Use | CORS (`fetch()` from a web app) |
|---|---|---|
| **`firmware/latest/`** – stable file names, always the newest release | **permanent links for the web application** | **yes** (`raw.githubusercontent.com`) |
| `firmware/dist/cwkey-<version>/` – versioned file names, kept for every release | a specific older version | yes |
| [GitHub Releases](https://github.com/OK2MTW/morse-key-hw/releases) – tag `v<version>`, the same 4 files + manifest | people (download page, release notes) | **no** (redirects without CORS) |

### 2.1 Permanent links (latest release)

Base URL: `https://raw.githubusercontent.com/OK2MTW/morse-key-hw/main/firmware/latest/`

| File | Purpose | Written via |
|---|---|---|
| [`manifest.json`](https://raw.githubusercontent.com/OK2MTW/morse-key-hw/main/firmware/latest/manifest.json) | version, sizes, SHA-256, board identification | – |
| [`cwkey-f411-full.bin`](https://raw.githubusercontent.com/OK2MTW/morse-key-hw/main/firmware/latest/cwkey-f411-full.bin) | first installation, F411 (bootloader + firmware) | ROM DFU, address **0x08000000** |
| [`cwkey-f401-full.bin`](https://raw.githubusercontent.com/OK2MTW/morse-key-hw/main/firmware/latest/cwkey-f401-full.bin) | first installation, F401 (bootloader + firmware) | ROM DFU, address **0x08000000** |
| [`cwkey-f411.uf2`](https://raw.githubusercontent.com/OK2MTW/morse-key-hw/main/firmware/latest/cwkey-f411.uf2) | update, F411 | copy to drive `STMF411BOOT` |
| [`cwkey-f401.uf2`](https://raw.githubusercontent.com/OK2MTW/morse-key-hw/main/firmware/latest/cwkey-f401.uf2) | update, F401 | copy to drive `BlackPill` |
| [`LICENSE-tinyuf2.txt`](https://raw.githubusercontent.com/OK2MTW/morse-key-hw/main/firmware/latest/LICENSE-tinyuf2.txt) | bootloader licence (MIT) | – |

The UF2 images start at 0x08010000 (behind the bootloader). Versioned copies of the same files:
`firmware/dist/cwkey-<version>/cwkey-<version>-<mcu>-full.bin` and `cwkey-<version>-<mcu>.uf2`.

Bootloaders alone (for recovery via ST-Link):
<https://raw.githubusercontent.com/OK2MTW/morse-key-hw/main/firmware/bootloader/tinyuf2-0.36.0-nowrp-stm32f411ce_blackpill.bin>,
<https://raw.githubusercontent.com/OK2MTW/morse-key-hw/main/firmware/bootloader/tinyuf2-0.36.0-nowrp-stm32f401_blackpill.bin>.

The package is created by [`firmware/package.ps1`](https://github.com/OK2MTW/morse-key-hw/blob/main/firmware/package.ps1)
(writes both `dist/cwkey-<version>/` and `latest/`). Sizes and SHA-256 are always in `manifest.json`, so they are not copied here.

For the application:
- **Download `latest/manifest.json` first**, read `version`, then the files it names (same directory). Do not hard-code
  sizes or hashes. Always verify each downloaded file against its SHA-256 from the manifest before writing.
- `raw.githubusercontent.com` sends `Access-Control-Allow-Origin: *` (verified 2026-11-11: HTTP 200, header present,
  SHA-256 of the downloaded file matches the manifest). GitHub may cache `main` for a few minutes after a release.
- For production it is still more robust to copy the files into the application's own static files (same origin, version under control).
- `manifest.json` is stored byte-exact (LF, no BOM), so its own hash is stable across platforms.

### 2.2 manifest.json (structure, since 0.3.0)
```json
{
  "name": "CwKey", "version": "0.3.0", "uf2_family_id": "0x57755a57",
  "bootloader": "TinyUF2 0.36.0-morse-key-hw-nowrp (...)",
  "outputs": { "buzzer": "PB8: ...", "speaker": "PA8: ...", "key": "PB9: ...", "ptt": "PB5: ..." },
  "targets": [
    { "mcu": "STM32F411CE",
      "full": { "file": "cwkey-f411-full.bin", "size": …, "sha256": "…", "base": "0x08000000" },
      "uf2":  { "file": "cwkey-f411.uf2", "size": …, "sha256": "…" },
      "uf2_app_base": "0x08010000",
      "bootloader_drive": { "volume_label": "STMF411BOOT", "info_model": "ST STM32F411 BlackPill",
                            "info_board_id": "STM32F411-BlackPill-rev2", "usb_vid_pid": "239A:0069" },
      "rom_dfu": { "usb_vid_pid": "0483:DF11", "bootloader_id": "0xD0", "flash_layout": "…03*128Kg" } },
    { "mcu": "STM32F401CC/CD/CE", … "volume_label": "BlackPill", "info_model": "STM32 STM32F401CxUx",
      "info_board_id": "BlackPill", "usb_vid_pid": "239A:005D", … "bootloader_id": "0xD1" }
  ]
}
```
In `firmware/latest/manifest.json` the file names are the stable ones (`cwkey-f411-…`), in `dist/cwkey-<version>/`
they carry the version (`cwkey-0.3.0-f411-…`). `uf2` is a single file per chip (as in 0.1.x); the short-lived 0.2.0
format with separate buzzer/speaker files was never released.

### 2.3 Flash layout after first installation
| Address | Sector | Content |
|---|---|---|
| 0x08000000–0x08003FFF | 0 (16 kB) | TinyUF2 (≈ 15.8 kB) |
| 0x08004000–0x0800FFFF | 1–3 (3 × 16 kB) | bootloader reserve (empty) |
| 0x08010000– | 4 (64 kB), 5… (128 kB) | CwKey (≈ 25 kB) |

## 3. Module and wiring (user instructions)

### 3.1 How to identify the variant
- **Marking on the chip:** `STM32F411CEU6` = F411, `STM32F401CCU6` / `STM32F401CDU6` / `STM32F401CEU6` = F401.
  The marking on the board ("WeAct Black Pill") does not tell the variant.
- After installation by the bootloader drive name: `STMF411BOOT` = F411, `BlackPill` = F401.

### 3.2 Board elements
| Element | Function |
|---|---|
| USB-C connector | power and data (PC, phone via OTG, power bank) |
| **NRST** button | reset. **2× quickly** (within ~0.5 s) = UF2 bootloader (USB drive). |
| **BOOT0** button | only for first installation: hold BOOT0, press and release NRST, release BOOT0 = factory (ROM) DFU |
| **KEY** button (PA0) | key control: short = speed 20 → 30 → 40 → 50 → 60 WPM, long (1 s) = mode B/C |
| blue LED (PC13) | after start 1 long blink = OK (3 short = crystal did not start), then lights on every dit/dah |

### 3.3 Pins (since 0.3.0, same on F411 and F401)

| Pin | Direction | Function |
|---|---|---|
| **B6** | input | dit paddle (jack tip), contact to GND, internal pull-up |
| **B7** | input | dah paddle (jack ring), contact to GND, internal pull-up |
| **B8** | output | sidetone for an **active buzzer**: HIGH while the tone sounds |
| **A8** | output | sidetone for a **speaker / passive transducer**: PWM tone to an NPN transistor (both B8 and A8 always run) |
| **B9** | output | **KEY** to the transmitter: HIGH while a dit/dah is sent, drives an NPN transistor |
| **B5** | output | **PTT** to the transmitter: HIGH from the first element until **500 ms** after the last one, drives an NPN transistor |
| G | – | ground |

### 3.4 Wiring the key and buzzer

Find the pins by the labels next to the headers/pins on the board (`B5`…`B9`, `G`/`GND`). They are the same on both
variants. B5 to B9 are in one row next to each other (together with 5V, GND and 3V3).

```
  KEY (paddles), 3.5 mm stereo jack                BLACK PILL
  ┌───────────────┐
  │ tip (TIP)     ├──── dit ───────────────────────► B6  (PB6)
  │ ring (RING)   ├──── dah ───────────────────────► B7  (PB7)
  │ sleeve (SLV)  ├──── ground ────────────────────► G   (GND)
  └───────────────┘

  BUZZER (active piezo, e.g. 32SC4120LF, ~4 kHz)
     (+) ──────────────────────────────────────────► B8  (PB8)
     (−) ──────────────────────────────────────────► G   (GND)
```

- **Key:** the paddles switch the input to ground, the voltage from the module is 3.3 V and the contact current < 1 mA. Iambic and
  single-lever keys use the same wiring. Swapped dits and dahs = swapped tip and ring (or turn the key around).
- **Buzzer:** **active** (with its own oscillator, beeps by itself on DC voltage). **Polarity matters**, + to B8.
  Wired like this it is quieter (3.3 V). Louder: + to **5V**, − via an NPN transistor (BC547/2N2222, base via 2.2 kΩ from B8,
  emitter to GND). **Quieter:** a 100–470 Ω resistor in series with the buzzer (PWM does not work with an active buzzer, see section 9). A passive piezo / transducer / speaker (without oscillator) goes to **A8** via a transistor, see 3.5.
- **Recommendation against interference** (longer cable to the key, close to the transmitter): external 4.7 kΩ pull-up from B6 and B7 to 3V3, a 1 kΩ series
  resistor and a 10–47 nF capacitor between the pin and GND. The firmware filters contact bounce itself; this helps against RF spikes.
- **Do not use:** PA11/PA12 (USB), PA13/PA14 (SWD), PB2 (BOOT1), PA0 (KEY button), PC13 (LED).

### 3.5 Speaker instead of the buzzer (output A8)

The speaker output gives an adjustable pitch (default 700 Hz), volume and soft edges (no clicks, H-12). It needs a
**passive** sound device and a transistor, because a GPIO pin cannot drive a coil directly.

Recommended part (mechanically close to the 12 mm buzzer): **passive electromagnetic transducer Ø 12 mm**, 2 pins with
7.6 mm pitch, coil 16–42 Ω, rated 3–5 V, resonance ~2.3–2.7 kHz (e.g. Same Sky/CUI CEM-1203(42), or the generic
"12085 passive 16 Ω" type; check the datasheet). For a fuller, lower tone: a **mini speaker Ø 20–28 mm, 8 Ω**, with a
33–47 Ω series resistor.

```
   5V ───┬────────────┐
         │            │
     (+) TRANSDUCER   1N4148 (cathode to 5V)
     (−) │            │
         ├────────────┘
         │  [10–47 Ω, optional, limits the coil current]
         │
         └──────── collector
                        NPN 2N2222 / S8050 (BC547 only with the 12 mm transducer, not with an 8 Ω speaker)
   A8 ──[1k]──┬──── base
              │
           [10k]    keeps the transistor off while A8 floats (reset, bootloader)
              │
   G  ────────┴──── emitter
```

- Add a **10 kΩ resistor from the base to GND**, so the transistor stays off while A8 floats (reset, bootloader).
- The diode is required (coil). A passive transducer must not beep on DC: if it beeps by itself, it is an active buzzer.
- Pitch, volume and edge time are constants in the firmware (`SPEAKER_TONE_HZ`, `SPEAKER_VOLUME_PCT`, `SPEAKER_RAMP_US`);
  later they will be settable from the application (open item F-10).

### 3.6 KEY and PTT outputs for a transmitter (since 0.2.0)

Each output drives an NPN transistor that switches the transmitter's key / PTT line to ground (open collector, modern rigs):

```
                          collector ─────► rig KEY (jack tip), or PTT for B5
   B9 ──[2.2k]──┬── base  NPN 2N2222 / BC547
                │         emitter ───────► rig ground (jack sleeve) and G
             [47k]
                │
   G  ──────────┘
```

- The **47 kΩ base pull-down is required**: during reset and in the bootloader the pins float, without it the rig could key.
- Timing: KEY follows every dit/dah exactly. PTT closes with the first element and opens **500 ms after the last element**
  (`PTT_HANG_MS`). A PTT lead time (`PTT_LEAD_MS`, default 0) can delay the first element after PTT closes.
- **Not galvanically isolated:** the computer's USB ground is connected to the rig ground. Spec H-20 asks for an
  optocoupler; this is a simple first version for rigs with a low-voltage, negative (to ground) key line. Check the polarity and
  voltage of your rig's key/PTT input (≤ 12 V, a few mA).
- Spec H-21 (training/transmit switch, KEY/PTT only from the local paddles): the outputs are driven only by the local keyer,
  nothing from USB can key them. The physical training/transmit switch is not implemented yet: unplug the rig cable for training.

## 4. First installation of a new module (factory (ROM) DFU)

**First installation is always done on a computer** (Windows, macOS or Linux), once for each new module. A phone or
tablet is not sufficient: iPhone/iPad cannot do DFU at all, and on Android it would be unreliable and hard to describe. Users
usually do not know DFU tools, so the instructions in the application guide them step by step with the tool from section 4.2. After first
installation a computer is no longer needed; UF2 updates (section 5) also work elsewhere.

### 4.1 User procedure
1. Install the tool on the computer (section 4.2), first time only, and download the correct `…-full.bin` file according to the chip marking
   (section 3.1).
2. Connect the module to the computer with a USB cable.
3. **DFU:** hold **BOOT0**, briefly press **NRST**, release **BOOT0**. The module appears as "STM32 BOOTLOADER"
   (`0483:DF11`), the LED is off.
4. Write `cwkey-f411-full.bin` or `cwkey-f401-full.bin` to address **0x08000000** with the tool (section 4.2).
5. Press **NRST** (BOOT0 released). The LED blinks long once and the module appears as a keyboard. Done.
   Further updates only via UF2 (section 5).

**Known problems of the factory DFU on the Black Pill:** especially on the F411 the ROM bootloader sometimes "hangs" (state `dfuDNBUSY`, write
error). Solution: disconnect USB, repeat step 2, **one** more attempt. If it fails again, the module cannot do DFU reliably and
first installation is only possible via ST-Link (section 7). Of the units we had, this happened 1× out of 3.

### 4.2 Recommended tool

| System | Recommendation | Why |
|---|---|---|
| **Windows** | **STM32CubeProgrammer** (GUI) | official tool from the chip manufacturer, free, **installs the DFU driver itself**. Other ways (dfu-util, WebUSB) need a manual driver replacement with Zadig on Windows, which is unnecessarily risky for an ordinary user. |
| **macOS** | STM32CubeProgrammer, or `dfu-util` (`brew install dfu-util`) | dfu-util is small and needs no driver |
| **Linux** | `dfu-util` (`sudo apt install dfu-util`), or STM32CubeProgrammer | ditto. Without root a udev rule for `0483:df11` is needed. |

**STM32CubeProgrammer: installation**
- Download from the manufacturer's page: <https://www.st.com/en/development-tools/stm32cubeprog.html> → "Get Software" → accept the
  licence. ST requires an e-mail (download via a link in the e-mail) or a myST account. The installer is hundreds of MB. On Windows leave
  the driver installation (USB DFU / ST-Link) checked.
- The instructions in the application should warn about these steps in advance (registration, size), so the user is not surprised.

**STM32CubeProgrammer: flashing (GUI, version 2.x)**
1. Module in DFU (section 4.1, step 3).
2. In the top right, in the connection type drop-down, select **USB** (instead of ST-LINK), refresh button (arrows) →
   Port **USB1** → **Connect**. Chip information appears on the right (Device: STM32F411xC/E or STM32F401xD/E).
   The instructions may ask the user to compare the chip name with the downloaded file (F411 × F401).
3. On the left the **Erasing & Programming** icon (arrow down into the chip).
4. **File path** → Browse → `cwkey-…-full.bin`. **Start address: `0x08000000`**.
5. Check **Verify programming**. Leave **Skip flash erase before programming** and **Run after programming**
   unchecked.
6. **Start Programming** → messages "File download complete" and "Download verified successfully".
7. **Disconnect** (top right), then press **NRST** on the module.

**What not to do in the tool** (the instructions must say this explicitly): do not press **Full chip erase**, do not open or change the
**OB (Option bytes)** tab, do not set read protection (RDP). None of this is needed, and wrong option byte settings can
lock the module.

**Command line** (for advanced users, same result):
- STM32CubeProgrammer: `STM32_Programmer_CLI -c port=USB1 -w cwkey-f411-full.bin 0x08000000 -v`
  (without `-e all`, without `-ob`, without `-rdu`). After writing press NRST.
- dfu-util: `dfu-util -a 0 -d 0483:df11 -s 0x08000000 -D cwkey-f411-full.bin` (without `:leave`, without
  `:mass-erase`). For F401 the file `cwkey-f401-full.bin`.

### 4.3 Optional: first installation directly from the web application (WebUSB + DfuSe)
Not required; the recommended way is the tool from 4.2. It makes sense as a more convenient option for **macOS, Linux and ChromeOS**
in Chrome/Edge. On Windows only if the WinUSB driver is already installed for DFU; otherwise the application sends the user to
STM32CubeProgrammer. Not Firefox, Safari or mobile devices.
- **Windows:** WebUSB needs the **WinUSB** driver for `0483:DF11`. With the ST driver (from STM32CubeProgrammer) or without a
  driver the browser does not open the device. The application should detect this (`requestDevice` does not offer the device or `open()` fails)
  and send the user to STM32CubeProgrammer (4.2). Do not recommend Zadig to users. **Verify on the prototype.**
- **Linux:** udev rule for `0483:df11` (access without root). **macOS, ChromeOS:** no driver.

**Protocol (DfuSe = DFU 1.1 + ST extensions, AN3156):**
1. `navigator.usb.requestDevice({ filters: [{ vendorId: 0x0483, productId: 0xdf11 }] })`, `open()`,
   `selectConfiguration(1)`, `claimInterface(0)`, `selectAlternateInterface(0, 0)`.
2. **Only alt 0 "Internal Flash".** **Never use** the alts "Option Bytes", "OTP Memory" or "Device Feature".
3. Read the name of alt 0 (interface string descriptor): e.g. `@Internal Flash  /0x08000000/04*016Kg,01*064Kg,03*128Kg`.
   `wTransferSize` from the DFU functional descriptor (2048 on STM32F4).
4. **Chip detection** (image selection):
   - number of 128 kB sectors 1 → F401CC (256 kB), 2 → F401CD (384 kB): **F401**,
   - 3 (512 kB) → F411CE **or** F401CE (same layout). Distinguished by the bootloader ID: the byte at **0x1FFF76DE**
     (`0xD0` = F411, `0xD1` = F401). Read with the DfuSe commands *Set Address Pointer* + `UPLOAD`. **Verify reading system memory
     via DFU on the prototype.** If it does not work, let the user choose by the chip marking (3.1).
5. **Erase:** for each sector touched by the image (image ≈ 91 kB → sectors 0–4: 0x08000000, 0x08004000,
   0x08008000, 0x0800C000, 0x08010000): `DNLOAD` block 0, data `[0x41, address LE32]`, then `GETSTATUS`, wait
   `bwPollTimeout`, repeat until state `dfuDNLOAD-IDLE` (5). **Never mass erase** (`0x41` without address) or
   *Read Unprotect* (`0x92`).
6. **Write:** *Set Address Pointer* `DNLOAD` block 0 `[0x21, 0x08000000 LE32]` + `GETSTATUS`. Then `DNLOAD` blocks
   `wBlockNum = 2, 3, …` of `wTransferSize` bytes: address = pointer + (wBlockNum − 2) × wTransferSize. After each
   block `GETSTATUS` until `dfuDNLOAD-IDLE`.
7. **Verify:** `ABORT` → *Set Address Pointer* 0x08000000 → `ABORT` → `UPLOAD` blocks 2, 3, … and compare with the image
   (or SHA-256 from the manifest). On mismatch report an error, **do not retry automatically**, offer one manual attempt.
8. **Finish:** do not use "leave" (jump to the application via DFU). The user presses NRST. Start-up thus always happens
   via a clean reset.
9. Errors: state `dfuERROR` or stuck in `dfuDNBUSY` → close the device, the user disconnects and switches to DFU again
   (4.1, step 2).

## 5. Firmware update (UF2)

### 5.1 User procedure
1. Module connected to a computer (or phone, see 5.3).
2. **Press NRST 2× quickly** (within ~0.5 s). A USB drive `STMF411BOOT` (F411) or `BlackPill` (F401) appears.
   If the key starts instead, the second press was too late; try again.
3. Copy the **correct** file to the drive: `cwkey-f411.uf2` to `STMF411BOOT`, `cwkey-f401.uf2` to `BlackPill`.
4. The module writes the file, restarts by itself (the drive disappears, the system may report "drive removed without ejecting", that is fine)
   and appears as a keyboard.

### 5.2 Bootloader drive
| File | Content |
|---|---|
| `INFO_UF2.TXT` | e.g. `TinyUF2 Bootloader 0.36.0-morse-key-hw-nowrp` / `Model: ST STM32F411 BlackPill` / `Board-ID: STM32F411-BlackPill-rev2` / `Flash Size: 0x00080000 bytes`. F401: `Model: STM32 STM32F401CxUx`, `Board-ID: BlackPill`, `Flash Size: 0x00040000 bytes` |
| `CURRENT.UF2` | current firmware (read only) |
| `INDEX.HTM` | link to the bootloader vendor |

Bootloader USB identity: F411 `239A:0069`, F401 `239A:005D` (mass storage).

### 5.3 Update from the web application
| Platform | How |
|---|---|
| **Chrome/Edge on a computer** | File System Access API: `showDirectoryPicker()` → the user selects the bootloader drive → the application reads `INFO_UF2.TXT`, selects the image from the manifest by `Model`/`Board-ID` → `getFileHandle('cwkey-….uf2', {create:true})` → `createWritable()` → write the whole file → `close()`. **Verify on the prototype** whether Chrome allows selecting the root of a USB drive. Fallback: `showSaveFilePicker()` (without reading INFO_UF2.TXT, the user selects the type). |
| Firefox, Safari (macOS) | the application offers the file for download and the user drags it onto the drive |
| **Android** (Chrome) | download + copy in the Files app to the USB drive (OTG). Support for the bootloader's FAT16 drive **to be verified**. |
| **iPhone/iPad** | download + Files app. Whether iOS mounts the bootloader's FAT16 drive **to be verified**. Otherwise update from a computer. |

**Mandatory checks in the application before writing:**
1. The file matches the chip according to `INFO_UF2.TXT` (or the user's choice) and the SHA-256 from `manifest.json` matches.
2. **Every UF2 block** (512 B, magic `0x0A324655`/`0x9E5D5157`, family `0x57755a57`) has a target address
   **≥ 0x08010000**. TinyUF2 writes a block anywhere. It does refuse to erase sector 0, but writing into it would damage the
   bootloader, and sectors 1–3 are not protected at all. The application therefore offers only files from our manifest.
3. After writing: `close()` may end with an error, because the drive disconnects during the write (the module restarts). Detect success
   by the drive disappearing and the key starting to send keys. Not by the return value.

### 5.4 What if a wrong image is flashed
The bootloader stays intact (if the checks in 5.3 were followed). An image for a different chip is written but does not start:
the LED does not blink, no keyboard appears. **Solution:** 2× NRST → drive → correct file.

## 6. Verification after flashing
- LED: 1 long blink after start (3 short = crystal did not start, USB may be unreliable).
- System: new HID keyboard + mouse, VID:PID **0483:5740** (default identity for now, will change, H-30).
- Paddles: the LED blinks with the elements, the buzzer beeps, the stream key (F13) arrives in the application.
- KEY button: speed/mode announcement in Morse on the buzzer.

## 7. Recovery
- **Bootloader damaged / drive disappeared:** the factory (ROM) DFU (BOOT0 + NRST) always works, because it is in the chip ROM. Write
  `…-full.bin` again (section 4).
- **Factory DFU does not work on a unit:** ST-Link (SWD: 3V3, SWDIO, SWCLK, GND at the end of the board):
  `STM32_Programmer_CLI -c port=SWD freq=1000 mode=normal -e "[0 4]" -w <…-full.bin> 0x08000000 -v -rst`
  (erase only sectors, never `-e all`, never option bytes).

## 8. Open items
| # | Item | Status |
|---|---|---|
| F-1 | Entering the bootloader with the KEY button (hold while connecting), more convenient than 2× NRST | proposed, not in firmware |
| F-2 | WebUSB DFU on Windows (WinUSB driver). The recommended way for Windows is STM32CubeProgrammer. | to verify, optional |
| F-9 | Walk through the STM32CubeProgrammer (GUI) instructions with a new module and add screenshots | waiting for a new module |
| F-3 | Reading the bootloader ID 0x1FFF76DE via DFU (F401CE × F411CE) | to verify |
| F-4 | Chrome `showDirectoryPicker` on the root of a USB drive | to verify |
| F-5 | Android/iOS: mounting the TinyUF2 FAT16 drive | to verify |
| F-6 | One universal image (the F401 version should also run on the F411: 84 MHz, 96 kB RAM) | to try, would simplify image selection |
| F-7 | Combined image via factory (ROM) DFU on a new module | **not tested**, waiting for a new module |
| F-8 | Firmware version is not readable from the application (only in the file name and in `CURRENT.UF2`) | later via the settings channel (F-10) |
| F-10 | Settings channel from the application (speed, pitch, volume, PTT times, jump to the bootloader) | approved: composite USB HID + MIDI, spec [midi-settings-spec.md](midi-settings-spec.md), target 0.3.0 |
| F-11 | KEY/PTT: galvanic isolation (H-20) and training/transmit switch (H-21) | not yet, first version uses plain NPN transistors |
| F-12 | Speaker output (A8): listening test with a real transducer / speaker, tune `SPEAKER_*` | postponed, waiting for the part |

## 9. Firmware version history
| Version | Date | Changes |
|---|---|---|
| 0.3.0 | 2026-11-12 | **One firmware for both sidetone parts:** active buzzer on B8 and speaker/transducer on A8 (PWM, NPN) driven together; release package back to 4 files (`full.bin` and `.uf2` per chip), manifest `uf2` is a single file again. Includes everything from 0.2.0. |
| 0.2.0 | 2026-11-12 | (not released separately, replaced by 0.3.0 the same day) Transmitter outputs: KEY on B9, PTT on B5 (NPN transistors), PTT hang time 500 ms after the last element, optional PTT lead. New **speaker** build: square-wave tone 700 Hz with volume (duty cycle) and 4 ms soft edges on B8 via an NPN transistor. Release package has 6 files (`full.bin`, `buzzer.uf2`, `speaker.uf2` per chip), stable links in `firmware/latest/`, manifest `uf2` split into `buzzer`/`speaker`. The failed buzzer-PWM experiment was removed. |
| 0.1.1 | 2026-11-11 | Keyer: the paddles are not read in the gap after an element, so a short press always gives just one dit (previously sometimes two). Buzzer: full drive. The PWM test for lower volume with an active buzzer failed (≤ 90 % does not sound, 95 % almost unchanged); volume can be reduced with a series resistor if needed. |
| 0.1.0 | 2026-11-10 | First package: TinyUF2 + CwKey for F411 and F401. Not published, superseded by version 0.1.1. |
