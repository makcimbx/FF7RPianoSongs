# Autoplay personal-record suppression: static evidence

Date: 2026-10-19. Mode: Integration, read-only static research.

## Scope and disposition

The selected behavior is **«Не сохранять рекорд»**: show the play's actual
results without replacing its per-song personal record when autoplay was used
at any point in that play. This is native personal-best and custom INI
protection, **not suppression of quest completion, rewards, or global-state
triggers**. Earlier broader completion-suppression research is not an
implementation requirement.

The existing `progress_lookup_display_caller_2` redirection is a supported
best-score write boundary. The 16-byte shape and synchronous consumer lifetime
are established below. Unrestricted concurrent/reentrant reuse of the current
single shared overlay is **not cleared**. No new hook is necessary; existing
owners already cover the lookup and its entire result consumer.

No implementation, focused gameplay validation, release qualification, or
whole-image proof of every possible persistence alias is claimed.

## Executable identity and evidence provenance

Canonical identity comes from `src/game/rva_catalog.json`:

| Field | Inspected target |
|---|---|
| Build ID | `ff7rebirth-steam-win64-6a16ced2` (1.005) |
| PE timestamp | `0x6a16ced2` |
| SizeOfImage | `0x099d9000` |
| Checksum | `0x0769ea6e` |
| Canonical file size | `124317952` bytes |
| Canonical SHA-256 | `752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc` |
| Ghidra program | `/ff7rebirth_.exe-1.005` |
| Ghidra display name | `ff7rebirth_.exe-9457d4` |
| Architecture / preferred base | Windows x86-64 / `0x140000000` |

The imported program's PE headers independently matched timestamp, image size,
and checksum. Imported bytes also matched the catalog signatures for
`score_calculate`, `progress_lookup`, `piano_score_mode_setup`,
`scoreinfo_resolver`, and the full 37-byte `piano_detail_update` signature.
The imported-file SHA-256 was **not independently authenticated**. The display
suffix is not hash proof. These findings are PE/signature-correlated to 1.005,
not a substitute for production executable identity validation. No 1.004
clearance is implied.

The catalog's inspected 1.005 entries for `score_calculate`,
`piano_score_mode_setup`, and `piano_detail_update` contained migration records;
`progress_lookup` relied on repository evidence. Those records alone did not
establish the behavior below. This investigation newly recovered the relevant
instructions, decompilations, and direct cross-references from the imported
image using read-only Ghidra tools. The detailed instructions were not exported
as separate artifacts; the locators below permit recovery from that program.
Source references describe the checkout inspected during this investigation,
not an independently qualified release snapshot.

All addresses below are **1.005 RVAs** unless explicitly marked VA. Use catalog
IDs for integration; do not hard-code these evidence locators into production.
Uncataloged helper addresses below are evidence-only, not approved hook targets.

## Normal score/result path

| Stage | Static evidence |
|---|---|
| Playing completion | State-9 update `FUN_1439b5290`, RVA `0x039b5290`, requests state 11 after native completion predicates. |
| Score-mode entry | Catalog `piano_score_mode_setup`, RVA `0x03998360`, calculates score through `score_calculate`. |
| Result transition | State-11 update `FUN_14398fac8`, RVA `0x0398fac8`, expires its timer and requests state 10. |
| Result entry / record writer | Catalog `piano_detail_update`, RVA `0x0399a334`, calls `progress_lookup`, then `score_calculate`, and writes the returned record inline. |

State descriptors in the imported image associate these entry/update functions
with the stated states. The state-request helper `FUN_140c9a160`, RVA
`0x00c9a160`, selects a descriptor; it does not itself invoke the entry callback.

`score_calculate` returns the calculated integer score. Its recovered body reads
the four counters and configured weights; the native personal-best assignment
is in the result consumer, not that calculation body. Keep the actual score
and rank calculations unchanged for the selected behavior.

## Lookup ABI, storage, and null behavior

