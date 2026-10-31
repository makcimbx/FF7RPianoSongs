# In-Game Validation

Use this matrix only for focused manual qualification on a supported executable. One DLL or ASI supports one game build, so every scenario is qualified per game build with the artifact compiled for it. Evidence collected on one game build does not carry to another; a newly cataloged build inherits no qualification. Automated agents must not launch the game. After explicit human authorization they may use the repository runtime-gate tool for transactional ASI installation, evidence collection, keep, and rollback. A human tester controls saves, launch, input, and game state.

The current release is the standalone proxy DLL. Player installation follows
[README](../README.md#install); authorized development DLL sessions follow the
[proxy procedure](BuildAndRelease.md#single-normal-import-proxy-experiment).
The ASI runtime-gate preconditions and session commands below apply only to that
alternative edition, not to the current proxy package. The gameplay matrix and
exact-artifact evidence requirements apply to both editions.

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

## Focused Startup Coexistence Run

Use the reviewed PME candidate with the selected unchanged external loader package.
Do not repeat a standalone launch for every diagnostic iteration without a concrete
isolation need. An existing standalone control is context, not acceptance of a new
artifact. Retain each run's DLL identity, full logs, crash
baseline and other-loader inventory; an unchanged extra loader is not a vanilla-game
control. Do not alter external Lua signatures or add a compatibility bridge to qualify
a PME-only startup change.

Check early native/guard publication followed by queued late preparation and successful
feature/service startup. Exercise immediate piano-menu input without a prescribed
wait: an early stock session must remain usable, and custom authority must require a
fresh observed opening rather than being synthesized into that session. On the fresh
opening, play one admitted custom song, pause/resume immediately and exit normally.
A launch-only success does not qualify this flow. Preserve any crash before another
launch; compare its fault with prior PME-disabled failures instead of attributing it
to coexistence solely by timing. Scope the result to the actual observed loader/mod
configuration, not universal compatibility.

For an owned post-CRT handoff candidate, retain its actual source/consumption and
publication outcome. Earlier entry-transport completion is not that authority, and
CRT state 2 alone is not allocator or custom-audio readiness. Record the exact external
settings, signatures and enabled native/Lua mods: a core-only run does not reproduce
an author's larger profile. UE4SS acceptance requires successful FName/FText/GU
discovery and its event loop, not merely a matched signature or the absence of a
crash. Do not infer a captured allocator exception from callback prints alone.

For window-input attachment changes, include a foreground launch and a launch left
in the background with the same loader/mod configuration. Do not bring the game
forward to assist initialization or prescribe a wait. Retain the selected window's
class/thread/HWND and any Win32 error: auxiliary consoles/overlays must not become
the input target. An absent game window is pending, not a terminal startup failure;
its later appearance must attach without restarting the service. After returning
focus, verify fresh piano-menu input and the custom-song flow above. Focus checks
for actual input must remain in force; background initialization is not permission
for background keyboard gestures. A single foreground success does not qualify
background startup.

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

## Focused Menu-Theme Performance Capture (Debug)

Use a reviewed, explicitly authorized diagnostic installation through the proxy
procedure above. Do not reuse a prior launch's log or infer installed identity
from the source checkout. The tester controls all input and exits normally before
canonical collection; do not attach a second profiler or install another hook as
part of this scenario.

Record the executable/artifact identity, theme, resolution, frame cap/VSync and
Frame Generation state. Keep those settings and the song list unchanged between
comparisons. The first opening and immediate reopen are separate cold/warm cases;
neither justifies a sleep before accepting input.
For opening latency, persist the custom theme before opening. If the tester instead
requests it later from Original, measure request-to-commit separately: Ready-to-commit
then includes deliberate human waiting and is not pure opening latency.

1. With Original selected, open the list, select adjacent songs, then scroll far
   enough in both directions to recycle rows. Close and immediately reopen.
   Start this comparison generation with Original already persisted and no pending
   custom request. Custom brush capture/upload/composition and palette binding
   should remain absent; ordinary native animation forwarding is still expected.
2. Select the affected theme, close/reopen, and repeat the same input sequence.
   Include immediate input on opening, rapid direction changes and immediate exit.
   Keep one theme per comparison generation: a switch within a generation mixes
   the corresponding aggregate window. The runtime's normal theme/commit markers
   identify which generation is suitable for the comparison.
3. Separately switch between available themes and Original, including an early
    switch while preparation is pending. Verify genuine animation alpha, text and
    Bottom restoration; note stock-colored flashes or delayed row styling.
    Normal explicit preparation now completes one requested program in its callback;
    deferred work must represent genuine missing readiness or qualified cleanup, not
    expiration of a performance clock. Check newly exposed rows in both directions:
    each setup repairs its exact row, without advancing unrelated pending requests.
    A finite operation may take longer on slow hardware; do not infer constant
    callback time or refresh-rate-independent FPS from the absence of a time budget.
   Compare Original after a custom theme with cold Original separately: restoration
   is transition work, not permission to keep preparing inactive themes or retain
   expensive palette interception indefinitely. Request a custom theme again to
   verify fresh registration after detachment and correct early-input cancellation.
4. Exit normally and collect the exact installed artifact's log. Retain a video
   and actual FPS/frame-time observations if available. Do not derive GPU FPS from
   the diagnostic CPU-side wall-clock measurements.

Summarize the collected log from the native repository root:

```powershell
python -B tools/menu_theme_perf_report.py 'path/to/FF7RPianoSongs.log'
python -B tools/menu_theme_perf_report.py 'path/to/FF7RPianoSongs.log' --json
```

The report separates latency samples from work scopes and exposes root loss,
validation/tree/member work, native-helper counts, cache outcomes, inactive-theme
preparation, row pending delay and teardown. Use self time to avoid adding nested
inclusive durations twice. Its p95 is a histogram bucket upper bound, not an exact
percentile. Inspect dropped/depth/clock/saturation indicators before attributing
costs; an abrupt process exit can lose the final incomplete window. Instrumentation
overhead is present: the metrics selftest prints a local real-clock capture-cost
benchmark, which is not an in-game frame-time calibration. Numeric FPS comparison
and exact visual observations remain the human part of this focused run.

## Focused Menu-Theme Observation (Debug)

This checks the read-only boundary described in [Menu Themes](MenuThemes.md),
not theme application. Use the reviewed build and an explicitly authorized
development installation; never replace the DLL while either game process runs.

1. Launch normally and open the piano song-selection menu. Capture the bounded
   `[menu_theme_observation]` event from its existing exact open callback.
2. Close and immediately reopen the menu. Capture the next session generation;
   there is no prescribed delay, new tick or timed-input window.
3. Check the copied before/after thread, TLS-role, GC, purge and package facts,
   session identity and first unavailable/drift reason. Do not bypass a failed
   observation or interpret zero-serial membership as live-object authority.
4. Confirm ordinary selection and closing still work with unchanged visuals,
   then exit normally and collect the exact installed artifact's log using the
   applicable development-session procedure in [Build And Release](BuildAndRelease.md).

Even healthy stable observations do not qualify texture creation, GC survival,
render readiness, palette animation, theme switching or another executable build.
Record unavailable/drift as unresolved diagnostic evidence, not a feature pass.

## First Background-Theme Slice (Debug, Pending)

Run only after the functional slice is built, independently reviewed and
separately authorized for a closed-game development installation. The retained
read-only observation does not authorize replacement. Preserve the installed
DLL/log, INI and any existing theme files through the canonical session workflow;
never overwrite unrelated themes. Use two small contrasting fixture themes
containing only the supported `MusicList/Bg_1` image override, plus `original`.
This scenario cannot qualify other images, colors or animation states.

1. Record the configured shortcut and selected ID. Launch normally and open the
   selection menu; verify the expected initial background and unchanged song rows.
2. Cycle both fixture themes and `original` using the configured Ctrl+function
   key. Exercise immediate successive presses without a waiting period. Require
   visible distinct backgrounds and exact original restoration, unchanged current
   song/selection/difficulty and no menu rebuild or native failure marker.
   Holding the chord must not repeatedly cycle; releasing and pressing again must
   produce another edge. Background-window input must not queue a delayed switch.
   Confirm the existing preview shortcut still works normally.
3. With a fixture selected, close and immediately reopen. Require original
   restoration before close, a fresh owner/generation on reopening, and reapplication
   of the saved selection using retained immutable resources, not repeated uploads.
4. Exit and relaunch normally with a fixture selected. Verify the remembered ID
   and rendered background agree; a successful in-process assignment alone does
   not prove persistence. Finally cycle back to `original` and exit normally.
5. Collect the exact artifact's full log and reviewed human visual observations.
   Correlate applied/restored/persisted outcomes and owner/generation with the
   installed provenance. For keyboard-delivery diagnosis, distinguish the sampled
   edge, queued ticket and owned message dispatch from actual application; native
   resource preparation alone is not evidence that the shortcut reached it.
   Missing fixtures, unsupported selectors or a failed save
   are not a successful remembered-theme test; fake tests and upload-dispatched
   results are not visual or GC-survival evidence.

Stop on compromised authority/identity/containment, partial restoration, native
fault or ordinary-menu regression. Do not retry through stale targets or add a
sleep. Use the canonical failed/compromised-session disposition after game exit.
No older-profile runtime, full-palette or release qualification is implied.

### Background theme controller input (pending)

With a separately reviewed controller-enabled build, hold L3 and tap L2 to cycle
backward, or R2 to cycle forward, including wrap through `original`. Keep L3 held
while releasing/repressing each trigger: one change per press, no hold-repeat.
Press both triggers together and release one: no delayed change until the gesture
has returned to neutral. Repeat after focus loss and with a controller disconnect;
no stale request may apply after returning. Verify Ctrl+F6 still moves forward and
R3 preview / L3+R3 autoplay retain their existing behavior. Record the actual
controller/provider, visible result and correlated queued/apply/persistence events;
fake XInput or native-key tests do not qualify physical controller delivery.

## Full Menu Theme Integration (Pending)

Preparation must leave Original's colors, gradients and highlights unchanged before
the first successful custom application. Verify release/repress and a held shortcut
separately: holding is not a readiness requirement and must not repeat switches.
Check static information/rank/score labels as well as dynamic values, and move the
selection across previously visible and recycled rows; only the actual selected row
may retain the selected styling. Return to Original and compare gradients as well
as RGB values with the unmodified menu.

Use a fresh process for the color-feedback correction: previously contaminated
MovieScene initial/preanimated caches are not repaired by returning to Original.
On the same visible rows, exercise native fades and selection in each custom theme,
then Original. Verify both visible restoration and, where an authorized copied-facts
diagnostic is available, untinted UObject source values while Slate remains themed.
Do not treat constant RGB suppression as a pass: genuine native color changes must
still be honored.

Run this consolidated scenario only after the complete adapter/palette build is
reviewed, built and separately installed through the closed-game workflow. The
previously accepted background prototype is not evidence for these additional
properties.

1. Prepare two contrasting themes covering main images/text/panels, button brush
   states, song-row normal/selected colors and speed ImageSet entries. Include a
   property in only one theme to verify that switching away restores its original.
   Archive owned obsolete probe fixtures outside active `Themes`; the ordinary
   cycle must contain exactly `original` and the two intended custom IDs.
2. On a cold first open, navigate immediately and request `original` while custom
   preparation is pending. The menu must remain responsive and stock styling
   selectable; early requests must not publish a partial theme or survive a
   retired owner. Correlate bounded preparation-stage timings with the actual
   observed stall, not only total startup time.
   Check the admission log includes both full fixture IDs: cycling only a probe
   background and `original` is not a full-menu pass. Keep a failing-budget
   candidate in the inventory to verify it cannot remove `original` or reject
   unrelated fitting themes.
   Cycle both directions with keyboard and controller. Verify every requested
   category changes together; a partially applied theme is a failure even if its
   ID was saved. Preserve native selection, speed control, navigation and fades.
   Pending or rejected candidates must not consume invisible cycle positions.
   Require actual ready/apply events for both full IDs, not indefinitely repeated
   deferred requests while a probe theme alone becomes ready.
3. Move selection immediately, scroll far enough to recycle rows, then return.
   Verify semantic selected colors, rank/outline updates and native highlight
   animation without old-theme colors leaking into reused rows. Unaffected menu
   subjects must not revert to Original or undergo a complete theme transition
   for one recycled row. Correlate row-local work and complete-application counts
   with navigation; separate time inside native row setup from theme work.
   Also refresh the current item's difficulty without scrolling. A verified
   same-association refresh must not discard its row ticket or wait for general
   theme admission; native brush/color changes made by setup must nevertheless
   become the new Original baseline. Contrast this with a genuine logical rebind.
   Record Frame Generation state when inspecting single-frame warped/doubled
   text; do not use it to dismiss multi-frame stock row plaques or the measured
   Original-to-custom reopening delay.
4. Select `original`, compare with the pre-test menu, then select a custom theme
    and close/immediately reopen. Check complete restoration and fresh-owner
   reapplication, not only the background. Check close latency and warm cached
   reopen separately from a cold first upload. Lose application focus with a
   custom theme active, allow native menu activity, and return: input requests
   must cancel without reverting visible styling or replaying a theme switch.
    Explicitly replace the piano artwork (`MusicList/BaseImg`) and inspect all
    native speed indices with the replacement ImageSet entries.
    Select a composition with a record so `Pnl_HighScore` is actually visible;
    verify the `MusicList/Deco` replacement and the primary/outline/shadow colors of
    both record labels. Check the detail-panel rank star's separate shadow too.
    Speed replacement artwork must account for the authored 18-by-11 brush and
    per-entry multiplicative tints without changing native index/geometry policy.
5. Check the current selection pointer and both glow layers while navigating songs:
   replace/tint `Cursor_Left`, `Img_BtnLight` and `HoverBase`, then restore Original.
   Native motion/fades must remain intact, and a replaced cursor owner must never
   receive restoration through its previous Subject. This is separate from the
   speed arrows and row `Pnl_Light`; no named native effect replacement is claimed.
   With preview audio active, switch between two visibly different Preview palettes
   and Original. Verify background, border/excerpt, spectrum, timeline and foreground
   colors together with native fade and unchanged audio/clock/controls. Missing
   roles restore defaults, not the previous theme's roles. A pending or rejected
   selection must not recolor the badge. Close/reopen and change songs during fade;
   retired-session colors must not appear on the new badge. Optional menu-theme
   failure must not suppress otherwise working preview in a later session.
6. Exit/relaunch to check the remembered ID and visible result together. Collect
   the exact-artifact log and human observations once for the combined scenario.

Stop on compromised ownership, partial restore, native fault or navigation/audio
regression. Do not wait for a retry or write through retired widgets. Fake/native
tests, successful uploads and import-only older mappings do not replace this
visual/lifecycle evidence or qualify another executable build.

## Scenario Matrix

Run only the row or bounded sequence that exercises the changed mechanic during Debug and Integration. The complete matrix is a release-readiness activity, not a prerequisite for every development candidate.

| Scenario | Action | Acceptance |
| --- | --- | --- |
| Loader and fail-closed gate | Start the game build this artifact was compiled for. Where a second supported build is installed, repeat with the artifact built for the other build; otherwise use a deliberately incompatible test condition only if a safe harness exists. | The matching build initializes once. A mismatch disables affected behavior without a crash or guessed address use, and the log reports the expected executable identity beside the observed one so the wrong download is identifiable without further tooling. |
| Discovery and rejection | Open the piano list with valid and intentionally invalid song folders. | Every valid descriptor appears once; invalid input is absent with an actionable log reason. |
| List and navigation | Move between native and custom rows, enter/leave details, and reopen the list. | Ordering, title, focus, and native rows remain stable with no stale custom identity. |
| Gameplay song card (development) | Play a custom song with `cover.png` and optional album/author credits, then one without artwork/credits, then a stock song. Include a long Unicode title, profile changes, immediate pause/resume, abort and replay; inspect the current resolution and an ultrawide or changed UI scale where available. | The top-right title remains readable; the cover sits to its left and the compact metadata row below it without covering note prompts. Missing fields leave no placeholders, and missing/rejected artwork does not prevent playback. No previous song's artwork/credits survive switching, exit or results. Pausing follows the existing title visibility. Record actual appearance and transitions; offline layout tests and successful texture preparation alone are not visual acceptance. |
| Selected-profile list metadata and return focus | Select different profiles, including labels 0/1/3/6/7 and a large label, and charts below/above 512 actions; play or abort one, return, and repeat while catalog preparation or republishing occurs. Include stock rows. | Displayed counts match the complete selected profile rather than its native prefix. Difficulty icons equal the selected label capped at six; zero shows none, and the title retains the exact label. Return focus resolves the last played song/profile by stable identity in the current list, not an obsolete row index, without overriding subsequent deliberate navigation. Missing songs and early input remain recoverable without prescribed waits. |
| Single focus registration and subsequent custom admission | Play a custom song, return to its bookmarked list position, immediately confirm a different custom song, and repeat. Include same-index reopening and immediate profile input. | Open's original focus-registration call is intercepted once, not followed by a second restore call. Confirmed selection survives Close; subsequent activations use the selected ready native cohort and custom chart rather than vanilla fallback. Correlate `focus_interception` and reservation/chart publication diagnostics with actual selected identity and heard audio; `custom_prepared`/`audio_published` alone are not playback proof. A restored highlight or runtime-gate Passed result does not qualify this scenario. |
| Result rank rendering | Obtain a star rank and a letter rank, then inspect results and reopen the song details. | Ranks use the native presentation. Rank thresholds, computed score and persisted song/profile identity remain consistent. |
| Negative page selection guard | With a valid custom profile present, repeatedly enter/leave its details and move immediately among native and custom rows so list rebuilds include both immediate selection and a transient no-selection state; then reopen the list. | Native `-1` no-selection transitions do not fault in the selected visibility setter, no page/list entry is substituted, and subsequent nonnegative selections retain normal focus and visibility. Any `unexpected_negative_bypassed` marker is retained for analysis. No tester delay is required. |
| Difficulty selection | Cycle all visible profiles with deliberate single controller D-pad clicks and keyboard arrows, including release followed immediately by a second press; start one, finish or abort, and reopen. | Each physical D-pad press changes exactly one profile even when multiple input providers overlap, held/repeated reports do not retrigger, keyboard behavior remains unchanged, actual labels remain sparse, selected metadata/chart match, selection locks during play, and per-level score identity remains separate. |
| Autoplay (development) | Record existing native and custom-profile best scores. Try the toggle outside the piano, then in the list and during stock/custom playback using Ctrl plus the configured F key and same-controller L3+R3. Hold, release and repress; exercise immediate pause/continue, off mid-song, return to list, replay and standing up. If the pause menu has no retry, replay through the list rather than inventing a menu action. Finish an assisted play, including one with autoplay subsequently disabled, then a fresh manual play. | Outside-piano chords produce no toggle or sound. Each accepted press produces one toggle and a distinct rising/on or falling/off sound; holding does not retrigger or combine different pads. Native note timing, simultaneous hands, grouped followers and extended tails remain correct. Disabling restores manual handling of future required roots. Pause/resume and list replay retain no stale play authority. Results still show actual score/rank, but assisted plays do not replace native or custom personal records; a new unassisted play can save normally. Capture actual heard signals and note/result behavior; observational Frida data or a startup gate is not a pass. |
| Song preview (development) | In the list, pass quickly over one custom song, dwell on another, then change rows and immediately confirm gameplay. Also stop a preview explicitly, lose focus, and close/reopen the list. Check a stock row and a warm cache. Compare preview and gameplay after changing master/music settings, including 10/5, 5/5 and music zero; restore preferred settings afterward. Separately select manual/off mode in the INI while the game is closed. For a row-attached UI, scroll across list edges and check the current resolution/scaling. | Automatic mode waits for dwell; manual mode waits for its control; off mode stays silent. Only the current custom song previews, without looping or stale asynchronous playback. Navigation, confirmation, focus loss and menu exit stop it before gameplay. Master/music attenuation is applied once; music zero silences preview and reopening uses the current settings rather than a cached gain. Unavailable settings never fall back to unity. Preview never starts charts or changes scores. The visual follows the selected visible row, stays inside its admitted bounds, and progress, song time and equalizer agree with heard audio; the badge stays hidden while scrolling and fades in and out with the preview; row changes and the manual toggle fade out without a click. Cold/warm caches agree without retaining whole-song PCM. Record actual heard transitions and UI alignment; a load marker, raw gain snapshot or startup pass is insufficient. |
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
| Progressive catalog readiness | Observe startup cache checking/rebuild text before entering the piano menu; then open immediately while cache work continues, close, and reopen during active playback/cleanup contention and again after repository preparation settles. Exercise both an already nonempty active custom catalog and the initial vanilla/empty-custom catalog. | Throughout Loading, including any background native menu lifecycle, the overlay shows checked/total, ready, and skipped counts with the running chocobo and distinguishes ordinary validation from actual typed rebuild work. It never exposes active/current-menu counts, count transitions, adoption/deferred state, or reopen guidance. Each quiescent reopen still exposes the complete ready lexical prefix; a recoverable authoritative `Blocked` open forwards once and remains usable without waiting. Ready shows one bounded `Piano Songs: Ready!` notice and fades out on schedule regardless of pending/aligned adoption or delayed first Canvas. Existing list/audio/registry ownership remains unchanged on `Blocked`; `TerminalFailure` alone remains suppressed. No tester delay, polling, deferred input, or automatic replay is required. |
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
