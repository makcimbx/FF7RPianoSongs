# FF7RPianoSongs v38 Production Cache Optimization

Date: 2026-07-15

## Result

Production startup now loads at most two independent song directories concurrently, joins all work, and publishes one sorted immutable registry replacement before hooks are initialized. Cache integrity, deterministic ordering, fail-closed errors, and v38 artifact bytes are unchanged.

- Cold six-song batch: 179.619 s serial before, 126.597 s with two workers after, 29.5% lower wall time.
- Warm six-song batch: 1.269 s serial before, 0.704 s with two workers after, 44.5% lower wall time.
- `song_repository_selftest`: 197.076 s measured before test optimization, 133.13 s in final CTest, 32.4% lower.
- Exact artifact comparison: 18 of 18 files matched; zero SHA-256 mismatches.
- Cache format/version: unchanged at v38 / `F7RPRT13` format 13.
- Final Release CTest: 10 of 10 passed.

No game was launched. Nothing was packaged, installed, or written to installed game/cache directories.

## Stage Profile

The profile used fresh cache-free copies of all six examples. Times are milliseconds from `std::chrono::steady_clock` trace boundaries. This was a separate capture from the coarse before/after batch benchmark, so its absolute totals should not be mixed with the batch totals.

### Cold Front End And Audio Processing

`guides` includes beat construction plus strong and weak metronome mixing and their gain-envelope/loudness/limiter processing. `clean audio` is the clean mode gain-envelope/loudness/limiter pass.

| Song | Total | Audio decode | MIDI generation | Guides | Clean audio |
|---|---:|---:|---:|---:|---:|
| Retired fixture A | 26,411.7 | 237.4 | 21,417.2 | 535.3 | 299.9 |
| Retired fixture B | 31,835.3 | 284.3 | 27,624.1 | 871.0 | 379.8 |
| Retired fixture C | 44,631.0 | 459.6 | 39,460.3 | 722.6 | 288.6 |
| Retired fixture D | 47,784.9 | 249.8 | 44,343.7 | 452.5 | 193.8 |
| Retired fixture E | 48,659.5 | 302.7 | 44,472.6 | 743.2 | 254.6 |
| Retired fixture F | 26,418.0 | 196.0 | 23,672.4 | 359.4 | 160.2 |

MIDI profile generation dominates every cold example. Audio decode is 196-460 ms. The combined guide and clean gain/loudness/limiter work is 520-1,251 ms.

### Cold PCM And HCA

The PCM column is all three float-to-PCM16 conversions. HCA modes are Mode2 clean, Mode0 strong guide, and Mode1 weak guide.

| Song | PCM conversion | Mode2 HCA | Mode0 HCA | Mode1 HCA | Three HCA total |
|---|---:|---:|---:|---:|---:|
| Retired fixture A | 280.7 | 1,174.8 | 1,141.6 | 1,081.4 | 3,397.8 |
| Retired fixture B | 182.3 | 763.9 | 761.7 | 793.7 | 2,319.3 |
| Retired fixture C | 247.4 | 1,089.0 | 1,046.2 | 1,060.4 | 3,195.6 |
| Retired fixture D | 167.8 | 733.6 | 742.0 | 719.1 | 2,194.7 |
| Retired fixture E | 186.4 | 834.3 | 834.4 | 825.5 | 2,494.2 |
| Retired fixture F | 158.1 | 563.3 | 603.0 | 564.5 | 1,730.8 |

Independent HCA calls were verified thread-safe and deterministic: each call owns its output, frame PCM, channel/MDCT state, and temporary buffers, while shared tables are immutable. A concurrent encoder selftest now requires concurrent outputs to match the serial baseline exactly.

Mode-level encoding was intentionally left serial. Parallelizing all three modes would add three simultaneous PCM16/encoder working sets to songs that already retain clean, strong, and weak full-song float PCM. Its measured ceiling is only about 1.7-3.4 seconds per cold song, while MIDI generation is 21.4-44.5 seconds. Song-level concurrency gives the useful startup gain with a clearer memory bound.

### Cold MABF And Writes

