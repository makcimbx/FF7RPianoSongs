# Analysis Evidence Index

Files in this directory are dated engineering evidence, not user contracts. Durable behavior belongs in `docs/`; mutable implementation and release status belongs in [Current Status](../docs/CurrentStatus.md).

## Evidence Retention Policy

- Canonical Markdown links must target tracked repository files.
- Ignored runtime logs such as `FF7RPianoSongs-*.log` are ephemeral session inputs, not durable evidence targets. Refer to their filenames only as code spans.
- Before a claim becomes durable, retain the relevant bounded excerpts or derived findings in a tracked evidence file, together with enough build and scenario context to interpret them.

## Game Builds

- [RebirthBuild1004RvaEvidence-20260808.md](RebirthBuild1004RvaEvidence-20260808.md) records how every catalog address for game build `1.004` was derived, the measured signature-uniqueness results, and the two build-specific dispositions: the positively established absence of `progress_lookup_display_caller_1` and the retained `probable` grade for `select_index_helper`.

## Architecture

- [PianoMenuSessionEvidence-20260729.md](PianoMenuSessionEvidence-20260729.md) preserves matching-build list open/close, state-exit, teardown, and session-generation evidence for Phase 4 coordination.
- [PianoMenuMetadataEvidence-20260802.md](PianoMenuMetadataEvidence-20260802.md) preserves the matching-build selected-index duration, note-count, and menu-detail ScoreInfo call chain plus the scoped metadata authority decision.
- [PianoPageSelectionGuardEvidence-20260803.md](PianoPageSelectionGuardEvidence-20260803.md) preserves the historical and decisive negative-selection crash evidence, exact single-site rel32 ownership, and durable bypass contract.
- [ControlledTeardownInvalidation-20260731.md](ControlledTeardownInvalidation-20260731.md) records the two invalidated live-teardown attempts and the resulting unsupported/NO-GO disposition.

## Chart And MIDI

- [ChartRowLimitEvidence.md](ChartRowLimitEvidence.md) preserves parser-cap, event ABI, ownership, synthetic-model, and runtime diagnostic findings.
- [ChartEventAbiGhidra.txt](ChartEventAbiGhidra.txt) is the underlying recovered chart-event ABI report.
- [Playable513ReserveEvidence.md](Playable513ReserveEvidence.md) preserves the verified-1.005 scoped-reserve, row-513 constructor/callback, native chart/shared-pointer ownership, synchronous controller binding and pre-expansion authority, validation, rollback, hook-lifecycle, and explicit nonclaim evidence for the restricted monotone-only experiment.
- [PlayableMultiTailEvidence.md](PlayableMultiTailEvidence.md) preserves the verified-1.005 small- and large-allocation reserve paths, bounded 513-through-8192 monotone multi-tail transaction design, native post-commit ownership, deferred group/chord/IgnoreSound/camera evidence, format-14 cache-policy boundary, and exact-8192 Development qualification plan.
- [RecoverChartEventAbi.java](GhidraScripts/RecoverChartEventAbi.java) is the Ghidra extraction script.
- [MidiDifficultyCalibration-v38-20260714.md](MidiDifficultyCalibration-v38-20260714.md) preserves authored-route calibration, generated fixture diagnostics, and performance observations.
- [ProductionCacheOptimization-v38-20260715.md](ProductionCacheOptimization-v38-20260715.md) records the cache optimizer investigation.

## Audio

- [AudioLifecycleEvidence-202607.md](AudioLifecycleEvidence-202607.md) is the consolidated lifecycle chronology and bounded disposition.
- [AdaptiveMabfModeEvidence.md](AdaptiveMabfModeEvidence.md) records adaptive MABF mode work.
- [NativeHcaParity-20260731.md](NativeHcaParity-20260731.md) records the bounded matching-build native structural/metadata/decode parity closure, independent decoder provenance, integrity and error measurements, and corrected slot/timing defects.
