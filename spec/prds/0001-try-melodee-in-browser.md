# PRD 0001: Try Melodee in the browser

- Status: done (M1–M4, 2026-10-11)
- Date: 2026-10-11
- Owner: Frederik
- Decision record: [ADR 0001](../adrs/0001-run-firmware-in-browser-as-wasm.md)

## Problem

You can only play Melodee on an FM-1. Frederik does not own one yet. He can render sounds and screens on the Mac,
but he cannot play the instrument: no way to try a UI flow, a sequencer change or a sound in real time.

Felucca, the firmware Melodee forked from, has a [Try page](https://hugelton.github.io/Felucca/webapp/try/).
It runs the real firmware in the browser. Melodee forked before that page existed (Felucca 0.8-beta, 2026-10-03;
the page came in Felucca 1.0.4, 2026-10-07).

## Goal

A web page, run from `localhost`, that runs the real Melodee firmware with the FM-1 panel on screen. It plays and
looks as the device does. No FM-1 needed.

The page stays fit to publish later, but v1 is not published.

## Users

| User | Wants |
| --- | --- |
| Frederik (developer) | play Melodee while he builds it: UI, sequencer and sound work without the device |
| Later, maybe: musicians and FM-1 owners | try Melodee before buying an FM-1 or installing a version |

## User stories

1. I build the page with one command and open it from `localhost`.
2. I press one start button and play notes on the on-screen keys or my computer keyboard.
3. I connect a MIDI keyboard in Chrome or Edge and play Melodee with it.
4. I make a pattern, save it, reload the page, and find it again.
5. I change firmware code, rebuild, reload, and play the change.
6. A test fails if the browser build boots wrong, sounds different from the host build, or is too slow.

## Scope

### In scope (v1)

- **Real firmware:** the Melodee sources compiled to WebAssembly. Same engines, presets, sequencer, UI and storage
  code as the release. No feature is rewritten in JavaScript.
- **Standard behaviour:** plays like the Standard (dual-core) release, including the 8-voice Prophet cap
  (see ADR 0001).
- **Panel:** every button, the 27 note keys, every encoder and MASTER volume. The LEDs as the firmware sets them
  (lit and dim).
- **Screen:** the 240×240 display, pixel for pixel.
- **Input:** mouse, computer keyboard (with a visible key map), Web MIDI in (channel messages). Touch as far as the
  ported Felucca page gives it.
- **Audio:** AudioWorklet at 44.1 kHz. Audio drives the device clock, as on the device.
- **Saving:** the flash areas that hold settings, projects, user presets and banks persist in the browser's
  IndexedDB, in the device's own flash format. A "reset device" action erases them. If storage is blocked, the page
  still plays and says that nothing is saved.
- **Page:** start button (browsers need a gesture to start audio), version label, key map, disclaimer (not
  affiliated with M-VAVE).
- **Build:** one command builds the page into `build/emu/`. One command serves it on `localhost`.
- **Tests:** a Node test boots the WebAssembly build, plays, saves, reloads and checks it renders the same
  samples as the native build. `tests/run_tests.sh` runs it when Emscripten is installed and skips it otherwise.

### Out of scope (v1)

- Publishing the page anywhere (GitHub Pages, CI).
- USB: the web editor connection, firmware updates, USB audio, the serial console. Their menu entries stay visible
  and do nothing, as on a device with no USB host.
- TRS MIDI, MIDI out, MIDI clock out.
- Crash screens, UBOOT, battery behaviour, the CPU1 fault fallback.
- Device CPU load. The browser does not show whether a sound fits the FM-1's CPU budget.
- Sample upload.
- Phone layout and testing on phones.

### Later (maybe never)

- Publish the Try page alone on the fork's GitHub Pages (`fred-tio.github.io/melodee`). Never an installer or
  firmware downloads while no build is tested on hardware.
- Phone layout and touch polish.
- Connect the web editor to the emulator inside the page (the editor protocol without USB).
- Export and import the browser's saved data as a file, or as an FM-1 backup.
- MIDI clock in.

## Requirements

### Functional

| ID | Requirement |
| --- | --- |
| F1 | After start, the page shows the splash, then the Stage HOME screen, as the device does. |
| F2 | A held key sounds and lights its LED. PLAY starts the sequencer and lights PLAY green. |
| F3 | All selectable engines (PROPHET, FM6, CZ-1, SID, DRUM) and their factory presets play. |
| F4 | A user preset or project saved on the panel is still there after a page reload. |
| F5 | "Reset device" erases the saved data and restarts the device. |
| F6 | Web MIDI note on/off, CC, pitch bend and aftertouch reach the firmware as USB MIDI does. |
| F7 | The computer keyboard plays the note keys and presses the buttons. The page shows the map. |
| F8 | The page shows the firmware version (`MELODEE_VERSION`). |

### Quality

| ID | Requirement |
| --- | --- |
| Q1 | The WebAssembly build renders the same samples as the native host build, bit for bit, for the same input. |
| Q2 | The heavy song renders in at most 50 % of real time in Node on the build machine (test gate). Heavy song: T1 PROPHET, T2 FM6, T3 CZ-1, sustained pads, 4-note chords held over 4 steps (a step holds at most 4 notes); T4 DRUM 909 kit, kick, snare and hats. It keeps the shared 16-unit voice budget full, the most the device plays at once. |
| Q3 | No audio drop-outs for 5 minutes of the heavy song in desktop Chrome on this Mac. |
| Q4 | Firmware sources need no browser-specific changes. All glue lives in `web/emu/`. An exception needs an ADR. |
| Q5 | Desktop Chrome, Edge, Firefox and Safari play. Web MIDI only where the browser has it. |
| Q6 | Keyboard focus is visible, and controls have accessible names. |
| Q7 | The page loads only its own files. No analytics, no network calls after load. |
| Q8 | Licences: GPL-3.0; keep the Felucca and fm1-x0x copyright lines on ported files; font licences ship with the page. |
| Q9 | No new firmware build dependency. Emscripten is needed only for the page and its test. |

## Milestones

| # | Result | Check |
| --- | --- | --- |
| M1 | WebAssembly build boots in Node: splash, HOME, a key sounds (done 2026-10-11) | `emu_test.mjs` boot and sound checks |
| M2 | Page runs on `localhost`: screen, panel, audio, keyboard (done 2026-10-11) | manual test |
| M3 | Saving, reset, Web MIDI (done 2026-10-11; MIDI from a real keyboard not yet tried) | `emu_test.mjs` save and reload check, manual MIDI test |
| M4 | Native bit-for-bit check and speed gate in `run_tests.sh` (done 2026-10-11) | `ALL HOST TESTS PASSED` |

## Success

- Frederik plays UI, sequencer and sound changes in the browser before an FM-1 is available.
- The emulator test is part of `tests/run_tests.sh` and passes.

## Risks

| Risk | Mitigation |
| --- | --- |
| Melodee draws while a screen transfer runs (strip canvas, DMA overlap). The glue must copy it right. | Reuse the screen stubs of `tests/ui_test.c` and `ui_render.c`, which already handle it. |
| Melodee's memory manager (`resources.c`), cache RAM and flash layout differ from Felucca's. | Use the host paths the tests use. Keep every flash sector the firmware wrote: the browser's flash starts erased, so no storage map is needed. |
| Prophet with 8 voices is the heaviest load. | Speed gate in the test. Show a load meter on the page. |
| The browser feels right, so a change looks safe for the device. | Say on the page and in the docs that it does not model the device's CPU or timing. Keep `regress` and `target_budget.py` as the CPU checks. |
| Saves get lost when browser data is cleared. | Say on the page that saves live in this browser only. Later: export to a file. |

## Decided

- No upstream PR. Melodee here is Frederik's own project (2026-10-11).
- v1 runs locally only. Publishing comes later, maybe never (2026-10-11).
- If ever published: the Try page only, no installer, no firmware downloads (2026-10-11).

## Open questions

None.
