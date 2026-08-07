# Audio Lifecycle Evidence (July 2026)

This chronology was extracted from mutable status prose. It records investigated routes and bounded observations; the durable lifecycle invariants are in `../docs/Architecture.md` and current release disposition is in `../docs/CurrentStatus.md`.

## Implemented Route At Capture Time

- Sidecar loading used generated `.cache/song.mabf.bin`, custom `BGMName=bgm_piano_09`, scoped `SQEXSEADPlaySetup` patching, `BGMSlotController::Set/Play`, and a Stop lifetime guard.
- Audio state was tracked as generation, phase, desired/patched song identity, sound, and controller. The temporary sound patch was restored immediately after original PlaySetup returned.
- A matching reentrant Stop marked state lifetime-unverified before original Stop, preventing a later unsafe write.
- Cross-song pending-sound reuse failed closed. Rebuild began only from a restored route with validated controller/sound identity.
- Native `Set(nullptr)` cleared the retained resource only under one-shot authorization for the exact controller/route generation. Normal `Stop -> Set(newSound) -> Play` transitions could hand authorization to exactly the next validated step; unrelated calls invalidated it.
- The route entered `Rebuilding` before native clear so reentrant Stop could neither commit evidence nor recursively rebuild.
- When re-entry omitted PlaySetup, the preferred route invoked the signature-gated native owner request wrapper at `0x0c92160` with captured request identity, allowing the higher-level piano state-machine tick to reproduce the native transition.
- Direct controller rebuild remained a fail-closed fallback when no compatible owner request profile existed.
- Controller lookup, manager-pause hooks, current slot/state, and owner/resource identities were observation-only unless an exact owned release was authorized.
- List-return release required current-controller lookup, UObject identity, chain continuity, null sound/resource expectations, and exact slot state.
- Native clear also removed `BGM+0x28`; the last validated source sound was therefore retained separately under GUObjectArray index/serial/class/name/outer/flags checks and rooted for process lifetime. This was an intentional bounded-retention tradeoff.
- Install rollback took the exclusive callback gate, restored journals while lifecycle observers remained active, then disabled hooks. Process detach performed no blocking teardown under the loader lock.

## Rejected Or Retired Approaches

- Replaying a retained patched sound pointer was removed as unsafe.
- Replaying observed slot fields plus helper calls produced a matching visible snapshot but did not restore pause.
- Complete original PlaySetup replay alone did not restore pause.
- Owner-memory profile replay was removed after it caused an automatic `0 -> 2 -> 4 -> 6` Stop transition and broke pause-menu resume.
- Ghidra identified BGM state initialization at `0x1249fb4` and resource prepare at `0x124848c`; they were not adopted as guessed direct calls in the release route.
- Same-pointer `Set` was insufficient because native internals return early; the investigated rebuild used `Set(nullptr)`, reacquisition, then `Set(sound)` so native reset/init/prepare could run.

## Focused Observation Chronology

1. Initial focused playback received one patched PlaySetup and repeated controller Play calls while sidecar state remained pending.
2. A 2026-07-10 run played/restored the first song. Re-entry and second-song selection produced neither PlaySetup nor Play, establishing prepared-BGM reuse.
3. Later traces observed matching Stop, `Set(newSound)`, and ordinary Play on one controller. Generation/state transitions established the authorization handoff needed before native clear.
4. A repeat run logged sustained second playback and successful restoration. Pause/exit produced no observed Stop/Set/Play or manager-pause callback, and the rebuilt stream continued; pause therefore remained unresolved in that route.
5. Native-versus-rebuilt slot tracing isolated differing fields. Replaying them plus a helper produced an identical snapshot but did not restore pause, motivating the higher-level owner-request route.
6. The owner request was captured only after a validated routed custom PlaySetup. Compatible same-route arms called the native request; the owner tick then issued fresh scoped PlaySetup for the active target.
7. A focused list-return release reached exact native cleanup and changed the slot from active to empty. The following arm exposed loss of the source sound from `BGM+0x28`, motivating separately validated retained-source identity.
8. A later run reached PlaySetup/Play but strict restoration rejected a field synchronously changed by native SQEXSEAD, disabled the route, and left the next entry silent. Restoration was then narrowed to distinguish validated native ownership transfer from rollback conflict.
9. Later v38 logs cover deferred handoff failure, route telemetry, sound-field telemetry, context isolation, quiescent commit, and retired BGM telemetry. These remain scenario-specific evidence, not a blanket release pass.
10. A retained 2026-07-28 development artifact completed a combined pause/resume, early-list-return, repeated-start, difficulty-change, natural-completion, results-return, and cross-song scenario. Root-to-leaf terminal lineage was authenticated before natural Stop, then callback retirement detached that terminal domain while retaining release ownership. The game's immediate post-Stop Set/Play refresh consequently remained an ordinary unmatched native refresh instead of reopening stale custom mutation authority. Results/list behavior remained usable, a retired local fixture was admitted with custom audio, chart judgments, and progress, and its early list return released only the custom token while preserving the canonical substrate. A later additional activation reached the process-exit evidence boundary before another exact list-return drain, so that final active-exit path is not treated as teardown qualification.

The raw session inputs were `FF7RPianoSongs-v38-route-telemetry-20260715.log`, `FF7RPianoSongs-v38-deferred-handoff-still-fails-20260715.log`, `FF7RPianoSongs-v38-sound-field-telemetry-20260715.log`, `FF7RPianoSongs-v38-context-isolation-custom-not-claimed-20260716.log`, `FF7RPianoSongs-v38-quiescent-commit-runtime-20260716.log`, and `FF7RPianoSongs-v38-retired-bgm-telemetry-20260716.log`. These ignored logs are not canonical evidence targets; the relevant derived observations are retained in the chronology above. Any future claim from them must preserve its bounded excerpts or findings in a tracked evidence file before it is treated as durable.

## Bounded Disposition

Focused evidence now establishes first play, repeated and cross-song admission, pause/resume, early list-return release, natural completion, results return, immediate post-completion native refresh isolation, and exact custom-token release for specific retained development artifacts. It does not qualify the complete lifecycle matrix. Retry, abort, cache cold/warm behavior, active-process exit, and broader teardown remain separate runtime claims before release.
