# Automatic piano input: static evidence and integration boundary

Date: 2026-10-19. Mode: bounded read-only Debug research supporting Integration.

## Outcome

**Not an implementation-ready whole-feature contract.** Native automatic
classification is independently recovered for the catalog default build. The
one-hook proposal that marks roots around the complete original chart update is
not approved: its mutation scope crosses native callbacks without a proven
storage/reentrancy barrier. A narrower three-hook design described below avoids
that particular lifetime extension, but pause eligibility and whole-seat command
authority are not established. Do not connect the toggle to native writes on the
basis of state 9, a retained chart, a timer, or a guessed pause flag.

Only this evidence file was written. No runtime/source/catalog changes, game
launch, debugger attachment, process control, or workers were used. Parallel
config, feedback, and record-suppression work was left untouched.

## Identity and recovery provenance

- Canonical selection: `src/game/rva_catalog.json` default build
  `ff7rebirth-steam-win64-6a16ced2`. Exact identity and already-cataloged addresses
  remain there; none below is a translated address for another build.
- Ghidra TCP instance: project `ff7r`, project path `D:/GhidraProjects/FF7R/ff7r`.
  Every program-specific query explicitly selected `/ff7rebirth_.exe-1.005`,
  internal name `ff7rebirth_.exe-9457d4`, PE, x86:LE:64:default, Windows compiler,
  image base `140000000`. Trace programs were not used.
- Imported DOS/PE headers and the `piano_chart_update` entry signature match the
  selected catalog identity. The installed executable was independently hashed
  again in this pass: file size and SHA-256 match that catalog build exactly.
- The permitted metadata tools do not expose the imported program's full file
  hash. Inline Java identity retrieval was refused because scripts are disabled;
  this was not bypassed. Thus disk authentication is exact; imported-program
  authentication is PE plus sampled-code corroboration, not a measured full
  imported-image hash.
- New recovery used Ghidra decompilation, full disassembly of the matcher,
  advancer, terminalizer and controller tick, direct code/data xrefs, state-table
  bytes, and read-only PE-section searches of the authenticated executable.
  Ghidra's inferred generic signatures were not accepted as ABI evidence.
- Retained starting evidence: `PianoControlSemantics-20261018.md`,
  `PianoMenuSessionEvidence-20260729.md`, `ChordVoicingRuntimeEvidence.md`,
  `PlayableGeneralizedChartEvidence.md`, and `ChartEventAbiGhidra.txt`.
  In particular, old migration records for input/adaptive judgment are not the
  source of the new timing/automatic conclusions here.
- No 1.004 parity or gameplay qualification was recovered. Any future consumer
  must be unavailable on builds without independently admitted equivalents.

## New function fingerprints and call contracts

These are retained static recovery locations, not approved production hook IDs.
Each prefix ends at an instruction boundary, matched the Ghidra bytes, and
occurred exactly once in the authenticated disk file. All use the native Windows
x64 ABI. Addresses here are Ghidra VAs in the selected program only.

| Function | Recovered ABI and authority | Complete-instruction entry prefix |
| --- | --- | --- |
| `FUN_1439a5c88`, event matcher | RCX borrowed event, DL sector byte; no consumed return. Direct callers are the two hand-selection branches in `FUN_14398a008`. | `40 55 48 8b ec b8 60 00 00 00 e8 e9 5e 7c fe 48 2b e0 4c 8b c1 45 33 d2 48 8b 09` |
| `FUN_1439ba584`, event advancement | RCX borrowed event; void. Called by the existing chart update's event loop. | `b8 28 00 00 00 e8 f2 15 7b fe 48 2b e0 48 8b 01 48 8b d1 33 c9 48 85 c0 0f 84 80 00 00 00` |
| `FUN_1439a6758`, terminalizer | RCX borrowed event; void. Only recovered direct code callers are matcher and advancement. | `48 89 5c 24 08 48 89 74 24 10 57 b8 40 00 00 00 e8 13 54 7c fe 48 2b e0 48 83 39 00 48 8b f9` |
| `FUN_1439ba4fc`, controller tick | RCX native controller, XMM1 float delta, AL continuation result. Native scenario wrapper `FUN_143258b18` tail-calls it after adjusting its RCX to its embedded controller. | `40 53 b8 30 00 00 00 e8 78 16 7b fe 48 2b e0 48 8b d9 48 81 c1 b8 0f 00 00` |

