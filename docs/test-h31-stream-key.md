# H-31 verification: stream key and iPhone behaviour (Safari)

Status: **test draft** (2026-11-08). Results will be recorded in `docs/h31-results.md`. The chosen key will then be reflected in the
specification (which lives in the application repository; the author approves changes there).

## Questions
1. Which keys from an external USB keyboard reach Safari on iPhone/iPad as `keydown`/`keyup`, including without an
   editable element in focus? (Older WebKit bug reports: without a focused element no events arrived,
   arrows did not arrive at all, see [WebKit 149054](https://bugs.webkit.org/show_bug.cgi?id=149054).)
2. Does a standalone **Left / Right Ctrl** arrive on iPhone (variant C, H-32)? WebKit has a test for it
   ([keydown-keyup-special-keys](https://webkit.googlesource.com/WebKit/+/15e4da4b5ce83fcfe6de6863df44c60571019d5f/LayoutTests/fast/events/ios/keydown-keyup-special-keys-in-non-editable-element-expected.txt)),
   but a real device has not confirmed it. Also watch out for Caps Lock, which can set `ctrlKey` on iOS
   ([WebKit 137072](https://bugs.webkit.org/show_bug.cgi?id=137072)), and for modifier remapping in iOS Settings.
3. Which stream key has no side effects **on its own or in combination with L/R-Ctrl** (in variant C, Ctrl and the stream key are
   pressed at the same time)?

## Criteria for the stream key
| # | Criterion |
|---|---|
| K1 | Both `keydown` and `keyup` arrive on all target platforms: Windows/Linux/macOS (Chrome, Firefox, Safari), Android (Chrome), iPhone/iPad (Safari). |
| K2 | Types nothing and has no default browser or system action (page scroll, focus, menu, shortcut, system panel), not even together with Ctrl. |
| K3 | Does not toggle a state (Caps/Num/Scroll Lock, IME, keyboard layout) and does not trigger accessibility features (Shift 5× = Sticky Keys in Windows). |
| K4 | `event.code` is stable across layouts (CZ/EN) and platforms. |
| K5 | When held longer (pass-through mode, tuning) autorepeat can be distinguished (`event.repeat`) and `keyup` arrives precisely. |
| K6 | `timeStamp` resolution and length jitter are acceptable (≤ 1 ms above USB jitter). |

## Candidates (order = working hypothesis)
| Key | HID usage | Assumption | Risk |
|---|---|---|---|
| **F13** | 0x68 | No default action in Windows/Chrome, does not print | macOS/iOS/Android: unknown. Linux xkb may map it to XF86Tools (launches an application?) |
| F14–F19 | 0x69–0x6E | ditto | macOS: F14/F15 brightness. Linux: XF86Launch5–9 |
| F24 | 0x73 | less used | Linux: F20–F23 = microphone / touchpad, **do not use** |
| Pause | 0x48 | no action in the browser | Ctrl+Pause = Break in a console. Unknown on iOS. |
| Scroll Lock | 0x47 | no action in the browser | **toggles a state** (K3), Excel |
| Lang1 / Lang2 | 0x90 / 0x91 | | IME (Korean, Japanese) |
| *Rejected* | | Shift (Sticky Keys, selection), Alt (menu, AltGr in CZ), GUI/Win (Start), Space/letters (they type, Ctrl+W closes the tab), Esc (exits full screen), Insert (Ctrl+Insert = copy), Menu, arrows (scroll) | |

## Procedure

### Step A: pre-test with an ordinary USB keyboard (now, without new HW and without flashing)
Verifies question 2 (Ctrl on iPhone) and the candidates the keyboard has (Pause, Scroll Lock, F1–F12 as an indicator of
F-key behaviour).
1. On the PC run `python -m http.server 8000 -d tools` in the repository (home network only, nothing is published).
2. On the iPhone (same Wi-Fi) open `http://<PC-IP>:8000/keytest.html`. Connect the keyboard via an adapter
   (USB-C → USB-A, for Lightning the "Camera Adapter").
3. On the page try three focus variants: **nothing** (page body), **focused block** (`tabindex`), **read-only text
   field**. For each press: L-Ctrl, R-Ctrl, both at once, Pause, Scroll Lock, F1–F12, space bar, Shift.
4. Record what arrived (`key`, `code`, `repeat`, `ctrlKey`) and what happened outside the page (system menu, shortcuts).
5. The same on Android (Chrome) and PC (Chrome, Firefox) for comparison. On the iPhone note the iOS version and the setting
   *General → Keyboard → Hardware Keyboard → Modifier Keys*.

### Step B: "keytest" test firmware (after buying the Pico, stage 1)
The Pico as a HID keyboard that, on a button (or paddle) press, goes through all candidates. For each:
- 20 pulses: 10 × 30 ms and 10 × 90 ms with a 30 ms gap (dits and dahs at 40 WPM),
- 10 pulses together with L-Ctrl and 10 with R-Ctrl (variant C),
- 1 hold of 2 s (OS autorepeat, K5).
A 2 s pause between candidates, an LED shows the candidate number. The `keytest.html` page computes for each `code` the number of presses,
the lengths and deviations from the expected values, and shows whether anything is missing or duplicated.

*Earlier variant (optional):* the same test on the Black Pill F411. That would however mean a DFU write to the only working unit →
only with explicit consent and a careful procedure (backup, one attempt, no option bytes). I recommend waiting for the Pico, because step A covers the main question
(Ctrl on iPhone) without risk.

### Step C: evaluation
- Table platform × candidate × (K1–K6). Choose **the first candidate that meets everything**. If the iPhone delivers no
  key without an action (neither F13+ nor Pause), consider a printable key with `preventDefault` in the application (worse because of K2 when
  focus is not in the application) and record it as a limitation in the guide.
- Set the chosen key in the firmware (constant, later a menu option), record the result in `h31-results.md` and
  propose a change to spec H-31.

## Test page
[tools/keytest.html](../tools/keytest.html): a standalone page without dependencies. Logs `keydown`/`keyup` (`key`, `code`,
`location`, `repeat`, `ctrlKey`, `timeStamp`), switches focus, can enable `preventDefault`, computes press lengths and
statistics per `code`, and exports to the clipboard (CSV). The same page serves as a provisional meter for H-42 (1) until
the application has the Settings → Key page.
