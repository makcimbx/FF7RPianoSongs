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
- During development, use targeted checks and an early reversible runtime scenario. Block completion for a concrete bug or plausible failure in the changed behavior, not a severity label, speculative hardening, or coverage depth alone. Report relevant untested scope without expanding the task automatically.
- Runtime correctness must not depend on sleeping or asking the tester to wait. Use explicit readiness/state transitions and safe early-input handling; test immediate as well as delayed pause, resume, exit, selection, and cleanup paths. A timeout bounds failure but is not proof of quiescence or ownership.
- Keep source processing and cache construction offline; game callbacks consume validated outputs rather than parsing user files.
- Never replace the installed ASI while the game is running; preserve rollback evidence when an install may overwrite a useful artifact or log.
- Choose the smallest focused manual scenario that exercises the changed mechanic. A launch-only check cannot validate flows that require game interaction.
- Debug and Integration can be tested and reviewed as scoped worktree diffs; a commit or stash is not a prerequisite. Commit or publish only when explicitly requested. Release packaging still requires its documented clean source identity.
- Require independent review for changes to hook addresses/calling conventions, unsafe memory, native lifetime/concurrency, or install/rollback behavior. Other changes normally need a diff review and affected tests, not a second agent or full release workflow.
- After a successful Debug or Integration run, keep the current reviewed development ASI installed by default and finalize the collected runtime-gate session with `Keep`. Roll back the ASI automatically only after failed or compromised evidence, a stop condition, or an explicit rollback request. Artifact disposition does not promote the task to Release mode; run full Release validation only for an explicit release, publication, package, or release-readiness claim.
- Rollback is lead-owned unless delegated. Retain the installer session, useful logs, tested artifact identity, and prior ASI. Preserve exact source and ASI/PDB separately only for unresolved native-failure reproduction, before overwriting a candidate still under analysis, or on request. A stash is one optional snapshot method.
- Remove only owned temporary diagnostics when no longer useful. Keep unresolved evidence and the needed rollback artifact; never restore the entire worktree or remove unrelated user stashes as routine cleanup.

## Optional External Automation

- External automation may provide product-specific pipeline, runtime, or validation helpers, but those helpers are optional and are not part of this repository contract. A standalone checkout requires only the files and commands referenced above.
