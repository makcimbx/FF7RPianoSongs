# MIDI Difficulty Calibration Evidence (v38, 2026-07-14)

This dated record preserves the calibration and fixture details removed from the durable `../docs/MidiGeneration.md` contract.

## Authored Evidence

The recovered authored routes showed that displayed level cannot be inferred from row count, APM, or one movement statistic.

| Authored chart | Lv. | Raw rows | Resolved frames | Unresolved remainder | Joint strain p95 / peak |
| --- | ---: | ---: | ---: | ---: | ---: |
| On Our Way | 1 | 212 | 124 | 88 | 2.791 / 3.325 |
| Tifa's Theme | 2 | 199 | 57 | 142 | 2.663 / 2.815 |
| Barret's Theme | 3 | 378 | 360 | 18 | 4.214 / 5.288 |
| Cinco de Chocobo | 3 | 287 | 219 | 68 | 3.443 / 3.871 |
| Aerith's Theme | 4 | 262 | 257 | 5 | 2.995 / 4.296 |
| Two Legs? Nothin' To It | 4 | 433 | 395 | 38 | 5.009 / 5.731 |
| Let the Battles Begin! | 5 | 337 | 277 | 60 | 4.252 / 4.723 |
| One-Winged Angel | 6 | 277 | 218 | 59 | 3.261 / 4.620 |

Resolved coverage was incomplete for several routes. Missing rows were treated as censored evidence, never as rests. Conservative margins varied with recovered coverage instead of promoting incomplete-route maxima to hard truths.

## Algorithm At Time Of Capture

1. Reject MIDI format 2, link tempo/meter events, and preserve stable source identities for non-drum C1-C7 attacks.
2. Cluster humanized onsets and track deterministic melody candidates using continuity, prominence, movement, and meter phase without synthesizing/transposing pitch.
3. Exclude actions before the native lead-in boundary or beyond known source-audio duration.
4. Construct deterministic alternatives by native frame and independently target actual levels using provisional APM/count bands.
5. Prefer exact prior-profile frame/pitch/chord witnesses for recognizability while allowing a better valid route.
6. Measure rolling action windows, streams, same-ID jacks, reversals, movement, octave changes, per-hand fatigue, imbalance, timing irregularity, and diagnostic full-song joint strain.
7. Search route-focused candidates deterministically with bounded beam/history storage and finalized-prefix pruning.
8. Validate each final chart against one coherent authored route rather than combining unrelated maxima.
9. Expose a profile only with meaningful growth and sufficient exact overlap; omit failed labels without compaction.
10. Reject without clipping when the minimum target exceeds 512 and stop only when monotone bounds eliminate all higher labels.

The optimizer used shared contiguous beam state, deferred row materialization and joint-strain analysis, cached finalized-prefix metrics, and parallelized only deterministic independent calculations. Imported-fixture tests used bounded worker processes with kill-on-close job ownership, deadlines, drain, and serial fallback. Output remained byte-identical while direct MIDI selftest and reviewed fixture cache generation were substantially reduced from their earlier runtimes.

## External Basis

The calibration borrowed general concepts from rhythm-game strain decay, rolling windows, jack/stream detection, and percentile bounds, but the final dimensions and thresholds were constrained by recovered FF7 Rebirth authored charts and native piano controls. External models were not treated as ground truth for native level labels.

## Example Diagnostics

These values are dated fixture evidence, not current promises.

