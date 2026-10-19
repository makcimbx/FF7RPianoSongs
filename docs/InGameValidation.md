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
[Build And Release](BuildAndRelease.md#transactional-runtime-gate) section.
`Prepare` installs only the reviewed ASI after two fail-closed process gates and
removes the archived pre-launch log from the live path so stale evidence cannot
be attributed to the new session.
It does not change the installed INI, songs, scores, or caches. `Collect` is
valid only after the human tester exits the game and refuses to attribute a log
when the installed ASI hash changed. The session manifest and full logs remain
outside Git; copy only a bounded sanitized excerpt into `analysis/` when the
evidence is worth retaining.

The runtime gate does not request live ASI teardown. Same-process teardown,
unload, and reload are unsupported and are not a qualification scenario. Keep the ASI,
mounted cohort files and retained native owners in place for the process lifetime;
the human tester exits the game normally before collection or replacement.

After successful Debug or Integration evidence, keep the reviewed current
development ASI installed by default and finalize the session with `Keep`.
Use `Rollback` after failed or compromised evidence, any stop condition, or an
explicit rollback request. `Keep` is an artifact disposition, not a Release
claim, and does not require rerunning the full Release pipeline. Do not leave a
runtime-gate session unfinalized after its evidence has been reviewed.

Installation success is not mechanic evidence. For the current native cached-audio
route, `startupOrdering=Native` requires ordered startup/barrier/hook/initialization
markers, repository settlement, the selected song's generated-artifact marker and the
backend's actual `stage=ready failure=none`. Terminal failure markers make the assessment
`Failed`. `Passed` establishes only this observed log boundary, not audible playback,
list/chart correctness or complete lifecycle behavior. A missed qualified bootstrap or
initializer is unavailable, not permission for late mounting or Sound source patching.

The tool retains older `Legacy`/`Progressive` sidecar assessment support and the
`loader-valid-sidecar` scenario name for compatibility; those are not current native
readiness instructions. `NotObservable` means no first startup marker; `NotObserved`
means the selected boundary was incomplete. Collection is immutable: use another
prepared session, not overwritten logs. Record older or unrelated failures separately.

## Focused Native Audio Integration Run

Use one prepared session after the native adapter's affected tests and safety
review pass. Preserve the previous ASI and check the decoder/bootstrap requirements in
[Build And Release](BuildAndRelease.md#native-cached-audio-development-dependency).
The authenticated executable decoder is preferred; the qualified companion is only a
fallback, not mandatory separate setup for an admitted embedded profile. The tester
launches the game normally; there is no timed human-input window or automated input.

In one continuous piano session:

1. Confirm native cohort readiness for the selected song, then start it and
   pause/resume immediately, then again later. Input before readiness must remain
   recoverable; do not prescribe a wait duration or substitute a stock route as a pass.
2. Retry it, return to the list, and activate it again with another available
   profile. Check the chart, duration, progress and score identity as well as audio.
3. Switch to a different custom song and then a stock song. Confirm no old audio,
   chart or profile survives. Reopen the list and select the first custom song again.
4. Exit the game normally, then collect the exact installed artifact's log.

Readiness alone is not a gameplay pass. Record actual heard audio, synchronization
and each transition; any stop condition ends the run without trying another route.
Per-play chart/profile retirement does not release the backend's retained native
acquisitions, registrations or files. No source replacement, synthetic request replay,
live release or unmount is part of this procedure. This bounded integration run is not
the full Release matrix, and qualification does not carry to another executable build.

## Scenario Matrix

Run only the row or bounded sequence that exercises the changed mechanic during Debug and Integration. The complete matrix is a release-readiness activity, not a prerequisite for every development candidate.

| Scenario | Action | Acceptance |
| --- | --- | --- |
| Loader and fail-closed gate | Start the game build this artifact was compiled for. Where a second supported build is installed, repeat with the artifact built for the other build; otherwise use a deliberately incompatible test condition only if a safe harness exists. | The matching build initializes once. A mismatch disables affected behavior without a crash or guessed address use, and the log reports the expected executable identity beside the observed one so the wrong download is identifiable without further tooling. |
| Discovery and rejection | Open the piano list with valid and intentionally invalid song folders. | Every valid descriptor appears once; invalid input is absent with an actionable log reason. |
| List and navigation | Move between native and custom rows, enter/leave details, and reopen the list. | Ordering, title, focus, and native rows remain stable with no stale custom identity. |
| Selected-profile list metadata and return focus | Select different profiles, including labels 0/1/3/6/7 and a large label, and charts below/above 512 actions; play or abort one, return, and repeat while catalog preparation or republishing occurs. Include stock rows. | Displayed counts match the complete selected profile rather than its native prefix. Difficulty icons equal the selected label capped at six; zero shows none, and the title retains the exact label. Return focus resolves the last played song/profile by stable identity in the current list, not an obsolete row index, without overriding subsequent deliberate navigation. Missing songs and early input remain recoverable without prescribed waits. |
| Single focus registration and subsequent custom admission | Play a custom song, return to its bookmarked list position, immediately confirm a different custom song, and repeat. Include same-index reopening and immediate profile input. | Open's original focus-registration call is intercepted once, not followed by a second restore call. Confirmed selection survives Close; subsequent activations use the selected ready native cohort and custom chart rather than vanilla fallback. Correlate `focus_interception` and reservation/chart publication diagnostics with actual selected identity and heard audio; `custom_prepared`/`audio_published` alone are not playback proof. A restored highlight or runtime-gate Passed result does not qualify this scenario. |
| Result rank rendering | Obtain a star rank and a letter rank, then inspect results and reopen the song details. | Ranks use the native presentation. Rank thresholds, computed score and persisted song/profile identity remain consistent. |
| Negative page selection guard | With a valid custom profile present, repeatedly enter/leave its details and move immediately among native and custom rows so list rebuilds include both immediate selection and a transient no-selection state; then reopen the list. | Native `-1` no-selection transitions do not fault in the selected visibility setter, no page/list entry is substituted, and subsequent nonnegative selections retain normal focus and visibility. Any `unexpected_negative_bypassed` marker is retained for analysis. No tester delay is required. |
| Difficulty selection | Cycle all visible profiles with deliberate single controller D-pad clicks and keyboard arrows, including release followed immediately by a second press; start one, finish or abort, and reopen. | Each physical D-pad press changes exactly one profile even when multiple input providers overlap, held/repeated reports do not retrigger, keyboard behavior remains unchanged, actual labels remain sparse, selected metadata/chart match, selection locks during play, and per-level score identity remains separate. |
| Autoplay (development) | Record existing native and custom-profile best scores. Try the toggle outside the piano, then in the list and during stock/custom playback using Ctrl plus the configured F key and same-controller L3+R3. Hold, release and repress; exercise immediate pause/continue, off mid-song, return to list, replay and standing up. If the pause menu has no retry, replay through the list rather than inventing a menu action. Finish an assisted play, including one with autoplay subsequently disabled, then a fresh manual play. | Outside-piano chords produce no toggle or sound. Each accepted press produces one toggle and a distinct rising/on or falling/off sound; holding does not retrigger or combine different pads. Native note timing, simultaneous hands, grouped followers and extended tails remain correct. Disabling restores manual handling of future required roots. Pause/resume and list replay retain no stale play authority. Results still show actual score/rank, but assisted plays do not replace native or custom personal records; a new unassisted play can save normally. Capture actual heard signals and note/result behavior; observational Frida data or a startup gate is not a pass. |
| Explicit chart | Play an explicit-note song through prompts and completion. | Prompt timing, pitch/chord identity, progress, score, and completion agree with the authored chart. |
| Ordinary dual-event count | Play an ungrouped five-row chart with two dual rows (seven events), placing a dual row at the end. Finish, return to the list, and immediately activate a different song. | All seven events, including both sides of the final row, remain playable. Capture post-expand native count, not just the descriptor's planned count. Completion and subsequent activation succeed without a required wait. |
| Grouping, filtering, and alternate input | On an ordinary short chart, compare a C/D/E monotone run with and without a contiguous nonzero group; compare full `pca_C` with `ignore_sound: ["En2"]`; compare default and alternate C4/C#4 at the same pitch. Use a quiet source recording so native note sounds are distinguishable. | One right-hand root triggers each timed follower without additional input; ignored E alone is absent from the chord's native sound; alternate variants use the expected distinct input with the same sound. Record actual input direction and heard notes. Do not infer this from FName transport or a planned count. Exercise immediate pause/resume, natural completion, and immediate list-return reuse. |
| Song-local chord compositions | On the exact game build being qualified (1.004 or 1.005), use quiet source audio and the SongFormat custom-composition example: full octave-3 C, E-filtered C, and fully filtered C. Include a grouped chord follower and a second difficulty using the same root definition. Press immediately when input becomes available, pause/resume immediately, retry, then abort or finish and immediately choose a song with a different C composition followed by a stock song. | The original input and scoring actions remain unchanged; only the authored charted sounds change. Exact filtering removes the named replacement sounds, including a silent-but-scored chord. Slot strengths, native timing, group followers, and both difficulties remain correct. No old composition survives reparse, retirement, or song switch; wrong-key/free-play remains stock. Early input is handled or rejected recoverably without a prescribed wait or silent stock substitution for a feature-dependent event. Capture actual heard pitches, current-song admission/revocation diagnostics and completion, not just planned sound IDs. Missing capability or unsupported build must reject the feature-dependent route. A 1.005 run does not qualify 1.004. |
| MIDI chart | Play representative low/high generated profiles. | Profiles are recognizable source-backed reductions; no impossible row, tail prompt, or label compaction appears. |
| Audio first play | Start a song from a cold cache. | Audio begins once, remains synchronized, and no native track leaks underneath. |
| Pause and resume | Pause during playback and resume repeatedly. | Audio/chart state pauses and resumes together without restart, drift, or automatic stop transition. |
| Retry and completion | Retry from gameplay, complete normally, and return to menus. | The game's native playback stops/restarts correctly; chart/profile admission retires without replay or stale active identity. Backend native owners and files remain retained until process exit. |
| Abort and song switch | Abort mid-song, select another custom song, then a native song. | No stale audio, chart, selection, callback, or score identity becomes active after the transition. |
| Retained cohort reuse | Play two custom songs in sequence, return to the piano list, then immediately reactivate a ready custom song without an artificial delay. | The exact retained cohort remains ready without remounting, reacquiring or re-registering it. A fresh per-play chart/profile reservation succeeds without old-song leakage; failed identity/readiness checks stay fail-closed. No live native release, unmount, source patch or replay is attempted. |
| Cache warm load | Reopen the same song without changing inputs. | Valid cache is reused and behavior matches the cold build. |
| Cache invalidation | Change one semantic input or request one rebuild. | Stale cache is rejected/rebuilt atomically; failed generation leaves no publishable partial descriptor. |
| Progressive catalog readiness | Observe startup cache checking/rebuild text before entering the piano menu; then open immediately while cache work continues, close, and reopen during active playback/cleanup contention and again after repository preparation settles. Exercise both an already nonempty active custom catalog and the initial vanilla/empty-custom catalog. | Throughout Loading, including any background native menu lifecycle, the overlay shows processed/total and ready counts and distinguishes ordinary validation from actual typed rebuild work. It never exposes active/current-menu counts, count transitions, adoption/deferred state, or reopen guidance. Each quiescent reopen still exposes the complete ready lexical prefix; a recoverable authoritative `Blocked` open forwards once and remains usable without waiting. Ready shows one bounded `Piano songs ready` notice and hides on schedule regardless of pending/aligned adoption or delayed first Canvas. Existing list/audio/registry ownership remains unchanged on `Blocked`; `TerminalFailure` alone remains suppressed. No tester delay, polling, deferred input, or automatic replay is required. |
| Normal process exit | Leave piano content, return to title where safe, and exit normally. | No crash, hang, delayed callback fault, or audio continuing past its expected stop occurs. This does not test live ASI unload or release of process-retained cohorts. |

## Evidence To Capture

- Exact executable identity, the game build the artifact was compiled for, and the artifact hash.
- Scenario name, song/profile, starting state, actions, expected result, and observed result.
- Relevant bounded log excerpts with secrets or personal paths removed.
- Whether the cache was cold, warm, invalidated, or rebuilt.
- The selected cohort/generation, decoder source, native readiness or first-failure
  reason, and which transitions were actually exercised. File preparation, retained
  object completion and row verification are separate from hearing correct playback.
- A clear statement of what was not tested.

Keep the complete runtime-gate session and immutable collected log under the configured
evidence root (by default `%LOCALAPPDATA%/FF7RPianoSongs/runtime-gates`). `Accepted`
means the reviewed artifact was kept, not that this matrix passed. Keep only useful
dated sanitized excerpts or source links under `analysis/`; summarize the current
artifact disposition and remaining gameplay gaps in [Current Status](CurrentStatus.md).
Experimental ZIPs and older backend diagnostics do not transfer qualification to the
current source, artifact or executable build.

## Stop Conditions

Stop the scenario immediately after any crash, hang, repeated native assertion, pointer/identity guard failure, unexpected owner generation, audio continuing after stop, chart/audio divergence, native list corruption, score identity corruption, or package rollback failure. Do not continue gathering evidence from a compromised session. Keep the loaded ASI and native backing files intact until process exit; collect evidence and use the runtime gate to restore the prior artifact only after the game is closed. Do not attempt live teardown or unmount as recovery.

Do not reinterpret a fail-closed rejection as a partial pass. Do not broaden a focused run after a stop condition.

## Smoke-Test Boundary

The stock launch-only smoke test proves only that the selected launch path reaches its own acceptance point without an observed startup failure. It cannot validate song discovery, list/UI integration, profile selection, chart mutation, progress, scoring, HCA playback, pause/resume, retry, completion, song switching, or teardown. Use it only for a focused mechanic that does not require entering the piano list, and never report it as evidence for the matrix above.
