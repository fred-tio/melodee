# PRD 0003: Humanize

- Status: built (M1–M4); M5 (listening test) open
- Date: 2026-10-11
- Owner: Frederik
- Decision record: [ADR 0002](../adrs/0002-humanize-reuses-global-id-23.md)

## Problem

The sequencer plays every note exactly on the grid. A step without its own velocity always plays at 96
([seq.c](../../firmware/src/seq.c) `seq_step`). Swing changes the grid, but each pass of the loop is identical.
Patterns sound mechanical, most of all drums and repeated synth lines.

## Goal

One knob, **HUM**, on the TEMPO page (next to BPM, SWG and QNT). It makes all sequenced notes sound played by a
person: a velocity curve that follows the beat, small random velocity changes, small random timing changes and
small random note length changes. At 0 % Melodee plays exactly as it does today.

## Users

| User | Wants |
| --- | --- |
| Frederik (developer) | patterns that sound alive without editing velocity and micro-timing step by step |
| Later, maybe: FM-1 owners | the same, with one knob |

## User stories

1. I turn HUM up on the TEMPO page and the drums and synth lines sound less mechanical.
2. Beat 1 sounds strongest and the off-beat 16ths sound softest, so the groove reads clearly.
3. My accented steps still sound like accents.
4. Each pass of the loop sounds a little different.
5. Chords stay tight: the notes of one step start together.
6. I save the project, load it again, and HUM is the same.
7. I set HUM in the web editor and the device shows the same value.
8. I set HUM to 0 % and the pattern plays exactly as before.

## Scope

### In scope (v1)

- **Control:** one global parameter, HUM, 0–100 %, in the free fourth slot of the TEMPO page
  ([params.c](../../firmware/src/params.c) page table). Saved with the project. Default 0 %.
- **Sources:** the sequencer steps of all four tracks (DRUM track and drum lane hits included), their timed notes
  (live recording, Capture, edited notes and hits: `recording.c`) and the arpeggiator.
- **Velocity curve per bar:** a fixed shape from the note's place in the global 4/4 bar. Strongest to softest:
  beat 1, beat 3, beats 2 and 4, the 8th off-beats, the 16th off-beats. HUM scales the depth of the curve.
- **Velocity jitter:** a random change per note, on top of the curve.
- **Timing:** a random offset per step, early or late, centred on the grid. Applied after swing.
- **Gate:** a small random change of the note length per step.
- **Editor:** the web editor shows and edits HUM next to BPM and SWG.

### Out of scope (v1)

- Live keys, MIDI IN and the panel's MIDI OUT. They play as today.
- Recording. Recorded notes keep the velocity and timing you played in the pattern; humanize never writes into it
  and is off for a track while it records. Their playback is humanized (see Sources).
- The metronome ([click.c](../../firmware/src/click.c)). It stays on the grid.
- Separate amounts for timing, velocity and gate.
- A per-track amount.

### Later (maybe never)

- Separate TIME and VEL amounts (a TEMPO 2 page).
- Per-track amount, so drums can be tighter than pads.
- Per-track feel: a fixed offset per track (bass pushes, snare lays back).
- Phrase accent: bar 1 of every 4 bars a little louder, bar 4 a little softer.
- Groove-aware timing: downbeats tighter, off-beats looser.
- A "take" mode: the same variation repeats for N bars.
- Humanize the panel's MIDI OUT when the sequencer sends MIDI.

## Requirements

### Functional

| ID | Requirement |
| --- | --- |
| F1 | The TEMPO page shows HUM in its fourth slot, 0–100 %, default 0 %. |
| F2 | HUM is saved and loaded with the project. A project saved before this feature loads with HUM 0 %. |
| F3 | At HUM 0 % the audio output is bit for bit the same as before the feature (`tests/golden.txt` unchanged). |
| F4 | Velocity curve at 100 %: beat 1 keeps the step's velocity; the 16th off-beats are 50 lower; the other positions lie between, in the order of the Scope section. HUM scales the curve linearly. |
| F5 | Velocity jitter at 100 %: up to ±30 per note, uniform. HUM scales it linearly. The result stays in 1..127. |
| F6 | An accented step and an accented drum hit stay at 127 minus a small downward jitter only (at most 6 at 100 %). The curve does not apply to them. |
| F7 | Steps with a recorded velocity or the default velocity get the curve and the jitter. |
| F8 | Timing at 100 %: each step's notes start up to ±30 ms from the grid, uniform. HUM scales it linearly. |
| F9 | The timing offset is at most a quarter of the step's length (after swing), so steps never swap order or overlap. |
| F10 | All notes and drum hits of one step get the same timing offset. Each note gets its own velocity jitter. |
| F11 | Gate at 100 %: the note length changes by up to ±10 % of the gate, uniform. Ties and slides stay legato. |
| F12 | The arpeggiator's notes get the same velocity, timing and gate variation as sequencer steps. |
| F13 | The random variation is new on every pass of the pattern. |
| F14 | With an external clock (USB or TRS), humanize works the same, on the clock's predicted step times. |
| F15 | The web editor shows and edits HUM. `EDITOR_PROTOCOL.md`, the firmware, the mock in `editor.html` and the web tests change together. |

