# CW Key hardware keyer: how it works and what it sends to the application

Document version: 3 (2026-11-12). Firmware: **CwKey 0.3.0** (repository `morse-key-hw`, `firmware/CwKey`).
For: application development (web trainer) against this HW. Describes the **actual state of the prototype**, not the target state.
The target behaviour is in the specification `specification.md` (version 2). H-xx/A-xx references point there. Deviations are in section 9.

## 1. Summary for the application

- The key is a **USB keyboard** (HID, no driver). The application reads it only via `keydown`/`keyup` in a focused window.
- Dits and dahs are formed by the **keyer in HW** with machine timing, and the HW plays the sidetone itself (piezo). The application **must not play
  sidetone** (A-04, A-12).
- **Mode B (default):** a single key **F13** is held down for the duration of each element (dit or dah). The application tells a dit
  from a dah by its length (A-11, A-13).
- **Mode C:** the same + **Left Ctrl = dit paddle, Right Ctrl = dah paddle** (raw paddle state, independent of the keyer).
- Speed, mode and other settings are **not sent** over USB. The application determines the speed from the dit length.
- There is no channel from the application to the HW (H-34 not yet). The key cannot be controlled or configured from the application.

## 2. Hardware (prototype)

- WeAct Black Pill STM32F411CEU6 board on a breadboard, USB-C to a PC or phone (OTG / adapter).
- Paddles: 3.5 mm stereo jack, **tip = dit, ring = dah, sleeve = ground**. Both iambic and single-lever keys work.
- Sidetone: active piezo buzzer on B8 (fixed tone ≈ 4 kHz) and/or, since 0.3.0, a speaker on A8 (700 Hz, volume and soft edges set
  in the firmware). Either way the application plays no sidetone.
- Since 0.2.0: KEY (B9) and PTT (B5) outputs for a transmitter, driven only by the local keyer. They do not change anything
  on USB. With a PTT lead time (`PTT_LEAD_MS`, default 0) the first element, including its USB stream key, starts that much
  later than the paddle press.
- KEY button on the board: speed and USB mode (section 5).
- Blue LED lights during each element.

## 3. USB identity

| Item | Value | Note |
|---|---|---|
| VID / PID | `0x0483` / `0x5740` | default values of the stm32duino core (ST). **Will change** (H-30, own PID). |
| Manufacturer / product | "STMicroelectronics" / board name + "HID in FS Mode" | default core strings. Target "CW Key" / "Morse" (H-30). |
| Serial number | derived from the chip's unique ID | no personal data |
| Interface | HID boot keyboard + HID mouse | **the mouse never sends anything** (core artefact) |
| Polling | interrupt IN, `bInterval` = 1 ms, full-speed | |
| Keyboard report | standard 8 B boot report: [modifiers, 0, key, 0, 0, 0, 0, 0] | the output report (keyboard LEDs) is ignored by the HW |

The application **must not identify the device by VID/PID or name**. In a browser this is not possible without WebHID anyway, and the values
will still change. What counts is which keys arrive (A-03, mapping learning).

## 4. Keyboard events

### 4.1 Keys

| Meaning | HID usage | Expected `event.code` | Expected `event.key` | Mode |
|---|---|---|---|---|
| **Stream** (tone on/off) | 0x68 (F13) | `F13` | `F13` | B and C |
| Dit paddle | modifier Left Ctrl (bit 0x01) | `ControlLeft` | `Control` | C only |
| Dah paddle | modifier Right Ctrl (bit 0x10) | `ControlRight` | `Control` | C only |

- **F13 is a provisional choice.** Open question H-31: behaviour on iPhone (Safari), macOS, Linux and Android is **not verified**.
  On Linux the system may remap F13 (e.g. XF86Tools), so `key`/`code` may differ there. **The application must therefore not hard-code
  F13.** It should have stream key learning (A-03: "press the stream key" → store `event.code`), with
  `F13` as the default.
- Distinguish keys by **`event.code`**, not by `event.key` (CZ/EN layout; `key` for Ctrl is the same for both sides).
- Nothing other than these three keys ever appears in the output. Neither the board button nor the piezo announcements send anything over USB.

### 4.2 Mode B: stream (default after power-up)

- `keydown F13` = start of element (tone on), `keyup F13` = end of element.
- Each element = exactly one keydown/keyup pair. The HW does not send autorepeat. An element lasts at most 180 ms (dah at 20 WPM), which
  is shorter than the usual OS autorepeat delay. Still, ignore `event.repeat === true` (A-02).
- Gaps between elements within a character are machine-timed (1 dit). Gaps between characters and words are made by the operator (the keyer
  is idle in the meantime), so they are arbitrary.

### 4.3 Mode C: stream + paddle state

- The F13 stream behaves **the same as in mode B**.
- In addition, the Ctrl keys mirror the **physical paddle state**: `keydown ControlLeft` when the dit paddle closes, `keyup` when it is released. The same for
  dah and `ControlRight`. Squeezing both paddles = both Ctrl keys held at the same time.
- The paddle state is filtered against contact bounce and interference (press about 1 ms and release about 3 ms after the contact, a new press no earlier than 8 ms after release) and is **independent of the keyer**. A paddle may be
  pressed for a shorter time than an element, or held across several elements.
- Because Ctrl and F13 are held at the same time, `keydown F13` arrives with `ctrlKey === true`. If the stream and a paddle change state in the same
  millisecond, both events arrive with the same or nearly the same `timeStamp` and **their order is not guaranteed**.
