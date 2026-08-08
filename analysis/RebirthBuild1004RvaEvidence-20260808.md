# Rebirth Build 1.004 RVA Derivation Evidence (2026-08-08)

## Scope and verification boundary

This note records how every catalog address for game build `1.004`
(catalog build `ff7rebirth-steam-win64-68fd6fde`) was derived. None of it is
reproducible from the repository alone, because the derivation ran against the
1.004 executable, not against tracked files.

All evidence here is static, read-only Ghidra decompilation, memory reads, and
byte-pattern searches against the 1.004 program. No live process was attached,
no memory was written, and no address was translated from build
`ff7rebirth-steam-win64-6a16ced2`. **Nothing in this note has been
runtime-validated on a 1.004 build.** Startup admission, hook installation, and
the focused scenarios in [In-Game Validation](../docs/InGameValidation.md)
remain outstanding for this build.

## Executable identity

| field | value |
| --- | --- |
| catalog build id | `ff7rebirth-steam-win64-68fd6fde` |
| game build | `1.004` |
| `pe_timestamp` | `0x68fd6fde` |
| `size_of_image` | `0x09bad000` |
| `pe_checksum` | `0x07877c99` |
| `file_size` | `126263624` |
| `sha256` | `76bd4d539878a8759433f1cd7c7b47d5e3abda0ef41572a664d2d606a9f09dc8` |

RVAs use `rva = virtual_address - 0x140000000`.

The analysis program was confirmed to be the 1.004 image, not 1.005, three
separate times across the session, including after a session-limit
interruption:

- `get_metadata` reported `function_count == 373972`; the 1.005 program reports
  `377293`. Unchanged at session start, midpoint, and after the interruption.
- `read_memory 0x143c35e90 / 16` returned `4885d2741548894a10488b4118488951`,
  unchanged on all three checks.
- `read_memory 0x14398db4c / 16` returned
  `02c5f259d3c5f82f15974b71027306c5`, which is *not* the pattern above. That
  negative check is what excludes the 1.005 image, where the 1.005 catalog RVA
  layout would have placed the matching bytes.

Total recovered symbol count for the program was `1721233`.

## Address derivation route

Signatures cataloged for one build are not portable: rescanning 1.004 with the
1.005 signature set locates only a small minority of them. Every address below
was therefore derived from the 1.004 binary by a semantic route and only then
given a signature. A signature hit was never treated as provenance.

The routes used, in order of preference:

1. **Matched call sites and call-graph position.** An entry was accepted when
   its caller/callee relationship reproduced the relationship the 1.005 entry
   holds — for example a call site identified by which function contains it and
   at what offset within that container, rather than by absolute address.
2. **String and vtable references** for functions anchored to unique literals.
3. **Return-site entries** (`*_return`, `*_call`, `*_caller`) derived by
   decoding the containing function and taking the instruction boundary
   following the identified CALL, never by adding a fixed delta to another
   build's value.

Sixteen entries were re-derived a second time as a by-product of decoding
`rel32` displacements inside their own signature runs (see below). Every such
independent decode agreed with the route-derived address.

### Independent cross-checks

Six `rel32` displacements inside the recorded signature runs were hand-decoded
and confirmed to land on independently established 1.004 addresses. These
confirm the address table; they do not confirm the signatures.

| CALL at | decodes to | RVA |
| --- | --- | --- |
| `0x143c46413` | `selected_entry_getter` | `0x03c4817c` |
| `0x143c41672` | `progress_lookup` | `0x03c48088` |
| `0x143c480aa` | `scoreinfo_resolver` | `0x03bff0a4` |
| `0x143c46406` | `weak_object_resolver` | `0x01905998` |
| `0x143c56d55` | `weak_object_resolver` | `0x01905998` |
| `0x143c56d62` | `widget_visibility_setter` | `0x00861e94` |

### Compile-time adjacency invariants

Three production `static_assert`/offset relations are checked against the
derived 1.004 values, because they are compile-time facts of the generated
tree rather than runtime checks:

- `piano_page_visibility_call` (`0x03c56d62`) `== piano_page_selected_resolve_call`
  (`0x03c56d55`) `+ 13`.
- `piano_page_no_selection_continuation` (`0x03c56d67`) `== piano_page_visibility_call + 5`.
- `piano_menu_list_open_call` and `piano_menu_list_cancel_close_call` are used as
  `+ 5` return sites; both derived values are the CALL instruction address.

All three hold in 1.004 with independently derived addresses. They were not
used to derive any address.

## Signature policy applied

Every stored signature uses an all-`x` mask and is **anchored at the entry RVA**
of its catalog entry, verified at exactly `module_base + rva`. Offset-anchored
runs are not expressible in the catalog schema and none is proposed.

`rel32` CALL/Jcc displacements and RIP-relative `disp32` operands **are**
permitted inside signature bytes for this build. Signatures are stored per
build, the version gate admits only the exact cataloged executable, and within
one image these displacements are fixed constants. They are also usually the
cheapest route to uniqueness, because the common
`MOV EAX,imm32 ; CALL __chkstk` prologue encodes each function's own distance
to `__chkstk`.

Uniqueness was **measured, not inferred**. Every recorded run was searched
image-wide with `search_byte_patterns` and its match count recorded verbatim.
Runs that collided were extended and re-measured.

- 46 `release` entries covered; 1 is absent from the build (see below).
- 39 of the remaining 45 have a run measured unique (`matches == 1`); 6 have no
  run at all, because their `validation.policy` does not use one:
  `fname_ctor`, `sead_descriptor_build`, `find_child_widget`,
  `bgm_controller_key_global`, `piano_audio_global`,
  `rank_text_widget_name_global`.
- 28 of those 39 runs are stored as catalog signatures. The other 11 belong to
  `data_only` entries, where the schema forbids signature metadata; their
  measurements are retained in the catalog evidence prose as address
  provenance only. They are `action_state_query_primary_return`,
  `selected_entry_getter_activation_return`, `persistent_chart_expand_caller`,
  `progress_lookup_display_caller_0`, `progress_lookup_display_caller_2`, and
  the six `scoreinfo_*_return` entries.
- 34 runs contain a `rel32` or RIP-relative displacement; 5 contain no
  displacement bytes at all.
- Longest run required: 37 bytes, for `piano_detail_update`.
- Entries that could not be made unique: none.

### Collisions measured and resolved in this pass

The displacement-free prefix is the longest run the earlier no-`rel32` guidance
permitted. Its measured image-wide match count is what withdrew that guidance.

| entry | displacement-free prefix | its matches | resolved run |
| --- | --- | --- | --- |
| `piano_on_menu_selected_index_changed_body` | 29 bytes | 34 | 34 bytes, unique |
| `piano_title_population` | 26 bytes | 12 | 31 bytes, unique |
| `piano_detail_update` | 32 bytes | 5 | 37 bytes, unique |
| `note_count` | 19 bytes | 3 | 24 bytes, unique |
| `weak_object_resolver` | 16 bytes | 2 | 24 bytes, unique |

`piano_on_menu_selected_index_changed_body`, `piano_title_population`, and
`piano_detail_update` are all `required_for_release_startup` hooks, so a
non-unique run there is a startup-validation hazard, not a cosmetic one. The
eight-register push block plus a frame `imm32` is a shared compiler idiom, not
a property of any one function; a 32-byte displacement-free run including the
unusual `LEA RBP,[RAX-0x378]` still collided five ways.

`weak_object_resolver` is a new finding. Its 16-byte run matches two sites in
1.004, `0x1418eb3d4` and `0x141905998`; the correct site is `0x141905998`. The
run was extended through the RIP-relative bound compare to 24 bytes, measured
unique.

### Collisions inherited from earlier passes, resolved by the `rel32` rule

