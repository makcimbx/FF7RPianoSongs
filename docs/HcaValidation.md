# HCA Validation

HCA/MABF confidence has four independent axes. Passing one axis does not imply another.

## Structural Validation

Structural checks prove that generated containers and frames satisfy the parser-level invariants implemented by this project: headers, offsets, sizes, slot layout, frame accounting, checksums, and bounded payload relationships. They detect malformed or truncated artifacts.

Structural success does not prove that the encoder matches native game output or that the game will play, stop, pause, resume, and release the resource correctly.

## Adaptive Validation

Adaptive checks prove that variable frame counts and payload sizes remain internally consistent across supported source durations and cache reuse. They cover dynamic HCA output and variable-size MABF assembly rather than assuming a fixed payload template.

Adaptive success does not prove codec parity. A structurally valid adaptive payload can still differ from the game's accepted encoder semantics.

## Native Parity

Parity requires real native game samples and an explicit comparison procedure. Compare decoded content, HCA metadata, frame boundaries, checksums, channel/rate assumptions, loop/trim behavior, and MABF slot semantics against representative native assets. Record tool versions, hashes, tolerances, and unexplained differences in dated evidence.

Synthetic fixtures, round trips through the project's own decoder, and byte-shape similarity are not native parity. Without dated matching-build real-sample evidence, this axis is a release blocker; any accepted evidence must state its bounded scope and exclusions explicitly.

The [2026-07-31 matching-build evidence](../analysis/NativeHcaParity-20260731.md) passes this axis for a bounded representative scope: all eight native piano MABFs were inspected, three standard-profile durations were independently decoded and regenerated, declared PCM/error tolerances passed, and every inspected header/frame checksum passed. It corrects dynamic 16-byte MABF slot semantics—including Mode0/1 zero-body prefixes ending in `01 00`, all-zero Mode2 prefixes, and a separate 46-byte metadata suffix—and exact inserted-sample timing. One native asset used an alternate extended header that was inspected but is not a generated output profile. No looped piano HCA was observed, so loop parity remains unsupported rather than inferred. This closure is not compressed-byte equivalence, universal CRI encoder parity, or game acceptance.

## In-Game Lifecycle

In-game validation proves behavior only for the tested artifact, executable, song, and scenario. It must cover first play, pause/resume, retry, completion, abort, song switch, cache warm load, and teardown.

A launch-only smoke test proves none of these behaviors. Successful playback onset alone does not prove stop/release ownership or later transitions.

## Result Reporting

Report each axis separately as `not run`, `failed`, `partially evidenced`, or `passed for <bounded scope>`. Keep dated result data separate from this durable contract and state clearly which axes remain unresolved.