| Song | Actual Lv. | Rows | APM | Windows | Half-second-gap stream | Overlap |
| --- | ---: | ---: | ---: | --- | --- | ---: |
| Retired fixture A | 1 | 199 | 78.30 | 2/3/5/10/18 | 5 / 1.53 s | 1.000 |
| Retired fixture A | 2 | 230 | 90.49 | 2/3/6/11/19 | 9 / 3.08 s | 0.889 |
| Retired fixture A | 3 | 256 | 100.72 | 2/4/6/13/23 | 10 / 3.47 s | 0.935 |
| Retired fixture A | 4 | 288 | 113.31 | 3/5/9/18/33 | 17 / 5.37 s | 0.906 |
| Retired fixture A | 5 | 333 | 131.02 | 3/6/11/25/43 | 31 / 11.20 s | 0.976 |
| Retired fixture B | 1 | 158 | 78.46 | 2/3/5/10/18 | 4 / 1.07 s | 1.000 |
| Retired fixture B | 2 | 182 | 90.37 | 2/3/5/12/20 | 4 / 1.07 s | 0.937 |
| Retired fixture B | 3 | 210 | 104.28 | 3/4/5/12/20 | 5 / 1.42 s | 0.802 |
| Retired fixture B | 4 | 243 | 120.66 | 3/4/6/14/25 | 11 / 3.53 s | 0.933 |
| Retired fixture B | 5 | 266 | 132.08 | 3/4/7/15/27 | 22 / 7.40 s | 0.967 |
| Retired fixture C | 1 | 276 | 78.33 | 2/3/5/10/19 | 5 / 1.47 s | 1.000 |
| Retired fixture C | 2 | 318 | 90.26 | 2/3/6/12/21 | 7 / 2.20 s | 0.949 |
| Retired fixture C | 3 | 367 | 104.16 | 3/4/7/14/27 | 15 / 5.13 s | 0.950 |
| Retired fixture C | 4 | 430 | 120.37 | 3/5/8/16/28 | 15 / 5.13 s | 0.965 |
| Retired fixture D | 1 | 201 | 78.50 | 2/2/4/9/18 | 6 / 2.50 s | 1.000 |
| Retired fixture D | 2 | 232 | 90.61 | 2/3/5/10/19 | 7 / 3.00 s | 0.816 |
| Retired fixture D | 3 | 268 | 104.50 | 2/4/8/14/23 | 9 / 2.25 s | 0.802 |
| Retired fixture D | 4 | 309 | 120.48 | 2/4/7/16/27 | 15 / 4.25 s | 0.802 |
| Retired fixture D | 5 | 355 | 138.42 | 3/5/10/19/32 | 19 / 7.00 s | 0.883 |
| Retired fixture D | 6 | 406 | 158.30 | 3/6/10/21/35 | 31 / 8.37 s | 0.885 |
| Retired fixture E | 1 | 232 | 78.24 | 2/3/5/10/18 | 3 / 0.80 s | 1.000 |
| Retired fixture E | 2 | 268 | 90.38 | 2/3/6/12/20 | 3 / 0.72 s | 0.836 |
| Retired fixture E | 3 | 309 | 104.21 | 2/4/7/14/24 | 5 / 1.42 s | 0.802 |
| Retired fixture E | 4 | 357 | 120.39 | 2/4/7/15/27 | 6 / 1.97 s | 0.812 |
| Retired fixture E | 5 | 410 | 138.27 | 3/5/8/16/29 | 17 / 6.65 s | 0.852 |
| Retired fixture E | 6 | 470 | 158.50 | 3/5/8/17/31 | 24 / 8.48 s | 0.822 |
| Retired fixture F | 1 | 151 | 78.48 | 2/3/5/10/16 | 6 / 2.25 s | 1.000 |
| Retired fixture F | 2 | 175 | 90.95 | 2/3/5/10/19 | 7 / 2.50 s | 0.901 |
| Retired fixture F | 3 | 193 | 100.09 | 2/4/6/11/19 | 8 / 3.00 s | 0.800 |
| Retired fixture F | 4 | 232 | 120.31 | 2/4/6/13/22 | 13 / 4.75 s | 0.834 |
| Retired fixture F | 5 | 267 | 138.46 | 3/5/7/15/26 | 21 / 7.25 s | 0.802 |
| Retired fixture F | 6 | 306 | 158.69 | 3/5/8/16/29 | 27 / 8.75 s | 0.914 |

## Omitted Fixture Levels

| Song | Omitted Lv. | Target rows | Reason at capture time |
| --- | ---: | ---: | --- |
| Retired fixture A | 6 | 335 | Insufficient meaningful growth over Lv.5. |
| Retired fixture B | 6 | 318 | Exact witness failed the selected authored-route movement bound. |
| Retired fixture C | 5 | 496 | Exact witness failed the selected authored-route movement bound. |
| Retired fixture C | 6 | 532 minimum | Minimum target exceeded the 512-row playable limit. |

These omissions demonstrate independent sparse labels and fail-closed sizing. They must not be used as expected output for later algorithms without rerunning source-derived tests.