| entry | earlier measurement | now |
| --- | --- | --- |
| `score_calculate` | 16-byte prefix, `>1000` matches (search capped) | unique at 21 bytes |
| `piano_chart_update` | 19-byte prefix, 58 matches | unique at 24 bytes |
| `piano_menu_controller_destructor` | 22-byte prefix, 6 matches | unique at 27 bytes, entry-anchored |
| `set_string_text` | offset-anchored 16-byte run, still 3 matches | unique at 16 bytes from the entry |
| `find_child_widget` | 27-byte prefix, 14 matches | not applicable; `validation.policy` is `build_identity` and the signature is null by design |

An earlier pass concluded that `score_calculate` "cannot be given a usable
all-`x` signature" and recommended relaxing the mask or falling back to
`build_identity`. That conclusion was a consequence of the withdrawn no-`rel32`
rule; no policy exception is needed. `piano_menu_controller_destructor`,
`piano_chart_update`, `set_string_text`, and `select_index_helper` had all been
proposed as offset-anchored runs for the same reason; all four are now
entry-anchored.

No RVA was revised by this pass. Every address is carried forward unchanged
from the derivation records; only signatures changed.

## `progress_lookup_display_caller_1` is absent from 1.004

This call site **does not exist** in build 1.004. That was established
positively, not by a failed search.

`progress_lookup` (`0x03c48088`, confirmed) has exactly **four**
`UNCONDITIONAL_CALL` xrefs in 1.004, against the five that 1.005 has:

| 1.004 call site | containing function |
| --- | --- |
| `0x03c12c97` | `piano_title_population` |
| `0x03c41672` | `piano_detail_update` |
| `0x03c47c5c` | the rank-text sibling helper |
| `0x03c4fa22` | `piano_on_setup_item_body` |

Each of those four maps to a 1.005 call site at an exact container offset. That
leaves the 1.005 `select_index_helper` site — which sits at offset `0xe4`
inside its container, and which the 1.005 catalog records at RVA `0x0398a151`,
`0xe9` past the 1.005 `select_index_helper` entry `0x0398a068` — as the single
unmatched member of the 1.005 set.

The 1.004 role-holder for `select_index_helper` was positively identified at
`0x023a7410` (below), and `progress_lookup` appears nowhere in its callee set
across all 3422 bytes. The over-merge hypothesis that could otherwise hide such
a call was independently ruled out: the function contains exactly **one** `ret`
across 732 instructions and exactly **one** caller, with no call landing on any
interior address. A blob of six or seven merged neighbours could not exhibit
that shape.

The absence is therefore a real difference in the 1.004 code, not a Ghidra
function-boundary artifact and not a failed search.

### Catalog and source consequences

The entry's `requirement` moves from `release` to `optional`. A `release`
address must be present in every declared build, and this one legitimately is
not. `install_policy` stays `data_only` and `hook_owner`/`hook_spec` stay null,
so no install relationship changes and hook `order` contiguity is untouched —
`order` values are collected only from entries that own a `hook_spec`.
`status` stays `production`, matching the existing
`title_view_converter_activation_return` shape (`optional` / `call_site` /
`data_only` / `production`).

The consequence for the runtime is that the display-caller allowlist is a
**per-build set**, not a fixed count of five. On 1.004 it has two members
rather than three. `src/game/progress.cpp` previously compared the observed
caller RVA against a fixed three-element array without excluding zero, while
`to_rva()` returns `0` for a caller outside the module. With the absent entry
rendering as the generator's `0x0` absence sentinel, that comparison would have
**accepted** any out-of-module caller. The predicate now rejects a zero caller
RVA outright, which makes both the sentinel and the out-of-module case
unmatchable. RVA `0` is the PE header and is never a valid call site, so the
rejection is unconditional rather than build-specific. This is the reciprocal
of the rule stated in [Architecture](../docs/Architecture.md): a consumer that
forms `exe_base + rva` must test the RVA for zero, and so must a consumer that
compares against a cataloged RVA.

## `select_index_helper` is `probable`, and why its address is still adopted

The 1.004 address is `0x023a7410`. It is retained as **probable**, not promoted
to confirmed. The role is established; the code *unit* is not the same shape as
the 1.005 entity.

