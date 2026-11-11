# Specification: telegraph key (VK) – HW keyer with USB and the application (version 2, 2026-11-08)

Purpose: **requirements for the HW and firmware development** (a separate project, vibecoding) and for the sending part of the application.
The specification does not prescribe a particular microcontroller or board – only behaviour and interfaces; the choice is the first step of the HW development.
Version 1 (2026-11-07) assumed a "dumb" converter (paddle state only, logic in the application). **Version 2 is based on the
author's decision of 2026-11-08** (source `docs/tema-hw-klic.md`): the HW is a **real keyer** with its own sidetone and KEY
+ PTT outputs for the transmitter; the application mainly receives the **resulting keying** and only decodes, evaluates and transmits it.
**1.0.0 only with the USB key and the D4 sending lessons** (2026-11-08, `docs/vydani-1.0.0.md`).

**Application status (0.13.10):** keyer `keyer.js`, sidetone, keys/2 buttons, adaptive decoder `decoder.js` (on the
display ~77 %), evaluation `sendeval.js`, sending exercises, CW Chat (`cwlink.js`, element timeline). The "straight
key" mode already accepts a single key = tone on/off. On hold until the HW exists: decoder tuning, D4, Z-24, merging `keyerui.js` +
`cwchannel.js`. HW: does not exist yet.

## 1. Principle and division of work (variant B, optionally C)
- **HW = keyer.** It produces dits and dahs with machine timing from the paddles, plays the sidetone itself (without the phone /
  Bluetooth delay of 50–150 ms) and in "transmitter" mode keys KEY/PTT. It works **even without a computer** (powered from a power bank).
- **USB = keyboard (USB HID, boot keyboard).** No drivers and no special browser APIs – Windows, Linux, macOS,
  Android (OTG), iPhone/iPad (USB-C / adapter), any browser (`keydown`/`keyup`).
- **Stream (variant B, always):** the resulting keying = **one key, press = tone on, release = tone off**.
  The application tells a dit from a dah by its length (machine timing → no errors).
- **Paddle state (variant C, optional, off by default):** in addition, press/release of each paddle as a different key (L-Ctrl / R-Ctrl)
  – for analysing the operator's hand in the application (Z-24). The firmware always supports it; it is enabled in the HW settings.
- **The application never keys the transmitter.** The transmitter is keyed only by the local paddles (safety, licence, operator responsibility).

## 2. HW and firmware requirements
### 2.1 Inputs and keyer
| # | Requirement |
|---|---|
| H-01 | **Paddle input:** 3.5 mm stereo jack (TRS): tip = dit, ring = dah, sleeve = ground. ESD protection, contact current ≤ 1 mA. |
| H-02 | **Modes:** iambic A, iambic B, single-lever (last press wins), **straight = pass-through** (straight key on the tip → directly to stream/sidetone/KEY). **No bug** (nobody would use it). |
| H-03 | **Keyer:** element memory (dot/dash memory, switchable), speed at least 5–40 WPM, dah:dit ratio 3:1 (adjustable 2.8–3.5), adjustable weight; paddle swap (left-handed). |
| H-04 | **Debouncing** 2–5 ms (default 3 ms). |
| H-05 | **Controls (speed, mode, options): subject of the HW development** (still open – potentiometer, buttons, menu announced in Morse in the sidetone…). Condition: settings must be possible **without a computer** and be retained after power-off. |

### 2.2 Sidetone
| # | Requirement |
|---|---|
| H-10 | **Buzzer** (can be switched off) **and headphone output** (3.5 mm jack). |
| H-11 | **Mix:** a line input (3.5 mm jack) from the audio output of a computer / phone is mixed into the headphones with the keyer tone → one pair of headphones is enough (receiving in CW Chat, receiving lessons). |
| H-12 | Tone pitch (e.g. 400–1000 Hz) and volume adjustable; tone edges shaped (≈ 5 ms, no clicks). |
| H-13 | **Sidetone latency ≤ 2 ms** from paddle closure (pass-through mode) / from element start (keyer) to the tone. |

