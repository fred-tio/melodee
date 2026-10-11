# Developing Melodee without an FM-1

You can build, test, listen and look at screens without the device. Only the install and the real-time behaviour
on the hardware need an FM-1.

## What works without the device

| Task | Needs | Tool |
| --- | --- | --- |
| Full firmware build (`.fwsc`) | Docker, JieLi toolchain, AC79 SDK | `./build.sh` |
| All host tests | the full build | `tests/run_tests.sh` |
| Hear an engine or preset | `build/gen` only | `build/host/hostsim` → WAV |
| Golden sound regression and CPU estimate | `build/gen` only | `build/host/regress` |
| See every screen as PNG | `build/gen` incl. UI font | `ui_render` (via `run_tests.sh`) |
| Click through the web editor | a browser | `web/editor.html?mock=1` |
| Play the whole firmware in the browser | `build/gen`, Emscripten | `web/emu/build.sh`, then serve `build/emu/` |
| Web protocol and backup tests | Node.js | `node web/test_web.mjs` |
| Installer CLI | Python | `tests/install_test.py` (simulated FM-1) |

## What needs the device

- Installing and the update entry on real flash.
- Real-time behaviour: audio drop-outs, MIDI timing, LCD timing, key and knob feel.
- USB enumeration on a real host (MIDI, audio class, CDC).
- Dual-core faults (CPU1 SRAM misreads appear only on some units), cache-RAM validation, battery use.
- Device tools: `fm1_install.py`, `usb_audio_stats.py`, `prophet_device.py`, `fm1_sample_upload.py`,
  `core1_fault_test.py`.

The host CPU numbers are a guide only. `target_budget.py` is the closest check to the device without one:
it counts render-loop instructions in the pi32v2 disassembly.

## Setup (macOS, Apple silicon)

Status on this machine (2026-10-11): set up. `./build.sh` builds in ~25 s, `tests/run_tests.sh` prints
`ALL HOST TESTS PASSED` in ~5 min. Three optional comparisons skip: PHYS vs DaisySP (`DAISYSP`), FM6 vs Dexed
(`DEXED_SRC`) and the official V15 restore (`FM-1_v15.fwsc` in the repo root).

### 1. Python

Homebrew Python is externally managed. Use a virtual environment in `.venv/` (git-ignored).

```bash
python3 -m venv .venv
```

```bash
.venv/bin/pip install Pillow fonttools mido python-rtmidi
```

The UI font generator needs Pillow's Raqm layout. Raqm loads FriBidi at run time:

```bash
brew install fribidi
```

`build.sh` and `run_tests.sh` set `DYLD_FALLBACK_LIBRARY_PATH` to Homebrew's `lib` for you.
Check: `.venv/bin/python -c 'from PIL import features; print(features.check("raqm"))'` must print `True`.

### 2. Toolchain and SDK (for the full build)

None of this needs an FM-1.

1. Start Docker Desktop. The toolchain is Linux x86-64; it runs under Rosetta in `linux/amd64` containers.
   Keep the repo under `/Users` so Docker can mount it.
