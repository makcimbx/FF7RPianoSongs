# Installed Song Generation Review — 2026-10-18

## Scope and identities

Debug/offline inspection of the user's actual installed song inputs, not release
qualification or an installation. The snapshot contains 30 songs: 29 MIDI-backed
sources and one authored source, Rufus. Both installed exports and fresh generation
contain 178 profiles. Rufus retains its authored labels 1, 2, 3, and 7.

The production cache tool used for fresh generation has SHA-256
`c866fbb58c9a82014d161894dfb2f370e57fa7efdb6b14521a339fbd8edc5fc9`.
Its configured target is the newer catalog build. `--playable-extended` matches
the captured enabled setting and the installed log's verified extended-chart
capability; this offline option does not grant runtime authority.

## Method and completed checks

- Copied source files into isolated, cache-empty directories. Recorded source,
  tool, configuration, installed manifest, and resolved-export SHA-256s.
- All 30 cold preparations succeeded. Every existing difficulty label remains
  present; missing labels were not silently substituted.
- Independently counted complete exported rows, events, and required roots for
  every installed profile: all 178 agree with the manifest's complete-plan
  fields. For 73 profiles, legacy prefix-row summaries are smaller than the
  complete export; those prefixes must not be mistaken for the whole chart.
- Each installed manifest cache key matches that song's accepted load recorded
  in the captured game log. This corroborates cache admission, not gameplay of
  every profile or acceptance of the newly generated charts.
- Rufus's notes in all four profiles and its custom chord voicings are exactly
  unchanged between installed and fresh resolved exports. It is not a MIDI
  reduction experiment. Stock-only benchmark mapping deliberately rejects custom
  voicings; structural counters can still be checked without substituting stock
  pitches for Rufus's sounds.
- After generation, hashes confirm all 151 inventoried installed source/export/
  manifest files and the INI remain unchanged. Installed binaries were not
  replaced; no game launch, attachment, or gameplay was performed.

Required roots, automatic followers, physical rows, and chord sound constituents
are different quantities. Reports use adjacent native group runs and count both
parentless actions on a dual root row. Assignment changes remain categorical:
enum differences are not angular distances, and reset/modifier/judgment costs
remain unknown. See [control-semantics evidence](PianoControlSemantics-20261018.md).

## Findings, ranked by evidence

Independent workload review covers all 30 songs/178 profiles. Independent
MIDI-source review covers all 29 generated songs/174 profiles in both installed
and current exports; Rufus is checked separately as an authored control.

### 1. Confirmed LH register substitution

Miku, difficulty 6, 12.400 s: the source harmony contains G#3/C#4/F4,
but the emitted `pca_Db` encodes Db2/F2/Ab2 (MIDI 37/41/44), without
`ignore_sound`. The new row also retains the source C5 RH input. The same-time
source identities do not make the chord's actual native pitches source-exact.

An independent bounded SMF reader reproduced the witness: tick 25200,
source time 12.352935 s, total offset 0.0405 s, compiled frame 744.
Track 0/channel 0 contains MIDI 72; track 1/channel 0 contains 56/61/65,
all velocity 80. None of 37/41/44 occurs at that source onset. Source MIDI
SHA-256: `b5377b151b460c6aaf08e94b0dc15af6bbe50a11cd57f7783117403e03fa2807`.

`build_chord_candidates()` in `src/pipeline/midi_chart_compilation.cpp` builds
`fresh_pitch_classes` and calls `infer_native_chord_match()` before the stricter
one/two-note carrier path. That full-match path records source identities but does
not check their absolute pitches against the chosen native voicing. The unique
superset path also derives its ignores from pitch classes, not absolute pitches;
it needs the same admission check. One/two-note carriers already compare exact
absolute pitches. The exact
Db constituent list is independently present in the recovered stock table and
`src/pipeline/native_chord_constituents.h`.

Across all generated difficulties, the independent source comparison found:

| Diagnostic | Installed | Current |
| --- | ---: | ---: |
| LH rows | 3,050 | 3,980 |
| Rows with native absolute pitches absent from corresponding source onsets | 910 | 1,038 |
| Affected songs / profiles | 24 / 128 | 24 / 135 |
| Unmatched native pitch occurrences | 2,338 | 2,719 |

These are profile-summed diagnostic occurrences, not distinct bad source phrases.
Matching considers every eligible source stream at the snapped 60 Hz onset;
a separate ±2-frame check gives the same absolute-pitch counts. It does not
recover every hidden LH source identity from the export. After allowing that
humanization window, all unmatched absolute pitches still have source support
for their **pitch class**: the observed problem is register/voicing substitution,
not demonstrated invention of new pitch classes. Twelve exact-frame class
exceptions are the same two humanized Messenger rows repeated over six profiles,
in both installed and current versions; they disappear in the window check.

This conflicts with the documented no-invented/no-transposed-pitches contract.
It predates dual selection; dual selection exposes it more often by admitting
previously competing LH candidates. The earlier native-chart roundtrip corpus
already used native voicings and did not expose this external-MIDI case. Fix the
full-match admission/witness check before treating higher LH counts as a musical
quality improvement. No production fix was made during this inspection.

Existing tests explain why successful checks did not reject this: chord
recognition tests explicitly permit inversions as fixed native IDs, while
voicing tests check stored `source_chord_pitches` metadata rather than equality
with the actual emitted native pitches. That metadata is serialized/hashed but
does not override the native voicing; the final source-pitch witness counter
only checks RH.

### 2. Fast changes still survive the difficulty model

These are required roots, not automatic followers. Times are on the resolved
audio timeline; frame intervals use 60 Hz. No human impossibility is inferred.

