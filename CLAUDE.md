# Melodee

Bare-metal synthesizer and sequencer firmware for the M-VAVE FM-1 (JieLi AC79, two cores, 240×240 LCD).
Four tracks, engines PROPHET, FM6, CZ-1, SID and DRUM. No RTOS, no heap. A fork of Felucca.
Web editor and installer talk to the device over USB-MIDI SysEx.

This fork (`origin`, fred-tio/melodee) is Frederik's own project. He builds it to learn firmware development:
explain the why when you change something. He does not own an FM-1 yet. Prefer changes that can be checked on the host.

`upstream` (keremimo/melodee) is only a source of future updates to pull. Never open a PR, issue or push there.

## Read first

- [spec/docs/architecture.md](spec/docs/architecture.md): modules, boot, ISRs, audio path, storage, protocol.
- [spec/docs/coding-guidelines.md](spec/docs/coding-guidelines.md): project rules. They override general habits.
- [spec/docs/local-development.md](spec/docs/local-development.md): setup and the host-only loop.
- [BUILDING.md](BUILDING.md): toolchain, build flags, release, dual-core notes.
- [README.md](README.md): the user manual and "What's new".
- [web/EDITOR_PROTOCOL.md](web/EDITOR_PROTOCOL.md): the SysEx editor protocol.

## Layout

- `firmware/src/`: application. One compilation unit: `melodee.c` includes every other `.c` file in order.
- `firmware/hal/`: header-only hardware layer. The only place for register access (the build checks this).
- `firmware/loader/`: update loader.
- `tools/`: `build.py`, code generators (`gen_*.py`), package and device tools.
- `tests/`: host tests that `#include` the firmware sources; `run_tests.sh`; golden hashes and CPU budgets.
- `web/`: `editor.html` (no libraries), installer, protocol JS, Node tests.
- `web/emu/`: the firmware compiled to WebAssembly (PRD 0001). Glue only; no firmware changes for it.
- `build/`: all output, git-ignored. `build/gen/` holds generated headers.

## Commands

```bash
PYTHON=.venv/bin/python ./build.sh                       # full build (~25 s); Docker Desktop must run
PATH="$PWD/.venv/bin:$PATH" tests/run_tests.sh           # all host tests (~5 min); needs a full build first
GOLDEN_UPDATE=1 PATH="$PWD/.venv/bin:$PATH" sh tests/run_tests.sh   # only for an intended sound change
node web/test_web.mjs                        # web editor protocol tests
web/emu/build.sh && python3 -m http.server 8000 --directory build/emu   # play it: http://localhost:8000/
```

Host-only (no toolchain): see local-development.md. Short form:

```bash
.venv/bin/python -c 'import sys; sys.path.insert(0,"tools"); import build; build.generate()'
.venv/bin/python tools/gen_909_tables.py build/gen/x0x_drum_tables.h
cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -o build/host/regress tests/regress.c -lm
build/host/regress tests/golden.txt tests/cpu_baseline.txt
```

Set up on this machine: `.venv/` (Pillow, fontTools, mido, python-rtmidi), FriBidi, `~/.jieli/toolchain`,
`~/fw-AC79_AIoT_SDK` (three verified files only), Emscripten (Homebrew). Docker Desktop must run for `./build.sh`.

## Hard rules

- Audio renders in the audio ISR. No blocking, flash writes or drawing there.
- `float` only, never `double`. Keep `-ffp-contract=off`. No `malloc`, no libc: use `libc.c` and `resources.c`.
- Engine IDs are append-only. Stored formats (projects, presets, banks, backups) stay readable. Add migrations
  and an old-format test.
- Editor protocol changes update `EDITOR_PROTOCOL.md`, the firmware, the mock in `editor.html` and the web tests together.
- Do not rewrite `tests/golden.txt` or budget files unless the sound or cost change is intended.
- Never ship `MELODEE_CORE1_TEST=1` or `MELODEE_OTA_DRYRUN=1`.
- Commits: Conventional Commits with scope (`feat(ui): ...`). Branch from `next`.

## Spec workflow

- `spec/prds/`: product requirement documents, one per feature.
- `spec/adrs/`: architecture decision records. Copy `spec/adrs/0000-template.md` to the next free number
  when the user takes a decision while building.
- `spec/docs/`: living docs. Update them when the architecture or a rule changes.
