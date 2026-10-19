# Analysis Evidence Index

Files in this directory are dated engineering evidence, not user contracts. Durable behavior belongs in `docs/`; mutable implementation and release status belongs in [Current Status](../docs/CurrentStatus.md).

## Evidence Retention Policy

- Canonical Markdown links must target tracked repository files.
- Ignored runtime logs such as `FF7RPianoSongs-*.log` are ephemeral session inputs, not durable evidence targets. Refer to their filenames only as code spans.
- Before a claim becomes durable, retain the relevant bounded excerpts or derived findings in a tracked evidence file, together with enough build and scenario context to interpret them.

## Game Builds

- [RebirthBuild1004RvaEvidence-20260808.md](RebirthBuild1004RvaEvidence-20260808.md) records the original 1.004 catalog recovery, measured signature uniqueness and build-specific dispositions. Later native-audio additions have separate imported-program profiles below; this August record is not evidence for every subsequent catalog entry.

## Development History

- [NativeAudioIntegration-20261017.md](NativeAudioIntegration-20261017.md) summarizes the replacement backend's failed and accepted Development scenarios with their limits; current disposition remains in Current Status.
- [DevelopmentMilestones-202609.md](DevelopmentMilestones-202609.md) retains earlier feature milestones and version-scoped release decisions without treating old status entries as current requirements.

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
- [VanillaCorpusRecovery-20261018.md](VanillaCorpusRecovery-20261018.md) corrects the retained corpus's mod/stock provenance, records independently checked stock root/follower counts, and bounds what those data establish.
- [Stock attack roundtrip and exact dual-input selection](MidiGenerationRoundtrip-20261018.md) — before/after note retention and required-input workload, including longer-series tradeoffs; offline only.
- [Installed song generation review](InstalledSongGenerationReview-20261018.md) — isolated regeneration of actual installed inputs, LH voicing contract violation, fast transitions, bilateral demands, and source-active chart gaps; not in-game qualification.
- [MIDI selection corrections](MidiSelectionCorrections-20261018.md) — source-exact LH, local transition and representative-gap follow-up, with isolated corpus evidence and qualification limits.
- [PianoControlSemantics-20261018.md](PianoControlSemantics-20261018.md) separates retained input/result-path evidence from the still-unrecovered sector, retrigger and judgement rules; assignment-transition metrics are descriptive, not physical playability proof.
- [MidiDifficultyCalibration-v38-20260714.md](MidiDifficultyCalibration-v38-20260714.md) preserves historical mod-corpus calibration, generated fixture diagnostics, and performance observations; it is not stock difficulty qualification.
- [ProductionCacheOptimization-v38-20260715.md](ProductionCacheOptimization-v38-20260715.md) records the cache optimizer investigation.

## Native Cached Audio

- [NativeCachedAudioEvidence-20261016.md](NativeCachedAudioEvidence-20261016.md) records the controlled native-container/registered-row selection and start/pause/resume result, its process-exit retention contract, and the boundary between that evidence and an implemented ASI replacement.
- [NativeMusicExportProfile-20261016.md](NativeMusicExportProfile-20261016.md) identifies the supported shipping unversioned timing properties and variable-size mapped-bank wrapper; it is a bounded serialization profile, not a general Music serializer or playback qualification.
- [NativeAudioLoaderHandoff-20261016.json](NativeAudioLoaderHandoff-20261016.json) pins the supported loader's synchronous export and distinct deferred-route unwind anchors; the export call alone does not establish an outside-DllMain origin.
- [NativeAudioBootstrapProfile-20261016.json](NativeAudioBootstrapProfile-20261016.json) distinguishes supported pre-CRT and pre-engine continuations and their baselines; static evidence does not establish an observed startup or later native mutation authority.
- [NativeAudioFirstSrwTrigger-20261017.json](NativeAudioFirstSrwTrigger-20261017.json) qualifies the exact first allocator callback chain and its pre-allocator restrictions, not generic CRT initialization.
- [NativeAudioBootstrapProfile-OlderBuild-20261017.json](NativeAudioBootstrapProfile-OlderBuild-20261017.json) records the independently recovered older startup chains and built-in decoder ABI; imported-program correlation is not observed older gameplay.
- [NativeAudioAssetProfile-OlderBuild-20261017.json](NativeAudioAssetProfile-OlderBuild-20261017.json) records older frozen rows and the different inherited Music property schema, including the correction of the earlier BGM resource-field interpretation.
- [NativeCachedAudioOffsets-OlderBuild-20261016.json](NativeCachedAudioOffsets-OlderBuild-20261016.json) retains the imported-program address handoff and its corrected structural interpretation, not older disk/gameplay qualification.
- [NativeAudioGameDecoder-20261017.json](NativeAudioGameDecoder-20261017.json) records both executable decoders, ordinary caller ABI and bounded default-callback/lifetime evidence; static profiles alone do not authenticate a running callback.
- [NativeAudioCrtRetpoline-20261017.json](NativeAudioCrtRetpoline-20261017.json) describes one metadata-declared Windows dispatch rewrite and authenticated system-image template; it neither allows arbitrary system trampolines nor identifies the prior game's unrecorded mismatch.
- [ChordVoicingRuntimeEvidence.md](ChordVoicingRuntimeEvidence.md) records the exact-1.005 charted-chord projection seam, positional velocity/filter semantics, and pre-judgement readiness boundary. It is static evidence, not gameplay qualification or 1.004 authority.
- [ChordVoicing1004Evidence.md](ChordVoicing1004Evidence.md) independently recovers the corresponding 1.004 calls, unique signatures, register handoff, layouts and readiness path. It authorizes the shared mechanism statically, not 1.004 gameplay or full monotone-inventory parity.

## Historical Audio And Format Evidence

The source-patching/borrower backend below has been replaced. Its past successes,
failures and ABI observations remain artifact-bound evidence, not instructions to
reintroduce PlaySetup replay or qualification of the native-container backend.

- [AudioLifecycleEvidence-202607.md](AudioLifecycleEvidence-202607.md) is the consolidated lifecycle chronology and bounded disposition.
- [NativeSubstitutionBoundary-20260924.md](NativeSubstitutionBoundary-20260924.md) records the source/artifact baseline and bounded investigation of a simpler preparation/reuse substitution boundary; it does not itself qualify a replacement route.
- [AdaptiveMabfModeEvidence.md](AdaptiveMabfModeEvidence.md) records adaptive MABF mode work.
- [NativeHcaParity-20260731.md](NativeHcaParity-20260731.md) records the bounded matching-build native structural/metadata/decode parity closure, independent decoder provenance, integrity and error measurements, and corrected slot/timing defects.
