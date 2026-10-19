# Autoplay: independently recovered 1.004 static parity

Date: 2026-10-19. Mode: **Debug**, one bounded older-build parity investigation.

## Outcome and authority limit

The four actuator/tick counterparts, their ABIs, unique imported entry prefixes,
and the consumed controller/chart/event/state-table layouts are recovered below.
The three-hook restore-before-callback mechanism has static parity. The existing
result consumer also supports a synchronous, seeded 16-byte record shadow.
These are implementation-ready **static per-build candidates**, not approval to
enable older gameplay without the identity/runtime gates below.

**No authentic older disk executable was available to this investigation.** The
import is PE/signature-correlated, not independently full-file authenticated.
No 1.004 pause trace, automatic-note mutation, record persistence, trampoline,
or gameplay qualification was performed. The 1.005 live observation does not
transfer. Only this new evidence file was written; no production/catalog edits,
build, install, launch, attach, process control, commits, or workers were used.

## Identity and evidence provenance

- Canonical build: `ff7rebirth-steam-win64-68fd6fde` in
  `src/game/rva_catalog.json`. Imported headers freshly read: MZ; e_lfanew
  `0x220`; PE AMD64; timestamp `0x68fd6fde`; SizeOfImage `0x09bad000`;
  checksum `0x07877c99`; preferred image base `0x140000000`.
- Canonical older file identity is 126263624 bytes, SHA-256
  `76bd4d539878a8759433f1cd7c7b47d5e3abda0ef41572a664d2d606a9f09dc8`.
  This is catalog identity, **not a newly measured imported-file hash**.
- Ghidra discovery found one TCP instance, project `ff7r`, path
  `D:/GhidraProjects/FF7R/ff7r`, PID 26112, endpoint
  `http://127.0.0.1:8089`. Programs listed: `/ff7rebirth_.exe-1.004`,
  `/ff7rebirth_.exe-1.005`, and two `/New Traces/pydbg/` programs (13052/6336).
  Discovery's open flags were stale; explicit older-program queries succeeded.
- **Every program-specific query explicitly selected
  `/ff7rebirth_.exe-1.004`.** Its internal name is `ff7rebirth_.exe`, PE,
  `x86:LE:64:default`, Windows compiler, base `140000000`; no overlay spaces.
  The imported pathname names the shared Steam executable location; it is not
  older-disk identity proof. No default program or trace was queried for parity.
- The current file at that installed pathname was freshly hashed: 124317952
  bytes, SHA-256
  `752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc`.
  It is the catalog default build, **not** an authentic older disk source.
- Fresh imported-byte reads matched the canonical older signatures for
  `piano_chart_update`, `piano_detail_update`, `progress_lookup`, and
  `score_calculate`, including the full 37-byte detail-entry signature.
- Starting records: `AutoplayRuntimeEvidence.md`,
  `AutoplayRecordSuppressionEvidence.md`, `ChordVoicing1004Evidence.md`, and
  older catalog entries. Their migration/provenance records were locators, not
  independent proof of the newly recovered behavior. New evidence is selected
  import disassembly/decompilation, bytes, data-table reads and code/data xrefs.
- Checkout HEAD during investigation: `5e769ff57f8a2db894da15cccb62c1dda76e1783`.
  The runtime worker's evolving source/catalog was read only to identify consumed
  contracts; this is not a source-snapshot or implementation review claim.

## Catalog candidates and exact ABI

All addresses in the remaining sections are **1.004 RVAs**, unless a `FUN_` VA
is explicitly given. Add older build keys to the existing IDs, not new owners.
Each entry prefix below was freshly read, checked against complete disassembly,
ends at an instruction boundary, and had **exactly one imported-program match**.
No disk-uniqueness claim is made. Masks can be all `x` at these exact lengths;
do not substitute 1.005 rel32 bytes or mask away the identity-bearing CALL.

