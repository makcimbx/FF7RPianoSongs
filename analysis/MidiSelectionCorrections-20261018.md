# Source-exact LH and local MIDI selection corrections — 2026-10-18

## Scope and baseline

Integration follow-up to the [installed-song audit](InstalledSongGenerationReview-20261018.md).
The user authorized three changes: literal LH voicing, local transition selection,
and representative attacks in source-active chart gaps. This is not a release or
a controller-playability qualification.

Comparisons use the frozen installed-source inventory from that audit, not a
second mutable read of the installed music library. There are separate cold
scratch copies for each production-tool revision; the already audited baseline
exports are reused with their input/tool hashes checked. Authored Rufus remains
an unchanged control, including its custom voicings and nonstandard label.

Baseline tool SHA256:
`c866fbb58c9a82014d161894dfb2f370e57fa7efdb6b14521a339fbd8edc5fc9`.

The comparison runner independently checks complete row/event/root counters,
literal source-pitch evidence, exact RH follower stream/tick witnesses, profile
presence, native assignment changes, and source-active gaps. It does not equate
legacy prefix counts with full charts, or source coverage with musical salience.

## Existing custom-voicing boundary

The existing override mechanism is authored-only and song-wide: one replacement
sound list per native chord ID, shared across every profile. It is not a free
per-event voicing slot for automatic MIDI. Source/config/cache equality and the
verified-width/native preflight remain authoritative. This task does not weaken
those checks or introduce a new automatic palette/ID-allocation contract.

A reduced native carrier can instead represent one or two literal fresh source
tones through the existing IgnoreSound path. Such a carrier must claim only its
emitted pitches, not the entire original harmony. A source bass outside the
verified native chord range cannot be invented in a different octave.

## Intermediate strict-admission comparison

The first guard-only tool (`18a47da32bca5ac7dcf83c807af0301888b9815e3a7625c68a452b7ce3356f8f`)
completed all isolated preparations. Across the generated profiles, independently
detected LH rows lacking literal source pitches fell from **1,038 to zero**.
This alone reduced LH inputs from **3,980 to 3,236**, so it is not treated as a
complete accompaniment fix. Potentially represented raw source-note attacks rose
from 85,701 to 87,472; these are sums over independently generated difficulties,
not distinct musical phrases or perceptual scores. Required-input totals changed
from 56,437 to 56,409 and no required ending moved. Authored control notes and
custom voicings remained exactly equal in the resolved projection.

## Source-exact reduced-carrier comparison

The follow-up tool (`4fcdbb865b1d30d56933bb6536845329a8a23aaad39fe3fe11f85e97d750542c`)
also completed every isolated preparation. It retains an actual source bass and,
when compatible, one further fresh tone; it does not discard an unsupported bass
to pretend higher tones are the original bass. This path is limited to recognized
full/superset harmonies that failed exact native voicing, not arbitrary chord
invention.

Across generated profiles, LH inputs recover to **3,311**, still with **zero**
literal-pitch mismatches. Potentially represented source attacks total 87,424;
required inputs total 56,416. Compared with the guard-only version this restores
75 LH inputs but exchanges some RH/source coverage through existing density and
route choices; it is not a universal improvement in every metric. All labels and
required endings remain unchanged, as does authored Rufus.

The Miku-like synthetic onset retains only source G#3 through a muted native
carrier, never Db2/F2/Ab2. Its unavailable C#4/F4 voices are not claimed as played.
The actual Miku Lv6 selection at 12.400 seconds instead keeps RH C5 alone;
neighboring LH attacks at frames 701 and 765 retain C#3 and D#3. The synthetic
case proves the G#3 carrier is available, not that selection must add it between
those existing LH inputs. The real wrong-register chord is gone, but its full
harmony has not magically been restored.
The retained native table spans MIDI 36–59 for LH sounds on this target. Full
automatic voicing outside that range needs a deliberate contract extension or
authored custom voicings; this correction does not silently enable one.

## Local transition selection

The second correction enforces the existing per-hand spacing floor on both source
seconds and the emitted 60 Hz frame grid, including protected-tail neighbors and
dual rows. For example, source attacks 80 ms apart can round to frames only four
frames apart; checking source seconds alone did not enforce the stated floor on
the actual chart. No new human timing threshold is introduced.

At comparable musical value, beam pruning and final selection now use the same
cached utility: source salience minus inverse-frame-gap penalties for changed
native assignment IDs, plus simultaneous changes of both hands. Alternate RH
identities and LH assignments are retained; intervening other-hand events do not
erase the previous input. Insertion evaluates outgoing as well as incoming
links. Automatic followers do not add required-control cost. Same-ID repetitions
still retain the existing jack/action workload checks.

This is a small descriptive selection preference, not calibrated movement time,
angular travel or a promise that every fast passage disappears. Stronger musical
evidence may outweigh it. Source-exact synthetic on/offbeat controls retain their
melody and accompaniment; actual-corpus comparisons are recorded separately.

The frozen second-stage tool
(`b6e5892da7954d8a57c409a3c3d84521ea075960b1ee30380895b71059e538a4`)
completed all isolated songs. Independent required-root checks found **zero
native-frame spacing violations**, zero literal-LH mismatches and unchanged
required endings/profile labels. Authored Rufus remains unchanged. Across the
generated profiles there are 56,451 required inputs, 3,427 LH inputs and 87,489
potentially represented source attacks.

The categorical switch-rate sum decreases from 2,439.196 in the exact-carrier
stage to 2,394.374; simultaneous-change rate decreases from 8.064 to 8.045.
These aggregate descriptive costs are neither human difficulty scores nor a
guarantee that every local passage improves. The source-active-gap diagnostic
falls from 256 to 235 across levels. At Lv6 the previous Weight gap no longer
meets that diagnostic, but Gate (22.417–26.883 s) and Megalovania
(84.033–88.033 s) still do; these remain explicit targets for the next correction.

