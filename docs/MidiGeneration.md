# MIDI Generation

## Purpose

When a song supplies MIDI, the pipeline can generate source-backed difficulty profiles that fit the native piano interaction. The generator is deterministic: identical semantic inputs produce identical profile data and cache identity.

## Input Behavior

- MIDI format 0 and 1 timelines are supported; format 2 is rejected.
- Tempo and meter events are linked before chart generation.
- Drum-channel events and pitches outside C1-C7 are ignored.
- No pitch is invented or transposed.
- Actions before the established audio lead-in or after known source-audio duration are excluded.
- Explicit JSON notes remain authoritative and bypass MIDI reduction.

## Profile Behavior

The generator clusters humanized onsets, tracks a melody path, groups alternatives by native frame, and searches source-backed reductions under calibrated movement, density, stream, jack, reversal, fatigue, imbalance, timing-irregularity, and recognizability constraints.

Each actual difficulty label is evaluated independently. A profile is exposed only when it is meaningfully larger than the previous visible profile, retains sufficient exact source identity, passes the shared validator, and remains within the playable chart limit. Failed labels are omitted without renumbering; higher labels may still be evaluated until a monotone size bound proves they cannot fit.

Generated level is not a simple function of row count, actions per minute, pitch span, or one strain statistic. Authored calibration routes overlap, and incomplete recovered evidence is treated as unknown rather than as rests or easy material.

## Timing And Audio

Generated charts preserve source onset identity. MIDI offset, alignment, and minimum lead-in participate in chart/audio alignment and cache identity. The required base audio owns chart timing and descriptor duration. Optional metronome guide synthesis changes only resolved MABF `Mode0`; `Mode1` and `Mode2` remain their clean resolved sources. It does not change chart timestamps, selected difficulty, or source duration.

## Author Expectations

- Sparse levels are expected and their actual labels are displayed.
- A profile can be omitted because no valid source-backed witness fits every constraint.
- A level whose minimum target exceeds 512 rows is not clipped into a publishable profile.
- Different source MIDI quantization, tempo maps, or note provenance can materially change results.
- Inspect logs and generated profile diagnostics before changing authoring inputs merely to force a level.

Dated authored-route calibration, fixture metrics, omissions, and performance measurements are repository-only evidence rather than part of this durable contract.
