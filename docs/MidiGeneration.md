# MIDI Generation

## Purpose

When a song supplies MIDI, the pipeline can generate source-backed difficulty profiles that fit the native piano interaction. The generator is deterministic: identical semantic inputs produce identical profile data and cache identity.

Repository chart generation parses and normalizes the MIDI source once into immutable
event, tempo, meter, key-signature, and source-identity data. Every independently evaluated difficulty
profile consumes that same normalized value; profile generation does not reread or
renormalize the file and does not share mutable analysis state. Independent consumers,
such as metronome-beat extraction, may parse the source separately.

## Input Behavior

- MIDI format 0 and 1 timelines are supported; format 2 is rejected.
- Tempo and meter events are linked before chart generation.
- Valid MIDI key-signature meta events are retained with exact tick, track,
  event order, signed fifth count, and major/minor mode. MIDI note events carry
  semitone numbers, not authored sharp/flat spelling. For black keys, generation
  uses flat spelling only while every track with active key-signature evidence
  consistently reports negative fifths. Missing signatures, neutral signatures,
  positive signatures, or conflicting active track evidence retain the legacy
  sharp spelling. Changes apply at their exact source tick. Titles, filenames,
  track names, and speculative chord function do not participate.
- Drum-channel events are ignored. Valid linked pitched events outside C1-C7
  (MIDI note numbers 24–96 inclusive are supported) are excluded before selection,
  with diagnostics identifying the exclusions. The same rule applies to ordinary
  and extended chart policies: an unsupported pitch does not by itself reject all
  otherwise usable profiles. A source with no supported pitched notes still fails.
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
salience, source preference, calibrated density goals, and route limits may select
different source events and produce different physical digests. A previously visible
profile is only source-backed preference evidence for the next label; it is not a
shared physical chart or a required nested root set.

Density targets guide selection; they are not a minimum number of inputs required
to publish a difficulty. A nonempty, source-backed chart that satisfies the actual
route and physical constraints can remain below its preferred density. Likewise,
growth from the preceding visible profile is a preference, not a reason to discard
an otherwise valid later label after an intermediate label was omitted. Higher
labels need not have strictly more rows: timing and input patterns also determine
their load. These preferences do not authorize relaxing the route's load ceiling.

Retained melody selection uses humanized onset context, then restores authoritative
source-event timing before timing-domain filtering, prominence measurement, and
materialization. Inferred chords retain their established cluster timing. Failed
labels are omitted without renumbering, and later labels are still evaluated.

Ordinary or unsupported-policy generation remains bounded by 512 rows. When the
verified exact-build extended policy is playable, automatic MIDI may select a complete
profile through the existing 8192-row and 8192-native-event limits. It is never
clipped: an ineligible or oversized profile is omitted fail-closed. This changes
only offline selection and generated cache identity; runtime chart representation
and explicit JSON grouping support are unchanged.

The 8192 representation limit is not an authorization for unbounded selector work.
Selection remains deterministic and bounded independently of elapsed time. The
work bound limits optimization, rather than establishing that the source cannot
produce a playable chart. A result still needs full-chart route, source, timing,
and physical validation: reaching a search bound is not permission to publish an
unchecked candidate or truncate the song at the last processed source event.
If no valid nonempty result is available, the profile remains omitted with a reason.

The beam keeps a route-feasible alternative where available, rather than allowing
density-first ranking to discard every feasible state. Its breadth adapts to the
projected analysis cost; at least the route traversals remain, and all candidate
frames are considered. The projection allowance applies to additional optimization
breadth, not to total CPU instructions or elapsed time. It is therefore not a
promise of instant generation or a strict total-operation ceiling. Cold diagnostics
report the selected beam width, traversed frames, measured full/prefix skill-analysis
row visits, and outcome. Warm-cache reuse does not repeat that optimization.

Generated level is not a simple function of row count, actions per minute, pitch span, or one strain statistic. Authored calibration routes overlap, and incomplete recovered evidence is treated as unknown rather than as rests or easy material.

## Timing And Audio

Generated charts preserve source onset identity. MIDI offset, alignment, and minimum lead-in participate in chart/audio alignment and cache identity. The required base audio owns chart timing and descriptor duration. Optional metronome guide synthesis changes only resolved MABF `Mode0`; `Mode1` and `Mode2` remain their clean resolved sources. It does not change chart timestamps, selected difficulty, or source duration.

