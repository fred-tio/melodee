# Melodee coding guidelines

These rules come from the existing code and the build checks. The general guidelines in
`~/.claude/coding-guidelines.md` also apply. Where they conflict, the project rule wins.

## Firmware C

### Structure

- The firmware is one compilation unit (`melodee.c`). A new `.c` file goes into `melodee.c` or into its group file
  (see [architecture.md](architecture.md#one-compilation-unit)), after the files it uses.
- If the new code is DSP or sequencer code, also include it in `tests/hostsim.c`, so host tests see it.
- Make every function and global `static`. Only ISR entry points and `mem*` are external.
- Put register access only in `firmware/hal/`. `build.py` (`mmio_check`) fails the build on volatile pointer casts,
  register-range literals or inline asm in `src/` or `loader/`.

### Memory

- No heap. No `malloc`. No standard library. Use the helpers in `libc.c` (`str_*`, `fmt_*`, `mem*`, `rng`).
- Get large working memory through a `resources.c` handle. Release it when the user leaves the feature.
- Put large static buffers in `.pool`. Put state that must survive a soft reset in `.noinit`.
- Use fixed-width types (`uint32_t`, `int32_t`, `int16_t`).

### Numbers

- Use `float` only, never `double`. The build uses `-Werror=double-promotion`. Write float literals with `f`.
- Most DSP is fixed point (Q15, Q12). Keep the Q format in the name or next to the declaration.
- Do not depend on fused multiply-add. Builds use `-ffp-contract=off`, so host and target renders match bit for bit.

### Real time

- The audio ISR renders sound. It must not block, wait, write flash or draw.
- Flash writes, drawing and protocol work run in the main loop.
- Data shared between ISR and main loop needs a clear owner. Follow the existing ring and flag patterns.
- Each new sound feature must fit the CPU budget. Measure it with `regress` (`cpu_baseline.txt`) and
  `target_budget.py` (`target_budget.txt`).

### Naming

| Kind | Style | Example |
| --- | --- | --- |
| HAL | `fm1_` prefix | `fm1_ticks()` |
| Module functions | module prefix | `seq_`, `ui_`, `ed_`, `ota_`, `ua_`, `usb_`, `cz_`, `fm6_`, `p5_`, `resource_` |
| Engines | `ENG_` + name | `ENG_SID` |
| Tables, constants | UPPER_CASE | `SID_RATE` |
| Types | `_t` suffix | `track_t`, `engine_t` |

### Files and comments

- Every file starts with the SPDX line and the copyright line. Copy them from a neighbour file.
  Code ported from another project keeps its own licence. Add the licence text to `LICENSES/` and a line to
  `LICENSING.md`.
- Use `/* */` comments only.
- Comment hardware facts, formats, units, Q formats, reasons and sources (for example "reSID: ..."). Do not narrate
  what the code does.
- A file that models a real device starts with a header comment: what it models and from which source.

## Compatibility

Users keep projects, presets and backups across versions. Breaking them loses music.

- Engine IDs in `ENGINES[]` are append-only. A retired engine keeps its slot as `ENG_RETIRED`.
- Every stored format (projects, patterns, user presets, native banks, settings, backups) must stay readable by
  newer firmware. Add a migration, not a new format with no reader.
- Add or extend a test with an old-format fixture (`tests/frozen_project.h`, `engine_retirement_test.c`,
  `project_test.c`).
- If a newer format cannot load on older firmware, say so in the README "What's new" section.
- Editor protocol changes: bump the version or add a capability block, update `web/EDITOR_PROTOCOL.md`, the mock in
  `web/editor.html`, and `web/test_web.mjs`. The mock tables are checked against the firmware's descriptors.

## Sound changes

- `tests/golden.txt` holds one hash per render. A changed hash means the sound changed.
- Update golden hashes only for an intended, reviewed sound change: `GOLDEN_UPDATE=1 sh tests/run_tests.sh`.
  Keep every unaffected entry.
- Same for CPU baselines: `BUDGET_UPDATE=1`. Explain the cost change in the commit.

## UI

- The screen is 240×240. Test every new screen in every palette with `ui_render`. The layout lint must pass:
  no clipped, hidden or overlapping text.
- Follow the navigation language: KNOB 1 across, KNOB 2 down, SELECT changes pages, OCT+ enters, OCT− goes back.
  Named values open pickers. Actions open sheets.
- New icons and fonts are generated (`tools/gen_*.py`). Do not commit generated headers. They go to `build/gen/`.

## Web

- `web/editor.html` and the installer have no libraries and no build step. Keep it that way.
- The protocol code sits between `/*PROTO-BEGIN*/` and `/*PROTO-END*/`, so Node tests can load it.
- Test with `node web/test_web.mjs` and `node web/test_backup.mjs`.

## Python tools

- Python 3, standard library first. Allowed dependencies: Pillow, fontTools (build); mido, python-rtmidi
  (device tools).
- Import device libraries lazily, so host-only use works without them.

## Git

- Conventional Commits with a scope, imperative, lower case: `feat(ui): ...`, `fix(seq): ...`, `perf(fm6): ...`.
  Common scopes: ui, seq, usb, midi, fm6, prophet, cz1, sid, project, motion, dual-core, web, installer, release.
- Branches: `feat/...` and `fix/...`, merged into `next`.
- Push and open PRs only on `origin` (fred-tio/melodee). Never open a PR, issue or push to `upstream`
  (keremimo/melodee). Pull from it only to take future updates.
- Release: `release/X.Y.Z` branch, `chore(release): ...` commit, tag `vX.Y.Z`. Build both variants:
  `./build.sh --release X.Y.Z` and `MELODEE_DUAL_CORE=0 ./build.sh --release X.Y.Z`.
- User-facing changes get a line in README "What's new".

## Before push

1. `./build.sh` (both `MELODEE_DUAL_CORE` settings if you touched audio, dual core or start-up).
2. `tests/run_tests.sh`. It must print `ALL HOST TESTS PASSED`.
3. Without the toolchain, run the host-only subset from [local-development.md](local-development.md) and say in the
   PR which checks did not run.

Never release a build with `MELODEE_CORE1_TEST=1` or `MELODEE_OTA_DRYRUN=1`.

## Decisions

Record design decisions that change a format, a protocol, a budget or the architecture as an ADR in `spec/adrs/`.
Copy `spec/adrs/0000-template.md`.
