# Autoplay lifecycle: authorized observational run, 2026-10-19

Mode: Debug. Human launched the existing installed product, loaded a save,
performed every game action, and explicitly authorized Frida observation.
No product installation, native invocation, gameplay/input/result mutation,
automatic playing, or sound playback was performed by the observer.

## Identity and capture

- Process: native `ff7rebirth_.exe`, PID 10644, not bootstrap PID 41204.
- Path: `D:/SteamLibrary/steamapps/common/FINAL FANTASY VII REBIRTH/End/Binaries/Win64/ff7rebirth_.exe`.
- Disk size and SHA-256 matched the default build in `src/game/rva_catalog.json`.
  Frida main-module path, mapped PE timestamp/checksum/image size, platform,
  and architecture matched the same selected build.
- ASLR base: `0x7ff701a50000`. Exact original fingerprints matched both
  unowned observation points; the pre-chart CALL target matched the catalog.
  The CALL bytes remained original after interception.
- Observer: workspace `Tools/Frida/autoplay_observer.js`; independent focused review and
  Node mock selftest passed before attachment. A bounded false-return record
  guard was added with a passing selftest after review.
- Frida session: `session_10644_1792423250`. Armed at the beginning of the
  route; no stop/error/overlap/layout failure through interval 191.
- Installed module was the existing product `D3DCOMPILER_43.dll`, unchanged
  by this experiment. This does not validate its unrelated pending fixes.

The tool transcript retains the messages. Below are selected copied facts,
not a claim that a complete raw Frida dump was separately archived. Times
are observer `Date.now()` values, not a native gameplay clock. One-second
summaries bound logging; they do not determine runtime authority.

## Human labels and observations

The human started outside the piano, then reported “in list”, “paused”,
“in list” after resuming/returning, and “stood up” after the second run.
The pause menu had Continue and return-to-list, **not a retry option**.
Replaying was therefore tested through the list, not a nonexistent retry UI.
The human was asked to select a stock song; its name/profile was not captured.

All observed ticks used thread 31972 and controller `0x7fef536a41f0`.

| Interval / time | Copied observation |
| --- | --- |
| 1–18 | No controller ticks or attempted chart calls while outside. |
| 19 / 1792423331119 | Current null/pending 2, then 2 → 3 → 4; chart null. |
| 29 / 1792423341123 | 4/pending 5 → current 5, chart null; human list label. |
| 67, 69, 73 | Activation transitions 5 → 6 → 7 → 8. |
| 77 / 1792423389143 | 8/pending 9 → current 9; chart A `0x7fecb2e24ed0`; first native elapsed 0, last 0.27202085; 23 attempted chart calls. |
| 78–84 | Chart A elapsed advances to 6.68268728; active/native-clock bytes both 1. |
| 85–134 | No observed ticks or attempted chart calls during the human-labeled pause. This is observation, not a recovered universal pause predicate. |
| 135 / 1792423447166 | After Continue: same chart A; elapsed resumes at 6.69868755 and reaches 7.49870825. |
| 136 | Chart A advances through 7.89870834. |
| 137 / 1792423449167 | 9/pending 4 → 4/pending 5 → current 5. Chart A pointer is **still retained** in the list. No attempted chart calls. |
| 159, 161, 165 | Second list activation again 5 → 6 → 7 → 8, retaining chart A until play entry. |
| 169 / 1792423481181 | 8/pending 9 → current 9; chart B `0x7fecb25d2f90`, elapsed resets to 0; 40 attempted chart calls. |
| 170–175 | Chart B advances through 6.480021; active/native-clock both 1. |
| 176 / 1792423488184 | Return route again 9/pending 4 → 4/pending 5 → current 5, retaining B. |
| 178 / 1792423490186 | List exit 5/pending 13 → current 13; chart B retained. |
| 188 / 1792423500190 | 13/pending 0, then tick AL continuation false. |
| 189–191 | No subsequent ticks or attempted chart calls; human reports stood up. |

## Integration implications and limits

- A selected/retained chart is not proof of active gameplay or seated state.
  The list and leaving animation retain it; a new play must have a new logical
  generation even if the allocator might later reuse the same address.
- Native scoped actuation must be admitted synchronously inside the owned
  chart update, correlate its controller/play generation, and restore any
  temporary root marker before terminalizer callbacks as statically documented.
  Never drive judgments from the input timer, retained clock, or elapsed silence.
- The observed pause stops the containing tick and chart-call path; resuming
  preserves chart/time continuity. This supports that route's scoped design,
  not a general pause predicate or authorization to mutate unobserved paths.
- Commands may operate on copied, generation-bound seated authority. Native
  pointers must not be read by the input worker. Leave/destruction must revoke
  authority; asynchronous silence cannot revoke it on a guessed timeout.
- The run did not exercise custom songs, results/completion, assisted scoring,
  actual toggle controls, sounds, another controller, alternate pause routes,
  or another executable build. It does not qualify autoplay or a release.

## Disposition

Human reported normal process exit; subsequent Windows process enumeration
confirmed both native game and bootstrap absent. The observer ended with the
process (no live product unload/reload). Final retrieved intervals through 258
contained no further ticks, attempted chart calls, or observer error markers.
No installer transaction was created, so no installer Keep/Rollback is needed;
the existing installed artifact remains in place.

Post-exit SHA-256 facts:

- Existing installed proxy: `972dc8aa5eef3962552dd24ce2625b379e2afc5aadfb05c42545f5f632db7912`.
- Retained observer source: `52bf4894856dd091b22a0388fd75d8672690c4711a11c8d18fac69c14fab183c`.

This closes the bounded observation, not feature validation. No gameplay
mutation or product artifact was installed, and no rollback was necessary.