Recovery recipe: start at catalog `piano_chart_update`; follow its event-advance,
input-sample and candidate-dispatch calls to the matcher; follow the common
terminalizer call and check its two code xrefs. Recover state-9 update from the
chart updater's native caller and its state-table entry. Follow controller
constructor/state-table setup and scenario wrapper to the controller tick.
Repeat byte/ABI checks before admitting any new catalog consumer. These records
alone do not authorize a hook transaction or establish asynchronous ownership.

## Native fields and required-action identity

The following offsets are static observations for this build, not configurable
knobs or UObject fields. Existing layout declarations and catalog consumers
remain canonical where already present.

| Object | Offset/type | Proven role |
| --- | --- | --- |
| Event, stride `0x90` | `+00` pointer | Owning chart, borrowed. |
| Event | `+08` pointer | Selected native side/control object, borrowed. |
| Event | `+10` pointer | Parent; null identifies required-action roots. |
| Event | `+18` pointer | Linked next descendant/follower traversal. |
| Event | `+24` float | Native event timestamp in chart seconds. |
| Event | `+48`, `+49` bytes | Input assignment and selected hand/side. |
| Event | `+4a` byte | Native result classification. |
| Event | `+4d` byte | Preview/active admission set by native advancement. |
| Event | `+4e` byte | Terminal/finalized guard. |
| Event | `+4f` byte | Native automatic/follower marker. Not a result byte. |
| Event | `+50` callable storage | Optional result callback. |
| Chart | `+34` float | Native elapsed time. |
| Chart | `+3c` float | Native late duration. |
| Chart | `+48` float; `+4c` int | Frame-rate divisor and preview-frame setting. |
| Chart | `+50`, `+54` settings | The two native matcher frame-window settings. |
| Chart | `+80` pointer; `+88` int | Contiguous event storage and live count. |
| Chart | `+a0`, `+a1`, `+a2` bytes | Active, native-clock mode, and whole-chart automatic propagation respectively. |

`FUN_1439a0744` counts only parentless events. Followers retain their own time,
assignment, side and sound identity; do not inject every sounding event as a
required action. This agrees with the retained generalized-chart evidence.
`+4b`/`+4c` event bytes have not been assigned invented gameplay meanings here.

Native event advancement permits some finalized events with result 0/3 to pass
its outer condition. That is not permission for the mod to reopen them. A
conservative future root actuator should require parent null, finalized false,
pending result 0/3, exact event membership/owner and admitted chart scope.
Already-marked roots should remain wholly native, without a mod restoration
journal. Do not change active, finalized, result, parent, or child fields.

## Timing and automatic classification

The existing chart update advances native elapsed time using delta or the native
clock helper, updates events, samples sectors via `FUN_14399e7b8`, and dispatches
active candidates through `FUN_1439a5de4` / `FUN_14398a008` to the matcher.
Neutral/no-new-sector is 8. Candidate collection is not a root-only iterator.

Normal matcher behavior compares assignment with sector; mismatch produces 1.
Its two inclusive native time-window checks can produce 5 and 6; other accepted
timing starts at 4. These are downstream of the real sampled-input path, not
adaptive-difficulty thresholds.

When event `+4f` is nonzero, the matcher bypasses sector/neutral rejection. If
native elapsed is before the timestamp, it returns without terminalizing; when
due it classifies 6 and calls the same terminalizer. Advancement classifies an
expired marked event as 6 rather than ordinary miss 2. Thus the native mechanism
also handles a frame crossing the late window; substituting only the correct
sector in the matcher does not cover that case.

The chart-wide `+a2` branch sets every event's marker. Constructor zeroing covers
it; expansion does not reset it. No supported setter or natural demo-mode entry
was recovered. Temporarily setting only `+a2` and restoring that byte is
incorrect: copied event markers survive disabling it.

The product's `custom_audio_playback_clock` currently returns false; it is not an
alternative scheduling clock. No external clock, OS synthesis, direct result
write or adaptive-judgment invocation is needed or approved.

## Marker consumers and callback/lifetime boundary

A read-only Capstone scan of the piano implementation neighborhood in the
authenticated executable, checked against Ghidra function bodies/xrefs, found:

