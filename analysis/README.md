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
- [PianoUiPresentationEvidence.md](PianoUiPresentationEvidence.md) records both-build list difficulty and native focus-restoration paths, plus the distinct RESULT rank/localization sink. The recovered borrowed text-setter ABI is not a requirement to replace the approved six-icon display cap with numeric text.
- [PianoPageSelectionGuardEvidence-20260803.md](PianoPageSelectionGuardEvidence-20260803.md) preserves the historical and decisive negative-selection crash evidence, exact single-site rel32 ownership, and durable bypass contract.
- [ControlledTeardownInvalidation-20260731.md](ControlledTeardownInvalidation-20260731.md) records the two invalidated live-teardown attempts and the resulting unsupported/NO-GO disposition.

## Chart And MIDI

- [PianoMonotoneAssignEvidence-20260905.md](PianoMonotoneAssignEvidence-20260905.md) records exact installed 1.005 stock monotone extraction, full retained-table parity, alternate C/C-sharp octave boundaries, and absence of C-flat alternate rows. It is asset evidence, not input/audio runtime qualification or 1.004 parity.
- [ChartRowLimitEvidence.md](ChartRowLimitEvidence.md) preserves parser-cap, event ABI, ownership, synthetic-model, and runtime diagnostic findings.
- [ChartEventAbiGhidra.txt](ChartEventAbiGhidra.txt) is the underlying recovered chart-event ABI report.
- [Playable513ReserveEvidence.md](Playable513ReserveEvidence.md) preserves the verified-1.005 scoped-reserve, row-513 constructor/callback, native chart/shared-pointer ownership, synchronous controller binding and pre-expansion authority, validation, rollback, hook-lifecycle, and explicit nonclaim evidence for the restricted monotone-only experiment.
- [PlayableMultiTailEvidence.md](PlayableMultiTailEvidence.md) preserves the verified-1.005 small- and large-allocation reserve paths, bounded 513-through-8192 monotone multi-tail transaction design and accepted endpoint qualification, native post-commit ownership, deferred group/chord/IgnoreSound/camera evidence, and the format-14 cache-policy boundary.
- [PlayableGeneralizedChartEvidence.md](PlayableGeneralizedChartEvidence.md) supersedes the deferred chord, dual-hand, IgnoreSound, and group scope only for the exact verified-1.005 generalized implementation contract; it preserves catalog-ready IgnoreSound-helper bytes, mixed row-level native grouping and deterministic split-row generation, compact event and parentless-action count semantics, post-parser full relinking and rollback authority, format-14 derivation, and the remaining runtime-only nonclaims.
- [RecoverChartEventAbi.java](GhidraScripts/RecoverChartEventAbi.java) is the Ghidra extraction script.
- [MidiDifficultyCalibration-v38-20260714.md](MidiDifficultyCalibration-v38-20260714.md) preserves authored-route calibration, generated fixture diagnostics, and performance observations.
- [ProductionCacheOptimization-v38-20260715.md](ProductionCacheOptimization-v38-20260715.md) records the cache optimizer investigation.

## Audio

- [ChordVoicingRuntimeEvidence.md](ChordVoicingRuntimeEvidence.md) records the exact-1.005 charted-chord projection seam, positional velocity/filter semantics, and pre-judgement readiness boundary. It is static evidence, not gameplay qualification or 1.004 authority.
- [ChordVoicing1004Evidence.md](ChordVoicing1004Evidence.md) independently recovers the corresponding 1.004 calls, unique signatures, register handoff, layouts and readiness path. It authorizes the shared mechanism statically, not 1.004 gameplay or full monotone-inventory parity.
- [AudioLifecycleEvidence-202607.md](AudioLifecycleEvidence-202607.md) is the consolidated lifecycle chronology and bounded disposition.
- [AdaptiveMabfModeEvidence.md](AdaptiveMabfModeEvidence.md) records adaptive MABF mode work.
- [NativeHcaParity-20260731.md](NativeHcaParity-20260731.md) records the bounded matching-build native structural/metadata/decode parity closure, independent decoder provenance, integrity and error measurements, and corrected slot/timing defects.