- Ctrl held longer (than the autorepeat delay, typically 250–500 ms) → Windows sends repeated `keydown` with `repeat=true`.
  Ignore them.
- Compatibility: L-Ctrl/R-Ctrl matches the Vail adapter, VBand and morsecode.world (H-32). These ignore the F13 key.

### 4.4 What the application does not receive

- No information about the mode (B/C) or the speed. Mode C is recognised by Ctrl events arriving.
- No events when the board button is pressed or during piezo announcements (speed, mode).
- After USB connection or reset the HW sends nothing until a paddle closes (only "all released" is sent).

## 5. Controls on the HW (user, not application)

| Action | Result | Confirmation |
|---|---|---|
| short press of KEY | speed 20 → 30 → 40 → 50 → 60 → 20 WPM | piezo announces the number in Morse, at the new speed |
| long press of KEY (1 s) | USB mode B ↔ C | piezo "B" / "C" |
| after power-up | **20 WPM, mode B** | settings are **not remembered** yet (section 9) |

## 6. Timing

### 6.1 Element lengths (PARIS: dit = 1200 / WPM ms)

| WPM | dit | dah (3×) | intra-character gap |
|---|---|---|---|
| 20 | 60 ms | 180 ms | 60 ms |
| 30 | 40 ms | 120 ms | 40 ms |
| 40 | 30 ms | 90 ms | 30 ms |
| 50 | 24 ms | 72 ms | 24 ms |
| 60 | 20 ms | 60 ms | 20 ms |

The dah:dit ratio is exactly 3:1 and the weight is 50 %. These settings cannot be changed yet.

### 6.2 Accuracy and what it means for the decoder
- The HW times with 1 µs resolution, without drift.
- USB transfers in 1 ms frames → the length measured in the browser varies by **±1 ms** (H-33: jitter ≤ 1 ms). On top of that comes the
  browser's `event.timeStamp` resolution (Firefox and Safari round it, typically to 1 ms) and OS scheduling.
- Recommendation for the decoder (A-13): tempo = median of the shorter length cluster (dits). Dit/dah threshold = 2 × dit.
  At 60 WPM a dit is 20 ms, so ±1–2 ms is up to 10 %, but the threshold at 2× has a 100 % margin.
- The browser uses `event.timeStamp` (monotonic, A-02). Do not rely on `Date.now()`.

### 6.3 Examples (mode B, 20 WPM, times relative to the first keydown)
- Letter **A** (·−): down 0, up 60, down 120, up 300.
- Letter **N** (−·): down 0, up 180, down 240, up 300.
- Squeeze of both paddles (iambic) with dit first: alternates dit, dah, dit… each with a 60 ms gap.

## 7. Keyer (behaviour the application will see)

- **Iambic B** with element memory (since 0.1.1: the paddles are not read in the gap after an element, the next element is decided only at its end, so a short press always gives just one dit): the opposite paddle pressed during an element is remembered and sent after it. When both paddles are released
  during a squeeze, iambic B adds one more opposite element.
- A single-lever key works on the same inputs (elements repeat while the paddle is held).
- **Pass-through mode for a straight key (H-02) does not exist yet.** A straight key plugged into the jack behaves like the dit paddle (when held it
  generates a series of dits), **not** as tone on/off. The "straight key" type in the application therefore makes no sense with this HW.

## 8. Recommendations for the application

1. Source type "HW keyer (USB)" (A-04): listen to the stream key (default `F13`, learnable), turn off sidetone in the application,
   measure tempo from the data.
2. Store the paddle state (C) alongside the stream if `ControlLeft`/`ControlRight` arrive. **Do not run a keyer in the application**;
   the resulting keying is the stream only.
3. Ignore `event.repeat`. On focus loss or page hide, release everything (A-02). Show the user that the window must
   have focus, because keys only go there.
4. Call `preventDefault()` on the stream key and on Ctrl, so the browser does not trigger shortcuts.
5. Event log with times (spec section 5, task 1, acceptance H-42 (1)). For comparison, the standalone page
   `tools/keytest.html` from the HW repository can be used: it logs `key`/`code`/`timeStamp`/`repeat`, computes lengths and detects lost
   or duplicated events.

## 9. Prototype deviations from specification.md v2

| Requirement | Prototype state |
|---|---|
| H-02 pass-through mode for a straight key | **missing** |
| H-03 speed 5–40 WPM, ratio 2.8–3.5, weight, paddle swap | only 20/30/40/50/60 WPM, ratio 3:1, weight 50 %, no swap |
| H-05 settings remembered after power-off | **no**, after power-up always 20 WPM and mode B |
| H-10/H-11/H-12 headphones, mix, adjustable tone | active buzzer ≈ 4 kHz (B8) and speaker output 700 Hz (A8, set in firmware, not from the app); no headphones |
| H-20–H-22 KEY/PTT | KEY and PTT via plain NPN transistors (not isolated), PTT hang 500 ms; no training/transmit switch yet |
| H-30 name "CW Key", own VID/PID | default core identity (section 3), plus a mouse interface |
| H-31 stream key | F13 **provisional**, not verified on iPhone |
| H-34 MIDI | no |
| H-42 acceptance | only a functional test so far. Latency and jitter are not measured. |

What may change in future firmware versions, and the application should be ready for it: the stream key, VID/PID and name,
speed range, a pass-through mode will be added (the stream will then be the straight key's tone on/off directly, lengths will no longer be machine-timed).