- Catalog `piano_event_construct`: initializes marker zero.
- Catalog `piano_event_deep_copy`: copies marker unchanged. Its recovered direct
  code callers are in the native parser `FUN_1439b3608`, not terminal callbacks.
- Matcher: reads marker to choose automatic versus sampled-sector matching.
- Advancer: reads marker to classify expiry as native 6 versus 2.
- Terminalizer: on result 5/6, writes marker 1 along the descendant chain only.
- Existing chart update: reads whole-chart automatic byte and writes all event
  markers when enabled.

Stack-relative displacement lookalikes were excluded. This is a bounded audited
neighborhood/direct-call result, not a proof that arbitrary aliased access,
cross-module callbacks, or indirect copies cannot exist. The chord cached-copy
function is a different native sound structure; do not conflate it with event
deep-copy merely because both have a copy role.

Terminalizer disassembly is important: it sets finalized, invokes the event
callable through `FUN_140964754`, **then** acquires a strong side-owner reference,
then marks descendants and invokes selected-side virtual `+20`. The event/chart
remain borrowed; the side owner's later strong reference is not chart-array
ownership. The callable and sound callback are real reentrancy boundaries.

State-9's native caller retains controller-owned chart storage for ordinary
gameplay, but no mod-accessible lease freezing its event allocation across these
callbacks was recovered. A before/after header equality test detects a changed
allocation; it cannot undo a copied temporary marker or make an old pointer
safe to dereference. Neither the custom registry snapshot nor a UObject handle
for some other object supplies that missing storage lease.

Consequently, do not mark a batch of roots before the complete original chart
update and blindly restore cached event pointers afterward. A stack guard/RAII
name alone does not prove the guard's pointees survived. The existing
`note_count.cpp::chart_update_detour` calls its original without an exception
recovery boundary; research must not silently add native-fault swallowing.

### Narrower actuator design supported by the instruction order

There is a structurally safer alternative to a whole-update mutation scope:

1. Use the existing chart-update hook only to establish a synchronous admitted
   chart/generation scope. Stock and custom charts need appropriate identity;
   custom Pending/Failed chart admission must remain rejected.
2. In matcher and advancer hooks, temporarily set marker 1 for one eligible,
   previously-unmarked required-action root. Record exact event/chart, old byte,
   scope generation and nesting ownership in thread-local state.
3. In a terminalizer hook, discharge that exact journal and restore the marker
   **before forwarding to the native terminalizer**, hence before any result or
   sound callback. Terminalizer does not read the root's marker; its native
   result and descendant propagation remain unchanged.
4. A matcher/advancer path that returns without terminalization has no external
   game callback on its automatic branch. Restore its byte before returning.
   A discharged guard must never reread/write the event after callbacks.

Full disassembly confirms that the automatic matcher branch and advancement
have no external game callback before their terminalizer call, and no event
loads/stores after that call. Stack-probe prologues are not gameplay callbacks.
This avoids carrying a temporary root byte across callback-driven replacement,
and admits nested work only after the outer journal is discharged. The native
result callback, follower propagation and original calls remain exactly once.

This is three new hook contracts, not a one-line chart flag. They would need one
transactional admission, catalog entries, ABI/unsafe-memory independent review,
and tests of nesting and denied restoration. Do not install partial coverage:
matcher-only substitution can still miss at advancement, and a missing
terminalizer intercept reintroduces callback-crossing mutation.

This design is not approved for production by this evidence. Exact native scope
admission/paused policy is unresolved below. Native faults are not recoverable
gameplay: do not swallow and continue, retry the original, or write through an
unproven old pointer. A failed journal/precondition disables new mutation; if a
byte cannot be restored before a callback, there is no proven continue-playing
recovery. Stop and retain failure/rollback evidence rather than claiming RAII
solves that case.

## Inner lifecycle, parent dispatch, and pause limitation

The native controller is not a UObject. Its constructor installs the inner
14-entry table `DAT_145bc25f0`; entries have stride `0x20` with state ID, enter,
update, exit. `FUN_140c9a160` only installs a pending table entry. Generic
`FUN_1421b26c8` invokes the old exit, publishes the new current entry, then invokes
the new entry; `FUN_1421bb2c8` subsequently calls its update. A pending transition
is not yet the current state. Enter/exit use RCX controller; update also receives
float delta. Do not use a decompiler's apparent extra arguments as a contract.