| Song | MABF build | Release validation | MABF hash | MABF write | Manifest write | Manifest hash | Runtime write |
|---|---:|---:|---:|---:|---:|---:|---:|
| Retired fixture A | 5.6 | 161.1 | 16.0 | 12.9 | 5.9 | 11.0 | 5.4 |
| Retired fixture B | 2.6 | 113.3 | 11.1 | 7.3 | 4.5 | 10.6 | 5.3 |
| Retired fixture C | 2.9 | 179.3 | 17.2 | 9.5 | 3.3 | 7.3 | 3.5 |
| Retired fixture D | 2.0 | 127.1 | 12.3 | 4.9 | 2.9 | 7.9 | 3.6 |
| Retired fixture E | 2.4 | 144.6 | 14.1 | 6.4 | 3.3 | 6.8 | 3.4 |
| Retired fixture F | 1.6 | 97.9 | 10.1 | 4.8 | 2.8 | 6.3 | 3.5 |

Release MABF validation, including all HCA frame geometry/CRC checks, is the largest post-encode step at 98-179 ms. Hashes and atomic writes are comparatively small.

### Warm Validation

| Song | Total | Runtime read | Runtime semantics | MABF read | MABF validation | MABF hash | Manifest read/render/compare |
|---|---:|---:|---:|---:|---:|---:|---:|
| Retired fixture A | 165.1 | 2.0 | 1.1 | 12.5 | 131.8 | 12.4 | 0.8 |
| Retired fixture B | 127.0 | 1.3 | 0.8 | 9.1 | 100.5 | 10.5 | 1.1 |
| Retired fixture C | 221.2 | 2.0 | 1.2 | 16.5 | 176.5 | 17.6 | 1.4 |
| Retired fixture D | 158.2 | 1.9 | 1.2 | 13.6 | 124.0 | 12.3 | 0.7 |
| Retired fixture E | 180.7 | 2.4 | 1.3 | 13.7 | 144.1 | 13.6 | 0.8 |
| Retired fixture F | 121.6 | 2.1 | 1.2 | 8.7 | 96.4 | 9.1 | 0.6 |

Warm time is deliberately dominated by full MABF validation. No integrity shortcut was added: runtime framing/checksum/semantic validation, all-mode MABF structure and HCA validation, complete MABF digest, canonical manifest byte comparison, and manifest digest remain mandatory.

## Production Changes

### Bounded Startup

- `src/main.cpp:114` uses a fixed maximum of two song workers.
- Candidates are enumerated and sorted before work begins.
- Each worker owns one preallocated candidate slot and buffers attributable trace/error text.
- A successful `LoadedSong` is reduced immediately to `SongDescriptor`, releasing decoded PCM before the worker claims another song.
- Workers join before logging/publication.
- The main thread walks slots in sorted candidate order, assigns visible indexes only to successes, logs errors deterministically, and calls `game::registry().replace` once at `src/main.cpp:204`.
- Hook initialization remains after repository loading, so hooks cannot observe partial mutation.

`discover_songs` received the same fixed two-worker, sorted-slot, join-then-publish model at `src/pipeline/song_repository.cpp:2517` for non-game callers and tests.

### Concurrency And Memory Policy

- Worker count is explicitly bounded at two; no `std::async` or unbounded task creation is used.
- Two cold songs can approximately double the serial song working set, but the bound does not scale with song count or hardware concurrency.
- Within-song strong, weak, and clean audio/HCA work remains serial to avoid adding simultaneous full-song PCM16/encoder buffers.
- A fixed 64-stripe per-directory mutex at `src/pipeline/song_repository.cpp:57` prevents duplicate same-directory cold builders from racing atomic replacements. Distinct songs remain parallel; a hash collision only causes safe extra serialization.
- Default JSON creation occurs before that lock, preserving the independent concurrent atomic-default-JSON test.
- Unexpected worker exceptions are converted to attributable fail-closed `IoError` results rather than escaping worker threads.

### Profiling

- `SongLoadTrace` now covers cache/runtime stages, audio decode, MIDI generation, chart compilation, guide/clean processing, each PCM/HCA mode, MABF build/validation/hash/write, manifest write/hash, and runtime write.
- `src/tests/song_repository_benchmark.cpp` and its CMake target print tab-separated per-stage elapsed milliseconds for one or more song directories.
- Trace callbacks do not participate in serialization and do not change cache keys or artifacts.