### 2.3 Transmitter outputs
| # | Requirement |
|---|---|
| H-20 | **KEY and PTT:** galvanically isolated from USB (optocoupler or isolated MOSFET), switching to ground like an open collector (modern rigs), filtering and RF immunity, 3.5 mm jack connectors. Polarity and voltage: to be stated in the HW documentation; older rigs (higher voltage, positive keying) out of scope of version 1. |
| H-21 | **Safety rule:** KEY/PTT is keyed **only from the local paddles** and **only in "transmitter" mode** (physical switch, position visible, e.g. LED). **Never from USB / the application / the network** – the firmware has no command that would key the output. In "training" mode KEY/PTT are permanently open. |
| H-22 | **PTT:** lead time before the first element (~10–50 ms, the element is delayed by it) and hang time after the last one (default derived from the speed) – **both adjustable**, PTT can be disabled (rig with QSK/VOX). |
| H-23 | Memories (CQ, call) and beacon **not in version 1**. WinKeyer protocol **no** (sending text from a computer contradicts H-21). |

### 2.4 USB
| # | Requirement |
|---|---|
| H-30 | USB HID keyboard (boot protocol), full-speed, polling interval **1 ms**, no driver; the product name contains "CW Key" / "Morse"; no serial number containing personal data. |
| H-31 | **Stream (B):** one key = tone on/off, sent in all modes (including pass-through and transmitter mode). The specific key is to be chosen on the prototype based on iPhone behaviour (Safari and Ctrl from an external keyboard has no verified source); it must differ from the paddle keys. |
| H-32 | **Paddle state (C, optional):** dit = **Left Ctrl**, dah = **Right Ctrl** (compatible with Vail adapter, VBand and other web trainers → the HW can be used elsewhere too); alternative `[` `]` selectable in the HW settings. Both paddles at once = both keys pressed. |
| H-33 | State changes only, **no autorepeat**; delay from event to sending the report **≤ 5 ms**, jitter **≤ 1 ms**. |
| H-34 | **Bidirectional channel (application → HW) not in version 1.** Later optionally **USB MIDI** (composite device HID + MIDI, modelled on Vail adapter) for setting speed/mode/tone from the application – Web MIDI: Chrome PC and Android, Firefox PC, Samsung Internet; **Safari/iPhone no** → settings stay on the HW there. The channel must not be able to key (H-21). |
| H-35 | Powered from USB (computer, charger or **5 V power bank** for operation without a computer); no internal battery; current draw < 100 mA. |