| Inner state | Static recovery and lifetime significance |
| --- | --- |
| 4 | Preparatory native actor/presentation state; its update eventually requests 5. Not an independently proven paused predicate. |
| 5 | Existing list/menu entry and callbacks. Exact Open/Close/widget-generation authority remains `menu_session_authority`. Activation Close and state-5 Exit do not mean standing up. |
| 6/7/8 | Activation/preparation/count-in path; state-8 update `FUN_143994994` requests 9 when its native presentation condition finishes. |
| 9 | Entry `FUN_143999af4` constructs shared chart storage, installs native callbacks, and expands it. Update `FUN_1439b5290` calls the chart updater with its owned chart and both sides, but may return early for native startup readiness. Missing side owners request 13; completed chart/effects request 11. Exit `FUN_1439a6344` neutralizes input and disables gameplay input without releasing the chart. |
| 11 | Post-play effect/timer sequence (`FUN_143998360`, `FUN_14398fac8`) eventually requests results state 10. Not pause. |
| 10 | Results. Exit `FUN_1439a6440` releases the controller's shared chart pair. Record persistence was not independently traced; a separate worker owns it. |
| 12 | Confirmation dialog. Callable executors `FUN_14399c398` / `FUN_14399c518` route choice 0/1 through `FUN_143983c80` to the controller's two stored destination-state bytes, then dismiss the dialog. Not automatically pause. |
| 13 | Leaving. Entry `FUN_143998a88` requests parent state 6; update `FUN_1439958bc` eventually requests inner 0. |
| 0 | Controller tick's continuation result becomes false when its current entry ID is 0. Destruction can bypass state exits; existing destructor authority remains mandatory. |

Additional recovered callbacks: `FUN_143983d44` requests 13; `FUN_143983d68`
requests the stored second confirmation destination; `FUN_143983e78` requests 13
for one native mode or 4 with a mode flag otherwise. The latter is a re-entry
route, but its UI label and complete retry/record/audio contract were not
recovered. Calling it a qualified retry API would exceed the evidence.

Controller tick dispatches inner state, camera/presentation update, then parent
state. The parent object is at controller `+170`, its dispatcher at parent
`+100`, table `DAT_1466309b0`, nine entries, default 8. Recovered parent updates
perform actor-action, camera, timer and return-to-idle transitions (0 to 1 to 8;
2 to 3 to 4 to 5 to 8; 7 to 8). This is not evidence of a pause state machine.

No explicit state-9 pause/resume route or independently named native paused
predicate was recovered. In particular `FUN_140ff07ac`, used by chart update to
zero a presentation delta, only reads a global context byte; its writer
`FUN_140fe0f80` also performs character/action effects. Neither gives sufficient
semantics to call it Pause. Zero delta, unchanged elapsed, nonzero chart active,
and retained state 9 are not substitutes. The possibility of chart callbacks
continuing with frozen time must not be silently treated as approved judgment
during a pause.

## Toggle, feedback and ownership handoff

Existing source seams remain the smallest infrastructure choices:

- `title.cpp` input service/poller publishes a released-to-held edge intent;
  evaluate L3 and R3 in the same controller sample. Reuse aggregate release,
  focus/device-reset and startup authority; no second native-input hook/poller.
- Lead-owned `core::Config::autoplay_function_key` selects Ctrl+F-key, default
  F8, 0 disabled. Runtime consumers must use that field, not a second parser.
- Intent carries a seated-generation value, not a native pointer. Native/game
  callbacks consume and acknowledge it. Worker threads never read controller,
  dispatcher, event or chart memory.
- Existing menu Ready can establish list eligibility. A distinct seated token
  would have to survive list activation, preparation, play and results, and be
  revoked on leaving/destruction. A native controller-tick observation can
  distinguish current/pending inner state synchronously, but is not by itself
  a proven paused/whole-seat admission contract. Do not reuse menu generation
  retirement or custom playback retention as that contract.
- Produce distinct short on/off feedback only for an accepted state change,
  after releasing mutation/authority locks. Feedback never supplies native
  readiness or timing. Its implementation belongs to the separate core work;
  no native sound function was proposed or called here.
- User policy is no record updates for any assisted play. The assisted latch
  must survive toggling off during the attempt; exact persistence suppression
  belongs to `AutoplayRecordSuppressionEvidence.md` and its worker, not this
  investigation. No score path is duplicated here.

