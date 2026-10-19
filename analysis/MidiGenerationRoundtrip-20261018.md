# Stock attack roundtrip and exact dual-input selection

Date: 2026-10-18. Integration, offline only; no installed-artifact or human-playability qualification.

## Method and identity

Use the corrected [stock corpus](VanillaCorpusRecovery-20261018.md), not the
retained mod corpus. The [portable benchmark](../docs/VanillaBenchmark.md) expands
stock chord sounds and exports exact 60-fps attacks, preserving roots, followers,
hand IDs and raw control assignments in a separate sidecar.

The pre-edit baseline was built/verified through the canonical incremental
`./build.ps1 -Configuration Release -Target song_cache_tool`, then preserved.
This was not a clean rebuild. The current tool was built after the selector change:

| Tool | SHA-256 |
|---|---|
| Baseline, generation v18 | `1f40a4cb6e123cb6f4df59f932d2e642f622fefbccb4f6dbbccbe922e153e6f1` |
| Current, generation v19 | `c866fbb58c9a82014d161894dfb2f370e57fa7efdb6b14521a339fbd8edc5fc9` |

The lead independently ran both tools on separate cache-empty copies of each of
the eight exported sources, using ordinary chart limits. MIDI, JSON and WAV hashes
were asserted identical between the two arms. Every emitted profile was compared;
the table below selects the actual stock display difficulty, without substitution.
Every generated onset/pitch belonged to the stock attack set in both arms.

Explicit assumptions: MIDI uses 60 BPM only as a lossless frame-time carrier,
constant velocity 75 and an assumed 12-frame gate; audio is an unrelated synthetic
1 kHz signal. These are not recovered musical tempo, performance, sustain,
velocity or stock audio. Audio-prominence quality and perceptual importance are
not evaluated. The same assumptions in both arms isolate selection changes, but
do not establish absolute arrangement quality or calibration for real recordings.

Authored JSON controls previously passed the baseline native pipeline with 100%
attack and original-root retention. Independent SMF decoding verified exact
exported attacks; the repaired decoder reproduced the independent six-table
recovery. Thus generator losses are distinct from decoding/export losses.

## Results at stock difficulty

Music recall matches distinct frame/pitch pairs. Root retention additionally
requires the original hand and exact assignment ID; alternate assignments can
therefore retain a sound without retaining that root identity.

| Stock chart | Lv | Music recall v18 → v19 | Original-root retention v18 → v19 | Required RH/LH v18 → v19 |
|---|---:|---:|---:|---:|
| Aerith | 3 | 61.2% → 70.3% | 57.0% → 66.9% | 62/23 → 72/27 |
| Barret | 3 | 86.3% → 91.3% | 50.5% → 56.0% | 143/14 → 144/18 |
| Two Legs | 4 | 62.1% → 78.0% | 37.1% → 52.2% | 127/17 → 138/39 |
| Let the Battles Begin | 5 | 54.6% → 54.6% | 28.1% → 30.1% | 209/9 → 208/10 |
| On Our Way | 1 | 42.1% → 45.1% | 36.5% → 39.2% | 52/5 → 53/7 |
| One-Winged Angel | 6 | 80.7% → 90.7% | 58.1% → 65.2% | 259/14 → 265/29 |
| Cinco | 3 | 58.8% → 70.0% | 43.4% → 55.2% | 86/19 → 96/29 |
| Tifa | 2 | 44.9% → 47.2% | 27.4% → 28.1% | 39/13 → 40/14 |

Across these selected profiles, retained musical attacks increase from 1,473 to
1,647 of 2,343 (62.9% → 70.3%); retained original roots from 621 to 721 of 1,461.
Required inputs increase from 1,091 to 1,189. This is a note-retention correction,
**not a demonstrated reduction in difficulty or fatigue**.

The controlled synthetic regression separately reproduces the root cause: six
supported chords simultaneous with a 24-attack melody used to retain 18 RH + 6 LH
inputs; v19 retains 24 RH + 6 LH in 24 physical rows. The offbeat control remains
24 RH + 6 LH. One-tick offsets sharing a rounded frame do not gain dual authority.
Existing spacing, full-route and physical/event validation remain mandatory.

## Workload findings and limits

The independent analyzer reports missing attacks/roots and additional required
roots, including promotions of originally automatic sounds. Counts of rows,
musical sounds and required inputs must not be substituted for each other.

At the explicitly chosen reporting cutoff of adjacent gaps ≤60 frames (one
second), the longest generated RH series for Fighters grows from 42 to 55 inputs;
for One-Winged Angel it grows from 56 to 90, and its LH series from 2 to 17.
These are descriptive series, not physiological recovery thresholds. They expose
the tradeoff rather than justify calling the new charts easier. For example,
OWA still has 265 RH/29 LH required inputs versus stock 231 RH/79 LH: the dual fix
does not solve all hand-role or spacing restrictions.

No LH quota, invented harmony, universal reset-time constant, pitch-reversal
hard gate or automatic threshold retuning was added. Current fatigue/reversal
metrics remain proxies, and historical route settings were not requalified by
fitting these same eight examples. Actual sector/retrigger/judgement rules remain
unrecovered; see [control semantics](PianoControlSemantics-20261018.md).

Before ergonomic claims or installation qualification: recover the input matcher,
then test a short repeated/neighbor/opposite-sector sequence and one generated
dual-with-followers fragment under explicit human gates. Perceptually important
notes require independently marked passages and listening with authenticated
audio, not just higher aggregate recall on synthetic audio.

## Retained experiment and checks

The task scratch trees `piano-stock-ab-20261018` and `piano-dual-integration`
retain the source hashes, executable identities, cold-cache logs, per-profile
comparisons, summary JSON and reproducible A/B/synthetic scripts. Extracted game
assets and generated caches remain outside Git. Permanent export/comparison
commands and Python selftests are in the benchmark guide.

Focused checks include the decoder and benchmark Python tests, native MIDI
generator/compilation/analysis tests, repository/cache golden and codec tests,
extended-chart checks, four synthetic cold/warm comparisons, and documentation
audit. No runtime hooks, ASI installation, game launch, package or release
qualification was performed. Generation v19 deliberately invalidates policy-keyed
caches without changing source formats, authored-chart semantics or binary layout.
