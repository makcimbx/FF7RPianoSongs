# In-Game Validation

Use this matrix only for focused manual qualification on a supported executable. One ASI supports one game build, so every scenario is qualified per game build with the artifact compiled for it. Evidence collected on one game build does not carry to another; a newly cataloged build inherits no qualification. Automated agents must not launch the game. After explicit human authorization they may use the repository runtime-gate tool for transactional ASI installation, evidence collection, keep, and rollback. A human tester controls saves, launch, input, and game state.

## Preconditions

- Checks required by the selected work mode have passed: targeted checks in Debug, a Release build plus affected tests and generated/catalog checks in Integration, or the complete CTest and release-audit pipeline in Release. Every runtime-gate `Prepare` validates exact Release provenance, production inputs, `dist` artifact identity, and executable identity. Identity is checked against the one build recorded in the artifact's provenance, not against the set of supported builds, so an artifact built for another game build is refused before installation. Its default `Packaged` mode additionally validates `package/`; focused Debug and Integration sessions may use `-Mode Development` without package or staged-document qualification.
- The tester has backed up relevant saves. The runtime-gate receipt retains and hashes the previous ASI until the session is finalized.
- Use only the song/input fixtures required by the focused scenario. The complete Release matrix uses an explicit chart, a MIDI-generated multi-profile song, a cache-rebuild case, and an intentionally invalid song; individual Debug and Integration runs do not need unrelated fixtures.
- Debug logging is enabled only for the focused run and restored afterward.
- Logger initialization truncates `FF7RPianoSongs.log`. Use `runtime_gate.ps1 -Action Prepare` before launch and `-Action Collect` after exit so the previous and current logs are preserved without relying on append behavior.

## Automated Session Boundary

From a repository checkout, use the exact `Prepare`, `Collect`, `Status`,
`Finalize`, and `Recover` commands in the repository-only
`docs/BuildAndRelease.md` transactional runtime-gate section.
`Prepare` installs only the reviewed ASI after two fail-closed process gates and
removes the archived pre-launch log from the live path so stale evidence cannot
be attributed to the new session.
It does not change the installed INI, songs, scores, or caches. `Collect` is
valid only after the human tester exits the game and refuses to attribute a log
when the installed ASI hash changed. The session manifest and full logs remain
outside Git; copy only a bounded sanitized excerpt into `analysis/` when the
evidence is worth retaining.

The runtime gate does not request live ASI teardown. Same-process teardown,
unload, and reload are unsupported after two formally successful shutdown
reports were followed by user-visible Fatal Errors. This is not a release
scenario or release requirement. Keep the ASI attached for the process lifetime;
the human tester exits the game normally before collection or replacement.

After successful Debug or Integration evidence, keep the reviewed current
development ASI installed by default and finalize the session with `Keep`.
Use `Rollback` after failed or compromised evidence, any stop condition, or an
explicit rollback request. `Keep` is an artifact disposition, not a Release
claim, and does not require rerunning the full Release pipeline. Do not leave a
runtime-gate session unfinalized after its evidence has been reviewed.

Installation success is not mechanic evidence. A `Passed` loader-sidecar
assessment proves only the ordered startup markers and one nonempty
`status=ready` sidecar marker selected by the session. Collection is immutable;
run another prepared session rather than overwriting its log evidence.
`NotObservable` means the
mod did not reach the first startup marker; `NotObserved` means the run did not
reach the complete selected boundary. Older or unrelated product failures must
be recorded separately rather than attributed to the current change.

## Scenario Matrix

Run only the row or bounded sequence that exercises the changed mechanic during Debug and Integration. The complete matrix is a release-readiness activity, not a prerequisite for every development candidate.

