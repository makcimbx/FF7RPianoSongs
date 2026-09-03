# MIDI Generation

## Purpose

When a song supplies MIDI, the pipeline can generate source-backed difficulty profiles that fit the native piano interaction. The generator is deterministic: identical semantic inputs produce identical profile data and cache identity.

Repository chart generation parses and normalizes the MIDI source once into immutable
event, tempo, meter, and source-identity data. Every independently evaluated difficulty
profile consumes that same normalized value; profile generation does not reread or
renormalize the file and does not share mutable analysis state. Independent consumers,
such as metronome-beat extraction, may parse the source separately.

## Input Behavior

- MIDI format 0 and 1 timelines are supported; format 2 is rejected.
- Tempo and meter events are linked before chart generation.
- Drum-channel events are ignored. Generated profiles reject a linked pitched
  event outside C1-C7 when verified extended policy is active; unsupported
  policy retains the established safe fallback behavior.
- No pitch is invented or transposed.
- Actions before the established audio lead-in or after known source-audio duration are excluded.
- Explicit JSON notes remain authoritative and bypass MIDI reduction.

## Profile Behavior

Every difficulty is reduced independently from the same immutable normalized
source. The deterministic selector tracks a monophonic right-hand melody, retains
source-backed fallback attacks where appropriate, and keeps exact or uniquely safe
native-superset chords. Exact source voicing remains attached to inferred chords so
IgnoreSound can suppress only verified extra native constituents. Accepted chords
are not converted to monotones merely to simplify publication.

All automatically generated rows serialize with `group_index = 0`. Every generated
monotone or chord event is therefore a parentless required action and participates
in spacing, route, strain, score-growth, and recognizability analysis. The selector
does not create automated followers or run topology. Difficulty-specific spacing,
salience, source preference, calibrated target bands, and route limits may select
different source events and produce different physical digests. A previously visible
profile is only source-backed preference evidence for the next label; it is not a
shared physical chart or a required nested root set.

Retained melody selection uses humanized onset context, then restores authoritative
source-event timing before timing-domain filtering, prominence measurement, and
materialization. Inferred chords retain their established cluster timing. Failed
labels are omitted without renumbering, and later labels are still evaluated.

Ordinary or unsupported-policy generation remains bounded by 512 rows. When the
verified 1.005 extended policy is playable, automatic MIDI may select a complete
profile through the existing 8192-row and 8192-native-event limits. It is never
clipped: an ineligible or oversized profile is omitted fail-closed. This changes
only offline selection and generated cache identity; runtime chart representation
and explicit JSON grouping support are unchanged.

The 8192 representation limit is not an authorization for unbounded selector work.
Before incremental beam expansion, generation computes an overflow-safe upper bound
on full-chart and finalized-prefix local-skill row visits from candidate frames,
candidate actions, beam width, and the profile's maximum selected rows. A projection
above the fixed offline analysis budget fails closed with `ChartStrainLimitExceeded`;
it does not use elapsed time, reduce the target band, clip rows, or publish a partial
profile. Sparse extended charts and ordinary protected-corpus-sized reductions remain
inside this separate computational contract.

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
- A generated profile can be omitted when no source-backed reduction in its
  calibrated band satisfies a supported route.
- Unsupported-policy levels remain subject to the 512-row reduction boundary.
- Verified extended levels are never clipped: complete selected material must fit
  both 8192 bounds and the restricted publication contract, or the profile fails closed.
- Different source MIDI quantization, tempo maps, or note provenance can materially change results.
- Inspect logs and generated profile diagnostics before changing authoring inputs merely to force a level.

Dated authored-route calibration, fixture metrics, omissions, and performance measurements are repository-only evidence rather than part of this durable contract.