| Existing/new catalog ID | 1.004 RVA | Native Windows x64 ABI | Exact complete-instruction prefix |
|---|---|---|---|
| `piano_autoplay_matcher` | `0x03c4c8ac` | RCX borrowed event, DL sector byte; no semantic return consumed | `40 55 48 8b ec b8 60 00 00 00 e8 75 26 46 fe 48 2b e0 4c 8b c1 45 33 d2 48 8b 09` |
| `piano_autoplay_advancer` | `0x03c60f58` | RCX borrowed event; void contract | `b8 28 00 00 00 e8 ce df 44 fe 48 2b e0 48 8b 01 48 8b d1 33 c9 48 85 c0 0f 84 80 00 00 00` |
| `piano_autoplay_terminalizer` | `0x03c4d37c` | RCX borrowed event; void contract | `48 89 5c 24 08 48 89 74 24 10 57 b8 40 00 00 00 e8 9f 1b 46 fe 48 2b e0 48 83 39 00 48 8b f9` |
| `piano_autoplay_controller_tick` | `0x03c60ed0` | RCX native controller, XMM1 float delta; AL continuation boolean | `40 53 b8 30 00 00 00 e8 54 e0 44 fe 48 2b e0 48 8b d9 48 81 c1 b8 0f 00 00` |
| `piano_autoplay_state_table` | `0x05d5f220` | data reference, 14 entries, stride `0x20`; no hook/signature-as-code | constructor references at `0x03c31534` / `0x03c31541` |

Matcher's two direct code calls are `0x023a7339` and `0x023a7357` in native
hand selector `FUN_1423a7300`. Both set RCX=event and zero-extend the selected
sector into EDX; neither consumes matcher RAX. Advancer's direct code call is
`0x03c610e5` in the catalog-owned chart updater. Terminalizer's direct code calls
are matcher `0x03c4c9f7` and advancer `0x03c60ff1`; other recovered xrefs are
metadata, not extra code callers. Tick's scenario wrapper `FUN_14352b0b0`
adds `0x30` to RCX and tail-jumps at `0x0352b0c5`, preserving XMM1. The scenario
constructor independently constructs the embedded controller at scenario+30.
Do not interpret RDX as tick's float delta or infer ABI from decompiler types.

Collision check against older catalog build keys found no existing entry at
the five candidate RVAs. Their entry spans are separate. Keep the existing
autoplay owner/transaction from the lead's catalog; do not independently hook
`piano_chart_update` (`note_count`), `piano_detail_update` (`title`),
`progress_lookup` / `score_calculate` (`progress`), or lifecycle targets
(`menu_session_authority`). Static catalog absence does not prove live bytes
are unpatched by another mod.

## Consumed layout and marker semantics

These consumed offsets equal the default-build static contract; they were
recovered in this import, not translated. Existing canonical layout definitions
remain the implementation authority; **no build-specific offset change is
required by these facts**.

| Object | Offset/type | Older proof / role |
|---|---|---|
| Controller C | `+f48` chart, `+f50` control block | state-9 entry installs a native shared pair; state-9 caller loads chart |
| C | `+380`, `+538` embedded sides | state-9 caller passes R8/R9 to chart update |
| C | `+fb8` embedded dispatcher | constructor and tick |
| C | `+fc0` int32 count, `+fc8` table pointer | constructor sets 14 and selected older table |
| C | `+fd0` current, `+fd8` pending pointers | dispatcher publication/request and tick |
| Chart | `+34` float elapsed, `+3c` float late duration | matcher/advancer/chart update |
| Chart | `+48` float divisor; `+4c`, `+50`, `+54` int32 frame settings | preview and native matcher windows |
| Chart | `+80` event data, `+88` int32 count, `+8c` capacity | contiguous loop uses stride `0x90`; constructor initializes header |
| Chart | `+a0/+a1/+a2` bytes | active / native-clock mode / whole-chart automatic propagation |
| Event | `+00` chart, `+08` side, `+10` parent, `+18` next link | constructor, linker `FUN_143c35e90`, terminalizer |
| Event | `+24` float timestamp, `+48/+49` assignment/hand | matcher, advancer, hand selector |
| Event | `+4a` result, `+4d` active-preview, `+4e` finalized, `+4f` automatic marker | full actuator bodies and constructor |
| Event | `+50` callable storage | terminalizer invokes it with the event |

