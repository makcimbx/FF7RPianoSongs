# Adaptive MABF Mode Evidence

## Confirmed

- Vanilla guide clicks are audible at the beginning and after mistakes, not continuously through successful play.
- The `bgm_piano_09` scaffold contains `Mode0`, `Mode1`, and `Mode2` records at header offsets `0x220`, `0x2d0`, and `0x380`.
- Shipping judgment callback `FUN_143991F24` reaches `FUN_1439A896C`, which maintains an adaptive tier and constructs the key `Mode` plus `tier + 1`.
- The tier mapping is exact: `-1 -> Mode0`, `0 -> Mode1`, `1 -> Mode2`.
- Judgments 1, 2, and 4 reset the success streak and demote one tier. Judgments 5 and 6 increment consecutive and cumulative success and can promote at two configured thresholds.
- PlaySetup and live judgment changes both resolve the named key through the active BGM resource, enqueue SQEXSEAD command `0x25`, and request an indexed `MabFile::Mode` transition.
- The current BGM resource is reached through `controller+0x28 -> slot`, then `slot+0xc0 -> resource`. Its requested numeric MABF mode is at `resource+0x54`; the requested key is at `resource+0x60`.
- `slot+0x30` is not a MABF selector. It lies inside the eight-byte priority/order value beginning at `slot+0x2c`.
- Internal source fields `+0xc4c` and `+0xc50` store committed/previous and requested/target mode indices.
- Historical custom MABF containers contain identical HCA bytes in all three modes. They prove scaffold playback only and cannot reveal audible Vanilla payload semantics.

## Historical Topology-Based Payload Hypothesis

> **Superseded 2026-08-09:** The topology establishes native mode selection,
> but did not by itself prove that Mode1 contained a metronome. The assignment
> below motivated the former generated mapping and is retained as historical
> reasoning only.

The native one-tier demotion topology supports this binary payload assignment:

| MABF record | Payload |
| --- | --- |
| Mode0 | click-mixed audio |
| Mode1 | click-mixed audio |
| Mode2 | clean audio |

This hypothesis would have made a mistake from clean Mode2 demote to an audible-click mode. With default generated thresholds `[8,16]`, it would also have kept initial playback click-enabled until the second threshold. User comparison later disproved the Mode1-metronome assumption for the generated-song contract.

Runtime evidence confirmed `resource+0x54` and `resource+0x60` followed `Mode0 -> Mode1 -> Mode0 -> Mode1 -> Mode2` while the direct judgment hook recorded the matching streak resets and promotions. Published thresholds matched the active profile.

## Corrected Observation

- The previous observer incorrectly read `slot+0x30` and stopped after native Set invalidated the tracked custom-route phase.
- The corrected read-only observer resolves the current controller, slot, resource, and sound object every tick, independent of custom-route phase.
- It logs `resource+0x54`, `resource+0x60`, slot state, sound identity, and resource identity without writing native state.
- ScoreInfo overlay logging records both published mode-change thresholds.

## Historical Production Interpretation

> **Superseded 2026-08-09:** The three-stage generated mapping below was the previous interpretation of the captured Vanilla payloads. It remains here only as historical evidence for that implementation.

- The captured Vanilla `bgm_piano_01` container proves all three payloads differ: Mode0 carries the strongest dry woodblock/guide layer, Mode1 carries a weaker guide layer, and Mode2 is clean. Generated songs mirror that three-stage contract with equal logical frame counts.
- Offline analysis of the decoded, analysis-only payloads found the initial Mode0 impacts at about `0.035`, `0.904`, `1.770`, and `2.639` seconds, with a fitted `0.866717`-second period (`69.227 BPM`). The first Vanilla prompt is `06_56` (`6.933333` seconds), so the guide establishes tempo about eight beats before gameplay begins.
- Beat-synchronous Mode0-minus-Mode1 excess peaks near `586 Hz`; about `83%` of its energy is below `750 Hz`, `97%` below `4 kHz`, and its 90% temporal-energy span is about `55 ms`. Mode1-minus-Mode2 is much weaker and has the same low resonance. These measurements inform deterministic low-body/band-limited-noise synthesis; no captured game audio is copied or packaged.

## Current Generated Contract

User comparison of original tracks established that generated songs must encode guide-mixed audio only in Mode0 and byte-identical clean source audio in Mode1/Mode2. The pipeline no longer synthesizes the weaker Mode1 guide. Disabled songs encode clean source audio in all three modes. Cache identity and validation distinguish the corrected enabled mapping from both the historical generated mapping and the disabled clean contract.

Newly generated JSON and packaged examples default `metronome.enabled` to `true`.

## Runtime Qualification Evidence

The qualifying Lets Be Friends run covered:

1. Initial playback before either promotion threshold.
2. Great/Perfect judgments through both thresholds.
3. A deliberate miss from clean Mode2.
4. Recovery through the lower threshold.
5. Any native Set/Play sound-object replacement during those phases.

Observed requested-mode/key sequence was `Mode0 -> Mode1 -> Mode0 -> Mode1 -> Mode2`; the deliberate miss occurred before reaching Mode2 in that run, while static handler analysis proves one-tier demotion from Mode2 to Mode1. The historical three-payload build used that inferred mapping and retained direct judgment/resource telemetry for audible qualification; the current writer follows the corrected generated-song contract above.