Catalog `progress_lookup`, RVA `0x039a1114`, has this recovered semantic ABI:

```cpp
Record16* progress_lookup(uint64_t packed_scoreinfo_fname); // RCX -> RAX
```

The current source typedef forwards four opaque argument slots. The inspected
native instructions establish only the packed key in RCX as a semantic input.
The returned pointer is borrowed global storage, with no ownership transfer.
When native mode is `0xE`, the ScoreInfo row resolves, and its byte at `+0x72`
is below eight, lookup returns:

```text
image_base + 0x070d5944 + index * 16
```

Otherwise it returns null. The table-base VA is `0x1470d5944`; its only recovered
exact-address cross-reference is lookup's address calculation.

In `piano_detail_update`, the call is at RVA `0x0399a38e`; its return site is
catalog **`progress_lookup_display_caller_2`, RVA `0x0399a393`**. RAX is saved
in RDI. Instructions at `0x0399a399` through `0x0399a3a7` exit before result
construction if the ScoreInfo row or lookup result is null.

Consequently, returning null is not a suitable replacement for showing normal
results without saving. When extending handling to native stock records,
preserve native null behavior rather than inventing a record for a native
miss. Current custom-song overlay handling can deliberately replace a null
original; that existing behavior is distinct from stock handling.

## Record shape and all state-10 pointer uses

| Offset | Recovered state-10 access |
|---|---|
| `+0`, uint16 | OR played flag `0x8000` at `0x0399a46e`. |
| `+2`, uint16 | Read/update one hit-count maximum. |
| `+4`, uint16 | Read/update another hit-count maximum. |
| `+8`, int32 | Compare/read personal best; write `max(previous_best, score)` at `0x0399a466`. |
| Other bytes | No accesses found in this consumer; do not assign unsupported field meanings. |

Sixteen bytes are supported by both the lookup stride and the consumer's own
native discard path: it zeroes a 16-byte stack record at `0x0399a41f`, then
selects that record at `0x0399a447` when a native configuration predicate is
false. The same result-building code proceeds against that writable scratch
record. The configuration predicate's user-facing meaning was not established.

The borrowed record and its interior pointers do not escape the inspected
state-10 consumer:

- Best-score comparison and update occur before the tally loop.
- Pointers `record+4` and `record+2` are placed in stack-local 32-byte tally
  descriptors at `0x0399a53b` and `0x0399a579`.
- The other two descriptors target a local scratch ushort, not additional
  native record fields.
- The tally loop's final record/interior-pointer dereference and write is at
  `0x0399a6f7`.
- Descriptor helper `FUN_140e8caa0`, RVA `0x00e8caa0`, copies the leading
  string representation, not the later pointer field. Descriptor destructor
  `FUN_143987bac`, RVA `0x03987bac`, destroys that string only.
- Later UI/result calls receive no record pointer. In particular,
  `FUN_143968ea8`, RVA `0x03968ea8`, uses RCX only; the caller's apparent RDX
  stack argument is not evidence of pointer retention.
- No record pointer is stored into controller/UI state or returned.

A correctly seeded writable shadow therefore covers this synchronous write
span without changing the actual score/rank. The native comparison against
the prior best also sets a result flag at `0x0399a43e`; shadowing alone does not
neutralize that flag or establish the exact wording of its eventual UI.

## Existing ownership and shared-overlay caveat

`src/game/progress.cpp` owns `progress_lookup` and `score_calculate`.
Its `g_progress_state_overlay` is an aligned process-lived 16-byte array
(line 35). The lookup detour loads custom progress, clears this array, seeds
flags and best score, and returns its address (lines 141-185). Eligibility
currently depends on selection, INI availability, and the caller allowlist;
this is not already a stock-song autoplay suppression policy.

Catalog `piano_detail_update` is already owned by `title`.
`src/game/title.cpp:781-787` brackets the whole original consumer with
`ScoreInfoPianoDetailUpdateScope`. Thus existing hooks provide both the inner
redirection point and the full synchronous outer lifetime. **Do not install a
second hook at either owned address.**

