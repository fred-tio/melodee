# 0002: HUM reuses the inert global id 23 (was G_INITSND)

- Status: accepted
- Date: 2026-10-11
- Related: [PRD 0003](../prds/0003-humanize.md)

## Context

PRD 0003 adds HUM, a global parameter on the TEMPO page, saved with the project. Every global parameter needs an id
in the `G_*` enum ([core.h](../../firmware/src/core.h)).

Facts:

- `G_COUNT` is 27 and fixed. The project formats store the globals as `int16_t g[G_COUNT]`
  ([project.c](../../firmware/src/project.c), `_Static_assert(G_COUNT == 27u, ...)`). The template has the same
  array. The editor protocol's `DUMP` sends `G_COUNT` values, and the editor reads the globals by position.
- Melodee reused inert ids four times before without a format change: `G_ROUTE` (14), `G_A4` (21), `G_RTYPE` (24),
  `G_DTYPE` and `G_DWEAR` (25, 26). Each kept the id, the count and the format.
- `G_CLRSEQ` (22) and `G_INITSND` (23) were the actions of the TOOLS page. Commit 6efb16e (2026-10-09) removed that
  page. No page uses them now. Their actions moved: "Clear pattern" is in the pattern sheet
  ([ui_popup.c](../../firmware/src/ui_popup.c)), SOUND INIT has its own key path
  ([ui_input.c](../../firmware/src/ui_input.c)). Their branches in `act_do` cannot run any more.
- An action knob set `ui.act`, never `song.g`. So the device always saved 0 in ids 22 and 23.
- The editor hides both ids (`G_SKIP` holds "CLRSQ" and "INIT" in [editor.html](../../web/editor.html)). It never
  sends a `SET` for them.
- The other free-looking ids are in use: `G_INFO` (15) shows CPU on SYSTEM, `G_MIDI` (12) picks the input whose
  status SYSTEM shows, `G_LOAD` and `G_SAVE` (18, 19) are PROJECT's actions.
- A project load restores every global except `G_SLOT`, `G_LOAD` and `G_SAVE`, clamped to the parameter's range.
  Formats before the delay came back reset the globals to their defaults (`proj_delay_off`).

## Decision

We give id 23 to HUM: `G_INITSND` becomes `G_HUM`, label "HUM", `F_PCT`, 0..100, default 0. The project format,
`G_COUNT` and the protocol's message layout stay the same. We delete the dead `G_CLRSEQ` / `G_INITSND` branches in
`act_do`. Id 22 stays inert for the next global.

## Options considered

- **Reuse id 23, `G_INITSND` (chosen):** no format change, no protocol layout change. Every saved project already
  holds 0 there, which is HUM's default. Follows the `G_ROUTE` / `G_RTYPE` precedent.
- **Reuse id 22, `G_CLRSEQ`:** the same in every way. Id 23 is taken so that 22, the lower one, is the next free
  id. No real difference.
- **Grow `G_COUNT` to 28:** a new project format (FUN17) with a migration, a new template, and a longer `DUMP`.
  An old editor would read the wrong values from a new device. Too much change for one value.
- **Store HUM outside the globals (a spare byte in the project's reserved tail):** no id, so the editor cannot
  `GET` / `SET` it, and the TEMPO page cannot show it as a normal parameter. Special cases everywhere.
- **Reuse `G_INFO`, `G_MIDI`, `G_LOAD` or `G_SAVE`:** they are in use (see Context).

## Consequences

- HUM is a normal global: the TEMPO page, `GET` / `SET`, `DUMP`, `WATCH` pushes, the project and the template
  carry it with no special code.
- Old projects load with HUM 0 %, so they play as before (PRD 0003 F2, F3). No migration is needed.
- One small risk: a third-party tool could have sent `SET 1 23 1` to an old firmware. That project would load with
  HUM 1 %. The official editor never did this. We accept it.
- `DESC` of id 23 changes from "INIT" (`N_GO`, 0..1) to "HUM" (`F_PCT`, 0..100).
  - New editor, old firmware: the editor finds HUM by its `DESC` label (as `gEng()` finds "ENG") and hides it when
    the label is missing.
  - Old editor, new firmware: the editor does not know HUM and does not show it. Nothing breaks.
- `EDITOR_PROTOCOL.md` gets a "Global ids" note for id 23, as for `G_ROUTE`. The firmware, the `editor.html` mock
  (`GP` table, `G_SKIP`, the TEMPO page) and the web tests change in the same commit.
- Tests: an old-format project with 0 in id 23 loads with HUM 0 %; `tests/golden.txt` stays unchanged.