| Current source/profile | Passage | Observed demand |
| --- | --- | --- |
| Gerudo Valley, d1 | 62.033–62.283 s | A4 → G#4 → A4, gaps 8/7 frames; both KeyAssign and Semitone change |
| Weight of the World, d4/d6 | 189.683–189.750 s | B4 → G#4 in 4 frames; KeyAssign/Semitone change |
| Yuffie Theme, d3/d4 | 184.317–184.400 s | C6 → F6 in 5 frames; KeyAssign 0 → 3 |
| Somnus, d6 | 89.333–101.667 s | 56 RH roots, adjacent gaps 10–20 frames; surrounding gaps 42/40 frames |

Gerudo's semitone oscillation is outside the existing reversal counter's
`movement >= 3` condition. The lead independently verified its exact roots at
frames 3722/3730/3737 and the control sequence 5/0 → 4/1 → 5/0.
Weight's d4 minimum gap changed from 5 to 4 frames; d5 instead changed from
4 to 5. Independent difficulty selection explains non-nesting, not whether a
particular local transition is comfortable.

### 3. New bilateral demands need their own check

Messenger d6, 104.717–106.967 s contains three same-frame changes in both
hands, eight RH roots with gaps 11–23 frames, and LH PageIndex/KeyAssign
changes. Its complete profile has 58 simultaneous-root moments and 51
simultaneous-control-change moments, versus no simultaneous roots in its
installed generated profile. More LH involvement can add coordination demands
without materially increasing the total number of inputs.

### 4. Source-active passages can remain unrepresented

- Megalovania d6, 84.033–88.033 s: a gap between chart events despite 38 supported
  B1/B2 MIDI attacks in track 1/channel 0, velocity 80. The corresponding
  short source-audio excerpt was additionally assessed by the configured
  audio-capable model: it heard continuing rhythmic bass, not a full musical
  pause. This is supporting subjective evidence, not exact pitch transcription
  or an in-game A/B.
- Gate of Steiner d6, 22.417–26.883 s: a gap between chart events despite nine source
  notes, including sustained sonorities and C3. This establishes source activity,
  not the perceptual importance of each omitted note.
- Weight of the World d6, 225.033–228.683 s: another retained gap with nine
  source notes.

These gaps already occur in installed exports. The existing eight-second source
coverage bridge does not prohibit these shorter holes. Their musical quality
cannot be decided from average density or a successful generation return code.

### 5. One apparent melody regression is not yet a perceptual finding

Eyes on Me d6, 183.400 s: the installed export retained track-0 D5 with
MIDI duration about 4.09 s; current instead selects a shorter track-1 C4, with
no D5 follower. This is a concrete selection change. However, a separate short
source-audio assessment did **not** hear a clearly sustained upper voice over
short lower accompaniment. Do not label it proven melody loss from MIDI gate
length alone; retain it for targeted listening/source-arrangement comparison.
Diagnostic tracker replay still chooses D5, which passes primary/evidence gates;
the loss therefore occurs later during root/beam selection, not melody tracking.
The exact beam decision was not traced natively.

Across all 174 current generated profiles, the independent source audit found
no unsupported RH pitches, exact tick/stream follower violations, missing
difficulty labels, or prematurely omitted final eligible attacks. These negative
checks apply to the exports and source-matching method, not an independent
runtime-cache decode or in-game acceptance.

## Aggregate hand-role results

Median LH share across MIDI-generated songs, excluding followers and Rufus:

| Difficulty | Installed | Current | Current simultaneous-root moments, summed across songs |
| --- | ---: | ---: | ---: |
| 1 | 2.40% | 3.77% | 108 |
| 2 | 3.24% | 4.57% | 146 |
| 3 | 4.33% | 6.28% | 239 |
| 4 | 4.26% | 6.83% | 308 |
| 5 | 5.20% | 8.34% | 428 |
| 6 | 5.42% | 8.25% | 587 |

Total required actions across those profiles changed from 56,166 to 56,437
(+0.48%). The last required-input time did not change in any profile. Gerudo
remains wholly RH at every generated difficulty. Neither zero LH nor independent
non-monotonic profile selection is automatically a defect, and stock hand
shares are comparison data rather than quotas. In particular, the full-LH
voicing defect above prevents interpreting all additional LH events as faithful
accompaniment restoration.

No generation policy, source file, or difficulty threshold was changed for this
audit. Priorities are exact full-LH voicing admission, then targeted low-level
transition and source-active-gap comparisons; not a blanket LH quota or a new
uncalibrated human-difficulty threshold.

## Local evidence

Evidence is retained outside Git under
`C:/Users/makci/AppData/Local/Temp/opencode/piano-installed-review-20261018/`:

- `inventory.json`: source identities, source classification, and copy locations;
- `generation-results.json`: cold preparation outcomes;
- `installed-export-consistency.json`: complete-plan counter comparisons;
- `installed-log-binding.json`: accepted-load cache-key correspondence;
- `profile-presence.json`: difficulty coverage and authored-control equality;
- `preservation-check.json`: post-run source/export/configuration hash checks;
- `installed/` and `current/`: installed snapshots and isolated fresh outputs;
- `workload/` and `musical/`: independent analysis scripts and findings.
- `musical/full-batch-summary.json`, `musical/assertion-proof.json`: complete
  source-audit diagnostics and the assertion-backed Miku witness;
- `lead-verified-gerudo.json`, `lead-miku-output.json`: lead-checked event facts;
- `verify_miku_onsets.py`, `lead-miku-source.json`: independent source-onset proof;
- `audio-review.json`: bounded source-audio model assessments and limitations.

The local driver is `../inspect_installed_piano.py` relative to that evidence
directory. Private song/audio/cache files are not added to the repository.