### Quality

| ID | Requirement |
| --- | --- |
| Q1 | No new stored-format break. Old projects, presets, banks and backups load. An old-format test covers HUM's default. |
| Q2 | `G_COUNT` and the global parameter ids stay fixed (they are part of the formats and the protocol). HUM reuses an inert id, or the change goes through a format migration. The choice gets an ADR. |
| Q3 | Humanize draws its random numbers from its own generator. At any HUM, step chance and arp random play the same notes as without humanize. |
| Q4 | Humanize runs in the audio ISR with no blocking. Its cost stays inside the CPU budget (`tests/cpu_baseline.txt`, `target_budget.py`). |
| Q5 | Host tests: the curve's order and depth, the jitter and offset limits, F3 (HUM 0 % bit for bit), F6 (accents), F9 (no reorder), and a project from before the feature. Tests seed the generator, so the output is fixed. |
| Q6 | The browser emulator (PRD 0001) plays humanize with no glue change. |

## Milestones

| # | Result | Check |
| --- | --- | --- |
| M1 | ADR for the parameter id and storage; HUM on the TEMPO page, saved, default 0 % | `ui_test`, project old-format test, golden unchanged |
| M2 | Velocity curve, jitter and accent rule for steps and arp | host test on velocities |
| M3 | Early and late timing, gate variation, F9 limit, external clock | host test on note-on times, `midi_timing_test` |
| M4 | Web editor and protocol | `node web/test_web.mjs` |
| M5 | Play it in the browser emulator | manual listening test |

## Success

- With HUM around 30 %, a drum and bass pattern sounds played, not programmed, in the browser emulator.
- At 0 % nothing changes: `tests/run_tests.sh` passes with the golden file untouched.

## Risks

| Risk | Mitigation |
| --- | --- |
| Early notes need the sequencer to trigger before the step starts. Today it triggers exactly at the step boundary. | Schedule each step's note-on up to 30 ms ahead. Hold F9's quarter-step limit. Test the order of note-ons at the shortest step length. |
| No free global parameter id: `G_COUNT` is fixed by the formats and the protocol. | Reuse an inert id (as `G_A4`, `G_RTYPE` and `G_DTYPE` did) or migrate the format. Decide in an ADR (Q2). |
| Humanize on the shared `rng()` would change step chance and arp random results. | Own generator (Q3). |
| With an external clock, the next step time is a prediction. An early note can come before a tempo change shows. | Use the clock's predicted step length, as the slicer and arp do. Accept a small error. |
| The browser emulator is not the device. Timing feel on the FM-1 is untested. | Keep the offsets in samples from the audio clock, as the device does. Listen on the FM-1 when one is available. |
| A velocity drop changes the sound of some engines more than others (filter envelopes, FM). | Listen on all five engines in M5. Lower the 100 % depths if needed. |

## Decided

- One knob, HUM, in the TEMPO page's free slot. No separate timing and velocity amounts (2026-10-11).
- Velocity curve: fixed beat accents plus random jitter per note (2026-10-11).
- Accents stay loud; recorded and default velocities get the curve and jitter (2026-10-11).
- Timing: early and late, centred on the grid (2026-10-11).
- 100 % means ±20 ms timing and ±25 velocity jitter; the curve's deepest dip is 25 (2026-10-11). Replaced below.
- After the first listening test (808 KIT, CZ-1 ELEC.BASS 1, ELEC.PIANO; barely audible even at 100 %): 100 % means
  ±30 ms timing and ±30 velocity jitter; the curve dips 12 (beat 3), 24 (beats 2 and 4), 36 (8th off-beats) and
  50 (16th off-beats). The old dip of 25 was about −2 dB on the 808 (2026-10-11).
- Sources: the sequencer on all four tracks and the arpeggiator. Not live keys, MIDI IN or recording (2026-10-11).
  Timed notes added below.
- All notes of one step share one timing offset (2026-10-11).
- The variation is new on every pass (2026-10-11).
- Gate humanize is in v1. Phrase accent and per-track feel are later (2026-10-11).
- The web editor shows HUM in v1 (2026-10-11).
- HUM is saved with the project, default 0 % (2026-10-11).
- The beat curve uses the note's place in the global 4/4 bar, not the track's step number, so polymeter and DIV
  work (2026-10-11).
- Timed notes (live recording, Capture, edited notes and hits) are humanized as they play, like steps: velocity,
  timing and gate. Their stored onsets stay. The offset comes from a hash of the step and the loop pass, so a step's
  notes move together; a note at the loop's start is never early, one at its end never late (2026-10-11).

## Open questions

- The exact curve values for beats 2/4 and the 8th off-beats. Tune by ear in M5.
- ELEC.BASS 1 (CZ-1) ignores velocity (amp velocity sense 0): only timing and gate reach it. Engine velocity response
  is out of scope here.