The mod's count limit (8192), logical generations and strict pending-state
policy are defensive policy, not recovered native maximums or native leases.

Matcher requires chart and active-preview byte. With marker zero it admits
pending result 0/3 and rejects neutral sector 8; assignment mismatch writes 1,
otherwise native frame windows choose 5/6 from initial 4. With marker nonzero it
bypasses neutral/assignment rejection. Before due time its automatic branch
returns pending (resetting a non-accepted provisional result to 0); at/after the
timestamp it writes native result 6 and calls terminalizer. Marked advancement
past the late window writes 6 instead of unmarked miss 2. Within the live late
interval it writes pending 3. Neither path needs a fabricated input sector,
external clock or direct mod result write.

Native advancement permits finalized events with result 0/3 through its outer
guard; matcher itself does not supply the mod's strict finalized/root guard.
Therefore admission must still require the exact synchronous chart scope,
event membership aligned to stride90, event owner=chart, parent=null,
finalized=false, result 0/3, and marker previously zero. Already-native-marked
events must not enter a restoration journal. Followers are not new required
actions: the native linker sets parent and prepends the next-link chain.

Constructor `piano_event_construct` clears result, preview/finalized and marker;
`piano_event_deep_copy` copies marker unchanged (loads/stores at
`0x03c31888/0x03c3188b`). Its two direct code calls are parser
`FUN_143c59e64` at `0x03c5a276/0x03c5a325`. Chart constructor
`FUN_143c319a8` zeroes all four bytes at +a0; expansion sets active/native-clock
but does not reset +a2. Chart update copies +a2 into every event marker when set.
Setting/restoring only chart+a2 would leave persistent event writes.

### Restore-before-callback proof

Full matcher/advancer disassembly shows no gameplay callback on their automatic
branches before terminalizer and no event load/store after that CALL. The normal
matcher window helper is not on the automatic branch. Stack-probe calls are not
gameplay callbacks. Terminalizer:

1. Checks chart and finalized, sets finalized at `0x03c4d3ab`.
2. Invokes event callable at `0x03c4d3bc` through `FUN_140c3dfc4`.
3. Only afterward promotes selected-side ownership through native strong-ref
   helper, then for result 5/6 marks descendants at `0x03c4d441`.
4. Invokes side-owner virtual +20 at `0x03c4d467` (RCX owner, R8 event, XMM1 tempo).

Terminalizer never reads the root's +4f marker. Restoring the exact introduced
byte **before forwarding terminalizer**, and discharging the guard so it never
touches the event afterward, therefore preserves native result/follower/sound
behavior without carrying that temporary byte across callbacks. A no-terminal
automatic return can restore before returning. All originals remain exactly
once. This supports the same three-hook mechanism, not a whole-update batch
mutation. Event/chart remain borrowed; later side-owner promotion is not a
chart-array lease. No callback-stable allocation or arbitrary concurrent alias
exclusion was established. A restoration failure is not a safe continue/retry
case; retain failure evidence, do not forward an unrestored root or swallow a
native fault. All three actuator hooks must be admitted together.

## Controller dispatch and per-play lifecycle

Table bytes were read for the complete `14 * 0x20` extent. Each entry has int32
ID at +0, enter pointer +8, update +10, exit +18; IDs are exactly 0..13. Table
membership, alignment and ID checks must precede dereferencing current/pending.
Helper `FUN_140a0be80` writes only pending at dispatcher+20. Generic transition
`FUN_1421fbb58` receives its internal state view at dispatcher+8: invokes old
exit, publishes pending as current, clears pending, then invokes new entry.
`FUN_14223da28` then calls current update with RCX=C and XMM1=delta. Pending is
a request, not a published state. Generic dispatcher gets delta in XMM2 because
its first two arguments are dispatcher and controller.

