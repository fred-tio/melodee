# 0001: Run the firmware in the browser as WebAssembly

- Status: accepted
- Date: 2026-10-11
- Related: [PRD 0001](../prds/0001-try-melodee-in-browser.md)

## Context

We want a web page that runs Melodee without an FM-1 (PRD 0001). It must play and look as the device does.
v1 runs on `localhost`; it should stay fit to publish later.

Facts:

- The firmware is one compilation unit with a header-only HAL. The host tests already compile the real sources with
  stubs: `tests/hostsim.c` (sound, sequencer), `tests/ui_test.c` and `tests/ui_render.c` (UI, screen).
- Builds use `-ffp-contract=off`, so host renders match bit for bit (351 golden renders pass on the Mac).
- Felucca has a working browser build in `web/emu/` (about 1,100 lines: C glue, AudioWorklet, page, tests). It came
  from fm1-x0x's browser build. Both are GPL-3.0, as Melodee is.
- Melodee keeps Felucca's hook names: `st_read` / `st_erase` / `st_prog`, `lcd_fill` / `lcd_blit`,
  `fm1_input_edges`, `fm1_enc_take`, `fm1_alnk0_irq`, the main loop in `main.c`.
- Melodee differs from Felucca: the strip canvas that draws during a screen transfer, `resources.c`, cache RAM,
  USB audio, the second-core job queue (`audio_worker.h`), the flash layout and the file names (`melodee.c`, `MELODEE_*`).
- GitHub Pages cannot set the COOP and COEP headers, so a later public page could not use `SharedArrayBuffer` or
  WebAssembly threads.

## Decision

We port Felucca's `web/emu/` design to Melodee:

- Emscripten compiles the firmware sources plus a glue file (`web/emu/melodee_web.c`) to one standalone `.wasm`.
  The glue replaces the HAL with a simulated FM-1: clock, audio half buffers, panel matrix, LEDs, a 240×240
  RGB565 frame, and a 1 MiB NOR image.
- The `.wasm` runs inside an AudioWorklet. Each audio request runs the device clock forward: the real audio ISR
  per 128 frames, the real main loop every 15 ms. Audio drives time, as on the device.
- The page sends input to the worklet and draws the screen and LEDs about 30 times a second. It saves changed
  flash sectors in browser storage and gives them back before boot.
- The second core is online but refuses every job: the glue sets `audio_worker_online` and makes
  `audio_worker_submit` return 0, so CPU0 renders each voice. This is `tests/dual_core_test.c`'s serial
  reference, which that test shows gives the same samples as paired rendering. The Standard release's voice caps
  apply (8 Prophet voices).
  If the inline worker fails in the prototype, we build with `MELODEE_DUAL_CORE=0` and document 5 Prophet voices.
- No firmware source changes. Glue code goes in `web/emu/`. A needed change in `firmware/` gets its own ADR.

## Options considered

- **Port Felucca's WebAssembly design (chosen):** proven on the same hardware model; reuses our host-test stubs;
  real firmware code, so the page cannot drift from the release; deterministic and testable in Node.
- **Write a new simulator in JavaScript:** a second implementation of every engine and screen. It would drift from
  the firmware. Rejected.
- **Emulate the AC79 CPU and run `melodee.bin`:** most exact, but no pi32v2 emulator exists, and it would be large
  and slow. Rejected.
- **Native desktop app (SDL):** good for developers, but a new dependency and no way to share a link later.
  Rejected for v1. The same glue could serve one later.
- **Real second thread (Web Worker + SharedArrayBuffer):** needs COOP and COEP headers. A local server can send
  them, GitHub Pages cannot, so it would block publishing later. The inline worker should be fast enough;
  the speed gate (PRD Q2) checks it. Rejected.
- **Single-core build only (`MELODEE_DUAL_CORE=0`):** simplest, but plays like the Single core release (5 Prophet
  voices). Kept as the fallback.

## Consequences

- Easier: try UI, sequencer and sound changes interactively without an FM-1. Publishing later needs only static hosting.
- Harder: `web/emu/` must follow firmware changes to the HAL hooks, `main.c`'s boot and loop, and the flash map.
  The Node test catches breakage when Emscripten is installed.
- New tool dependency: Emscripten (`brew install emscripten`), for the page and its test only. The firmware build
  does not need it.
- No change to any stored format or the editor protocol. Saved data in the browser uses the device's own flash
  format, so a later export to an FM-1 backup is possible.
- The page does not show the device's CPU load. The docs and the page must say so.