## Final integration blocker

There is no approved production actuator/eligibility pair for the requested
whole feature. The missing fact is specifically **whether and under what native
authority chart judgment callbacks execute while pause/confirmation/retry is
active, and which synchronous seated-generation boundary admits/revokes queued
toggle commands through those routes**. Existing state-9/chart ownership proves
neither. Therefore an implementation cannot yet promise pause-safe automatic
input and immediate seated-only toggle acceptance.

The whole-update root-write proposal additionally lacks a callback-stable
storage lease and must not be used as a shortcut. The three-hook alternative
above removes the proven callback-crossing weakness but does not manufacture
the missing lifecycle fact. This is a stop boundary, not a recommendation to
guess a flag or keep expanding the integration.

Affected existing catalog IDs: `piano_chart_update`, `piano_score_expand`,
`piano_event_construct`, `piano_event_deep_copy`, `piano_event_result_callback`,
`piano_menu_list_open`, `piano_menu_list_cancel_close`, `piano_menu_state5_exit`,
`piano_menu_controller_destructor`. New functions above remain research-only.

Checks: repository/source reads; selected-program static decompilation,
disassembly and xrefs; PE mapping; exact installed-disk identity and four unique
complete-instruction prefix checks. No build/tests, game interaction, pause or
retry trace, live memory validation, 1.004 parity, or release claim.

## Frida observation plan (static preparation only)

Scope: one outside/list/play/pause/resume/retry/leave correlation, after the
human's explicit ready/attach authorization and independent observer review by
the lead. This pass did not attach, launch, inject, or write an observer. Two
observation points suffice; no per-event matcher/advancer/terminalizer probes.
"Read-only" here means observational callback behavior, not an assertion that
inline instrumentation leaves executable pages unchanged.

### Precisely recoverable unowned points

Both locations are absent from default-build catalog entries/hook ownership and
have no address-literal references in current `src/`. This establishes repository
ownership only: the lead must also reject any live-byte mismatch or preexisting
patch/observer at either interception span. Never attach to the product-owned
`piano_chart_update` entry or other catalog-owned hook targets.

1. **Controller tick entry `FUN_1439ba4fc`.** Use its 25-byte complete-instruction
   prefix from the fingerprint table above (rechecked unique once on the exactly
   authenticated disk file in this pass). RCX is the native controller C, XMM1
   is float delta, AL is continuation. Do not decode the second integer argument
   register as that float. Delta capture is unnecessary for the minimal plan.
   Observe entry and ordinary function return; preserve only the controller
   value in that invocation's observer-local frame for the return capture.
   Capture controller facts immediately on entry/return, before returning to
   native caller continuation. Do not dereference chart pointers here.
2. **State-9 pre-chart-call instruction `1439b540b`**, inside unowned
   `FUN_1439b5290`. Use an instruction-point observation only, not a fictitious
   function-return listener at this mid-function address. At this exact CALL,
   RBX=C, RCX=the synchronous chart argument, R8=C+380, R9=C+538; XMM1 is float
   delta. The live chart argument has just been loaded from C+f48. Capture a few
   fixed chart facts there, before native execution resumes; never retain it for
   later dereferencing.

The unique 34-byte fingerprint starts at `1439b53fa`; the interception point is
**fingerprint +0x11**, not its beginning:

```text
48 8b 8b 48 0f 00 00 4d 8b cf 4d 8b c4 c5 f8 28 ce
e8 18 52 00 00 48 8b 8b 48 0f 00 00 e8 e8 b9 fe ff
```

Disassembly: load chart from C+f48; set side arguments; move saved float delta
to XMM1; CALL catalog `piano_chart_update`; reload C+f48; CALL the native chart
presentation query. Resolve that CALL's relative target against the selected
catalog, rather than copying its catalog-owned destination into an observer.
These bytes matched Ghidra and occurred once in the authenticated executable.
Relocate the recovered locations using the loaded executable base relative to
the selected program's image base; never use the Ghidra VA as a live pointer.

**Important measurement limit:** this point proves an invocation of the chart
update entry, not that the product detour forwarded to native judgment. Its
custom-chart readiness gate may suppress the original. Correlate the existing
admission diagnostics or use an ordinary stock chart for the first bounded
lifecycle observation. Frozen elapsed alone does not distinguish pause from a
product gate. No additional hook on the owned entry is permitted to resolve it.