Role evidence:

- It is the sole callee of the confirmed selected-index body for the selection
  pair, invoked exactly twice as `(holder, *(ctrl+0x480), 1)` and then
  `(holder, newIndex, 0)`.
- It carries the `+0x484` sticky toggle (set 1 / compare 0 / set 0) and the
  `0x4000` entry flag (`MOV R15D,0x4000` twice).
- Its single caller is the confirmed
  `piano_on_menu_selected_index_changed_body`, matching the 1.005
  single-caller relationship.

Divergence from 1.005:

- 3422 bytes against 503; 33 callees against 13.
- It contains an `_Init_thread_footer`/`atexit` magic-static block that the
  1.005 helper does not.
- It calls `set_string_text` directly.
- It does not call `progress_lookup` anywhere.

Over-merge was independently ruled out by the one-`ret`/one-caller/no-interior-entry
evidence recorded in the previous section. The most consistent reading is that
sibling helpers are inlined into this unit in 1.004.

### Adoption decision

The address is adopted. The deciding question is what the product's hook
contract actually depends on, and that is a source question, not an address
question.

`select_index_helper_detour` in `src/game/selection.cpp` is a pass-through. It
takes `(void* wrapper, int32_t index, uint8_t flag)`, consults the product's
own registry for a descriptor, discards the result, and on **every** path
returns `g_original_select_index_helper(wrapper, index, flag)` with the
arguments unmodified. It does not read or write through `wrapper`, does not
inspect the callee's internals, does not depend on the callee's size, callee
set, or side effects, and does not depend on `progress_lookup` being reached.

The hook contract is therefore exactly: the entry address, the
`uintptr_t __fastcall(void*, int32_t, uint8_t)` ABI, and a prologue that
matches the cataloged signature at install time. All three hold at
`0x023a7410`. Inlined siblings change the body behind the entry; they do not
change the entry, the ABI, or the arguments the detour forwards. The catalog
entity for 1.004 denotes the entry point holding this role, and the unit
boundary differs by build — recorded here rather than silently absorbed.

Had the detour depended on the narrow 503-byte helper — for example by reading
callee-local state, by assuming a specific side-effect set, or by relying on
the `progress_lookup` call this unit does not make — adoption would have been
refused and the catalog entity would need redefinition for 1.004 instead. It
does not.

The `probable` grade is retained deliberately. It was not promoted to make the
set look complete, and in-game qualification on a 1.004 executable is what
would settle it.

## Second derivation pass: the remaining 33 entries

Everything above describes the first pass, which established 45 of the 78
catalog entries; its coverage and uniqueness counts are scoped to those 45. This
section covers the second pass over the remaining 33. The same rules apply
unchanged: static read-only analysis of the 1.004 image only, semantic route
first and signature second, and every recorded run re-read at its own entry RVA
and *measured* image-wide with `search_byte_patterns` rather than assumed unique.
Every run recorded in this pass measured exactly one match.

Outcome of the second pass:

| disposition | count |
| --- | --- |
| adopted, graded confirmed | 25 |
| adopted, graded probable | 1 (`bgm_slot_setup`) |
| already present from an earlier pass, still probable | 1 (`bgm_controller_key_global`) |
| held back deliberately | 1 (`piano_audio_state_tick`) |
| unestablished | 5 |
| positively absent from 1.004 | 1 (`progress_lookup_display_caller_1`, above) |

Build `ff7rebirth-steam-win64-68fd6fde` therefore moves from 45 to **71 of 78**
addresses and from 20 to **33 of 38** hook specifications.

### Routes used in this pass

1. **Call-edge chains from a confirmed anchor.** The strongest route available
   without symbols, and the one that settled the piano event quartet. The
   confirmed `piano_score_expand` (`0x03c5fad8`) makes exactly one call, at
   intra-function offset `0x5c`, and it lands on `piano_score_parser`
   (`0x03c59e64`). Inside that parser the ordered piano-band direct calls form
   four consecutive pairs, each target called exactly twice; the third pair is
   the already-confirmed `piano_event_link` anchor (`0x03c35e90`), which fixes
   the phase of the sequence and aligns the flanking pairs one-to-one with the
   1.005 order construct / deep_copy / link / destruct.