However, process lifetime does not establish exclusive use:

- `g_progress_mutex` protects overlay initialization only
  (`progress.cpp:159-168`), not native accesses after return.
- Other admitted allowlisted lookups clear/reseed the same buffer.
- No second direct lookup occurs in the inspected state-10 body, but this
  does not exclude concurrent threads or transitive callback reentrancy.
- `HookCallbackGate::try_enter` (`src/core/hooks.cpp:118-125`) counts admitted
  callbacks; it does not serialize their execution.
- The existing thread-local detail scope tracks nesting; it does not reserve
  the shared record buffer.
- Holding `g_progress_mutex` across the original result consumer would
  deadlock its nested lookup/score-calculation detours, which acquire that
  mutex.
- A rejected progress callback currently returns the original pointer
  (`progress.cpp:145-146`). A suppression guarantee must account for this
  fallback rather than silently allowing a tainted play to reach native
  record storage.

Layout/non-escape clearance is therefore **not unqualified concurrency
clearance** for the current single buffer. Preserve exact play/key authority
and ensure the shadow cannot be reset/reused while borrowed. Per-play autoplay
taint, input, and timing are owned by the separate runtime investigation.

## Other personal-best writers and custom INI

All five recovered direct callers of `progress_lookup` were inspected:

| Consumer RVA | Finding |
|---|---|
| `0x03966a6c` | List renderer; reads played status and rank. |
| `0x0398a068` | Menu handling; reads score and ORs menu flag `0x4000`, not best score. |
| `0x0399a334` | State-10 result entry; the recovered native personal-best writer. |
| `0x039a0ccc` | Reads best score and constructs rank text. |
| `0x039a8c18` | Menu renderer; reads flags, score, and rank. |

Recovered playing/completion and result/retry-transition functions showed no
additional personal-best writes. This is bounded call-path evidence, not
whole-image exclusion of aliases, aggregate save/load copies, indirect
consumers, or a recovered disk serializer. The menu-only flag is not a reason
to expand this task into completion suppression.

The custom INI is a separate persistence sink. Production source search found
one `record_score` call, in `score_calculate_detour`
(`src/game/progress.cpp:69-98`). Native calculation reaches this hook during
both score-mode setup and state-10 result construction. The call updates
custom progress through `record_score` (lines 244-282); the INI writer stores
`Played`, `HighScore`, and `LastScore`
(`src/game/progress.h:237-245`), with an optional `LastPlayedDifficulty`
preference write in the surrounding persistence path.

**Caller-2 shadowing alone cannot protect custom INI records:** an earlier
score calculation can already have saved them. The no-record policy must
cover both calculation invocations without replacing their returned score.

## Interpretation correction and handoff checks

`progress_lookup_display_caller_2` is not display-only: its consumer writes
personal best, played status, and tally maxima through the returned pointer.
The current source's `display_overlay` diagnostic and catalog naming do not
change that call-site contract. Caller `_1` also writes a menu flag, although
not best score. Existing redirection at `_2` is already a write-destination
overlay, not merely a presentation substitution.

Primary affected catalog IDs are `progress_lookup`,
`progress_lookup_display_caller_2`, `piano_detail_update`, and
`score_calculate`; `piano_score_mode_setup` anchors the earlier INI opportunity.
No global-state/reward gate is required by this scope.

Next focused check: a deterministic overlapping/nested-consumer test should
prove that the shared shadow is not reset/reused until the existing outer
result wrapper returns, while native-record and INI sentinels remain unchanged.
Later human-controlled gameplay must separately verify actual results display,
unchanged saved personal best, and retry into a clean manual play. These checks
were not performed here. On compromised runtime evidence, follow canonical
process-exit/install rollback procedures; live unload/reload is not recovery.

Only this evidence document was written. No source/catalog changes, workers,
live attachment, game launch, memory mutation, installation, or Git publication
were performed.
