# Melodee architecture

Melodee is bare-metal firmware for the M-VAVE FM-1. It is a 4-track synthesizer and sequencer. There is no RTOS
and no heap. A web editor and a web installer talk to the device over USB-MIDI SysEx.

Melodee is a fork of [Felucca](https://github.com/hugelton/Felucca). The user manual is [README.md](../../README.md).
Build and release details are in [BUILDING.md](../../BUILDING.md).

## Hardware

| Part | What |
| --- | --- |
| SoC | JieLi AC79 (pi32v2 "r3" cores), 2 cores at 360 MHz, single-precision FPU |
| Memory | internal SRAM banks, XIP NOR flash; 7 data-cache ways reused as RAM (`MELODEE_CACHE_RAM`) |
| Display | 240×240 ST7789 LCD on SPI1 with DMA |
| Audio | I2S (ALNK0), 44.1 kHz, half buffer of 128 frames (2.9 ms) |
| Input | note keys, buttons, 4 knobs (encoders / ADC), master volume |
| I/O | USB device (MIDI, audio class "Melodee In/Out", optional CDC), TRS MIDI IN on UART1 |

The stock firmware is single-core. Melodee can use CPU1 for synth voices (see [Dual core](#dual-core)).

## Repository layout

| Path | What |
| --- | --- |
| `firmware/src/` | the application: engines, sequencer, UI, storage, USB, editor protocol |
| `firmware/hal/` | header-only hardware layer (`fm1_*.h`) and the vector / ISR assembly. The only place with register access |
| `firmware/loader/` | the update loader (RAM-resident USB-MIDI updater), its own unity build |
| `firmware/crt0.S`, `app.ld` | start-up code and linker script |
| `tools/` | build (`build.py`), code generators (`gen_*.py`), package tools, device tools |
| `tests/` | host tests in C and Python, golden hashes, CPU budgets, `run_tests.sh` |
| `web/` | web editor (`editor.html`), web installer (`index_pkg.html`), their JS, site builder |
| `assets/` | icons, fonts, samples, reference data used by the generators |
| `docs/` | topic notes (CZ-1 SysEx, recording, Prophet prototype) and UI design references |
| `spec/` | our planning: `docs/` (this file), `prds/`, `adrs/` |
| `build/` | all output (git-ignored): `gen/` headers, firmware, `host/` test binaries, renders |

## Build model

### One compilation unit

The firmware is a unity build. The compiler gets one C file: `firmware/src/melodee.c`. It `#include`s every
other `.c` file in a fixed order. Each file uses what the files above it define, so the order matters.

Group files include their members:

| Group file | Includes |
| --- | --- |
| `engines.c` | `dsp.c`, every `eng_*.c`, `resources.c` |
| `seq.c` | chord, motion, recording, click, seq_erase, midi_control, midi_clock |
| `fx.c` | perform, delay |
| `usb.c` | usb_audio_stream, usb_audio |
| `ui.c` | favorites, screen, scale_picker, browse, seq_edit, seq_notes, seq_undo |
| `ui_draw.c` | ui_sections, ui_stage, ui_browser, ui_patterns, ui_pages, ui_list, ui_popup, ui_slots, ui_motion, ui_scales |
| `ui_input.c` | ui_name, ui_new |
| `project.c` | pattern_store, settings_persist |
| `upreset.c` | cz_bank, fm6_bank, up_fm6, prophet_user, native_presets |
| `editor.c` | every `editor_*.c` |
| `eng_drum.c` | drum_808, `x0x/drum909` |

`main.c` comes last. Nearly every symbol is `static`. Only ISR handlers, `fm1_cstart` and `mem*` are external.

### Build pipeline (`tools/build.py`)

1. **Generate** headers into `build/gen/` with Python: UI font, icons, keycaps, palettes (Pillow, fontTools),
   lookup tables, factory patches (FM6, CZ-1, Prophet), 909 tables, scale check.
2. **Compile** `melodee.c` to LLVM IR with JieLi clang 4.0.1 (pi32v2). `size_fns.py` marks main-loop functions
   `minsize`. Then assemble and link with `crt0.S`, `hal/fm1_vec.S`, `hal/fm1_isr.S` and `app.ld`.
3. **Build the loader** from `firmware/loader/` (LZ4-compressed).
4. **Check** the image: the HAL boundary (`mmio_check`), no calls from `.ram_text`, no chip-ROM references,
   CPU1 loop in RAM, stack sizes, pool headroom. Any failure stops the build.
5. **Package** app + loader + three AC79 SDK files into `build/melodee.fwsc` (`fm1pkg_make.py`).

The JieLi toolchain is Linux x86-64 only. On macOS each tool runs in a `linux/amd64` Docker container.

### Build options

`MELODEE_*` macros select features. Defaults are in `melodee.c`, `build_options.h` (shared with `crt0.S`) and
`core.h`. `build.py` turns `MELODEE_X=0|1` in the environment into `-D` flags. See the table in BUILDING.md.
Two release variants exist: Standard (`MELODEE_DUAL_CORE=1`) and Single core (`MELODEE_DUAL_CORE=0`).

## Runtime

### Boot

```
SPL ──► _start (crt0.S) ──► fm1_cstart (main.c)
                              │ time, reset reason, watchdog, hold CPU1
                              │ boot guard: 2 failed boots in 30 s ──► UBOOT (ROM update mode)
                              │ zero .bss/.pool, copy .data .ram_text .dsp_text .dsp_tables to RAM
                              │ cache-RAM self-test, memory guards
                              ▼
                            fm1_main
                              persist_boot, settings, LCD + splash, input/ADC/panel,
                              melodee_init (default tracks), start CPU1 worker, audio,
                              USB, UART MIDI, 10 kHz timer, IRQs on, restore project
                              ▼
                            main loop (~15 ms)
```

### Execution contexts

| Context | Rate | Does |
| --- | --- | --- |
| Audio ISR `fm1_alnk0_irq` (`audio.c`, priority 3) | every 128 frames | sequencer tick, voices, modulation, engines, FX, mix; voice shedding and load metering |
| Timer ISR `fm1_timer5_irq` (`main.c`, priority 4, nests into audio) | 10 kHz | input scan, ms counter, USB poll at 2 kHz, UART MIDI poll, USB audio service |
| Main loop (`main.c`) | ~60 fps | watchdog, battery, editor/OTA service, UI input, settings save, CPU1 health, LEDs, `ui_draw` |
| CPU1 worker (`audio_worker.h`, `hal/fm1_multicore.h`) | per job | FM6 operator and Prophet voice render jobs |

There is no USB interrupt. USB is polled from the timer ISR.

Rule of thumb: sound happens in the audio ISR. Flash writes, drawing and protocol work happen in the main loop.

### Audio path

```
seq.c (steps, motion, chords, recording, MIDI clock)
   │ note on / off, parameter changes
   ▼
voice.c (voice allocation, budget of 16 units, envelopes, mod.c matrix)
   │ engine_t.note_on / render per voice
   ▼
engines (eng_*.c) ──► per-track post, slicer.c ──► fx.c (DIST, chorus, delay, reverb buses,
                                                         perform.c FX layer) ──► mix ──► I2S
                                                                              └──► USB audio stems
```

### Engines

An engine is a `static const engine_t ENG_X` in its `eng_*.c` file (`core.h` defines `engine_t`). It has a name,
8 edit parameters, presets, 4 HOME knobs, a voice cap, `note_on`, `render`, and optional hooks (`amp`, `desc`,
`block`, `keys`, `done`, `alloc`, `legato`, `post`, `cap`, `units`).

`engines.c` lists them in `ENGINES[]`. The index is the engine ID that projects and presets store. Indices are
append-only. A retired engine keeps its slot as `ENG_RETIRED` (or a conversion such as `ENG_FM4_GONE`), so old
projects still load. `MELODEE_LEGACY_EXTRAS=1` brings some retired engines back for tests.

Selectable today: PROPHET, FM6, CZ-1, SID (ID 3, was LOFI) and DRUM (808, 909).

### Dual core

CPU0 prepares voices. CPU1 renders paired FM6 or Prophet voices into private buffers. CPU0 joins the jobs and mixes
in the original order. The mailbox has a check word per request. A misread, a fault or a 10 ms stall retires CPU1
for the session. Then CPU0 renders everything and Prophet drops from 8 to 5 voices. Details: BUILDING.md,
"Second-core FM6/Prophet rendering".

### Memory

No `malloc`. `resources.c` gives out fixed handles (`RES_ENGINE0..`, `RES_DELAY`, ...) from linker-derived SRAM gaps,
the unused `.pool` and cache RAM. Large static buffers live in `.pool`. State that must survive a soft reset lives in
`.noinit`. `libc.c` is a small freestanding libc.

### Storage

`storage.c` writes A/B sector pairs in NOR flash, so a torn write never loses the last good copy.
On top of it: `project.c` (projects, patterns, settings), `upreset.c` and the `*_bank.c` / `*_store.c` files
(user presets, native FM6 / CZ-1 / Prophet banks). Every format must stay readable by newer firmware.

### UI

`gfx.c` draws in horizontal strips into a small canvas. There is no full framebuffer. `lcd.c` sends only changed
regions over SPI DMA, and drawing can overlap a running transfer. Screens live in `ui_*.c`. Palettes: NIGHT, DAY,
CONTRAST (plus MONO checks). Navigation: KNOB 1 moves across, KNOB 2 moves down, SELECT changes pages,
OCT+ enters, OCT− goes back. Design references: `docs/design/`.

## Host side

### Editor protocol

SysEx frame `F0 7D 46 4C <cmd> <args> F7`. Every valid request gets exactly one reply with the same command.
Commands cover info, parameters, steps, presets, projects, samples, user presets, tracks, motion, backup, native
patch banks and push notifications. The protocol has versions v1..v7 and capability blocks in INFO.
Specification: [web/EDITOR_PROTOCOL.md](../../web/EDITOR_PROTOCOL.md). Firmware side: `editor.c`.

### Update

`ota.c` handles the M-UPGRADE entry. The loader in `firmware/loader/` then pulls the image over USB-MIDI.
`tools/fm1_install.py` and `web/fm1ota.js` are the two host implementations. `web/fm1backup.js` saves a full
backup first.

### Web

`web/editor.html` is a single-file editor without libraries (Web MIDI). `?mock=1` replaces the device with a
built-in simulation. `web/index_pkg.html` is the installer. `web/make_site.py` builds the GitHub Pages site.

### Browser build

`web/emu/melodee_web.c` compiles the firmware with a simulated FM-1 (clock, audio, panel, screen, flash) to
`build/emu/melodee.wasm` (Emscripten, no imports). Audio drives the device clock. The second core is online but
refuses every job, so the Standard voice caps apply and CPU0 renders everything. See ADR 0001.

## Tests

`tests/run_tests.sh` compiles the firmware sources for the host with the system `cc`. Tests `#include` the
firmware `.c` files directly, with stubs for the hardware.

| Test | Checks |
| --- | --- |
| `regress.c` | golden hash of every engine × preset render (`golden.txt`), level health, voices, CPU per sample (`cpu_baseline.txt`) |
| `target_budget.py` | static cycle estimate of render loops from the pi32v2 disassembly (`target_budget.txt`) |
| `ui_render.c` | every screen in every palette from the real drawing code; layout lint; PNGs in `build/ui_new/` |
| `ui_test.c` | UI logic with stub buttons and knobs |
| `*_test.c` | storage, projects, migration, MIDI, USB audio, OTA, loader, each engine, FX, sequencer |
| `install_test.py` | the CLI installer against a simulated FM-1 |
| `web/*.mjs` | editor protocol against the mock, backup, package builder, update protocol |

There is no CI. The developer runs `tests/run_tests.sh` before pushing.

## Branches and releases

Feature branches (`feat/...`, `fix/...`) merge into `next`. A `release/X.Y.Z` branch gets a
`chore(release): ...` commit, merges back into `next` and is tagged `vX.Y.Z`. `main` follows the release.
`origin` (fred-tio/melodee) is this project. `upstream` (keremimo/melodee) is only a source to pull updates from;
nothing goes back there.
