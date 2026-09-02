# MIDI Generation

## Purpose

When a song supplies MIDI, the pipeline can generate source-backed difficulty profiles that fit the native piano interaction. The generator is deterministic: identical semantic inputs produce identical profile data and cache identity.

Repository loading parses and normalizes the MIDI source once into immutable event,
tempo, meter, and source-identity data. Every independently evaluated difficulty
profile consumes that same normalized value; profile generation does not reread or
renormalize the file and does not share mutable analysis state.

## Input Behavior

- MIDI format 0 and 1 timelines are supported; format 2 is rejected.
- Tempo and meter events are linked before chart generation.
- Drum-channel events and pitches outside C1-C7 are ignored.
- No pitch is invented or transposed.
- Actions before the established audio lead-in or after known source-audio duration are excluded.
- Explicit JSON notes remain authoritative and bypass MIDI reduction.

## Profile Behavior

The generator clusters humanized onsets, tracks a melody path, groups alternatives by native frame, and searches source-backed reductions under calibrated movement, density, stream, jack, reversal, fatigue, imbalance, timing-irregularity, and recognizability constraints.

Distinct-frame, right-hand-only events that violate the selected profile's
existing right-hand spacing threshold may form a contiguous group instead of
being removed solely for that violation. The threshold is the profile's spacing
constraint in source seconds, not a universal frame window: easier profiles can
therefore automate a longer fast run while a harder profile leaves the same
representable events as manual inputs. Events that already satisfy the profile
spacing remain ungrouped. A root can own multiple followers when every adjacent
gap in the uninterrupted run violates that threshold; nonadjacent members of
that same run do not conflict again. A left-hand, mixed-hand, or non-monotone row,
or an adjacent gap that satisfies the threshold, ends the run and forbids grouping
across it. Same-frame collisions are still collisions. The group
root, not its followers, counts as the required right-hand action for gameplay
metadata and strain; an independent left-hand chord remains its own action. All
retained followers remain physical chart rows, and the native row limit still
applies to those rows. Generated output also fails closed rather than requiring
more than 255 distinct byte-valued groups.

Each actual difficulty label is evaluated independently. A profile is exposed only when it is meaningfully larger than the previous visible profile, retains sufficient exact source identity, passes the shared validator, and remains within the playable chart limit. Failed labels are omitted without renumbering; higher labels may still be evaluated until a monotone size bound proves they cannot fit.

Generated level is not a simple function of row count, actions per minute, pitch span, or one strain statistic. Authored calibration routes overlap, and incomplete recovered evidence is treated as unknown rather than as rests or easy material.

## Timing And Audio

Generated charts preserve source onset identity. MIDI offset, alignment, and minimum lead-in participate in chart/audio alignment and cache identity. The required base audio owns chart timing and descriptor duration. Optional metronome guide synthesis changes only resolved MABF `Mode0`; `Mode1` and `Mode2` remain their clean resolved sources. It does not change chart timestamps, selected difficulty, or source duration.

Before any difficulty reduction, eligible C and C-sharp events in the complete
canonical right-hand source sequence are assigned normal or verified `_2`
monotone identities by a contour planner. For each event, the immediately
preceding and following canonical source pitches contribute one mismatch when
the normal low-C sector is used beside lower material or the `_2` high-C sector
is used beside higher material. The planner lexicographically minimizes total
mismatches, then sector transitions between consecutive eligible source events,
then the number of `_2` assignments. Thus an eligible event at the upper boundary
of lower material prefers `_2`, one at the lower boundary of higher material
prefers normal, and repeated/tied eligible runs resolve uniformly to normal. The
result is stored by exact immutable MIDI source identity before profile selection,
so an event that survives in different profiles keeps the same identity. No hash,
ordinal alternation, or RNG participates. Exact source chord voicing is retained until IgnoreSound
derivation. Exact chord matches remain preferred and unchanged. A partial source
harmony of at least three distinct pitch classes may use a native chord superset
only when exactly one supported template contains its pitch classes, its lowest
source tone is that template's root, each class has one source event on one
track/channel, and
every intended pitch class exists in the verified native voicing. IgnoreSound is
then populated, in native table order, only with mapped native constituents absent
from the intended source harmony. Ambiguous qualities, inversions, unsupported
voicings, or more than three required ignores decline the partial match.

Native ninth assignments deliberately omit the template seventh: their verified
voicing is root, third, fifth, and ninth. Superset matching therefore declines a
partial ninth that relies on the seventh, and never emits that nonmember as an
ignored sound.

## Author Expectations

- Sparse levels are expected and their actual labels are displayed.
- A profile can be omitted because no valid source-backed witness fits every constraint.
- A level whose minimum target exceeds 512 rows is not clipped into a publishable profile.
- Different source MIDI quantization, tempo maps, or note provenance can materially change results.
- Inspect logs and generated profile diagnostics before changing authoring inputs merely to force a level.

Dated authored-route calibration, fixture metrics, omissions, and performance measurements are repository-only evidence rather than part of this durable contract.