Generated notation is independent per side and does not change row timing. A
retained monotone receives an explicit native note/dot value only when its
authoritative source tick length exactly equals one of the ten schema values
(whole through dotted sixteenth) as a rational multiple of the MIDI ticks per
quarter. An inferred chord receives an explicit value only when every represented
constituent has the same exact supported tick length. The resulting native types
are 0 through 4 (whole through sixteenth); native one-third and one-sixth types
5 and 6 are intentionally unsupported. Nonmatching or heterogeneous
lengths use that side's legacy fallback from the generated row `duration_beats`.
Integer tick-ratio comparisons are deterministic; no floating-point duration
guessing is used. Generated rows remain ungrouped. This derivation is covered by
`midi_generation=independent_ungrouped:key_signature_spelling+exact_note_values+exclude_unsupported_pitches+soft_density_growth+bounded_feasible_beam:v13`.

Before any difficulty reduction, eligible C and C-sharp events in the complete
canonical right-hand source sequence are assigned normal or verified `_2`
monotone identities by a contour planner. Eligibility is restricted to octaves
2–6 for both classes (MIDI C2=36 through C#6=85); no boundary `_2` is invented.
Ordinary C7 already uses the high-C native assignment. Exact 1.005 file-data
evidence is retained in [Monotone Assignment Inventory](../analysis/PianoMonotoneAssignEvidence-20260905.md);
this does not establish exact 1.004 monotone parity or in-game input behavior.
For each event, the immediately
preceding and following canonical source pitches contribute one mismatch when
the normal low-C sector is used beside lower material or the `_2` high-C sector
is used beside higher material. The planner lexicographically minimizes total
mismatches, then sector transitions between consecutive eligible source events,
then the number of `_2` assignments. Thus an eligible event at the upper boundary
of lower material prefers `_2`, one at the lower boundary of higher material
prefers normal, and repeated/tied eligible runs resolve uniformly to normal. The
result is stored by exact immutable MIDI source identity before profile selection,
so an event that survives in different profiles keeps the same identity. No hash,
ordinal alternation, or RNG participates. A flat-spelled event never receives a
C-sharp `_2` identity. Automatic chord IDs remain pitch-class based except for
the separately verified ordinary D-flat major assignment. Pitch-class-1 major
harmony uses `pca_Db` only when every source constituent's authoritative tick
resolves to the same unambiguous flat key-signature context and the
compile-selected exact generated catalog exposes verified D-flat-major asset
  evidence. Missing, neutral, positive, conflicting, boundary-straddling, and
  unknown-build evidence retains `pca_Cs`. Exact and safe-superset
paths make the same identity decision before IgnoreSound is derived from that
selected chord's complete verified constituent row. The selected native-asset
capability identity participates in every authored-JSON and MIDI repository
  cache key. Exact 1.004 and 1.005 use one verified asset capability after
  semantic equality of all 170 rows and the owner's exact-source provenance
  attestation; runtime name resolution remains fail closed. This offline rule
  is not a human runtime qualification for build 1.004.
Exact source chord voicing is retained until IgnoreSound
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
- A generated profile can be omitted when selection cannot produce a valid
  nonempty source-backed chart within the route and physical constraints. Missing
  a preferred density or growth target alone is not a rejection reason.
- Unsupported-policy levels remain subject to the 512-row reduction boundary.
- Verified extended levels are never clipped: complete selected material must fit
  both 8192 bounds and the restricted publication contract, or the profile fails closed.
- Different source MIDI quantization, tempo maps, or note provenance can materially change results.
- Inspect logs and generated profile diagnostics before changing authoring inputs merely to force a level.

Every successful cold or warm load best-effort refreshes `.cache/resolved-song.json` with the
visible generated profiles, their actual sparse labels, and every complete row including validated
extended tails. It is source-compatible convenience output, not cache or gameplay authority. Copy
its `profiles` into `song.json` to freeze the current generated result as explicit authoring; those
explicit profiles then take precedence even if `song.mid` remains. Omitted generated labels remain
absent, and profile-varying derived metadata is recomputed rather than frozen misleadingly at the
root.

Dated authored-route calibration, fixture metrics, omissions, and performance measurements are repository-only evidence rather than part of this durable contract.