2. **Container enumeration.** `title_view_converter` was found by enumerating
   every direct call target of the confirmed 1.004 activation container
   `FUN_143c40db4` and comparing callee prologues, rather than by searching for
   the 1.005 prologue image-wide.
3. **String literal anchors.** `create_package` was recovered from the three UE
   `CreatePackage` fatal-log literals, which are referenced from that one
   function and nowhere else in the image.
4. **Vtable slot references.** `game_viewport_client_post_render` was recovered
   from the `UGameViewportClient` primary vtable at `0x145fdadf0` and the
   Draw-side virtual call that targets the same slot.
5. **ABI plus semantic body reading.** Used where no anchor existed but the body
   is self-identifying: `player_input_input_key` (stride-`0xf0` KeyStateMap,
   double-click detection), `sead_onmemory_bank_release` (blocking `Sleep(1)`
   spin only when the asynchronous parameter is zero),
   `sead_onmemory_bank_kind_lookup` (same shared lookup helper and critical
   section as the release entry), and both `FCanvas` helpers.
6. **Return-site decoding.** `title_view_converter_activation_return` was
   obtained by decoding the `e8 rel32` CALL at `0x03c40f9c` and taking the
   following instruction boundary, never by adding a 1.005 delta.

### Two identity corrections that explain earlier failures

Both are cases where a 1.005-derived prologue prefix was silently wrong for
1.004, so a prefix search either returned nothing or returned only decoys. They
are recorded because the same mistake is available to any future pass.

- **`game_viewport_client_post_render` moved vtable slot `+0x320` → `+0x318`.**
  One virtual was removed ahead of `PostRender` in 1.004. The entry is a thin
  26-byte forwarder whose own tail jump moved `0x328` → `0x320` by the same one
  slot. The consequence is that the 1.005 call-site signature for
  `game_viewport_client_draw_post_render_call` (`ff 90 20 03 00 00 ...`) returns
  **zero** matches in 1.004; the 1.004 run is the same bytes with the slot byte
  changed to `18`. A search that found nothing was therefore correct evidence of
  a slot change, not evidence that the call site was gone. Two look-alike
  forwarders at `0x0138a2c4` and `0x05006a58` were rejected: both insert a
  `MOV RCX,[RCX+0x78]` / `MOV RCX,[RCX+0x30]` adjustment before the vtable jump
  and are unrelated adaptors.
- **`create_package` prologue frame immediate `0x50` → `0x60`.** The prologue is
  otherwise byte-identical between builds. That single immediate byte is why a
  1.005-prefix search returned five unrelated candidates and missed the real
  function entirely. It was recovered only through its three fatal-log string
  literals, which is why string anchoring outranks prologue matching whenever a
  unique literal exists.

The general lesson both cases carry: a prologue prefix is a *search accelerator*
derived from the other build, not evidence. Neither entry would have been found
by the accelerator, and one would have been mis-assigned to a decoy.

### `piano_score_parser` grew from `0x5c8` to `0x891` bytes

Body size was an explicit corroborator for three of the four piano event
entries — `piano_event_construct` (`0xda`), `piano_event_deep_copy` (`0x10d`)
and `piano_event_destruct` (`0x29`) are byte-for-byte the same size as their
1.005 counterparts. Their container is not: `piano_score_parser` grew by `0x2c9`
bytes between builds, a 46% increase.

That is recorded because it bounds how far size may be trusted. The parser was
identified purely by the call edge from `piano_score_expand` and by the internal
pair ordering; had size been used as a filter it would have been rejected. Small
leaf helpers appear to survive builds unchanged while their callers absorb
inlined code, so size agreement is usable as confirmation and never as a search
key.

## Probable addresses in build 1.004