### Exact dispatcher facts for bounded callback capture

Generic dispatcher disassembly and constructor/table bytes establish:

| Controller-relative field | Type and meaning |
| --- | --- |
| C+fb8 | Embedded inner dispatcher, not a pointer to another object. |
| C+fc0 | Signed int32 table count; constructor sets 14. |
| C+fc8 | Table pointer; selected-program table is `DAT_145bc25f0`. |
| C+fd0 | Current entry pointer, or null before publication. |
| C+fd8 | Pending entry pointer, or null; a request, not current state. |
| C+f48 | Chart pointer fact only in tick captures; canonical declaration is `runtime_layouts::PianoChartController::chart`. |

Each table entry is 0x20 bytes: signed int32 ID at +00, entry function pointer
at +08, update at +10, exit at +18. Re-read table bytes confirmed IDs 0..13 in
order. Before dereferencing current/pending, require the expected relocated
table pointer/count and a null or exactly aligned member of that table's
14-entry extent. Validate its ID equals its member index. Do not chase arbitrary
current/pending pointers. Generic update loads current at dispatcher+18; request
helper writes pending at dispatcher+20; transition processing clears pending
after old exit/current publication, before new entry. Entry versus return pairs
therefore distinguish pending requests from transitions processed this tick.

At the pre-chart-call point require RCX nonnull, the synchronous C+f48 value
equal to RCX, and the expected side-register relationships. Copy chart+34
float elapsed, chart+a0 byte active, and chart+a1 byte native-clock mode. This
caller has reached the actual native call after startup/side-owner checks.
Chart/control-block ownership stays native; capturing these facts creates no
lease and does not make either chart or controller a UObject. Do not chase the
control block, event arrays, callbacks, selected-row objects, or descendants.

### Capture, interpretation, and stop conditions

- Copy only those fixed facts inside synchronous callbacks. No memory reads
  from host timers, UI label handlers, background tasks, or saved pointers.
  No native calls, setters, writes, argument/return replacement, result changes,
  hook chaining over another owner, or hook-recovery experimentation.
- Keep tick/current/pending/chart-pointer changes plus bounded interval counters
  for pre-chart-call invocations and their copied first/last elapsed values.
  Format/send summaries only from copied facts; no per-frame console/IPC dumps.
  If a timer flushes copied summaries, it must never dereference native memory.
- Include monotonic observation sequence, thread ID and invocation nesting.
  Controller/chart addresses are correlation facts, not durable identities;
  pointer reuse or overlapping controllers cannot be silently merged. Do not
  infer a lifetime generation from an address alone.
- Human labels distinguish outside, list, playing, pause, resume, retry and
  leave. Silence or state 9 does not itself name pause. A tick return AL=0,
  current/pending 13, or chart-pointer replacement is useful evidence, not
  automatic permission to mutate. State-9 exit alone does not free the chart.
- Stop and report on identity/signature/ownership mismatch, unexpected dispatcher
  layout or entry pointer/ID, unreadable capture, null/mismatched chart at the
  actual callsite, nonfinite elapsed, unsupported register-capture behavior,
  unexpected nesting/concurrency, observer overhead/backlog, crash/assert, or
  apparent observer-induced behavior change. Do not retry failed native reads,
  follow stale pointers, patch around another hook, or swallow native faults.
- No post-exit pointer read, hot product unload, automated launch, or silent
  attach. Lead owns instrumentation disposition/recovery and the reviewed
  observer. End the experiment after the single labeled route; this plan does
   not authorize a mutation experiment or qualify autoplay.

### Observer implementation adjustment

The retained workspace `Tools/Frida/autoplay_observer.js` intercepts the **start of the load block**
at the fingerprint beginning, rather than the CALL at +0x11. RBX supplies the
controller and the observer synchronously reads C+f48 before the native load.
It does not assume RCX/R8/R9 have already been set at that earlier instruction.
This avoids relocating the CALL and changing its native return address; the
CALL bytes and catalog destination are validated before interception, and its
bytes must remain untouched afterward. Tick return captures only AL, never
dereferences the retained controller after callbacks. No source or product hook
is added. Host-side exact disk authentication remains a prerequisite.
