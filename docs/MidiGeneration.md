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
- Drum-channel events are ignored. The generalized physical-chart path rejects a
  linked pitched event outside C1-C7; the legacy fallback retains its prior
  behavior of ignoring those events.
- No pitch is invented or transposed.
- Actions before the established audio lead-in or after known source-audio duration are excluded.
- Explicit JSON notes remain authoritative and bypass MIDI reduction.

## Profile Behavior

When verified generalized chart policy is available, the generator first builds
one profile-independent reduced physical chart. The established deterministic
melody tracker retains one monophonic right-hand voice across humanized onset
clusters, while exact or unique-safe-superset chord inference retains sparse
left-hand harmony at the spacing accepted by the hardest profile. Inner voices,
doubled melody tones, and ambiguous harmony are not emitted as extra monotones.
Retained melody attacks use their authoritative source-event timing rather than
the humanized cluster anchor, including for timing-domain filtering and audio
prominence. Inferred chord rows retain their established cluster-anchor timing;
out-of-domain constituent identities do not contribute to retention. Generated rows contain exactly one native event:
the retained melody monotone precedes an optional inferred chord at the same
frame. The chart rejects rather than truncates above either 8192 physical rows or
8192 native events.

Generalized diagnostics distinguish the normalized source inventory from the
eligible reduction domain. `source_events` is the supported normalized note count
before lead-in and known-audio-duration filtering. `candidate_actions` excludes
those timing rejections but still includes inner voices and source doublings.
`selected_retention` is the fraction of those eligible source identities represented
by either the retained melody or the source constituents of retained chords; it is
not the physical-row or required-action ratio.

Every profile uses this identical physical row/event identity. Difficulty changes
only deterministic row-level group topology and therefore the number of required
roots. A run root or continuation may be a monotone or chord row; explicit authored
dual rows remain supported by the shared model. Mandatory equal-time RH edges are
always grouped. A compact selector works directly on canonical physical-row
identities; it never translates roots from the legacy reduced candidate domain and
never removes rows from the shared reduced chart. Previously visible roots are pinned, and later profiles
add only physical-row roots, so easier root sets remain subsets of harder ones.
Before route selection, consecutive equal-frame monotones become indivisible
atomic units and a deterministic planner adds the smallest root set needed to
stay inside the vanilla-derived automation envelope: at most seven followers,
125 frames from root to last follower, 102 frames between adjacent run rows, and
no more than zero, one, and two positive-delay followers within 3, 6, and 15
frames of the root respectively. Same-frame followers count toward the total but
not those positive-delay windows. A chord is always a separate mandatory root,
as is the first physical unit at or after an explicit MIDI meter change. An
atomic unit that cannot satisfy the envelope by itself rejects the profile.

At each later addition the selector considers a bounded deterministic frontier
favoring downbeats, canonical melody and salience, contour reversals, leaps of at
least seven semitones, and temporal undercoverage. Thirty-frame run spans and
twenty-frame adjacent gaps are soft coverage targets, not extra rejection
limits; repeated pitches alone never create a boundary. Exact root-only route
quality breaks otherwise equal ranked choices. Group
IDs are run-local serialization values and cycle through 1..255 after a zero or
different separator. Every event on a continuation row is automated and does not
enter action, strain, score-growth, or recognizability counts. Calibrated APM and
tolerance define a target band over the complete physical active span. The selector
uses the lower edge of that band as its deterministic minimum, never drops below
inherited roots, and continues adding roots up to the upper edge when necessary to
find a route-feasible topology. Every candidate insertion must leave both adjacent
automation segments inside the hard envelope before it can enter the beam. Infeasible
beam states are ranked by exact root-only
route load normalized by that route's evidence margin before soft musical preference;
feasible states retain the documented
musical ranking. If mandatory or inherited roots exceed the profile target band,
or no physical-domain topology in that band passes the root-only route,
that label is omitted; profile generation never deletes additional physical rows.

Without verified generalized policy (including build 1.004), the accepted v44
legacy path remains unchanged: it clusters humanized onsets, tracks a melody path,
and searches source-backed reductions under calibrated constraints.

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
across it. In the legacy fallback, same-frame collisions are still collisions. The group
root, not its followers, counts as the required right-hand action for gameplay
metadata and strain; a chord on a continuation row is automated by the same
row-level run. All
retained followers remain physical chart rows, and the native row limit still
applies to those rows. Generated output also fails closed rather than requiring
more than 255 distinct byte-valued groups. Generalized output instead reuses IDs
across separated runs.

Each actual difficulty label is evaluated independently. In the generalized path, every
visible profile has the same physical digest and a strictly larger nested parentless-root
set than the previous visible profile; duplicate root topologies are omitted, but legacy
minimum/maximum action-growth percentages do not reject an otherwise valid generalized
topology. The legacy path retains its meaningful-growth and exact-source-overlap rules.
Failed labels are omitted without renumbering; higher labels may still be evaluated.

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
- A generalized profile can be omitted when no nested physical-root topology in its
  calibrated band satisfies a supported route; legacy profiles retain source-witness rules.
- Legacy fallback levels remain subject to the 512-row reduction boundary.
- Generalized levels are never clipped: complete physical material must fit both
  8192 bounds and a valid topology, or the profile fails closed.
- Different source MIDI quantization, tempo maps, or note provenance can materially change results.
- Inspect logs and generated profile diagnostics before changing authoring inputs merely to force a level.

Dated authored-route calibration, fixture metrics, omissions, and performance measurements are repository-only evidence rather than part of this durable contract.