The catalog schema has no confidence field. A probable address is therefore
indistinguishable from a confirmed one by inspection of `rva_catalog.json`
alone, which is exactly the failure this section exists to prevent. Every
probable 1.004 address is listed here, and each carries an explicit
`PROBABLE, not confirmed` marker in its catalog `evidence.detail` prose.

Three entries in build 1.004 are probable: `select_index_helper` (recorded in
its own section above), `bgm_slot_setup`, and `bgm_controller_key_global`.

### `bgm_slot_setup` (`0x01213e68`) — adopted

`install_policy` is `callable`, so the product forms a function pointer at this
address and calls it directly. A misidentified address behind a direct call is
the highest-consequence failure the catalog can carry, so adoption was decided
on evidence rather than on the entry's low `diagnostic` status.

Positive evidence:

- The three-parameter ABI matches `BgmSlotSetupFn`
  (`void __fastcall(void*, uint8_t, uint64_t)`) exactly.
- The function is called **directly by the confirmed 1.004
  `sqexsead_play_setup` anchor** (`0x01212540`). This is a call edge from an
  independently established address, the same route class that settled the
  piano event quartet, and it is independent of prologue shape.
- The object layout the body touches matches the documented slot layout in
  `src/game/runtime_layouts.h`. The body guards on and stores the flag at
  `slot+0x5e`, forwards flag and context to three helpers on the sub-object at
  `slot+0xc0`, and on `flag == 0` sets `slot+0x9c`. All three are declared
  `SqexSeadSlot` fields (`observed_field5e`, `bgm`, `observed_field9c`), and
  `observed_field5e` and `observed_field9c` are precisely two of the five fields
  `apply_slot_setup_profile` snapshots and rolls back **around this very call**.
  A three-of-three agreement between an independently decompiled body and a
  layout recorded from a different build is corroboration, not coincidence.
- The 14-byte entry-anchored run was measured unique image-wide.

What remains unproven, and why the grade stays probable:

- The 1.005 counterpart sits at `0x0142260c`, far outside the 1.005 BGM cluster
  (`0x01248xxx`–`0x01249xxx`) that corresponds to the 1.004 cluster at
  `0x01212xxx`. Confirming that the 1.005 function has the same body and the
  same call edge from `play_setup` requires the 1.005 program, which the bridge
  serving this session does not host. Cross-build role equivalence is therefore
  asserted from the 1.004 side only.
- No runtime evidence exists on any 1.004 executable.

Why adoption is safe even if the identification were wrong:

- `g_slot_setup_available` is set at startup from
  `signature_matches(exe_module, find_rva_signature("bgm_slot_setup"))`. When the
  entry is absent for the active build, `find_rva_signature` returns null,
  `signature_matches` returns false for a null spec or a zero RVA, and the flag
  stays false. `apply_slot_setup_profile` re-reads the flag with acquire
  ordering immediately before forming the pointer and returns without calling.
  This bounds *build drift*, not misidentification — the signature was derived
  from this address, so it necessarily matches on this image.
- The call itself runs inside `call_bgm_slot_setup_seh`, so an access violation
  in the callee is contained and reported as failure.
- Every field the sequence mutates (`+0x34`, `+0x48`, `+0x5e`, `+0x60..0x9b`,
  `+0x9c`) is snapshotted before the call and restored by `rollback_setup_fields`
  with verifying read-back if the native call fails or the
  controller → slot → bgm chain no longer matches; a failed rollback disables the
  entire audio route.
- On native failure `g_slot_setup_available` is latched false, so the address is
  never called again in that session.
- The whole path is unreachable until `capture_slot_setup_profile` has succeeded,
  which itself requires the live controller lookup, a UObject identity match, and
  a readable controller → slot → bgm chain.

The residual risk a wrong address would carry is silent corruption inside an
unrelated callee invoked with a slot pointer in `RCX` — SEH catches faults, not
wrong-but-valid writes. That risk is judged acceptable against the call edge from
a confirmed anchor plus the three-field layout agreement. It is not zero, which
is why the grade is not promoted.

### `bgm_controller_key_global` (`0x091e3320`) — retained, unchanged