| Scenario | Action | Acceptance |
| --- | --- | --- |
| Loader and fail-closed gate | Start the game build this artifact was compiled for. Where a second supported build is installed, repeat with the artifact built for the other build; otherwise use a deliberately incompatible test condition only if a safe harness exists. | The matching build initializes once. A mismatch disables affected behavior without a crash or guessed address use, and the log reports the expected executable identity beside the observed one so the wrong download is identifiable without further tooling. |
| Discovery and rejection | Open the piano list with valid and intentionally invalid song folders. | Every valid descriptor appears once; invalid input is absent with an actionable log reason. |
| List and navigation | Move between native and custom rows, enter/leave details, and reopen the list. | Ordering, title, focus, and native rows remain stable with no stale custom identity. |
| Negative page selection guard | With Find the Flame profile 3/difficulty 4/238 notes present, repeatedly enter/leave its details and move immediately among native and custom rows so list rebuilds include both immediate selection and a transient no-selection state; then reopen the list. | Native `-1` no-selection transitions do not fault in the selected visibility setter, no page/list entry is substituted, and subsequent nonnegative selections retain normal focus and visibility. Any `unexpected_negative_bypassed` marker is retained for analysis. No tester delay is required. |
| Difficulty selection | Cycle all visible profiles with deliberate single controller D-pad clicks and keyboard arrows, including release followed immediately by a second press; start one, finish or abort, and reopen. | Each physical D-pad press changes exactly one profile even when multiple input providers overlap, held/repeated reports do not retrigger, keyboard behavior remains unchanged, actual labels remain sparse, selected metadata/chart match, selection locks during play, and per-level score identity remains separate. |
| Explicit chart | Play an explicit-note song through prompts and completion. | Prompt timing, pitch/chord identity, progress, score, and completion agree with the authored chart. |
| MIDI chart | Play representative low/high generated profiles. | Profiles are recognizable source-backed reductions; no impossible row, tail prompt, or label compaction appears. |
| Audio first play | Start a song from a cold cache. | Audio begins once, remains synchronized, and no native track leaks underneath. |
| Pause and resume | Pause during playback and resume repeatedly. | Audio/chart state pauses and resumes together without restart, drift, or automatic stop transition. |
| Retry and completion | Retry from gameplay, complete normally, and return to menus. | Prior resources stop once; retry starts cleanly; completion releases ownership without replay or leak. |
| Abort and song switch | Abort mid-song, select another custom song, then a native song. | No old audio, chart, selection, callback, or score identity survives the transition. |
| Post-play retained-substrate reuse | Play two custom songs in sequence, return to the piano list after cleanup, then immediately activate another custom song without an artificial delay. | If the retained canonical request remains exactly unchanged at native state 4, the list-return reset qualifies once and the next reservation/admission succeeds. A changed request still requires its authenticated rebase. Cleanup denial such as `native_route_not_proven` remains fail-closed, and no qualification step stops/sets/plays native audio or releases canonical ownership. |
| Cache warm load | Reopen the same song without changing inputs. | Valid cache is reused and behavior matches the cold build. |
| Cache invalidation | Change one semantic input or request one rebuild. | Stale cache is rejected/rebuilt atomically; failed generation leaves no publishable partial descriptor. |
| Progressive catalog readiness | Observe startup cache checking/rebuild text before entering the piano menu; then open immediately while cache work continues, close, and reopen during active playback/cleanup contention and again after repository preparation settles. Exercise both an already nonempty active custom catalog and the initial vanilla/empty-custom catalog. | Throughout Loading, including any background native menu lifecycle, the overlay shows processed/total and ready counts and distinguishes ordinary validation from actual typed rebuild work. It never exposes active/current-menu counts, count transitions, adoption/deferred state, or reopen guidance. Each quiescent reopen still exposes the complete ready lexical prefix; a recoverable authoritative `Blocked` open forwards once and remains usable without waiting. Ready shows one bounded `Piano songs ready` notice and hides on schedule regardless of pending/aligned adoption or delayed first Canvas. Existing list/audio/registry ownership remains unchanged on `Blocked`; `TerminalFailure` alone remains suppressed. No tester delay, polling, deferred input, or automatic replay is required. |
| Teardown | Leave piano content, return to title where safe, and exit normally. | No crash, hang, delayed callback fault, or leaked playback occurs. |

## Evidence To Capture

- Exact executable identity, the game build the artifact was compiled for, and the artifact hash.
- Scenario name, song/profile, starting state, actions, expected result, and observed result.
- Relevant bounded log excerpts with secrets or personal paths removed.
- Whether the cache was cold, warm, invalidated, or rebuilt.
- A clear statement of what was not tested.

Store dated runtime evidence under `analysis/` and summarize only the current disposition in [Current Status](CurrentStatus.md).

## Stop Conditions

Stop immediately and restore the prior artifact after any crash, hang, repeated native assertion, pointer/identity guard failure, unexpected owner generation, audio continuing after stop, chart/audio divergence, native list corruption, score identity corruption, or package rollback failure. Do not continue gathering evidence from a compromised session.

Do not reinterpret a fail-closed rejection as a partial pass. Do not broaden a focused run after a stop condition.

## Smoke-Test Boundary

The stock launch-only smoke test proves only that the selected launch path reaches its own acceptance point without an observed startup failure. It cannot validate song discovery, list/UI integration, profile selection, chart mutation, progress, scoring, HCA playback, pause/resume, retry, completion, song switching, or teardown. Use it only for a focused mechanic that does not require entering the piano list, and never report it as evidence for the matrix above.
