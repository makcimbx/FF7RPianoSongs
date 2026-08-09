# Native Documentation

This index is for the repository source tree. The binary package intentionally contains only the user-facing subset listed in `package-docs.json`.

## Players And Song Authors

- [Player Guide](../README.md) covers requirements, installation, use, settings, updates, troubleshooting, compatibility, and limitations.
- [Song Format](SongFormat.md) is the canonical authoring contract.
- [Changelog](../CHANGELOG.md) records public versions.
- [Chart Limits](ChartLimits.md) explains the playable chart-size boundary.
- [MIDI Generation](MidiGeneration.md) describes generated profiles and author expectations.
- [HCA Validation](HcaValidation.md) separates the audio validation axes and their claims.

## Maintainers And Release Reviewers

- [Build And Release](BuildAndRelease.md) is the canonical build, test, audit, and package workflow, including the procedure for adding a supported game build.
- [Current Status](CurrentStatus.md) records supported behavior, blockers, latest verification, and artifact disposition.
- [In-Game Validation](InGameValidation.md) defines stable manual scenarios, acceptance criteria, and stop conditions.
- [Architecture](Architecture.md) records durable ownership and lifecycle invariants.
- [Mode-Specific Audio Design](ModeSpecificAudioDesign.md) is the implemented offline role-resolution, processing, cache, and validation reference for optional Mode1 and Mode2 source overrides.
- [RVA catalog](../src/game/rva_catalog.json) is the machine-readable inventory of supported game builds, addresses, and hooks. [Architecture](Architecture.md) documents its schema and the per-build generated trees.

## Reverse Engineering Evidence

- [Analysis Index](../analysis/README.md) separates dated evidence from durable contracts.
- [Chart Row Limit Evidence](../analysis/ChartRowLimitEvidence.md) preserves parser, ABI, ownership, and runtime qualification research.
- [MIDI Calibration Evidence](../analysis/MidiDifficultyCalibration-v38-20260714.md) preserves authored-chart calibration and fixture metrics.
- [Audio Lifecycle Evidence](../analysis/AudioLifecycleEvidence-202607.md) preserves the current native lifecycle chronology.
