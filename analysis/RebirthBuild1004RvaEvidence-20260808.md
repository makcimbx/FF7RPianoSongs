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

## Known defects in the catalog carried by this note

- The 1.005 catalog signature for `weak_object_resolver` is the same 16-byte run
  that collides two ways in 1.004. It should be re-measured against the 1.005
  image and extended. Out of scope here: this note must not perturb the 1.005
  generated tree.
- The catalog evidence prose for `scoreinfo_progress_source_return` and
  `scoreinfo_rank_text_return` names the wrong containing function, in **both**
  builds. The RVAs are correct.
