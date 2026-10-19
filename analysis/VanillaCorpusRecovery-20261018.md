# Stock Piano Corpus Recovery — 2026-10-18

## Scope and provenance

Read-only extraction from the installed stock `pakchunk3-WindowsNoEditor.utoc`
was compared with the retained parent-workspace `Analysis/VanillaPiano` assets
and the *Piano Minigame Songs — From the World of FF7* replacement container.
The base container was addressed directly, not through a merged mod mount.
Both package names were joined to their ExportBundleData chunks before comparing
the extracted bytes. The lead independently reran the comparison.

| Package | Stock bytes | Retained/mod bytes | Stock SHA-256 |
| --- | ---: | ---: | --- |
| `/Game/DataObject/Resident/PianoScore` | 103440 | 190368 | `a2dc74150a2d50f8f2e33e35c898fe64a370dc06a3ee0714c817ffa0aebfeffa` |
| `/Game/DataObject/Resident/PianoScoreInfo` | 5696 | 5844 | `b7184b8520c1eae265dd30ba5c6f4f072ec0b160516d7770caca9f22f96f5dad` |

For each package, retained bytes equal mod bytes and differ from stock bytes.
This establishes content provenance, not the historical extraction procedure.
The retained mod files were not overwritten. The installed executable matched
the cataloged newer build; this investigation does not establish older asset
parity. Exact supported executable identities remain in `src/game/rva_catalog.json`.

## Decoder correction

The old frozen-array/string decoder interpreted tagged relative pointers as
unsigned, losing backward references. Raw zero FNames were also reported as
unknown instead of absent, and a short name-scan cap truncated some tables.
An in-memory correction recovered all stock timestamps and joined the present
monotone/chord IDs. Missing optional array tails must keep their native defaults;
arrays need not all have identical lengths. Empty hand IDs mean no prompt on
that side, not silence in the underlying recording.

## Stock interaction counts

Grouping follows adjacent runs: the first row's present hands remain parentless;
continuation events become automatic followers. A dual root row has two required
actions. Omitted group tails default to zero. These counts reconstruct the
retained static parser/linking model; they are not fresh gameplay evidence.

| Chart | Display level | Rows | RH roots | LH roots | Followers | LH share |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Journey / On Our Way | 1 | 143 | 141 | 7 | 2 | 4.7% |
| Tifa | 2 | 133 | 130 | 16 | 1 | 11.0% |
| Barret | 3 | 171 | 89 | 20 | 69 | 18.3% |
| Cinco | 3 | 159 | 93 | 52 | 44 | 35.9% |
| Aerith | 3 | 113 | 103 | 39 | 3 | 27.5% |
| Two Legs | 4 | 173 | 117 | 88 | 12 | 42.9% |
| Fighters / Let the Battles Begin | 5 | 337 | 208 | 48 | 120 | 18.8% |
| One-Winged Angel | 6 | 277 | 231 | 79 | 33 | 25.5% |

Totals: 1,506 rows, 1,461 required actions, 284 followers, 243 dual rows.
All eight reconstructed action totals equal the corresponding stock highest
score threshold divided by 100. This is an independent consistency check,
not proof of every native scoring rule. Four Two Legs rows contain neither
hand ID and do not create prompt events.

## Consequences and limits

- The older [calibration report](MidiDifficultyCalibration-v38-20260714.md) and
  its hardcoded regression summaries describe a partially decoded mod corpus.
  They do not validate original-game difficulty. The exact derivation of every
  current envelope scalar was not recovered; do not claim all constants were
  demonstrably measured from that corpus.
- Stock hand participation varies substantially. Neither a sparse-LH target
  nor a compulsory 50/50 split follows from these data.
- Root counts, not rows or sounded chord constituents, measure required inputs.
  Group followers must remain separate from player workload.
- Stock ScoreInfo BPM fields are zero. Exact scheduled frame onsets are
  recoverable; acoustic note-offs, sustain, velocities and a complete piano
  performance must not be invented and labelled exact.
- Public MIDI and Basic Pitch transcription are secondary musical references,
  not substitutes for these recoverable authored attacks. Retained transcription
  audio has not been authenticated against the corrected stock corpus.
- Controller boundaries, neutral-return/retrigger rules and judgement windows
  require their own evidence; see [control semantics](PianoControlSemantics-20261018.md).
  This corpus alone cannot qualify a human-motion simulator, physiological fatigue
  model or new difficulty thresholds.

The decoder corrections now have synthetic regression tests in the optional
parent `Tools/test_decode_ff7r_piano_dataobject.py`. Decoding the six extracted
stock tables with that repaired decoder reproduced every row of the independent
in-memory recovery. The standalone [benchmark](../docs/VanillaBenchmark.md)
exports exact attack timing, preserves input/group identities separately, and
compares native cache output without treating synthetic audio as stock music.

The initial reproducible extraction/comparison recipe and its assertion-backed
`proof.json` are retained in the task's `piano-provenance-20261018` scratch
directory. No game installation, launch, attachment or runtime mutation was
performed.