## Exact Artifact Equivalence

The serial baseline artifacts were generated first, backed up, and then the same six source directories were cleared and regenerated by the final two-worker build. Comparing in the same paths is required because v38 intentionally binds the absolute sidecar path into `runtime.bin` and canonical `manifest.json`.

| Song | `runtime.bin` SHA-256 | `manifest.json` SHA-256 | `song.mabf.bin` SHA-256 |
|---|---|---|---|
| Retired fixture A | `5ea974524921ddf6e476b894b2efc2b000209672501bfebaaeda12eb9c68933f` | `4ac703d00cd1211751bf05aabf42df2e2aeea6ebec433938b3642c76346a121a` | `8b3fb1d6aef90b55f70a62d75262482c8bc1b51a6290a05e825cd1b156f833d2` |
| Retired fixture B | `b5d9b6a915470cb19e92ff9790834f80b381744ca7599aeb0294e52b51c8f74f` | `44734cbaec33adbedcc52e3ea7dcf79f2eedc2a01c9a340e35b234b17d81f7fd` | `0014c21541a6fbc9e5472f64f385bf7c8af8becddeccd034d7e215baaea7a428` |
| Retired fixture C | `eddca67d99a81cad031f9f1d7836b7ad3a208e6a67cc10a2dd7631840324b106` | `344461e4153598c1217cceaec4742e10a57cda84555c91e454d0a5c97b2b1c64` | `85320b7b3905ebc1aecbce51285efc3e48606c0a29e9e9a1903982eec2866064` |
| Retired fixture D | `0e3f13088c0480f87bc0d98acd14e146dfbcc7eb81b0f212349de1af55a8ef85` | `4ada4f4351c63c831ebef50d10571aae7df2692c93e93fb8d31566d7f533bff9` | `b4e5ee3dbcdca47a045a5dbab32e6ac2ca98855c0e734106eea513f30e5bef63` |
| Retired fixture E | `de1c98975a782ed1213dbc2f67c0e25063c16d1bc2914955bdc64712e6b38c25` | `f6f55ffebb441ca925a6aa2dc773bd22a9e4a56d9c35c66496f34fc23d75b185` | `5f3bb7c331da688e64fb693565ee6e3802299ef860eed26b7f6e1a802257e339` |
| Retired fixture F | `61294843b8614d182990a134d25a9bbbcc227e1eb22ae791e698dd6aaab53f0c` | `815b1c12311b9d233a9624a15aebe34f3a90691c459fab3b46c7a01943dcf39a` | `37ead8879264f2ce7a3c9102509de1ce7dc1a7b72158c68553d92658ace53f52` |

Result: 18 comparisons, zero mismatches. There is no byte or semantic change and no cache-version bump.

## Batch Benchmarks

### Before: Serial

| Song | Cold seconds | Warm seconds |
|---|---:|---:|
| Retired fixture A | 23.287 | 0.208 |
| Retired fixture B | 18.199 | 0.165 |
| Retired fixture C | 32.889 | 0.274 |
| Retired fixture D | 40.590 | 0.209 |
| Retired fixture E | 42.011 | 0.240 |
| Retired fixture F | 22.642 | 0.173 |
| Batch | **179.619** | **1.269** |

### After: Fixed Two-Worker Pool

Per-song elapsed times overlap and therefore do not sum to batch wall time.

| Song | Cold elapsed seconds | Warm elapsed seconds |
|---|---:|---:|
| Retired fixture A | 31.503 | 0.224 |
| Retired fixture B | 27.686 | 0.183 |
| Retired fixture C | 44.712 | 0.316 |
| Retired fixture D | 58.993 | 0.232 |
| Retired fixture E | 54.150 | 0.247 |
| Retired fixture F | 31.548 | 0.181 |
| Batch wall | **126.597** | **0.704** |

Cold batch wall time improved by 53.022 s (29.5%). Warm batch wall time improved by 0.565 s (44.5%). Individual cold jobs slow under contention, which is why the worker count remains two rather than scaling with CPU count.