Already present in the catalog from an earlier pass and **not** re-derived here.
Its `requirement` is `release`, and the generator rejects a `release` entry that
is missing from any declared build, so it cannot be withheld without
reclassifying the entry. It is recorded here so it is not mistaken for a
confirmed address.

`validation.policy` is `data_reference` and no signature applies, so there is no
byte-level check of this address at any point. Nothing verifies that the 1.005
`sqexsead_play_setup` passes the cataloged global; the single missing proof is a
read of the 1.005 `play_setup` body to see which global feeds its controller-key
argument, and that requires the 1.005 program this bridge does not serve. Until
then the 1.004 value is unverified by that route.

### `piano_audio_state_tick` (`0x03cb2ba4`) — held back

The address was derived and is **not** in the catalog. It stays absent, which is
a supported state: `requirement` is `research` and the generator only forbids a
`release` entry from missing a build.

The candidate: of the three functions image-wide carrying the 1.005 prologue
register/frame shape, this is the only one that consumes the incoming `XMM1`
float — `VMOVAPS XMM9,XMM1` immediately after frame setup — matching
`PianoAudioStateTickFn = void __fastcall(void*, float)`, followed by timing
arithmetic against `[R15+0x224]`. The other two candidates only spill
callee-saved `XMM10`–`XMM13` and never read `XMM1`. The 31-byte run measured
unique.

Why that is not enough:

- The identification rests entirely on prologue shape plus float-parameter
  consumption. That is a discriminator *among three prologue-shape matches*, not
  a tie to the piano audio subsystem. There is no call edge to any confirmed
  1.004 anchor, and the single caller `FUN_143ca8918` is itself undocumented, so
  the owning subsystem is unverified.
- The consequence of a wrong target is not bounded the way `bgm_slot_setup`'s
  is. `RawRvaHook::install` verifies the full cataloged prologue before hooking,
  but that signature was derived from this address, so it bounds build drift and
  not misidentification — the same limitation as the signature gate above, with
  none of the compensating anchor evidence.
- `piano_audio_state_tick_detour` is not an observation-only pass-through. It
  takes the audio route operations lock, drives `g_piano_audio_owner_tick`, the
  tick nonce, `g_piano_audio_owner_custom_playsetup` and the pause/resume marker
  batch, and its deferred actions reach the OnMemory bank release path. Installed
  on an unrelated function it would drive the product's own audio route state
  machine from a false owner pointer at that function's call frequency —
  corruption of product state rather than a contained native fault.

Being an `optional_hook` bounds the cost of *absence* (installation is skipped
and logged as `optional_hook_disabled`), not the cost of a wrong target. Absence
is the cheaper error here, so the entry is held.

Next experiment to settle it: anchor through the caller rather than the callee.
Identify `FUN_143ca8918` first — its own callers, and whether it touches
`PianoAudioGlobal::owner` (`+0x1b0`) or `PianoAudioOwner` fields (`state +0x08`,
`packed_key +0x50`, `request_index +0x7c`) from `src/game/runtime_layouts.h`.
A confirmed edge into the piano audio owner object would settle both entries at
once; without one, the prologue-shape match should not be adopted.

## Entries still unestablished in build 1.004

Five entries have no 1.004 address. All are `diagnostic` or `research`, none is
`release`, and their absence is a supported catalog state. The next experiment
recorded for each is the one that pass two would have run with more budget.

- **`bgm_manager_pause`.** The 1.005 entry sits at `0x02a78014` with
  `bgm_slot_pause_transition` `0x64` later and `bgm_slot_resume_transition` at
  `0x02a77658` — a tight three-function cluster in a band that is **not** the
  SQEXSEAD BGM cluster (1.005 `0x01248xxx`–`0x01249xxx`, 1.004 `0x01212xxx`). The
  corresponding 1.004 band was not located, and the prologue
  `40 53 b8 30 00 00 00 e8` exceeds 1000 matches image-wide with no
  discriminating tail. *Next experiment:* the BGM manager singleton is
  `0x1490fd850` with an int32 pause count at `+0x54`
  (`SqexSeadManager::pause_count`). Run a **function-scoped** instruction search
  for `INC`/`DEC`/`ADD`/`SUB` against `dword [reg+0x54]`, restricted to functions
  that also touch the `+0x28` slot array and `+0x30` slot count. The image-wide
  form of that search did not return within 120s. The manager-side pause should
  iterate the slot array and call the per-slot transition, which would settle all
  three entries at once.