| State | Independently recovered older lifecycle facts |
|---|---|
| 0 | null enter/update/exit. Tick returns AL=0 when published current ID is 0; null current returns true. |
| 4 | enter `0x03c3f338`, update `0x03c56400`; update requests 5 after native predicates. Not a pause predicate. |
| 5 | existing menu/list entry, update and exit owners remain canonical; exit `0x03c4ce78` disables gameplay input without chart release. |
| 6/7/8 | activation/preparation/count-in; state-8 update `0x03c3bfe0` requests 9 after timer/presentation predicate. |
| 9 | enter `0x03c40db4` constructs/replaces shared chart, installs callbacks, expands; update `0x03c5bd98`; exit `0x03c4cf68` neutralizes inputs/disables input, does not release chart. |
| 11 | enter is catalog `piano_score_mode_setup`; update `0x03c37e0c` expires timer and requests 10. |
| 10 | enter is catalog `piano_detail_update`; exit `0x03c4d064` clears chart and releases the shared pair. |
| 12 | enter `0x03c3f79c` constructs confirmation dialog; null update/exit. No independently recovered pause/retry label or scheduler predicate. |
| 13 | enter `0x03c3fbc4` requests parent state6; update `0x03c3cb7c` eventually requests inner0. |

At state-9 chart CALL `0x03c5bf13`: RBX=C, RCX=[C+f48], R8=C+380,
R9=C+538, XMM1=float delta. Missing side owners request13. Native startup-ready
gate may return before this call. Completion/effect predicates request11.
This proves a synchronous tick -> state9 -> owned chart-update nesting seam,
not that every retained chart or state9 frame is active gameplay. Tick performs
inner dispatch, presentation, parent dispatch, another UI update, then returns
continuation; native controller is not a UObject.

List re-entry does not free the old chart. A new logical play must not be keyed
only by allocation address; state9 re-entry constructs/replaces it and addresses
can be reused. Autoplay taint must outlive toggling off and states11/10, then a
fresh play must reset it. Destruction `piano_menu_controller_destructor` tears
down shared storage/dispatcher directly; state exits are not guaranteed. Extend
its existing owner to revoke authority, never add a competing destructor hook.

**Remaining older lifecycle blocker:** this static path does not prove that
the scenario wrapper/tick/chart calls stop during the 1.004 human pause route.
There is no older live pause/resume/list-replay/leave trace here. Do not promote
default-only observation, zero delta, retained state9 or timeout silence into
older pause/seated authority. Copied generation-bound commands may never cause
worker-thread native reads. No exhaustive indirect/reentrant lifecycle claim.

## Existing no-personal-record boundary: older parity and difference

`progress_lookup` semantic ABI is packed ScoreInfo FName in RCX -> borrowed
Record16 pointer in RAX. Native mode must equal E; row must resolve; row+72
byte must be <8. Otherwise null. Older storage is base + `0x072a21b4` + index*16,
**not the default storage address**. No new global hook or storage mutation is
needed. Preserve null for stock misses; fabricating one would alter native
result construction, while custom overlay admission is a separate existing rule.

In the owned `piano_detail_update`, CALL `0x03c41672` returns at catalog
`progress_lookup_display_caller_2` (`0x03c41677`). RAX is saved in RDI;
null row/record branches exit at `0x03c4167d..0x03c4168b`. This is a real writer,
not display-only:

- +8 int32: best-score compare `0x03c4171d`, max assignment `0x03c4174a`.
- +0 uint16: played flag OR8000 at `0x03c41752`.
- +4/+2 uint16: tally maxima; interior pointers placed in stack-local 32-byte
  descriptors at `0x03c4181b/0x03c41859` and their following stores.
- The consumer independently creates a zeroed 16-byte stack record at
  `0x03c41703` and can select it at `0x03c4172b` on a native config predicate.
  Its UI meaning is not established.
- Final record/interior-pointer store is `0x03c419db`. The tally copy helper
  `FUN_141081984` copies only the leading string, not later descriptor pointers;
  `FUN_143c31e78` destroys only that string. Later UI calls retain no record
  argument; `FUN_143c14968` consumes RCX only. The consumer does not store a
  record pointer into controller/UI or return it.