## Selftest Optimization And Coverage

Repeated runtime/MABF/manifest corruption cases previously regenerated the same immutable 8.5-second cold fixture after every rejection. `load_song_directory` now has a production-defaulted `rebuild_invalid_cache=true` argument. Tests pass `false` only for validation-only corruption probes.

Each corruption probe now proves all of the following:

- Strict validation returns `CacheMiss`.
- No warm song is published.
- The trace never enters `audio_decode_started`, a robust non-wall-clock performance assertion.
- Mutated runtime, MABF, and manifest files remain byte-identical during rejection.
- Immutable known-good artifacts are restored before the next independent mutation.

The original real cold rebuild after manifest invalidation and exact warm reload remain, so the cold/warm boundary is still exercised. Runtime framing/checksum/trailing-byte cases, rechecksummed semantic mutations, profile/chart/omission mutations, coordinated manifest mutations, all three MABF mode corruptions, and structurally valid substituted MABF coverage remain independent.

The row-limit fixture retains the same approximate tick span and every growth, recognizability, repair, >512 omission, publication-limit, and warm-cache assertion. It now uses 1,200 onsets at 180 ticks and 460,000 us/quarter with 210 seconds of audio instead of 1,800 onsets at 120 ticks and 500,000 us/quarter with 226 seconds. It still creates a complete candidate above 512 rows, but removes redundant source density and work.

Additional coverage verifies:

- Sorted successes and sorted failure-code order under bounded discovery.
- Equivalent chart-policy identities/generations across concurrent loads.
- Two same-directory cold callers both succeed, followed by a valid warm load.
- No atomic `.tmp.` remnants remain.
- Concurrent HCA encodes are byte-identical to serial output.

Final `song_repository_selftest` stage times in CTest:

| Stage | Time |
|---|---:|
| closed schema | 15.2 ms |
| atomic default JSON | 44.8 ms |
| bounded discovery/race | 32.8 ms |
| growth/cache/manifest | 1.436 s |
| normal policy | 67.3 ms |
| extended diagnostic | 2.012 s |
| adaptive metronome | 210.5 ms |
| gain envelope/HCA | 117.2 ms |
| limiter manifest | 89.4 ms |
| row limit | 51.193 s |
| Synthetic envelope integration | 22.188 s |
| Synthetic reviewed profiles | 55.657 s |
| Complete executable | **133.13 s** |

The synthetic envelope test independently decodes generated audio and validates exact frames, envelope/loudness/limiter/timing/metronome contracts. The synthetic reviewed-profile test independently regenerates profiles and checks witnesses, conflicts, native timing, routes, windows, and stream ceilings. No independent corruption, atomic, metronome, gain, limiter, diagnostic-tail, or reviewed-profile coverage was deleted.

## Verification

- Final Release rebuild: passed in 11.074 s; a repeat diagnostic scan found no warning/error text.
- Full Release CTest: 10/10 passed, zero failures, 187.67 s CTest time.
- `midi_generator_selftest`: passed, 54.07 s; its source was not edited for this task.
- `song_repository_selftest`: passed, 133.13 s.
- `hca_encoder_selftest`: passed, including concurrent determinism.
- `git diff --check`: zero substantive findings; existing workspace line-ending notices only.

## Residual Risks

- Cold speedup depends on CPU, storage, and memory bandwidth. Measurements showed slower individual jobs under contention even though batch wall time improved.
- Two simultaneous cold songs can roughly double the previous serial high-water memory. The fixed cap prevents growth with song count; mode-level parallelism was rejected to avoid a further multiplier.
- A 64-stripe lock collision can serialize unrelated song directories. It cannot corrupt output or alter publication order.
- Warm startup remains intentionally CPU-bound by full all-mode MABF/HCA validation. Streaming or memory mapping was not adopted because measured file read/hash time is small relative to semantic/frame validation and the current bounded reads preserve simple fail-closed behavior.
- No in-game run was performed by instruction. Startup ordering and publication are covered structurally and by repository selftests, but final list presentation remains an in-game validation item.
