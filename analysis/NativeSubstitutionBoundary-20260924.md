# Native Substitution Boundary Investigation (2026-09-24)

## Scope and disposition

Bounded Debug evidence for the Integration change
`simplify-native-integration-for-portability`. The question is whether custom
song data can enter native preparation/reuse without reconstructing playback in
the mod. This document is evidence, not authorization to remove ownership guards
or invoke guessed native helpers. Production audio replacement is not yet
authorized by a proven seam.

## Source and artifact baseline

- Investigation source: clean native repository commit
  `4377739f28051c77c1e5bca9d39d9df759381e1e`, before this change's independent
  descriptor/capability/build edits. Parent planning files are not native inputs.
- Latest accepted development artifact recorded in
  [Current Status](../docs/CurrentStatus.md#integration-complete-native-chords-and-fractional-note-values-2026-09-24):
  session `20260924T092820Z-912e59aedcec`, ASI SHA-256
  `c714c628089122e238c0ba20d140eeec53b06c79ea7c2ffb45840ea1664a992b`.
  Its retained session identifies production-input-set SHA-256
  `f590730c4640f76f9de039799006b52f661ed0f3826536f6e4030e47f69d3754`.
- Both the session and immutable evidence identify catalog
  `ff7rebirth-steam-win64-6a16ced2`, executable SHA-256
  `752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc`.
  The recorded disposition is `Accepted/Keep`; aggregate assessment is `Passed`
  for `loader-valid-sidecar`, not complete audio or UI qualification.
- Retained post-run log SHA-256:
  `ac0d718d14cdce93cbaa9274543c498c7275743b53c9afa74a73bcb344c648ae`.
  Session/evidence files and the log were read from the default runtime-gate
  directory documented in [Build And Release](../docs/BuildAndRelease.md).
  These hashes are recorded receipt identities, not freshly recomputed hashes.
  No installed ASI, live process or installed cache was inspected or changed.

The artifact and source baselines are deliberately separate. The session's
startup log identifies product version `0.2.2`; later source commits prepare
`0.2.3`. The session receipt does not supply a source commit sufficient to assert
that its complete production inputs equal the investigation commit. Comparing
`714582b..4377739` shows only release metadata/docs and repository-test changes;
it does not retroactively establish that the session contained exactly
`714582b`. Comparing `e8c2802..4377739` for `src/game` shows only chart-patch,
extended-chart and synthetic-model changes. The owner-request/PlaySetup
coordinator, its activation/borrower/retirement includes, selection and menu
session sources have no intervening changes in that range. This narrows source
drift but does not transfer all gameplay claims across artifacts.

## Repeated-selection evidence, not a resurrected old blocker

The complete [Current Status](../docs/CurrentStatus.md) chronology matters:

1. `20260907T085725Z-ef9b9fc8976c` failed: subsequent selections lost custom
   authority after duplicate native focus registration. It was rolled back.
2. The paragraph describing the single-Open interception correction as not yet
   installed is historical. Later session `20260907T101402Z-8c7002ae6b9d` was
   accepted; its retained `user-disposition.md` explicitly records the tester's
   report of no stock replacement after list return.
3. `20260907T140643Z-ed03f8f87463` was accepted with ASI SHA-256
   `bfceeefa569578f085d35b1a7e91a196681847c1f4b8382c50e22de9dbf4947d`.
   Its retained `manual-result.md` records repeated custom playback, Alicia
   profile changes, early list return and Forest Maiden completion, with nine
   single-original-call menu opens. The immutable aggregate assessment remains
   `NotObserved`; the manual evidence does not relabel it.
4. The latest session above supplies additional bounded repeated-publication
   evidence. The following derived facts are retained here so claims do not
   depend on a Markdown link to an ignored or external log.

In that latest session's `log-after.log`, all thirteen `focus_interception`
records have `original_calls=1 ready=1` (lines 298 through 2872). Eight chart/audio
transactions reach `custom_prepared` and terminal `audio_published`. Selected
examples from the recorded tuples are:

| Preparation | Song key | Native request | Pre-write / published log lines |
| --- | --- | --- | --- |
| 1 | `14051324838298727502` | `0x300000008` | 350 / 378 |
| 2 | `1187376837506045301` | `0x500010008` | 1770 / 1797 |
| 5 | `8131491014181032842` | `0xb00010008` | 2494 / 2521 |
| 6 | `8131491014181032842` | `0xd00010008` | 2583 / 2608 |

These tuples retain the same reported controller/slot/BGM/sound pointers within
the session while requests differ; raw pointers are omitted here. They support
different-song and same-key custom admission with native object reuse on that
artifact. They do not identify the native cache key, prove an earlier
substitution seam, or prove acoustic correctness from logs alone. The user's
positive auditory/play feedback remains the separately recorded bounded result.

## Baseline limitations and attribution rule

There is no demonstrated unresolved recurrence of the old duplicate-focus
failure in the reviewed baseline evidence. Do not convert its superseded pending
paragraph into a prerequisite to repeat the fix. Equally, do not claim the
current source has a freshly qualified complete playback matrix.

Unproven here: exact current-source artifact equivalence, immediate pause timing,
every retry/abort/profile/stock-transition combination, complete native borrow
termination, leak-free exit, other executable builds and packaged artifacts.
The latest machine evidence explicitly excludes complete audio lifecycle and
list/UI/chart mechanics. Prior accepted observations are not evidence for
removing the current orchestration.

Before attributing a future candidate failure to this refactor, name its source,
artifact and scenario and compare against the baseline above. Only a specifically
missing comparison requires a new human-authorized session under
[In-Game Validation](../docs/InGameValidation.md). Static and independent offline
work may proceed. No new runtime session was performed for this baseline record.

## Substitution gate

**No-go for production replacement.** The bounded static investigation reached
the stop boundary without proving a substitution contract across first play,
prepared same-song reuse and different-song selection. No production audio
behavior was changed. Tasks 1.2 and 1.3 remain incomplete; recording this result
does not complete the successful-route gate in task 1.4.

### Static identity and applicability

Fresh read-only Ghidra analysis used imported program
`ff7rebirth_.exe-9457d4`, labeled 1.005, at image base `0x140000000`.
Owner-request, PlaySetup and BGM-prepare entry bytes match the corresponding
1.005 catalog signatures and their recovered call relationships match the
source. The initial pass did not independently check the imported PE fields.
A later read-only pass obtained timestamp `0x6a16ced2`, image size `0x099d9000`
and checksum `0x0769ea6e`, matching the catalog. Available Ghidra metadata did not
expose the imported file's SHA-256 or file size. Therefore these are PE-field and
signature-correlated findings, not a full imported-file hash verification; the
separately hashed installed executable does not supply that missing identity.
The 1.005 PlaySetup
catalog record itself is migrated evidence, not independent recovery. No 1.004
image was rechecked. The addresses below identify this investigation only; use
`src/game/rva_catalog.json` as the build-specific authority, not this table as
portable RVAs or callable API declarations.

### Route and ownership map

| Boundary | Static observation | What remains unproved |
| --- | --- | --- |
| Owner request and sound lookup | Owner request at VA `0x140c92160` reads global+`0x1b0` and forwards to `0x140c92330`. Tick `0x140824c64` has a path through resolver `0x140c91584` to PlaySetup `0x141249a44`. Resolution uses the per-request context at owner+`0x454` or the managed streaming table, detailed below. The context field is not itself an owning sound map. Catalog IDs: `piano_audio_request`, `piano_audio_state_tick`, `piano_audio_global`, `sqexsead_play_setup`. | The existing wrapper supplies no custom context or dedicated resource binding, and prepared reuse can bypass lookup entirely. |
| PlaySetup and Set | PlaySetup calls controller lookup, Set `0x141249ce0`, slot setup and Play. Underlying Set `0x141249e58` returns early for the same sound; otherwise it reaches BGM initialization `0x141249fb4` and prepare `0x14124848c`. Catalog IDs include `bgm_slot_set`, `bgm_slot_setup`, `bgm_slot_play`, `bgm_controller_lookup`, `bgm_prepare`. BGM initialization is the historical investigation location, not a separately cataloged callable contract. | PlaySetup-only substitution does not cover the historically observed bypass on prepared reuse. No guessed init/prepare call is authorized. |
| Source and resource ownership | Current substitution uses `SqexSeadSound+0x38`. Prepare can resolve the source asynchronously and populate BGM request+`0x48` and backing-resource+`0x70`. Transfer `0x1412496ac` moves these fields and clears them on the source. | Follow-ups below establish borrowing and a conditional native-reference/token-removal lifetime contract. A handle transfer alone does not prove closure; release timing and replacement resource provisioning remain separate obligations. |

Source anchors for interpreting those reads:

- `src/game/runtime_layouts.h`, `SqexSeadSound` and `SqexSeadBgm` layouts;
  `audio_sidecar_runtime.h:24-115` and `.cpp:149-176,204-211` provide immutable
  MABF storage and a shared prepared-prefix snapshot. These are plausible
  mod-side owners but do not establish the native borrow's end.
- `audio_sead.cpp:4620-4760` patches several sound/BGM fields around preparation,
  not only the source pointer. `bgm_prepare_detour` at `9730-9809` has a hook
  declaration and shutdown disable path but no found installation/enable
  reference; it is not an established active production seam.
- `rebuild_armed_controller_route` at `5762-6207`, with real callers near
  `12574-12595` and `13931-13979`, clears through `Set(nullptr)`, patches fields,
  replays captured PlaySetup arguments and verifies native rebound. Source
  presence establishes this intervention, not execution on every gameplay run.
- [Historical lifecycle evidence](AudioLifecycleEvidence-202607.md), Rejected Or
  Retired Approaches and chronology, records reuse omitting PlaySetup/Play and
  pause failures of helper/slot/owner-state replay. Those observations are not
  newly reproduced results.

### Conditional simplification, not a deletion authorization

If native selection can be shown to resolve replacement storage for all three
cases and own preparation thereafter, forced clear/reacquisition, captured
PlaySetup replay and their patch journals are deletion candidates. That does
not remove the separate obligations represented by copied-token release,
borrower retirement, stale-callback isolation, exact-once forwarding and
failed-install rollback (`audio_borrower_state.inc:1-46,86-136` and the
production-wiring/ownership sections of [Architecture](../docs/Architecture.md)).
No replacement ownership explanation yet justifies deleting those obligations.

### One proposed comparison, not performed

Before any runtime investigation, confirm the exact executable and development
artifact under [In-Game Validation](../docs/InGameValidation.md) and obtain
separate human authorization for the observation/attachment or development
installation actually needed. Do not automatically install, launch or attach.

Compare naturally occurring first play, same-song replay and different-song
selection, including immediate pause/resume. Read-only observations would
correlate the selected key with owner resolution, sound identity/source+`0x38`,
Set/prepare/PlaySetup, BGM sound/request/backing-resource fields, native transfer
and final release. The decisive questions are whether a common resource identity
and interception boundary exist and when replacement storage ceases to be
borrowed. Existing PlaySetup/Play markers cannot answer this: prepared reuse may
omit both, and those markers do not identify final borrow termination.

Stop on identity drift, unresolved borrow, mixed stock audio/custom chart or
pause failure; follow the documented session rollback procedure if compromised.
An inconclusive comparison requires agreement on another bounded experiment,
not unrestricted engine tracing. At the static gate decision, no comparison,
live attachment, installation, build or gameplay test had been performed.
The separately authorized read-only observation below does not complete the
overall change or qualify an audio replacement.

## Read-only CE observation: first start and pause

The user subsequently launched the game, connected Cheat Engine MCP, selected
the process and opened the piano list. Authorization was ordinary process-memory
reading and existing-log inspection only. No debugger attachment, breakpoint,
injected call, memory write, script patch, installation or game launch was done
by the agent. The installed original ASI, not the new offline-refactor build,
continues to own its existing hooks and mutations.

### Identity and menu snapshot S0

- Process `ff7rebirth_.exe`, PID `14820`, module base `0x7ff65bc30000`.
  Live PE header: x64, timestamp `0x6a16ced2`, image `0x099d9000`, checksum
  `0x0769ea6e`. Fresh disk SHA-256 matches the 1.005 catalog hash above.
- Loaded ASI module path is the installed `End/Binaries/Win64/FF7RPianoSongs.asi`;
  its fresh disk hash is `c714c628089122e238c0ba20d140eeec53b06c79ea7c2ffb45840ea1664a992b`,
  matching the accepted baseline artifact. Current log starts at
  `2026-09-24 17:38:13.925`, reporting product `0.2.2`. No source-to-artifact
  equivalence with the new working changes is asserted.
- Global pointer `0x7fefdfa51550` resolves owner `0x7feedfa25800`. At the list,
  owner state was zero, active/requested key `0x4ae`, request index 3.
  Controller `0x7feb9fc0ef10` resolved slot `0x7fec9fbbd2e0` and BGM
  `0x7fec4013d680`; slot state 4, request `0x300000008`, backing field zero.
  Sound `0x7fee816d7e00` had object index 273826, name `0x4ae`, and source
  `0x7ff221446330`. These are session-local observed pointers, not future lookup
  authorities. Separate memory reads are not an atomic native snapshot.

### First custom start and user-reported pause S1

The requested action was Alicia (Expedition 33), unchanged difficulty, then
immediate pause. The user reported "На паузе" and subsequently confirmed:
"Да звучала Alicia. Во время паузы нет звука да." Thus correct audible song and
silence while paused are human-observed for this step. Existing log records
Alicia difficulty 4 with 489 events and custom publication; the log alone is not
acoustic proof. Controlled resume, replay and switching were not performed;
later exit-log observations are recorded separately below.

Derived event sequence from the current `FF7RPianoSongs.log`:

| Local time / log span | Observed event |
| --- | --- |
| 17:49:19.802, lines 598–600 | Custom chart/preparation, song key `11623859545934477302`; pre-write snapshot still contains the pre-existing request. |
| 17:49:19.813, lines 611–623 | Original PlaySetup receives sound `0x7fee816d6000`, temporary sidecar `0x1e5f9b90000`, five patches; native Set forwarded once with replay depth zero. Captured owner key is `0x4e8692`. Scoped restore reports success. |
| 17:49:19.814, lines 624–631 | Source+`0x38` restored to `0x1e8cd810420`; custom publication succeeds. Original bank token `2323581960193` and custom token `2349382434817` are distinct and both reported present. |
| 17:49:22.801–22.823, lines 640–659 | Owner requests key `0x4ae`. Native transfer/Stop moves old custom request `0x400010008` to destination BGM `0x7fec403e7a80`; current BGM is rebound to canonical sound `0x7fee816d7e00`, new request `0x500000008`. Existing mod records two borrowers, pending retirement and detached route authority. |
| 17:49:23.838, lines 663–664 | Retirement vector becomes empty (`expected_handle_retired=0`, count 0), immediately followed by error `retirement_cleanup_failure first=native_route_not_proven origin=1 frozen_generation=2 monitor_generation=3 live_generation=4 backing_generation=3 backing_provenance=retired_vector_presence lifecycle_epoch=0 detached_ordinal=0`. |

CE reads after the user's pause confirmation agree on the current controller,
slot and BGM chain: canonical sound/key `0x4ae`, request `0x500000008`, state 4,
backing field zero. The custom sound still has index 273855 and name `0x4e8692`,
but its source is already the restored native `0x1e8cd810420`, not the sidecar.
The former transfer destination no longer contains a plausible BGM sound/request
tuple when read after retirement; its historical address was not followed again.
This is consistent with loss of that object's lifetime, not proof of the final
bank borrow ending or permission to use the stale pointer.

### Stop disposition

The planned sequence was stopped at this first pause because cleanup cannot
prove the route and the retired pointer is no longer a valid observation anchor.
The user was asked to remain paused, not to proceed to replay or song switch.
No crash, mixed audio or audible pause failure has been established; the user's
feedback confirms first-play audio and pause silence. Conversely, the error must
not be counted as successful cleanup. No guard relaxation or automatic escalation
to a debugger is authorized. No replacement was installed, so there is no new
candidate installation to roll back. At this stop decision, same-song replay,
different-song selection, resume and final borrow closure were unobserved.

### Bounded static explanation of the cleanup failure

The inspected source explains a retained, uncommitted cleanup, not an automatic
retry and not proof that the runtime is permanently disabled:

- `audio_cleanup_policy.cpp:721–735` reports the first failed predicate in an
  ordered projection. `native_route_not_proven` means earlier predicates passed,
  but neither an empty route nor an authenticated native successor was proved.
  It does not by itself prove a dangerous route or failed native pause.
- `audio_retirement_cleanup.inc:1626–1666` and
  `audio_cleanup_policy.cpp:833–890` require either a valid empty current route
  (null sound, zero request, state 0) or a matching deferred handoff in
  `RetainedFailure`/`NativePlayForwarded`, with exact generation, lease,
  controller, slot/BGM, retired request and live requested-sound identity. A
  nonempty successor also needs a different sound, newer valid request and state
  2 or 4. The later CE snapshot satisfies only some visible native facts; it is
  not a contemporaneous observation of that internal deferred-handoff record.
- The failure returns before lease transition, route reset and release
  scheduling (`audio_retirement_cleanup.inc:1780–1868,1942–1957`). This branch
  does not itself set `g_audio_route_disabled`.
- The retirement monitor has already changed from `Waiting` to `Quiescent`.
  Its ordinary poller only handles `Waiting`, invoking this cleanup on that
  transition (`audio_cleanup_policy.cpp:737–759`;
  `audio_retirement_cleanup.inc:1988–2081`). More ticks do not retry the failed
  proof. A separate authenticated aggregate-close/terminal-Stop path can attempt
  cleanup (`audio_retirement_cleanup.inc:1006–1096,1351–1473`), but arbitrary
  callbacks or elapsed time do not guarantee it. Retained retirement authority
  and active lease can obstruct a fresh-clear activation claim
  (`audio_selection_activation.inc:2937–2960`).

The old request's disappearance from the retired vector is narrower evidence
than final custom-bank borrow closure or permission to release the copied token.
The historical destination is not a valid continuing pointer anchor. Do not
delete a release guard while retaining the mutation and token ownership that
depend on it. Equally, do not treat an unrecognized successor as proof the game
made an invalid transition: the native route and the mod's recorded handoff are
distinct facts.

A focused search of the existing log after this analysis found the line-664
failure but no additional `cleanup=retained`, `deferred_route` or aggregate
closed/retired diagnostic identifying the failed successor conjunct. Absence of
these searched records is not proof no deferred state existed. At that inspection,
the log SHA-256 was
`a7992d8eeb63b57a501d711b62c4845cc1dfea07ee066db07d92e2347186da65`;
this identifies the observation before any later user exit can append records.

These semantics are confirmed for the inspected source, not proof the installed
baseline ASI was built from those exact source lines. User-observed pause success
and the logged failure are artifact observations; the retained/no-poll-retry
explanation is source-based. The bounded comparison remains stopped. Further
runtime diagnostics require a separately agreed scope and any appropriate
attachment/installation authorization; the user need not keep this paused session
open for additional static work. No debugger, new hook or diagnostic ASI was used.

### Subsequent user-reported exit: cleanup completes on another path

After the user reported exiting the game, the existing log was read again, with
no further live memory access. It now contains 743 lines; SHA-256 at inspection:
`8913cfc6e4800621b4059cb7ce2da66fdc2b4a20f75b1224cf0a9173583ff462`.
This supersedes the unresolved-outcome snapshot above, not the fact that the
earlier retirement-cleanup attempt failed.

- At 17:57:27.126–.127 (lines 665–683), native callbacks and aggregate-resume
  records restore custom sound and confirm custom request `0x600010008`.
  This is logged native continuation while the user was ending the session,
  not a separately controlled or audibly confirmed resume test. No second
  song selection is established.
- At 17:57:28.515–17:57:29.163 (lines 688–731), another transfer and canonical
  request `0x700000008` are followed by `list_return`, canonical-substrate
  relinquishment and release of the route lease. An intermediate
  `superseded_custom_request_present` refusal is not the final outcome.
- At 17:57:29.539 (lines 735–737), the separate `aggregate_closed` path records
  `exit_release_claimed`, `release_inflight` and `release_committed`.
- At 17:57:29.550–.561 (lines 740–742), bank lifecycle records
  `phase=complete reason=custom_absent`, followed by `exit_cleanup_complete`
  and `aggregate_exit_complete`. The resulting route is enabled and Idle,
  with lease 0, owned 0 and cleanup 0.

Thus the retained cleanup did resolve through the separate qualified path in
this observed sequence. Do not describe this session as permanently stuck or
the first error as preventing all later playback/cleanup. The source conclusion
that ordinary retirement polling does not retry remains distinct from the
observed success of another path.

The final line (743) is an error-level `activation_terminal` record on
`controller_destructor`, rejected for `reservation_state`, with no confirmed
reservation. Its independent meaning was not investigated here. No blanket
error-free teardown, leak-free shutdown or source-to-artifact equivalence is
claimed. Logged custom-token absence narrows the lifetime question for this
existing implementation and sequence; it does not yet establish a simpler
substitution boundary or same-song/different-song reuse coverage. The audio
replacement gate remains open.

## Offline follow-up: the cleanup authorities are not interchangeable

The later successful path does not prove that the failed direct-successor
predicate eventually became true. Three distinct obligations are involved:

| Authority | What it actually discharges |
| --- | --- |
| Deferred Set/Play handoff | The mod's native-call forwarding obligation; it is not the bank-release owner (`audio_sead.cpp:10455–10550,11044–11148`). |
| List-return route restoration | Exact restored mutation/lineage permits relinquishing route bookkeeping without clearing the canonical native slot; detached bank ownership remains (`bgm_playback_aggregate_policy.h:2364–2434`; `audio_sead.cpp:14661–14754,14840–14940`). |
| Aggregate borrower closure | Matching request/identity/observer closure authorizes release of the retained detached custom token, not the canonical token (`audio_borrower_lineage.inc:1686–1816,1843–2205`). |

The aggregate claim uses the same retained canonical/custom token pair. Its
native release path passes a copy of `action.custom`. However, a successful
`release_committed` outcome can also mean `AlreadyAbsent`; that marker alone
does not establish which native release call ran. The subsequent `custom_absent`
observation is the recorded completion evidence.

The observed early cleanup was not necessary for this run's eventual closure.
It is not proven redundant for nonaggregate, empty-route, natural-completion or
earlier-before-list-return cases. Releasing on retired-request disappearance
alone would not account for the later custom continuation recorded in this log.
No deletion candidate or diagnostic state was added on that assumption.

Existing production-policy tests already exercise the relevant distinction:
forwarded native Play can satisfy its obligation despite `RetainedFailure`;
direct-successor and authenticated terminal cleanup have different authority;
route restoration can succeed without retaining the custom live request; and
aggregate release remains blocked while a preserved Set-created request exists.
These are covered in `runtime_lifecycle_cleanup_retirement_tests.cpp:549–600,
895–987,1104–1140` and `bgm_playback_aggregate_policy_selftest.cpp:1013–1117,
2033–2041`. Unknown contemporaneous handoff facts were not invented to build a
synthetic reproduction of this session's precise failure.

The following focused checks were run successfully on the current working
source, with no new test or production-code edits:

```powershell
./build.ps1 -Configuration Release -Target @("runtime_lifecycle_selftest", "bgm_playback_aggregate_policy_selftest") -Parallel 8
ctest --test-dir build -C Release -R '^(runtime_lifecycle_selftest|bgm_playback_aggregate_policy_selftest)$' --output-on-failure
```

Both targets built and both tests passed. Only test targets were selected; no
ASI was installed or game session started. These checks validate existing policy
counterexamples, not the native copy/borrow contract or a replacement seam.

## Static follow-up: on-memory preparation borrows the payload

This follow-up resolves the concrete source branch, not an earlier callable
substitution seam. All VAs below refer only to the imported image described
above; they are not newly cataloged/signature-validated native-call contracts.
No exited-session pointer was dereferenced or live process observed.

Reflection registrations identify `USQEXSEADSound` (size `0x550`) and derived
`USQEXSEADMusic` (size `0x5f0`). Constructors `0x1407bafac` and `0x1418cb0b8`
install vtables `0x1455e57f0` and `0x145744c38`. In both, virtual `+0x370` resolves
to `0x140923620`, which returns zero. Virtual `+0x378` resolves respectively to
`0x142a745c4` and `0x142a7404c`, building `.sab` and `.mab` names. Those name/streaming
paths do not establish payload copying. The actual class/vtable of the observed
session's sound was not independently captured; these are static class mappings.

The positive on-memory evidence comes from a separate path:

| Stage | Static observation |
| --- | --- |
| Select on-memory source | `0x141247748` uses sound+`0x41c` and, if sound+`0x548` has no bank token, obtains and registers source data. |
| Resolve source header | `0x1407bea94` reads sound+`0x38`. An odd first header word encodes a payload pointer relative to that header; the mod's header word `0x21` resolves to header+`0x10`, its MABF bytes (`audio_sidecar_runtime.h:59–100`). |
| Register bank | `0x1407beab4` → `0x1407beafc` → `0x1407beb68` validates MABF magic through `0x1418b9538` and constructs `SQEX::Sd::Driver::OnMemoryBank`. Its virtual `+0x40` implementation `0x1407bee50` stores the supplied payload pointer at bank+`0x50`. Registration yields a distinct token retained at sound+`0x548`. |
| Construct file view | Virtual `+0x30` implementation `0x140a0256c` places that same pointer into a `MabFile` view, rather than copying the payload. |
| Create music request | Consumer `0x140a023ac` reads offsets inside that view and passes an interior payload pointer onward; `0x1409fff8c` stores it at request+`0x98`. A request therefore retains a pointer derived from the external MABF allocation. |
| Release bank | Cataloged asynchronous release `0x1407bad3c` marks registry-entry+`0x1c`; it does not wait for removal. Examined OnMemoryBank destructor `0x1407beedc` does not establish ownership/freeing of the external payload. Neither observation proves the last request reader has stopped. |

For the inspected request acquisitions, token lookup `0x1407a0fd8` identifies a
live registry entry; callers `0x141249350` and `0x141247c7c` check bank kind and
use entry+`0x10`. Selection key, sound UObject, source-header address, registry
token and BGM request are distinct identities; none may substitute for another
without a specific relationship. The `.mab`/`.mab.bytes` FileStreamingBank path
through `0x14553240c` is not evidence for on-memory buffer ownership.

The current `MabfVirtualAllocation` owns a stable address while its prepared-prefix
node remains owned; reset/destruction calls `VirtualFree`. Nodes share immutable
predecessors (`audio_sidecar_runtime.cpp:22–31,149–176`). Catalog replacement and
shutdown retain active ownership/release gates before resetting that storage
(`audio_sead.cpp:13439–13506,16550–16582`). This supplies a concrete conditional
mod-side owner, not evidence of a native payload copy or permission to replace
the existing sound object.

Positive borrowing evidence rules out freeing replacement bytes merely after
PlaySetup returns; it does not justify retaining every existing orchestration
mechanism. The follow-up below establishes a conditional native lifetime boundary
instead of attempting to reproduce all native reader accounting in the mod.

## Static follow-up: native bank references are the lifetime authority

The traced successful music-request path retains a native bank reference:

1. Request initialization `0x1409fff8c` resolves the bank and calls
   `0x140a05328`, atomically incrementing bank+`0x18`. Failed initialization
   returns before acquiring that reference and is not successful registration
   or an ordinary token-absence completion.
2. Terminal completion `0x140a0ee24` calls `0x141241bec`, which stops associated
   activity and decrements the bank reference through `0x140a0425c`, before
   request destruction.
3. Asynchronous release `0x1407bad3c` marks the registry entry. Worker
   `0x140a0ef90` requests stops for the bank and waits for the release flag plus
   a nonpositive reference count. It then unlinks the entry through
   `0x141660284`, invokes bank virtual `+0x48`, and destroys the bank.

This is a native ownership mechanism, not simply disappearance of an old BGM
request. Material processing through `0x140a0dfc0` and `0x140a03878` need not be
reimplemented as a second mod-side reader graph absent evidence of a bypass of
that mechanism. These facts cover the identified successful request family,
not an arbitrary unrecognized native object or failed initialization.

### Post-unlink tail and external allocation

Bank registration `0x1407beb68` passes ownership flag zero to `0x1407bee50`, which
stores the payload at bank+`0x50` and the flag at bank+`0x58`. Destructor
`0x1407beedc` frees payload-minus-`0x10` through a native allocator only when that
flag is set. The flag-zero destructor does not read or free the external
payload. A positive ownership-true registration was not established; the
setter/destructor contract is not a claim that such a path was observed.

Registry removal precedes the final callback and destructor, so token absence
is not destructor completion. For the statically mapped Sound/Music route,
virtual `+0x48` resolves through `0x1416fdb90` to the shared no-op virtual `+0x08`
implementation `0x140743b50`. This examined callback plus the flag-zero
destructor exposes no post-unlink payload read. The conclusion is conditional
on that callback implementation; the exited session's actual sound vtable was
not independently captured. It does not apply automatically to arbitrary
subclasses or other bank kinds.

For this contract, retain the immutable externally owned allocation through an
otherwise-authorized native release. Absence of the **same successfully
registered token** is then a defensible native tracked-borrow closure before
reclaiming its bytes. Do not transfer `VirtualAlloc` storage to the native
ownership-true destructor: its allocator is different. Failed registration or
an unsupported/unreadable token observation is not successful closure.

**Timing remains separate from completion.** Requesting release can stop the
bank's playback. This lifetime boundary does not authorize release on pause,
does not identify the end of the player's song session, and does not solve
first-play/reuse/song-switch resource selection. No production code or native
call declaration was changed. The entry-route investigation below concerns the
existing high-level owner request versus controller reconstruction, not further
proof of every native decoder reader.

## Entry-route decision: the missing resource binding precedes playback

**The existing owner-request wrapper cannot replace controller reconstruction.**
In tick `0x140824c64`, equal derived current/selected keys plus
`0x14124a160` confirming slot state 4 and matching active sound FName select the
prepared path through `0x14124a124` (slot setup). It bypasses sound resolution
and PlaySetup. Wrapper `0x140c92160` → `0x140c92330` queues a request; it does not
override that branch. Even if PlaySetup occurs, Set `0x141249e58` can return
early for the same sound rather than prepare a new source.

Current custom descriptors have distinct mod identities but `base_slot=0`;
list rows clone that base, and the selection getter temporarily aliases to it
(`song_descriptor_builder.cpp:88–92`; `list_patch.cpp:311–330`;
`selection.cpp:492–494,556–577`). A different mod song therefore does not imply
a different native key, sound or bank. The source's request-profile checks are
stricter than native arguments, but removing them does not remove this native
prepared-reuse counterexample. Existing rebuild callers cannot be deleted on
the assumption that the wrapper forces preparation.

A dedicated stable Music object per descriptor would make correct same-song
reuse desirable, rather than something the mod must defeat. A genuinely different
key resolving to a different sound would trigger native preparation on switching;
stock return would resolve the original stock object. Merely changing a sound's
genuine FName while requesting the base alias only defeats the name comparison:
the ensuing lookup still receives the alias key, not an automatic binding to
the dedicated object. A separately proven context-aware resolver could be an
alternative, but global current mod selection is not such a contract.

### Native lookup provisioning, not an arbitrary object registry

The final bounded static pass refined the earlier lookup interpretation:

| Operation | Observed contract in the imported image |
| --- | --- |
| Context propagation | `0x140c92330` stores its ninth argument at request-entry+`0x168`; tick copies it to owner+`0x454`. Wrapper `0x140c92160` supplies zero. The field is per-request lookup context, not the owning sound table. |
| Managed lookup | Resolver `0x140c91584` falls back to `DAT_14869bb90`, with `0x18`-byte entries containing packed key at `+0` and stream-record pointer at `+8`; sound is at record+`0x48`. Initializer `0x141b4c7f4` starts the table empty; `0x1415d324c` inserts key/record and `0x1415d3564` looks up a key. |
| Acquire asset | `0x1411c682c(key, acquire/release, priority)` is used by the tick. A missing key queues `AddStreamAssetRequest`; worker `0x1411c6be0` derives a packaged path, including `/Game/Sound/BGM/...` for recognized names, and loads it asynchronously. |
| Publish loaded asset | Completion `0x1411c5ee4` stores the loaded UObject at record+`0x48` and inserts the key/record pair. Existing requests increment record+`0x28`. |
| Relinquish asset | `0x1411c5ff4` decrements the record count and at its terminal condition removes the entry, relinquishes the streamed object and clears record+`0x48`. This is observed stream-record management, not an independently established arbitrary UObject root/GC contract. |

The fallback is not simply a fixed stock array. Nevertheless its demonstrated
insertion path publishes an asynchronously loaded asset record; it is not a
qualified operation for registering an arbitrary supplied Music UObject. The
current source's proposed selection-map insertion returns false
(`selection.cpp:374–403`). Uncalled `ensure_alias_music` proposes a single name
and templated UObject, without per-descriptor binding, initialized song-specific
source, retention, duplicate-name policy or failure rollback
(`audio_sead.cpp:3585–3655`). `CreatePackage` and `StaticConstructObject` remain
research/signature-only catalog entries, with explicit source failure reasons
`construct_detour_not_complete` and `object_lifetime_and_rollback_not_proven`
(`audio_sead.cpp:13356–13370`; `rva_catalog.json`, corresponding address records).
Their presence is not a ready asset-provisioning API.

### Bounded stop and minimum next deliverable

The current sidecar-only integration does not provide a dedicated native
selection-key/asset binding before the reuse decision. This is the concrete
blocker, not a request to keep inspecting arbitrary lifecycle predicates.
No production route is selected; no further broad engine search or repeated
first-play/pause session is justified by these results alone.

Before a resource-based replacement can be implemented, a separately bounded
asset/lookup prototype must establish one immutable descriptor's genuine native
key, intended Music/source object, lookup relationship, retention through native
use and restoration to stock. The native source/header/token/refcount facts above
can constrain that prototype offline. They do not supply the asset or authorize
invoking the research-only construction functions. Whether to expand into that
new integration must be decided explicitly; content assets, unsafe construction
calls or an enduring second backend are not silently added to this change.

All follow-up reads remained static. Full imported-file SHA and the installed
baseline ASI's exact source equivalence remain unproved. No source behavior,
catalog address, hook, native object, package or user song format was changed.

## Approved prototype preparation: task 1.5 remains blocked

The user subsequently approved the one-descriptor, one-build prototype in the
OpenSpec design's decision 1a and invoked its implementation. This authorized
the bounded offline contract investigation below, not installation or native
invocation. It did not turn the preceding construction scaffolding into a
qualified API. The selected static target remains the cataloged build used by
the preceding investigation; its imported-image applicability limits remain.

### Context is a registry selector, not a supplied Music object

The newly inspected contextual resolver branch at `0x140c91584` calls
`0x140d92940(context, derived_selected_key, 0)` when context is nonzero. Its
immediate callees consult a shared two-level keyed registry. The request context
is neither the dedicated Music pointer nor a mod-owned local map. Tick consumes
that context independently of this final resolver as well.

The internal request implementation `0x140c92330` accepts the ninth argument
which reaches owner+`0x454`; the cataloged wrapper `0x140c92160` supplies zero.
No qualified call contract for supplying that additional argument, registering
an arbitrary object in the contextual registry, or retaining it there was
established. Intercepting only the final lookup cannot be assumed to satisfy
the earlier reuse decision and other contextual consumers. These observations
therefore do not admit an unscoped current-selection override as a prototype.

### Construction parameter recovery is narrower than provisioning

Static construction body `0x140b1eb58` confirms the existing local `0x40`-byte
parameter block's class, outer, FName, flags and template fields before allocation
and class-constructor dispatch (`audio_sead.cpp:204–224`). The mapped Sound/Music
constructors `0x1407bafac` and `0x1418cb0b8` establish defaults/vtables. This is
useful ABI evidence, but neither operation establishes the descriptor's source
at sound+`0x38`, a selected-key binding or retention of the newly supplied object.

Package operation `0x1445330d4` can return an existing package. The object
allocation path at `0x140b1ee70` detects qualified-name conflicts and can
recycle/reinitialize an existing object. Consequently the unused helper's single
stock-like name/template is not a collision-safe, reversible provisioning
contract (`audio_sead.cpp:3585–3655`). No owning/root reference and
partial-construction rollback contract for the proposed new object was
established in this slice. This does not claim that the engine lacks such a
mechanism or that every existing product object is unretained.

The canonical `fname_ctor` entry has migration/build-identity-only provenance
for this target. `create_package` and `static_construct_object` remain
research/signature-only (`rva_catalog.json`, corresponding records;
`audio_sead.cpp:13356–13370`). Newly inspected bodies were not promoted to
callable catalog entries. A callable name constructor alone does not publish a
native song key or an asset lookup binding.

### Candidate admission and actual disposition

| Task 1.5 obligation | Result of this pass |
| --- | --- |
| Existing validated descriptor/MABF storage | Available for reuse; no new format or pipeline work is needed. This does not provide a native object. |
| Genuine selected key and intended Music/source lookup | Not established. Current rows still alias the stock base and proposed map insertion still returns false. |
| Provisioning arguments and duplicate-name behavior | Parameter layout and a collision/reinitialization hazard identified; no complete supported provisioning operation selected. |
| New object's owning reference and partial-failure restoration | Not established. Constructor signatures and a shared-registry read cannot supply this ownership. |
| External MABF borrow completion | The earlier conditional native bank contract remains applicable only after successful registration on its supported path. It cannot validate missing object provisioning. |
| Exclusive diagnostic route | Required, but not implemented: either existing Armed-route rebuild caller would invalidate evidence if it cleared the controller/replayed PlaySetup to rescue the candidate. |

The minimum unresolved deliverable is still **one collision-safe native key →
initialized Music/source binding with a supported owning reference and failure
disposition**, preserving the genuine stock resource. The contextual branch and
constructor slice did not produce it. This is a concrete prerequisite to task
1.6, not an additional requirement to model all game behavior or prove every
decoder reader.

Task 1.5 remains incomplete; no opt-in shell, speculative native call, fabricated
stream record or detached model test was added merely to advance the checklist.
Tasks 1.6–1.8 were not started. Existing focused test results were not rerun:
there is no candidate implementation whose argument/ownership boundaries they
could yet validate. A future authorized session would still need to show the
genuine key/object/source, first play, pause/resume, replay and stock restoration
without either old rebuild caller manufacturing success. Another unchanged
pause observation cannot establish this missing provisioning contract.

At that point the game-managed packaged-asset route remained an alternative
requiring a scope decision, not proof that content was necessary or already
worked. The user subsequently approved the temporary content assessment below.

## Approved content fixture: existing editor support is insufficient (2026-09-25)

The user authorized one temporary Music asset/container via the existing editor
workflow, without installing it or changing the final song-folder requirement.
Source inspection and read-only editor reflection found concrete authoring and
loading-path gaps. This is not another native lifecycle-predicate investigation.

In the parent workspace, `FF7R2UProj/Source/ENDEditor/Private/`
`SQEXSEADMusic_Factory.cpp:12–19` advertises XML import and the genuine Music
class. However, its entire `FactoryCreateBinary` at lines 42–50 ignores the input
buffer, constructs a default object, computes unused paths and returns. The
Sound factory behaves identically. Neither imports even reflected XML metadata
or native payload. SQEXSEAD Music/Sound class implementations supply settings
and defaults, with no SQEXSEAD source serializer; inherited SoundWave audio
serialization is not MABF preservation evidence.

Headless editor probes confirmed the loaded Sound/Music factory classes and
their corresponding supported SQEXSEAD classes, including Music despite its
absence from Python module enumeration. The probes created/imported/saved no
assets. Their scripts completed, but the commandlet processes exited 1 because
the pre-existing local `Content/Sound/BGM/bgm_piano_09.uasset` has a malformed
package tag. That unrelated asset was preserved, not used as a template. This
failure and the missing importer are separate; no successful cook is claimed.

The native loader's `/Game/Sound/BGM/...` identity also differs from a normal
content plugin's mount. The inspected Alpakit workflow excludes `/Game`, and its
staged filesystem remap preserves plugin package identity rather than remapping
it into `/Game`. Neither factory addresses that mismatch. Local cooked research
artifacts are not demonstrated uncooked/editor-round-trip inputs.

At the end of that first pass, `FF7R2UProj/Mods/FF7RPianoLookupFixture/` contained only
an inspection script and README with source anchors, reproducible command,
output location and limits. It is not a loadable plugin; no Music asset,
container, native constructor call, new hook or installation was produced.
Task 1.5 remains blocked, and 1.6–1.8 remain unstarted. A demonstrated payload
authoring/preservation route and loading identity are needed before a candidate;
developing missing toolchain support is a material scope choice, not an excuse
to package a class shell or silently overwrite stock content.

## Preserved-package controls narrow the tooling blocker (2026-09-25)

The user next authorized a bounded assessment of existing genuine Music
serialization, rather than building an importer or polishing the old runtime
orchestration before deciding its replacement. That assessment yielded actual
offline controls, not merely another proposal. The parent fixture README owns
the exact commands, artifact hashes and cleanup paths; binary game assets and
generated containers remain untracked in task-owned `Saved/PianoLookupFixture`.

### A genuine source and a working converter path

The retained `PianoAudioBuilds/mary-runtime-sidecar/baseline/bgm_piano_01.uasset`
is an extracted Zen ExportBundleData blob, not an editor `.uasset` template.
Its report identifies the stock container, path and chunk; it is byte-identical
to the retained PianoAudioProbe and AudioMogControl originals. Source/format
inspection found one Music export, valid names/public identity, an embedded
MABF and three HCA markers. Earlier AudioMog same-identity rebuilding exists,
but its retained output has an unresolved serial-size discrepancy and does not
establish insertion of the current descriptor's exact bytes.

Pinned retoc initially failed conversion from the single pakchunk because the
global `LoaderInitialLoadMeta` chunk was absent. Its aggregate failed count did
not make the process return nonzero; a PTY diagnostic exposed the actual error.
Using the installed Paks directory as input, still filtering only this one
Music and excluding shaders, produced one legacy `.uasset`/`.uexp` pair with
zero failed assets. The complete 11,228,128-byte opaque export matched the
retained control, and conversion back to Zen retained it again. The regenerated
header/chunk differed, as expected from format conversion; whole-package
identity is not claimed. No UObject deserialization/resaving by the reconstructed
editor or native payload importer was involved.

### Distinct metadata without rewriting audio bytes

The previous raw replace-all renamer also changes embedded BGM names inside
the MABF. The new task-owned diagnostic instead modifies only three exact,
length-prefixed package/object identity strings in the hash-bound legacy
header, leaving the entire export unchanged. Existing retoc rebuilds the Zen
name hashes, public export identity and container metadata. Read-only checks
confirm `/Game/Sound/BGM/bgm_piano_09.bgm_piano_09`, all name hashes, the public
hash, unchanged class/template/outer/super indices, and complete export equality.
The internal stock bank/material labels are deliberately unchanged.

Both converter-created containers passed `retoc verify`; this checks container
integrity, not native loading semantics. Ten focused tests checked preservation,
the narrow identity transformation and rejection of changed payload, bad trailer,
wrong header, altered name hashes or public identity. The verifier is expressly
for this reviewed single-export shape, not a general asset serializer. No
manually guessed package-store entry or stock override was used. These artifacts
contain stock control audio, **not** the selected descriptor's custom MABF.

### Loading evidence and remaining boundary

Further existing-UAT source inspection found `skipcook` staging of prepared
cooked paths and opaque `.uexp` byte copying during IoStore assembly. It also
found `RemapPluginContentToGame`, but only for target package names already in
the loaded release registry; this is not general new `/Game` identity provisioning.
The parent `Docs/06-alpakit-packaging.md` now distinguishes that conditional
logical redirect from Alpakit's filesystem remap. UAT was not invoked. Its
offline launch option must explicitly be `None`; omitting a copy flag alone is
not enough to prevent a game launch in this checkout.

Legacy `CurrentPianoSongStatus.md:304–305` and retained diagnostics provide a
genuine positive `StaticLoadObject`/preload observation for an earlier minimal
09 carrier. Later disposition at lines 136–138 reports failure to reproduce
even that exact carrier, and preload did not produce playback. Its runtime
association is documentation/report-supported, not hash-bound. That artifact
used a manually built store entry and payload-renamed AudioMog audio; it is
not the new converter-created control and does not qualify the new deployment.

The blanket assumption that preserving Music needs a full importer is therefore
removed. Still unproved are replacement of the actual serialized source record
with the descriptor's validated MABF, native selection/stream lookup for this
new identity, and retention/stock restoration in the candidate. The next narrow
static check is the real serialized source/length contract, not blind marker
replacement or another game-lifecycle model. Tasks 1.5 and the production audio
gate remain open. No package installation, new game session, native candidate
or production audio behavior change occurred.

## Serialized source boundary and an equal-size control (2026-09-25)

The source-reader investigation did not recover a general Music serializer.
Native reflected inheritance is Music → SQEXSEADSound → SoundWave → SoundBase
→ UObject; MemoryMappedAsset is a separate branch. The reconstructed local
`MemoryMappedAsset.cpp:94–137` both resembles the measured wrapper and explicitly
excludes sounds. It cannot establish Music's inherited serializer. The examined
base constructor initializes the runtime source pointer to zero; post-load and
on-memory acquisition consume it through the already cited header resolver.
No immediate native load/fixup operation assigning that pointer was recovered.
The imported-image applicability limits above still apply.

Three actual exports nevertheless establish a useful bounded layout observation:
original 01, original 08 and AudioMog-rebuilt 08 all have an enclosing length at
export+0x70, relative pointer 0x21 at +0x80, and equal counts at +0x88/+0x8c.
The pointer lands at +0x90; the enclosing length is count+16; MABF's declared
size equals count. The declared array stops **48 bytes before the export ends**.
Three zero words there resemble empty patch lists, but the native suffix reader
and remaining bytes are unproved. Earlier marker-to-EOF hashes measured a larger
span and must not be used as an independently parsed source-array length.

Blindly resizing the export or replacing marker-to-EOF would therefore be
unsupported. Instead, the approved one-song diagnostic now has a **same-size
original-audio control**. The parent fixture synthesizes an arpeggio at the
reviewed source's exact PCM geometry, and the existing target-selected offline
pipeline accepts its authored chart and generates a validated audio artifact.
No production encoder/parser/cache behavior changed. The declared bank length
matches the original exactly; embedding replaces only that span and leaves all
Music properties, source wrapper/counts, export length and opaque suffix intact.
This is an experiment avoiding unknown size/fixup edits, not a recovered general
serializer, a new user-song restriction, or proof the game accepts those bytes.

The pipeline's existing MABF artifact convention includes 48 bytes after the
declared size. The fixture does not copy those bytes over the object's suffix;
it preserves the original suffix and checks equality to the generated **bank
span** separately from whole-artifact identity. Section/name/index metadata stays
unchanged as well. Whether native Music consumers accept this new bank with those
fields is still a runtime/source-binding condition, not established by hashing.

Existing retoc converted the prepared equal-length legacy pair into a named
container. Container verification passed. Extraction and focused checks proved
its entire header equals the earlier independently verified named-stock header
and its entire export equals the intended bank-only modification. Thirteen
preservation/identity/source-span tests and four fixed-size replacement tests
passed, including rejection of resizing, unchanged stock audio, and accidental
artifact-tail replacement of object metadata. The parent fixture README owns
exact commands, generated identities, cleanup paths and reproducibility limits.

Only the OFF song-cache tool was reconfigured/built for the fixture target; no ASI
was built or installed in this step. No game/editor session, native invocation,
stock overwrite or variable-size replacement occurred. Task 1.5 remains open for
the actual native key/lookup, source acceptance, retention and stock restoration;
task 1.6 has not started. The next bounded question is the existing native-key
injection point for this real control, not further generic serializer recovery.