A private, seeded, writable 16-byte shadow lasting through the **entire owned
result consumer** covers this recovered synchronous span. Seed all 16 bytes;
assign no invented semantics to unused bytes. Native actual score/rank remains
unchanged. Shadowing can still allow the consumer's new-best UI flag to be set;
it is not proof of exact result wording. No shared-buffer concurrency clearance
is implied: nested/concurrent borrowers need distinct ownership, not a mutex
held across native callbacks or a process-lived buffer reused before return.

Fresh lookup xrefs recover four direct code consumers, not default's five:
list renderer `FUN_143c1264c`, result consumer, score/rank text
`FUN_143c47c40`, and menu renderer `FUN_143c4f960`. The other three read flags/
score; no additional personal-best assignment was found in those bodies.
The missing older `progress_lookup_display_caller_1` stays **absent**, not zero.
An important inlined sibling, `FUN_1423a7410`, references the same table directly:
it calculates the record, reads score, and ORs menu flag4000 (store
`0x023a7f7f`, following read `0x023a7f84`), not best score. Thus four lookup xrefs do not mean four total
storage aliases, and caller-2 shadowing is not blanket persistence interception.

`score_calculate` reads four uint16 counters/weights and returns nonnegative
int score in EAX; it does not save the personal record itself. Both owned
`piano_score_mode_setup` and result entry call it. The existing custom INI sink
in `progress.cpp::score_calculate_detour` must be suppressed for the assisted
play at both opportunities, without substituting its return score. No older
native per-build change is needed for that source-side policy. Quest/reward/
global completion suppression is outside the requested personal-record policy.

## Handoff, checks and next experiment

Lead integration may add the five older build keys above after independent
address/ABI review; consumed layouts need no older offset branch. Existing
owned boundaries must be extended in place. Keep executable authentication,
exact prefix validation, table membership checks, full hook transaction and
fail-closed unavailable behavior; absent older keys are the current safe state.
Do not call this an older disk-qualified or gameplay-qualified feature.

Recovery recipe: select the older import explicitly; start at canonical
`piano_chart_update`; follow advancement and candidate/hand dispatch to matcher;
follow their common terminalizer. Follow state9 update's data xref to the state
table, its constructor xrefs and scenario wrapper to tick. Re-read complete
prefixes/table and replay unique-pattern searches. Start result proof at the
canonical caller-2 CALL/return pair and follow every record/interior-pointer use
through the final tally store and descriptor destruction. These are reproducible
locators, not independently retained full disassembly exports.

Checks performed: canonical docs/evidence/source reads; imported MZ/PE and four
existing-signature correlations; complete actuator/tick/result disassembly;
constructor/dispatcher/lifecycle/record-helper decompilation and targeted xrefs;
14-entry table bytes; four unique complete-prefix searches; older catalog
collision check; installed default-file hash. An unbounded instruction search
timed out and was replaced by exact entry-pattern/call-path recovery; it supplies
no negative evidence. No tests/build were appropriate for this evidence-only pass.

Concrete gaps: authentic older file hash and disk byte uniqueness; live entry
ownership/trampoline/unwind and changed-runtime checks; older pause/continue,
list replay, generation/destruction, assisted actual results and unchanged
native/custom best scores. Whole-image alias exclusion, general callback
concurrency exclusion and all UI routes were not proved.

Next focused experiment, only after separate human authorization and matching
older executable authentication: one stock-song outside/list/play/pause/continue/
return-list/replay/stand-up observational route using the older tick and
state9 pre-chart seam, never a second hook on product-owned chart update. Retain
entry/return current/pending, copied chart facts and continuation without later
pointer chasing. Then separately test actual assisted notes and record sentinels
on the reviewed older product; observation alone is not mutation qualification.
On identity/readability/ownership failure, stop rather than translate addresses
or retry stale memory. Runtime failure recovery remains canonical normal process
exit and lead-owned rollback/collection, never live ASI unload/reload.