### 2.5 Development, licence, acceptance
| # | Requirement |
|---|---|
| H-40 | **Separate repository** (e.g. `~/morse-key-hw`, public on GitHub OK2MTW), vibecoding; this spec is its requirements document. No personal data and no employer (same as the application). |
| H-41 | Licence: **HW CERN-OHL-P, firmware MIT** (compatible with the application's EUPL-1.2); sources publishable, third-party code only with a compatible licence (Vail adapter = MIT, only as a reference). |
| H-42 | **Acceptance test:** (1) the Settings → Key page in the application shows events with timestamps – 100 elements from a generator / oscilloscope: USB ≤ 5 ms, jitter ≤ 1 ms, no lost or duplicated events, both paddles at once handled correctly; (2) sidetone ≤ 2 ms (oscilloscope paddle → headphone output); (3) line input mix without hum (ground loop); (4) KEY/PTT on a transmitter with a dummy load – lead/hang time, open in training mode, keying from USB impossible; (5) PC (Chrome, Firefox), Android (Chrome), iPhone (Safari). |

## 3. Application – inputs
| # | Requirement |
|---|---|
| A-01 | **Keying sources:** **HW keyer (USB)** – new, default for HW owners; further the PC keyboard (L/R-Ctrl) and two on-screen buttons with the application keyer for those without the HW (buttons experimental only). |
| A-02 | Timestamp = `event.timeStamp` (ms, monotonic); ignore `event.repeat` (+ own filter 0.12.2); on focus loss / page hide release everything. Keys only go to the focused window (a property of keyboards) – the page must indicate this. |
| A-03 | Mapping learning ("press the stream key", "press dit / dah"), stored in the device settings. |
| A-04 | **Type "HW keyer (USB)"** = stream as a straight key (current path) + **application sidetone off** + evaluation without hand metrics (A-14). If the HW also sends paddle state (C), the application stores it alongside the stream for hand analysis (Z-24), but does not run the application keyer. |

## 4. Application – keyer, sidetone, decoder, evaluation
| # | Requirement |
|---|---|
| A-10 | The **application keyer** (`keyer.js`) remains only for the keyboard and buttons (without HW). |
| A-11 | **Stream from HW:** element lengths are machine-timed → speed = dit length from the data (the application learns the speed by measurement, H-34 is not needed). |
| A-12 | **Sidetone** in the application: off for the HW keyer (sound is produced by the HW, H-10/H-11); otherwise as today. |
| A-13 | **Decoder:** for the HW stream the dit/dah threshold is derived from the dit length (exact); character/word spaces stay adaptive (made by the human). The adaptive decoder 0.12.4 remains for the keyboard and straight key. |
| A-14 | **Evaluation:** for HW, character correctness, spaces between characters and words, speed; **element ratio and variation only for the straight key** (with a keyer they are made by the machine). Hand analysis only with paddle state (C, Z-24). |
| A-15 | **D4 sending lessons:** iambic / single-lever only (2026-11-08), characters → groups → words → QSO, progression as in receiving (2 sessions ≥ 90 %), lesson mode `vysilani`. Straight key only free keying. |
| A-16 | **CW Chat:** the stream from HW is transmitted as today (`cwlink`, element timeline); application sidetone off, the other station is played by the application (into the HW via line input H-11). The network never keys the transmitter (H-21). |

## 5. Tasks for the application (once there is a HW prototype)
1. Key settings: type **"HW keyer (USB)"** (A-04) – learning the stream key, sidetone off, event log for acceptance H-42.
2. Decoder: branch for machine timing (A-13) + tests with a synthetic stream; tuning on recordings from the prototype.
3. Evaluation `sendeval.js`: "HW keyer" profile without ratio and variation (A-14).
4. D4 sending lessons (A-15) – design only with the prototype, data as a new lesson mode.
5. Z-24 hand habits – only if the HW sends paddle state (C); otherwise optional.
6. CW Chat: "HW keyer" source option (A-16) + merging `keyerui.js` / `cwchannel.js`.
7. Later: HW settings from the application via Web MIDI (H-34), only where the browser supports it.
8. Guide (cs+en): wiring (paddles, headphones, line input, KEY/PTT), window focus, iPhone.

## 6. Open questions (for HW development)
- HW controls without a computer (H-05) – potentiometer / buttons / menu.
- Stream key (H-31) and iPhone behaviour (Safari + external keyboard, Ctrl) – verify with a prototype.
- Microcontroller and board (USB HID + MIDI, tone generation, mix) – first step of the HW development.

## 7. Research (2026-11-08)
- WebHID: desktop Chrome/Edge only; Android, Firefox, Safari no ([caniuse](https://caniuse.com/webhid)).
- WebSerial: desktop Chrome/Edge; Android only from Chrome 148 and with OS support (Android 17), iPhone no
  ([blink-dev Intent to Ship](https://groups.google.com/a/chromium.org/g/blink-dev/c/HBJ-uYFvkpM)).
- Web MIDI: Chrome PC and Android, Firefox PC, Samsung Internet; Safari/iOS no ([caniuse](https://caniuse.com/midi)).
- Vail adapter (MIT, SAMD21): keyboard L-Ctrl = dit, R-Ctrl = dah; keyer and sidetone in the adapter, settings
  via MIDI ([GitHub](https://github.com/Vail-CW/vail-adapter)).
- WinKeyer (K1EL): serial protocol for sending text from a computer, paddles take priority – not needed for training.
- iPhone Safari and external keyboard keys: only old WebKit bug reports (arrows, key identifiers) – verify with
  a prototype ([WebKit 149054](https://bugs.webkit.org/show_bug.cgi?id=149054)).
