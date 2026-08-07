# FF7RPianoSongs Guidance

## Scope

- This file applies only to this standalone native-product repository.
- Treat this repository as the complete product boundary. Historical content-mod
  prototypes and external reverse-engineering workspaces are not part of the
  standalone source tree.

## Canonical Ownership

- Documentation index: `docs/README.md`.
- Mutable implementation status and blockers: `docs/CurrentStatus.md`.
- Supported game builds, exact game addresses, and executable identity: `src/game/rva_catalog.json`. Its schema, the absence sentinel, and the per-build generated trees under `src/generated/<build-id>/` are described in `docs/Architecture.md`; the procedure for adding a game build is in `docs/BuildAndRelease.md`. Never hand-edit generated source.
- Build, test, package, install, and rollback commands: `docs/BuildAndRelease.md`.
- Song authoring and source contract: `docs/SongFormat.md`.
- Focused manual scenarios and evidence requirements: `docs/InGameValidation.md`.
- Do not duplicate those mutable facts in this file or project skills.

## Code Boundaries

- `src/core/` owns generic platform, configuration, logging, safe-memory, and hook infrastructure; `src/pipeline/` owns descriptor-driven offline song parsing and artifact production without game-memory dependencies; `src/game/` owns FF7 Rebirth integration and consumes validated descriptors.
- Keep public song behavior driven by `SongRegistry` and `SongDescriptor`, not song-specific runtime branches.

## Runtime Safety

- Fail closed on unsupported executable identity, signature mismatch, unsafe memory, invalid artifacts, stale object identity, or incomplete lifecycle evidence.
- Install hooks transactionally and roll back partial installation before teardown.
- Never install a second hook at an address already owned by the product; extend the existing callback or shared state.
- Keep temporary runtime mutations scoped and reversible, and do not promote observation into mutation without focused evidence.

## Runtime Observability

- Keep actionable event-driven diagnostics in production when they make future runtime failures identifiable without adding another diagnostic build.
- Prefer stable named stages, outcomes, and first-failure predicates over ambiguous catch-all messages. Preserve enough generation, identity, ownership, and rollback provenance to correlate a failure safely.
- Keep capture bounded and behavior-neutral: copy fixed facts under existing synchronization, then format after native mutation authority and protected locks are released. Diagnostics must not repeat native reads or production predicates.
- Remove only duplicate, per-frame/tick spam and unnecessary raw dumps. Do not delete high-signal admission, bridge, cleanup, token-ownership, release, readiness, or rollback markers merely to reduce log volume.
- Consolidate or retire superseded one-off markers after equivalent stable first-failure coverage exists in a shared diagnostic projection. Observability must not become an append-only reason for keeping obsolete orchestration in a mega-file.

## Workflow Safety

- Select a work mode explicitly: **Debug** for one bounded hypothesis and reversible evidence, **Integration** for one coherent durable change with focused compatibility tests, or **Release** only for an explicit distribution/readiness claim. Follow `docs/BuildAndRelease.md` for validation; do not run release packaging and full qualification during each diagnostic iteration.
- During active development, prefer targeted checks and an early reversible runtime scenario. Only direct crash/corruption/deadlock/native-mutation/rollback risks block the run; defer broader Medium assurance and coverage work until the behavior is proven, then resolve it before final Integration acceptance.
- Runtime correctness must not depend on sleeping or asking the tester to wait. Use explicit readiness/state transitions and safe early-input handling; test immediate as well as delayed pause, resume, exit, selection, and cleanup paths. A timeout bounds failure but is not proof of quiescence or ownership.
- Keep source processing and cache construction offline; game callbacks consume validated outputs rather than parsing user files.
- Never replace the installed ASI while the game is running; preserve rollback evidence when an install may overwrite a useful artifact or log.
- Choose the smallest focused manual scenario that exercises the changed mechanic. A launch-only check cannot validate flows that require game interaction.
- Keep temporary diagnostics and differential experiments uncommitted. After focused checks, checkpoint a coherent durable Integration batch before review and use its SHA for review, build, package, and runtime evidence.
- Before a reviewed uncommitted Debug candidate is installed, preserve its exact temporary source in a named local stash tied to the base SHA and runtime-gate session. Restore temporary source after the run, but keep the stash readable until evidence analysis and durable promotion are complete.
- After a successful Debug or Integration run, keep the current reviewed development ASI installed by default and finalize the collected runtime-gate session with `Keep`. Roll back the ASI automatically only after failed or compromised evidence, a stop condition, or an explicit rollback request. Artifact disposition does not promote the task to Release mode; run full Release validation only for an explicit release, publication, package, or release-readiness claim.
- Rollback is lead-owned unless explicitly delegated; workers must not remove reviewed Debug source, artifacts, or evidence. Always retain the runtime-gate session, tested artifact hash, provenance identity, and prior-artifact rollback copy. Preserve a separate exact ASI/PDB bundle linked to the stash and session when the candidate will be removed or overwritten before analysis, when protected native failure evidence may need reproduction, or when a task explicitly requires binary retention; a successfully kept artifact does not need a redundant copy solely because it was a Debug build.
- At stable boundaries, prune superseded lead-owned `debug:` stashes and linked bundles after their sessions are analyzed and no active task, session, manifest, or unresolved evidence depends on them. Keep only the active candidate, a needed last-known-good fallback, and explicitly retained unresolved evidence; never remove unrelated user stashes.
- Create blocking-review corrections as follow-up commits rather than amending the reviewed checkpoint. Local checkpoint permission never implies push or publication.

## Optional External Automation

- External automation may provide product-specific pipeline, runtime, or validation helpers, but those helpers are optional and are not part of this repository contract. A standalone checkout requires only the files and commands referenced above.