- **`bgm_slot_pause_transition`.** Expected to be the per-slot callee of
  `bgm_manager_pause`. *Next experiment:* settle `bgm_manager_pause` first, then
  take its per-slot callee. The 1.005 adjacency is a hint only — this session
  already found two SQEXSEAD functions that broke relative order between builds,
  so adjacency must not be used to derive the address.
- **`bgm_slot_resume_transition`.** Counterpart of the pause transition; 1.005
  `0x02a77658`. *Next experiment:* same chain — once the pause path is
  identified, the resume counterpart is the sibling that decrements the manager
  pause count at `+0x54` and drives the inverse slot transition.
- **`end_text_block_set_text`.** 1.005 `0x00afbef8`. The prologue prefix
  `48 89 5c 24 08 57 b8 50 00 00 00 e8` exceeds 1000 matches image-wide, the
  image carries no `SetText` symbol, and no confirmed 1.004 anchor with a direct
  call edge to it was found within the available context. *Next experiment:*
  anchor through the consumer. `src/game/list_patch.cpp:106` calls this as
  `SetTextFn` on a widget it reaches from the confirmed anchors
  `find_child_widget` (`0x00803390`) and `set_string_text` (`0x0098eaf4`).
  Recover the 1.004 list-patch call path from those two and read the `SetText`
  target off the actual call site — the same call-edge method that settled the
  piano event quartet.
- **`static_construct_object`.** 1.005 `0x00b1eb58`. The frame-independent
  prologue prefix still matches 111 sites; all three exact-frame candidates
  (`0x01304f34`, `0x01635044`, `0x0236a320`) have only one caller each, which is
  disqualifying for the central UObject factory. The callees of the confirmed
  `create_package` (`0x047aa91c`) were checked on the theory that `CreatePackage`
  reaches `StaticConstructObject_Internal` via `NewObject<UPackage>`, but none of
  its 19 callees carries the entry shape, so that path is inlined or indirect in
  1.004. *Next experiment:* identify `StaticAllocateObject` first — the callee
  `StaticConstructObject_Internal` invokes before running the class constructor —
  then take its caller with the one-parameter `FStaticConstructObjectParameters`
  ABI (`sizeof 0x40`, Class at `+0`, Outer at `+8`, Name at `+0x10`).
  `FUN_140e79dc4` was the `StaticAllocateObject` guess from the `create_package`
  callee list but has only 6 callers, so it must be verified or rejected before
  that chain is trusted.

## Corrected build attribution in `analysis/ChartEventAbiGhidra.txt`

That retained Ghidra export is cited as catalog evidence for the 1.005
`piano_event_link` entry, and its header named only `PROGRAM ff7rebirth_.exe`.
Every RVA in it is 1.005, but the 1.004 Ghidra project opened for this pass
carries the **same** program name, so the header was not build-identifying:
anyone replaying those addresses against the live bridge during a 1.004 session
would have read unrelated code at each one. The header now states the game
version, catalog build id and PE timestamp explicitly and points at this note for
the 1.004 counterparts. No data line in that file was altered.

## Known defects in the catalog carried by this note

- The 1.005 catalog signature for `weak_object_resolver` is the same 16-byte run
  that collides two ways in 1.004. It should be re-measured against the 1.005
  image and extended. Out of scope here: this note must not perturb the 1.005
  generated tree.
- The catalog evidence prose for `scoreinfo_progress_source_return` and
  `scoreinfo_rank_text_return` names the wrong containing function, in **both**
  builds. The RVAs are correct.
