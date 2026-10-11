# PRD 0002: Funky Monkey splash screen

- Status: done (M1–M4, 2026-10-11; not yet seen on an FM-1)
- Date: 2026-10-11
- Owner: Frederik
- Decision record: none yet

## Problem

At power-on, Melodee shows a plain text splash: "MELODEE" and "MULTI-ENGINE SYNTH" on the theme background.
Frederik wants his own picture there: the "Funky Monkey" pixel-art band (a keytar monkey with an afro, a saxophone,
drums and bass on a stage with spotlights).

The splash also goes by fast. It shows while the firmware starts, then 430 ms more. Frederik wants to see it for at
least 1.5 s.

## Goal

At power-on, the FM-1 shows the Funky Monkey picture for at least 1.5 s, then the HOME screen. The browser emulator
(PRD 0001) shows the same picture for the same time.

## Users

| User | Wants |
| --- | --- |
| Frederik (developer) | his own look on his own firmware |
| Later, maybe: FM-1 owners who install Melodee | to see at once that Melodee runs, not the stock firmware |

## User stories

1. I switch on the FM-1 and see the Funky Monkey picture.
2. The picture stays for at least 1.5 s, then HOME comes.
3. I start the browser emulator and see the same picture for the same time.
4. I edit the picture in a pixel editor, rebuild, and see the change.

## Scope

### In scope

- **Picture:** the supplied picture (2026-10-11) without its white border. The black frame stays. Square crop of
  the art, scaled to 120×120 pixels and reduced to 16 colours. The firmware draws each pixel as a 2×2 block, so it
  fills the 240×240 screen.
- **Text:** the picture is shown as it is, including "FUNKY MONKEY" and "FIRMWARE v1.0". The text does not follow
  the release version. `MELODEE_VERSION` stays where it is today.
- **Time:** at least 1.5 s from when the picture is on screen until HOME. The boot work runs while it shows. The
  firmware waits only for the time that is left.
- **Source file:** the finished 120×120, 16-colour picture as a PNG in `assets/`. A generator packs it into a
  header in `build/gen/`, as the icon and font generators do.
- **Emulator:** the browser build draws the picture with the same firmware code and keeps it for the same time.
- **Test:** a host test draws the splash and compares every screen pixel with the picture data the generator
  made from the PNG.

### Out of scope

- A full-resolution or full-colour picture. It does not fit in flash (see [Flash budget](#flash-budget)).
- Skipping the splash with a key press.
- Animation, sound at boot.
- Renaming the firmware to "Funky Monkey". Melodee stays the name in the README, the web page and the version
  strings.
- The UBOOT screen, crash screens and the About screen.

### Later (maybe never)

- A full-resolution picture, if flash becomes free (for example, after a feature is removed).
- A splash that shows the real version number.

## Requirements

### Functional

| ID | Requirement |
| --- | --- |
| F1 | At power-on, the splash shows the Funky Monkey picture on the full 240×240 screen, with no white border. |
| F2 | The picture stays on screen for at least 1.5 s and at most 1.7 s after it is drawn, then HOME comes. |
| F3 | The OCT− + OCT+ panel setup at power-on still works. |
| F4 | The browser emulator shows the same picture for the same time. |
| F5 | Changing the PNG in `assets/` and rebuilding changes the splash. No hand-written pixel data. |

### Quality

| ID | Requirement |
| --- | --- |
| Q1 | The picture adds at most 7.5 KB to the firmware image. |
| Q2 | The firmware image still fits the app slot. `build.py` checks it. |
| Q3 | The splash code and the emulator share one draw function. The emulator glue no longer copies the splash. |
| Q4 | No new build dependency. The generator uses Pillow, which `.venv` already has. |
| Q5 | No sound change: `tests/golden.txt` and the CPU budget files do not change. |
| Q6 | The watchdog never fires during the longer splash. |
| Q7 | `tests/run_tests.sh`, `node web/test_web.mjs` and the emulator test pass. |

## Flash budget

Measured on 2026-10-11 with the 1.1.0 build:

| Item | Size |
| --- | --- |
| App slot (`APP_SLOT`) | 581,564 B |
| Firmware image (`build/melodee.bin`) | 568,068 B |
| Free in the app slot | 13,496 B |
| Free flash outside the app slot | 8 KB (`0x9D000..0x9EFFF`), not written by the update package |

The picture options, measured on the supplied picture:

| Option | Size | Result |
| --- | --- | --- |
| 240×240, full colour (RGB565) | 115,200 B | does not fit |
| 240×240, 16 colours, LZ4 | about 20,000 B | does not fit |
| 240×240, 8 colours, deflate | about 11,400 B + decoder | fits, but leaves almost no flash for features |
| **120×120, 16 colours, raw 4 bits per pixel** | **7,200 B + 32 B palette** | **chosen: fits, leaves about 6 KB** |

LZ4 and run-length coding save little or nothing on the 120×120 picture (5,960 B and 7,129 B). The firmware has no
decoder for them. Raw 4-bit pixels need no decoder.

## Milestones

| # | Result | Check |
| --- | --- | --- |
| M1 | PNG in `assets/`, generator writes the header, build fits the slot (done 2026-10-11: +7,272 B) | `./build.sh` passes |
| M2 | Firmware draws the picture and keeps it for 1.5 s (done 2026-10-11) | `ui_test` splash check; `emu_test.mjs` timing check |
| M3 | Emulator uses the shared draw function (done 2026-10-11) | `emu_test.mjs` |
| M4 | README "What's new" and the architecture doc updated (done 2026-10-11) | review |

## Success

- Frederik sees the Funky Monkey picture for at least 1.5 s when Melodee starts, in the browser now and on the FM-1
  later.
- All host, web and emulator tests pass.

## Risks

| Risk | Mitigation |
| --- | --- |
| At 120×120 the band's faces and the drum text lose detail. | Accepted (2026-10-11). The PNG can be touched up by hand in a pixel editor. |
| The picture uses most of the free flash. | It uses 7.2 KB of 13.5 KB. Later features may need it back; then the splash can go to fewer colours or lower resolution. |
| The longer wait triggers the watchdog. | The watchdog is armed at 8 s and not fed during boot. 1.5 s is far below. Feed it while waiting anyway. |
| The timer that counts milliseconds starts late in the boot. | Measure the 1.5 s with the hardware tick counter (`fm1_ticks`), which runs from reset. |
| A device test is not possible: Frederik has no FM-1 yet. | Check on the host and in the emulator. Look again on the first device. |
| "FIRMWARE v1.0" on the picture does not match the release version. | Accepted (2026-10-11). The real version stays on the About screen and the web page. |

## Decided

- 120×120 pixels drawn at 2×, 16 colours, raw 4 bits per pixel (2026-10-11).
- The picture text stays as it is, "v1.0" included (2026-10-11).
- The black frame stays; only the white border goes (2026-10-11).
- The 480-byte row buffer lives in `.pool`: `.data` + `.bss` has less than 1 KB left (2026-10-11).

## Open questions

None.