Independent inspection of the completed exports confirms a mixed local outcome:
combined categorical switch cost improves in 135 generated profiles, worsens in
30 and is unchanged in nine versus the carrier stage. Bilateral cost alone
increases in 50 profiles. Eyes Lv6 increases required inputs from 841 to 878;
Messenger Lv4 increases combined cost despite unchanged total inputs. Density
and musical preference remain ahead of workload minimization, and the bounded
beam is not a global optimizer.

The Weight four-frame transition is removed at Lv4/Lv6, but the Gerudo Lv1
A/G#/A clip and Messenger Lv6 bilateral clip remain. Descriptively measured
Somnus Lv6 and Alicia Lv5 RH series become longer. Lumiere Lv4 loses 17 distinct
RH onset/pitch pairs, including B3/E4 around 101.750/102.117 seconds; this check
alone does not establish loss of all LH sounds or perceptual importance.
Accordingly, this stage fixes frame-policy correctness and adds a preference;
it does **not** establish uniformly reduced fatigue or solve every fast passage.
The independent script, per-profile comparisons and exact clips are retained in
`design/stage2-review.py`, `.json` and `.md` under the local evidence root.

## Bounded representative-root repair

After beam selection, one internal RH-only root may be exchanged for an already
admitted fresh source candidate. The first/last roots, protected melody, dual
rows and LH roots are preserved. A candidate already claimed by another RH/LH
source witness is ineligible. Coverage is the sorted descending distance from
admitted source-onset frames to selected required-root frames, not a phrase or
audio-perception model.

The exchange must improve that coverage vector without reducing raw source
salience or increasing categorical switch cost, while satisfying existing
source/frame spacing and a coherent difficulty route. Search is bounded to eight
targets/donors and eight full validation attempts; at most one exchange commits.
The original and proposal are compiled with their actual automatic followers
and active row/event policy. An unpublishable proposal leaves the original chart
unchanged; keeping input count alone does not prove row/event capacity.

The positive regression reduces the coverage radius from 80 to 40 frames with
four required inputs. A capacity regression keeps the original complete
512-row chart under ordinary policy and admits its 513-row repair only under
extended policy. Negative fixtures cover unavailable fresh evidence, source
reuse, protected roots, infeasible spacing/routes and musical/control losses.
These checks demonstrate the operation and rollback, not universal gap removal.

The final tool
(`a4e79e758a6bb5d5846d658618f30a3fdde9bdafc22c7f80a0f59226fdf1d623`)
completed all 30 isolated songs / 178 profiles. Independent export comparison
finds one RH-only exchange in **106 generated profiles**, none in the others;
every profile keeps its required-input count and LH roots versus stage two.
Combined categorical switch cost never increases per profile. Exact source and
follower checks, native-frame floors, all labels/endings and authored Rufus pass.

Across generated profiles, required inputs remain **56,451**, including 3,427 LH;
literal-LH mismatch rows remain **zero**. Potentially represented source attacks
increase from 87,489 to 87,518. Diagnostic source-active gaps decrease from 235
to 215 across levels; there are **none at Lv6 under that diagnostic**. This does
not imply that every source attack or perceptually important phrase survives.

Concrete Lv6 exchanges (seconds are compiled frame / 60):

| Song | Added source-backed RH root | Displaced RH root |
| --- | --- | --- |
| Gate of Steiner | C4 at 24.183 s | G4 at 228.783 s |
| Megalovania | B2 at 86.033 s | C3 at 118.283 s |

These are representative attacks, not restoration of every note in either gap.
The sorted coverage vector and raw salience improve/nondecrease by construction;
the displaced notes' perceptual importance is not independently established.
Bilateral cost alone can rise even when combined switch cost falls, so this
operation also does not prove less bimanual fatigue.

## Focused validation and remaining scope

Canonical target builds and all eight selected CTests passed: MIDI generation,
compilation, analysis, repository cold/warm behavior, runtime-cache codec,
manifest rendering, extended-chart model and documentation audit. Each frozen
production-tool stage was checked on isolated source copies; no installed cache
was used as a writable test target. This is Integration evidence, not release
packaging or gameplay qualification.

The generated-policy identity advances to v22. Generated MIDI caches rebuild;
authored source formats, custom-voicing behavior and binary layout are unchanged.
Remaining tradeoffs include native LH pitch-range limits, reduced/unselected
accompaniment, stage-two local regressions and longer series, bounded repair
search, and unverified perceptual/controller comfort. No global hand quota,
invented harmony, transposition, new physiological threshold or runtime hook
change was introduced.

## Evidence retention

Local evidence root:
`C:/Users/makci/AppData/Local/Temp/opencode/piano-three-fixes-20261018/`.

`baseline/` preserves the pre-change tool/source/diff, `runs/` contains isolated
cache preparations and independent summaries, and `design/` retains bounded
assertion-backed control/gap examples. These are local reproduction artifacts,
not packaged game assets or a second product tree.
`runs/coverage/repair-verification.json` records each actual exchange and the
independent count/LH/combined-cost assertions; per-stage `comparison.json` files
retain spacing, endings, gaps and exact real-song clip differences.

## Qualification boundary

No installed source/cache, INI, ASI or proxy is intentionally changed by these
offline runs. No game is launched or attached. Generated dual sound/scoring,
actual stick retrigger/sector rules, controller comfort and perceptual phrase
preservation require their own focused evidence; see the
[control-semantics limits](PianoControlSemantics-20261018.md) and
[manual validation procedure](../docs/InGameValidation.md).