2. Get the JieLi toolchain (downloads from JieLi's package server to `~/.jieli/toolchain`):

   ```bash
   tools/get_toolchain.sh
   ```

3. Get the AC79 SDK files. The package uses only three files from `cpu/wl82/tools/`. Gitee (the source in
   BUILDING.md) refuses anonymous clones (HTTP 400). JieLi has no GitHub copy. This mirror has the tested tag:

   ```bash
   D=~/fw-AC79_AIoT_SDK/cpu/wl82/tools; B=https://raw.githubusercontent.com/amitv87/fw-AC79_AIoT_SDK/AC79NN_SDK_V1.2.1_2023-12-13/cpu/wl82/tools; mkdir -p $D/cfg && for f in uboot.boot cfg_tool.bin cfg/eq_cfg_hw.bin; do curl -fsSL "$B/$f" -o "$D/$f"; done
   ```

   ```bash
   (cd ~/fw-AC79_AIoT_SDK/cpu/wl82/tools && shasum -a 256 uboot.boot cfg_tool.bin cfg/eq_cfg_hw.bin)
   ```

   The hashes must match `SDK_SHA256` in `tools/build.py`. Then the files are identical to the reference, whatever
   the source. `build.py` warns on a mismatch.

4. Node.js for the web tests.
5. Emscripten for the browser build (PRD 0001): `brew install emscripten`. `run_tests.sh` skips it without `emcc`.

### 3. Build and test

```bash
PYTHON=.venv/bin/python ./build.sh
```

```bash
PATH="$PWD/.venv/bin:$PATH" tests/run_tests.sh
```

Output: `build/melodee.fwsc`, test binaries in `build/host/`, screen PNGs in `build/ui_new/`, demo WAVs in
`build/*_demo/`.

## Host-only loop (no toolchain, no Docker)

`run_tests.sh` stops at the update tests if `build/melodee.fwsc` is missing. Until the toolchain is set up, run the
parts yourself.

Generate the headers:

```bash
.venv/bin/python -c 'import sys; sys.path.insert(0, "tools"); import build; build.generate()'
```

```bash
.venv/bin/python tools/gen_909_tables.py build/gen/x0x_drum_tables.h
```

Without FriBidi, every generator except the UI font succeeds. Sound tests work; UI tests need the font.

Build and run the sound regression (checked on this Mac: 351 golden renders pass, ~6 s):

```bash
mkdir -p build/host && cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -o build/host/regress tests/regress.c -lm
```

```bash
build/host/regress tests/golden.txt tests/cpu_baseline.txt
```

Render a sound to a WAV (engine index, preset index, mono flag, output):

```bash
cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -o build/host/hostsim tests/hostsim.c -lm
```

```bash
SECS=4 CHORD=3 build/host/hostsim 0 0 1 build/host/out.wav
```

More `hostsim` options (`PSET`, `PSWEEP`, `STEPS`, `DRUMS`, `TRACKS=DIR`, `BENCH`) are listed at the top of
`tests/hostsim.c`. Any other test builds the same way; copy its compile line from `tests/run_tests.sh`.

Open the web editor with a simulated device:

```bash
python3 -m http.server 8000 --directory web
```

Then open `http://localhost:8000/editor.html?mock=1` and press **Connect**. The header shows
"MELODEE v1.1.0 (MOCK)". `&mockauto=0` stops the demo playback.
`&legacy=1` simulates old firmware.

## Play Melodee in the browser

The real firmware, compiled to WebAssembly, with the FM-1 panel on screen (PRD 0001):

```bash
web/emu/build.sh
```

```bash
python3 -m http.server 8000 --directory build/emu
```

Open `http://localhost:8000/` and press **Switch on**. Keys: `Z`–`'` and `Q`–`O` play notes, `1`–`=` are the
buttons, `[` `]` OCT, arrows turn SELECT and ALGORITHM. Drag or scroll on a knob to turn it. Saves stay in this
browser (IndexedDB); **Reset device** erases them. A MIDI keyboard works in Chrome and Edge.

After a firmware change, run `web/emu/build.sh` again and reload the page. The page does not model the FM-1's CPU
or timing: check cost with `regress` and `target_budget.py`.

## Working on a feature without the device

1. **Sound:** change the engine, render it with `hostsim`, listen to the WAV. Run `regress`. Update golden hashes
   only for an intended change.
2. **UI:** change the screen, run `run_tests.sh`, look at `build/ui_new/*.png` and `build/ui_new/report.txt`.
   Compare with the mockups in `docs/design/`.
3. **Protocol / editor:** change `editor.c` and `web/editor.html` together, including the mock. Run the web tests.
4. **Storage / formats:** write a host test with an old-format fixture. Torn writes and migrations are testable on
   the host.
5. **Budget:** keep `cpu_baseline.txt` and, with the toolchain, `target_budget.txt` green.

Mark everything that still needs a device check in the PR, so someone with an FM-1 can verify it.

## When you get an FM-1

1. Make a full backup with the web installer first. Recovery from a failed install needs
   [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).
2. Install your build: `python3 tools/fm1_install.py build/melodee.fwsc` (`--info` shows the device identity).
3. Read device stats: `tools/usb_audio_stats.py --cores --voices --window`.
4. Prefer the Single core build (`MELODEE_DUAL_CORE=0`) if the unit crashes with Standard.
