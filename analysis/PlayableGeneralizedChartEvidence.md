# Playable Generalized Chart Runtime Evidence

This file retains the exact 1.005 static evidence and bounded implementation
authority for generalizing the accepted monotone-only extended-chart transaction
to the chart shapes emitted by the MIDI generator: monotone, chord, ungrouped
dual-hand, chord IgnoreSound, RH-only run grouping, prefix-to-tail group crossings,
equal-time grouped monotones, and run-local GroupIndex reuse.

It supersedes the “future work” disposition for those shapes in
[PlayableMultiTailEvidence.md](PlayableMultiTailEvidence.md), but only under the
executable, policy, helper, validation, ownership, and rollback gates below. It is
not runtime qualification, a release claim, authority for another executable, or
authority to launch the game. Camera cues, nonzero strength, nonzero dot behavior,
and generator-produced grouped-dual rows remain excluded.

## Evidence Classes, Checkpoint, And Executable Identity

Repository source checkpoint used for this investigation:

```text
e8335860e62d1589135000d5d94f111245b16348
```

All native addresses, bytes, ABIs, layouts, and implementation authority in this
file are scoped to exactly:

```text
catalog build id: ff7rebirth-steam-win64-6a16ced2
game version:     1.005
PE timestamp:     0x6a16ced2
SizeOfImage:      0x099d9000
PE checksum:      0x0769ea6e
file size:        124317952
SHA-256:          752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc
image base:       0x140000000
Ghidra program:   ff7rebirth_.exe-9457d4
```

Evidence is classified as follows:

- **Direct static evidence** is matching-image disassembly/decompilation or exact
  executable-byte search. The current follow-up queried the existing Ghidra
  program read-only with `auto_analyze=false`; it did not modify Ghidra state.
- **Retained static evidence** is the matching-build export in
  [ChartEventAbiGhidra.txt](ChartEventAbiGhidra.txt) and the native ownership,
  allocator, and event-layout proof in
  [Playable513ReserveEvidence.md](Playable513ReserveEvidence.md) and
  [PlayableMultiTailEvidence.md](PlayableMultiTailEvidence.md).
- **Runtime evidence** is limited to immutable accepted sessions
  `20260902T175102Z-a761305b6d59` and
  `20260902T202554Z-ba1ee1e9374f`. They qualify one monotone event per source row
  at exact event totals 513 and 8192. They do not qualify the new shapes here.
- **Source-derived evidence** is the descriptor, chart-patch, compiler, MIDI, and
  format-14 behavior at the named checkpoint. It is not executable identity.
- **Bounded implementation decision** is an architecture authorized by the direct
  and retained evidence, but not yet observed in game.
- Existing `rva_catalog.json` entries are repository migration records unless a
  retained evidence file independently recovers their semantics. This file does
  not promote catalog presence alone into native proof.

No 1.004 helper address, signature, ABI, side layout, parser behavior, or
generalized runtime authority is derived here. Generalized admission on 1.004 must
fail closed.

## Catalog-Ready IgnoreSound Helper Evidence

### Identity and exact signature

`FUN_1407FDAA4` is the native ordered-unique qword insertion helper used by the
piano parser for chord IgnoreSound values:

```text
VA:            0x1407FDAA4
RVA:           0x007FDAA4
function end:  0x1407FDB2E (exclusive)
body length:   138 bytes
parser call:   0x1439B3A7F / RVA 0x039B3A7F
call bytes:    E8 20 A0 E4 FC
return:        0x1439B3A84 / RVA 0x039B3A84
```

The following entry-anchored signature is machine-usable for a 1.005
`rva_catalog.json` signature record. The mask has exactly 138 `x` characters, one
per byte:

```json
{
  "id": "piano_event_ignore_sound_insert",
  "rva": "0x007fdaa4",
  "signature": {
    "bytes": "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 b8 20 00 00 00 e8 c2 e0 96 01 48 2b e0 48 8b 01 48 8b f2 48 63 79 08 48 8b d9 48 8d 14 f8 48 3b c2 74 1f 48 8b 0e 48 39 08 74 0b 48 83 c0 08 48 3b c2 75 f2 eb 0c 48 2b 03 48 c1 f8 03 83 f8 ff 75 17 8d 4f 01 89 4b 08 3b 4b 0c 7f 21 48 8b 13 8b c7 48 8b 0e 48 89 0c fa 48 8b 5c 24 30 48 8b 6c 24 38 48 8b 74 24 40 48 83 c4 20 5f c3 8b d7 48 8b cb e8 54 c2 f5 ff eb d3",
    "mask": "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  }
}
```

Read-only image-wide exact-byte search returned one match, at `0x1407FDAA4`.
A short 21-byte entry prologue was highly nonunique, and a 52-byte body sequence
still matched twice; implementations must not shorten this exact-build signature
without independently remeasuring uniqueness. Ghidra reported 207 total xrefs to
the generic helper: 206 unconditional calls and one data xref. The relevant piano
xref is exactly `FUN_1439B3608+0x47F` at `0x1439B3A7F`.

### ABI and semantics

Microsoft x64 ABI:

```cpp
uint32_t insert_ordered_unique_qword(
    QwordVectorHeader* header,       // RCX
    const uint64_t* value);          // RDX
```

The returned value is the existing or newly appended zero-based index. Only the
index-sized low result is semantically relevant.

Header layout:

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | `uint64_t*` | Owned data pointer. |
| `+0x08` | `int32_t` | Element count. |
| `+0x0C` | `int32_t` | Element capacity. |

Recovered normal-return behavior:

1. Scan existing qwords in index order.
2. If equal, return the existing index without changing the header.
3. Otherwise retain `old_count`, publish `count=old_count+1`, and compare with
   capacity.
4. If growth is required, call `FUN_140759D80(header, old_count)`.
5. Write the qword at `header->data[old_count]`.
6. Return `old_count`.

This preserves first-occurrence order and deduplicates later equal values. The
piano parser calls it up to three times on the chord temporary's owned vector
before deep-copying that event into compact chart storage.

### Allocation ownership and cleanup boundary

The insertion helper owns no independent allocation; any grown data belongs to
the vector header passed in RCX. For a direct tail event that header is
`event+0x38`. `FUN_143988A04` / RVA `0x03988A04` destroys callback state and frees
that owned vector. Therefore normal partial construction is recoverable:

- zero the final off-count event slot first;
- call the event constructor, which initializes the IgnoreSound header empty;
- register the event as destructor-owned before the first insertion;
- after one, two, or three successful insertions, any later normal predicate
  failure is cleaned by one event-destructor call;
- reverse destruction of multiple constructed tails frees each vector exactly
  once.

There is no ordinary insertion-failure return. Count is incremented before native
growth. This evidence does **not** claim that OOM, fatal allocator handling, C++
exception, arbitrary interruption, or SEH during `FUN_140759D80` leaves a state
from which the mod may continue or invoke cleanup. An allocator/SEH fault is not a
transaction rollback path.

## Event, Hand, Constructor, And Callback Contract

### Exact helper inventory

| Entity | RVA | Bounded role |
| --- | ---: | --- |
| `piano_score_expand`, `FUN_1439B9104` | `0x039B9104` | Existing outer expansion hook. |
| parser, `FUN_1439B3608` | `0x039B3608` | Parses 512 source rows into a compact event vector. |
| strict FName Find, `FUN_140B517C4` | `0x00B517C4` | Resolve only; unknown names remain zero. |
| event constructor, `FUN_143987734` | `0x03987734` | Initialize one `0x90` event and resolve assignment. |
| event deep copy, `FUN_143987624` | `0x03987624` | Native parser prefix only; not needed for direct tails. |
| callback builder, `FUN_143982EA0` | `0x03982EA0` | Build inline result callback state. |
| result callback, `FUN_143991F24` | `0x03991F24` | Native result trampoline. |
| IgnoreSound insertion, `FUN_1407FDAA4` | `0x007FDAA4` | Ordered-unique append to owned qword vector. |
| event link, `FUN_14398DB4C` | `0x0398DB4C` | Prepend a child to one run root. |
| event destructor, `FUN_143988A04` | `0x03988A04` | Destroy callback and free IgnoreSound. |
| event reserve, `FUN_142AD3EF4` | `0x02AD3EF4` | Reserve final `0x90`-byte event allocation. |
| raw event growth, `FUN_142913540` | `0x02913540` | Parser append growth; forbidden after successful final reserve. |
| native note count, `FUN_1439A0744` | `0x039A0744` | Count parentless events. |
| chart update, `FUN_1439BA628` | `0x039BA628` | Traverse all compact native events. |

The constructor ABI is:

```cpp
Event* construct_event(
    Event* destination,
    Chart* chart,
    void* side,
    uint32_t ordinal,
    float time,
    float strength,
    uint8_t note_type,
    uint8_t dot_type,
    uint64_t packed_fname);
```

Relevant event layout remains:

```text
+0x00 chart                 +0x08 side
+0x10 root/parent           +0x18 root-child-head / child-next
+0x20 parser ordinal        +0x24 time
+0x28 strength              +0x2C packed FName
+0x38 data                  +0x40 IgnoreSound count
+0x44 IgnoreSound capacity  +0x48 assignment (8 = unresolved)
+0x49 assignment selector   +0x4A result/state
+0x4B NoteType              +0x4C DotType
+0x4D..+0x4F runtime state  +0x50..+0x8F callback state
```

For zero-based source row `r`, compact event plans are emitted in this exact
order:

| Hand | Presence | Side | Ordinal | Identity |
| --- | --- | --- | ---: | --- |
| Monotone/RH | nonempty `monotone_id` | `*(chart_row+0x18)` | `2*r` | strictly resolved monotone FName |
| Chord/LH | nonempty `chord_id` | `*(chart_row+0x10)` | `2*r+1` | strictly resolved chord FName |

The parser constructs the monotone temporary first, then the chord temporary,
and compact-appends those that are present in the same order. An empty hand still
consumes its ordinal position. One source row may therefore generate one or two
compact events, and compact event index is not source row or ordinal.

### Authorized direct-tail recipe

For every planned tail event:

1. Before native mutation, validate checked source row, hand, ordinal, time,
   NoteType, DotType, group policy, and strict nonzero packed FName.
2. Select only the exact native side above and retain it transaction-locally.
3. Zero the final stable off-count `0x90`-byte slot.
4. Call `FUN_143987734` with native strength `0.0f`.
5. Immediately mark the slot destructor-owned in private transaction state.
6. Validate returned pointer, chart, side, ordinal, time, strength, FName,
   NoteType/DotType, null links, initial runtime state, assignment `+0x48 != 8`,
   and assignment selector `+0x49` in `{0,1}`. The constructor, not the mod,
   selects assignment bytes through `FUN_143986324` then `FUN_14398639C`.
7. For a chord, insert and validate the expected IgnoreSound vector as described
   below. Monotones must retain an empty vector.
8. With `Chart* captured=chart`, call
   `FUN_143982EA0(event+0x50,&captured)` only after all other owned construction
   succeeds. Callback publication is last.
9. Validate callback function `module+0x03991F24`, zero external state, callback
   vtable `module+0x06433078`, and captured chart.
10. Do not link any event until every tail event and the complete prefix mapping
    have validated.

Direct construction is authorized because destinations are in the final reserved
allocation, remain outside native count, are zeroed first, and are individually
tracked for reverse destruction. Tail construction does not call the native deep
copy helper.

### IgnoreSound qword equivalence, ordering, and validation

`chart_patch.cpp` resolves `monotone_id`, `chord_id`, and each nonempty
`ignore_sound_id` through the descriptor chart resolver and stores each result as
a packed `uint64_t`. For IgnoreSound, the exact source checkpoint writes those
qwords into `OwnedDescriptorChartArrays::ignore_sound_ids` in source slot order.

The matching parser then:

1. queries slot presence at `0x1439B3A51`;
2. fetches that slot's qword at `0x1439B3A68`;
3. copies it unchanged into a local qword;
4. passes its address to `FUN_1407FDAA4` at `0x1439B3A7F`.

This directly establishes conversion equivalence: a tail IgnoreSound string is
resolved through the same strict packed-FName path used by `chart_patch`; no
hashing, hand translation, index conversion, or FName insertion is added.

Process slots 0, 1, and 2 in that order, skipping empty strings. The native helper
deduplicates while preserving first occurrence. The generalized descriptor/cache
policy should reject duplicate nonempty IgnoreSound IDs, as the current compiler
does. Runtime postvalidation must nevertheless compare the event's vector with
the exact ordered-unique packed-qword projection, including pointer/count/capacity
coherence. A chord with no IgnoreSound retains the constructor's empty header.

## Parser Group Suppression And Full Relinking Authority

`piano_event_link`, `FUN_14398DB4C`, is already independently retained at RVA
`0x0398DB4C`. Its catalog-ready exact 1.005 signature is:

```json
{
  "id": "piano_event_link",
  "rva": "0x0398db4c",
  "signature": {
    "bytes": "48 85 d2 74 15 48 89 4a 10 48 8b 41 18 48 89 51",
    "mask": "xxxxxxxxxxxxxxxx"
  }
}
```

ABI and effect:

```cpp
void link_event(Event* root, Event* child);

child+0x10 = root;
child+0x18 = root->old_child_head;
root+0x18  = child;
```

### Exact native run algorithm

The parser tracks previous GroupIndex at chart `+0xA3` and one run-local root:

1. Group zero clears the root.
2. A nonzero ID after zero, or a nonzero ID different from `chart+0xA3`, starts a
   new run.
3. The new run root is the current monotone event when present, otherwise the
   current chord event. No event on that first row is linked.
4. When the current nonzero ID equals the immediately preceding ID, call link for
   the monotone event if present, then for the chord event if present.
5. Store the current ID to chart `+0xA3` after processing the row.

Because link prepends, a continuation dual row produces a chain whose immediate
order is chord then monotone then the old child chain. Grouped-dual semantics are
therefore understood but remain excluded from the first generated-MIDI policy.
That policy authorizes groups only when every row in the run is RH-only monotone;
chord-only and dual-hand rows must use GroupIndex zero.

### Suppression and reconstruction strategy

The parser's event-vector growth performs raw relocation and does not rebase group
pointers. A 512-row prefix may emit up to 1024 events. Generalized implementation
is authorized to avoid that hazard as follows:

1. Derive the complete row/event/group plan before native mutation.
2. Patch every native prefix GroupIndex to zero while preserving immutable source
   group IDs in the transaction plan.
3. Intercept the parser's one pristine request for 512 events and reserve final
   total event count `E`; require no later event growth.
4. Let the parser return an unlinked compact prefix of exactly `P` events.
5. Map and validate prefix events by compact plan order, ordinal, side, FName, and
   ownership fields rather than by source-row index.
6. Construct and validate every tail event in final off-count slots.
7. Apply one full source-order group plan over stable prefix and tail indices.

This strategy is implementation-authorized under the exact gates in this file.
It replaces the older design that retained parser-built prefix links. It supports
runs wholly in the prefix, wholly in the tail, and runs whose root is in row 511
and whose first tail child is in row 512.

Equal `TimeStr` values are valid for adjacent rows: the grouping algorithm depends
on source order and adjacent GroupIndex, not a strict time increase. The runtime
plan must preserve stable generator order and require nondecreasing decoded times.
Several same-time RH monotones may therefore become separate events in one RH-only
run and one parentless required action. This is a game-mechanic approximation of
polyphonic RH MIDI, not a claim of multiple independent simultaneous RH inputs.

Group IDs are run-local bytes. IDs 1 through 255 may cycle indefinitely when the
next intended run is separated by zero or uses a different adjacent ID. Reusing
the same ID on adjacent rows continues the current run; it does not create a new
one. Global historical uniqueness is not a native invariant.

### Link journal and rollback

Before the first link, snapshot chart `+0xA3`. For each planned link, immediately
before calling the helper, append a journal record containing:

```text
root index and prior root+0x18
child index and prior child+0x10
child index and prior child+0x18
```

All addresses are derived from checked indices only after final allocation
identity is known. Prefix links must initially be null because parser grouping was
suppressed; tails must be constructor-null. Repeated mutations of one root are
journaled separately, so reverse replay restores the exact prior chain.

Normal rollback order is:

1. If count-last occurred and exact synchronous identity still holds, restore
   native count from `E` to `P` first.
2. Restore chart maximum time if it changed.
3. Replay the link journal in reverse, restoring child and root fields.
4. Restore chart `+0xA3`.
5. Reverse-destruct every privately registered tail and zero each slot.
6. Leave the coherent enlarged allocation, prefix count `P`, and parser-owned
   prefix events to native lifecycle.

After successful transaction publication and outer-detour return, all links and
all counted events are native-owned. No asynchronous unlink, tail destruction,
count repair, or native-pointer dereference is authorized.

## Four-Count Contract

For complete compiled source rows, define:

```text
R = source row count
P = compact native events generated by source rows [0, min(R,512))
E = compact native events generated by all R rows
A = events that remain parentless after the planned group links
T = E - P
```

Each nonempty monotone contributes one event and each nonempty chord contributes
one event. Every accepted row must contain at least one event.

The exact consumer contract is:

| Value | Required use | Evidence |
| --- | --- | --- |
| `R` | Serialization, source identity, diagnostics, row ordinals | Format-14/source model and parser ordinal construction. |
| `P` | Required parser return count and rollback count | Parser compact-appends only present hand events. |
| `E` | Reserve target, final chart `+0x88`, allocation bounds | Parser/vector ABI and dynamic `FUN_1439BA628` traversal. |
| `A` | Native/menu/list note count and expected result total | `FUN_1439A0744` counts only `event+0x10==0`; caller `FUN_1439A84E4` publishes it to list/detail UI. |

`FUN_1439BA628` traverses all `E` events twice and bases completion on event state.
`FUN_1439A6758` terminalizes one event once and, for recovered terminal states 5
and 6, propagates child state through the root's `+0x18` chain. Native results
read four `uint16_t` counters and call score function `FUN_1439917F4`; they do not
consume `R`, `P`, or `E` directly.

The expected result-counter sum is `A`, not `R` or `E`. That follows strongly from
the parentless note-count contract and root-to-child terminal propagation, but the
exact counter-increment site was not recovered. `result_sum==A` remains a required
runtime observation, not a static proof.

The existing runtime field named `note_count` cannot continue to mean both event
count and displayed action count. A generalized descriptor/committed transaction
must retain `R`, `P`, `E`, and `A` separately. If a compatibility `note_count`
field remains, it may mean only `A`. Reads of chart `+0x88` are event-count reads
and must be named, bounded, and compared as `E`.

## Event And Source Limits

The bounded generalized policy is:

```text
1 <= R <= 8192
512 <= P <= 1024 when R >= 512
1 <= E <= 8192
1 <= A <= E
T = E - P
event allocation bytes = E * 0x90, checked in 64-bit arithmetic
monotone ordinal = 2*r
chord ordinal = 2*r+1
```

Extended admission additionally requires `R>512` and `E>P`. All-dual input has
`E=2R`, so its maximum source-row count is 4096. All-single-hand input may use all
8192 retained rows. Mixed input is admitted by exact derived `E`, not by a blanket
4096-row ceiling. At `R=8192`, the largest possible admitted chord ordinal is
16383, safely representable in the event's `uint32_t` ordinal field.

Capacity is selected from `E`, never `R` or `A`. The accepted maximum allocation
remains:

```text
E:                 8192
raw/helper bytes:  0x120000 / 1,179,648
stored capacity:   8192
```

The parser's native request remains exactly 512 at call RVA `0x039B36B3`; the
shared reserve detour substitutes `E` once under the existing caller, TLS,
header, transaction, and one-hit gates. On return the parser must have produced
exactly `P`, and native raw growth must not have executed.

## Format 14 And Policy Boundary

Runtime-cache format 14 already stores counted source/config rows, the compiled
512-row prefix, counted retained source/compiled tails, and all fields needed to
derive the generalized plan: monotone, chord, three IgnoreSound IDs, NoteType,
DotType, camera, GroupIndex, and `TimeStr`. Its counts are 32-bit and its current
defensive row bound is 8192. Equal-time rows are representable as separate ordered
rows with identical strings.

Therefore `R`, `P`, `E`, `A`, compact event plans, and link plans can be derived
after decode without a binary-layout change. Format 14 need not become format 15
unless implementation chooses to serialize new counts or plans. Use one canonical
derivation in compiler validation, cache semantic validation, descriptor
construction, and runtime admission; do not maintain independent formulas.

Generalized mutation requires a new policy identity and generation. One suitable
identity is:

```text
chart_rows=native512+generated;events<=8192;hands=mono,chord,dual;ignore=verified;groups=rh-runlocal;build=1005
```

The exact final string is an Integration decision, but it must identify the
event-based bound and generalized shape. The existing monotone-only identity must
not authorize this path. Old extended caches consequently fail identity/generation
validation. MIDI generator semantics may use a MIDI-scoped cache-key component;
ordinary unchanged 512-row authored caches do not require blind global
invalidation or a format bump.

### Exact model and derivation envelope

No new serialized native pointer or flattened event object is authorized. The
coherent Integration field split is:

| Owner | Required durable/immutable fields or derivation |
| --- | --- |
| `Note` / `ChartNote` | Existing ordered row fields remain the canonical source: monotone, chord, IgnoreSound slots, GroupIndex, time, NoteType, DotType, and camera. No hand-event serialization is required. |
| `DiagnosticChartRetention` | Existing `source_row_count`, `native_prefix_row_count`, descriptor hash, and counted source/compiled tail rows remain sufficient. The prefix count here continues to mean **rows**, not `P`. |
| `DifficultyProfileDiagnostics` | Add or derive distinctly named native-prefix event count `P`, total native-event count `E`, and parentless required-action count `A`; do not overload existing row/action scheduling fields. |
| `SongDifficultyProfile` | Retain `source_row_count`, `native_prefix_event_count`, `native_event_count`, and `required_action_count`. Existing `chart_notes` and `extended_chart_tail_notes` remain ordered row vectors. Compatibility `note_count`, if retained, equals `A`. |
| `LoadedSong` / policy snapshot | Keep accepted/published source-row limits, add or derive the 8192 native-event cap, and require exact generalized policy identity/generation. |
| Immutable event plan | For each compact event: source row, hand, compact index, checked ordinal, time string/decoded time, side kind, packed FName, NoteType/DotType/strength, ordered IgnoreSound qwords, and GroupIndex. No native address. |
| Immutable link plan | Checked root and child compact indices in exact application order, plus final GroupIndex byte. No native address. |
| Transaction / committed token | Transaction-local state holds `R/P/E/A`, plans, native pointers, construction registry, and rollback journal. The committed token retains only `R/P/E/A`, hashes, identities, and generations. |
| Manifest | Render source rows `R`, prefix events `P`, total events `E`, parentless actions `A`, and tail events `T` separately so evidence cannot conflate them. |

One canonical pure derivation must feed compiler admission, cache semantic
validation, descriptor construction, manifest rendering, runtime preflight, and
tests. MIDI generation must replace the one-row-per-frame map with stable ordered
multi-row storage, recycle GroupIndex by adjacent run semantics, and remove the
diagnostic-fixture-only retention gate only when the generalized policy is active.
The first policy rejects empty rows, grouped chord/dual rows, camera, nonzero
strength, and nonzero dot before cache publication.

## Fail-Closed Generalized Transaction

### Offline and immutable admission

Before native mutation:

1. Rebuild the complete source rows from prefix and retained tail and require
   exact format-14 semantic/hash agreement.
2. Require the new exact policy identity/generation and the verified 1.005
   executable.
3. Derive one immutable checked event/link plan and exact `R/P/E/A`.
4. Enforce the event/source bounds and RH-only group policy.
5. Require camera zero, strength zero, supported NoteType, and DotType zero.
6. Strictly resolve every event and IgnoreSound FName and retain only packed
   values, indices, times, fields, and immutable descriptor identity.
7. Verify constructor, destructor, callback, reserve, FName, event-link, and the
   full 138-byte IgnoreSound helper signatures before admitting generalized
   mutation.

### Synchronous native transaction

1. Require the accepted exact persistent caller return RVA `0x03999F53`, guarded
   selection/audio identity, depth-one original-inflight TLS, exclusive global
   claim, and nonterminal activation generation.
2. Validate chart, chart row, `chart+0x80` header, chart `+0x118` controller,
   reciprocal `controller+0xF48==chart`, and nonnull control block at `+0xF50`.
3. Publish transaction-local `R/P/E/A`, immutable plan, generations, expected
   header, and one-hit reserve state. Native pointers remain synchronous only.
4. Patch the first 512 source rows with descriptor data but GroupIndex zero.
5. At exact parser reserve return RVA `0x039B36B8`, require the pristine header and
   native request 512, substitute `E` once, and validate returned allocation and
   capacity. All nonmatching calls forward unchanged.
6. Run the original parser.
7. Revalidate every identity, the returned allocation, `count==P`, capacity,
   writable extent, parser FPS/max time, and all `P` prefix events against the
   compact immutable plan. Require no links because grouping was suppressed.
8. Recompute all tail times with the live post-parser FPS. Require global
   nondecreasing compact-plan time.
9. Construct, IgnoreSound-populate, callback-build, and validate all `T` tails in
   final slots while native count remains `P`.
10. Apply and validate the full group-link journal; set and validate final
    chart `+0xA3`.
11. Write and validate full maximum event time.
12. Revalidate transaction/terminal/header/allocation/prefix/tails/links.
13. Write native event count `E` last and re-read it.
14. Publish the immutable pending committed token under the existing terminal
    generation serialization before returning.

### Normal rollback and lifecycle

Before count-last, a normal failure restores maximum/link/group state, then
reverse-destructs registered tails and leaves native count `P`. After count-last
but before outer-detour return, rollback is authorized only while exact synchronous
identity can be re-proved: restore count to `P` first, then maximum, links,
`chart+0xA3`, and tails in the order specified above.

If identity has drifted, do not guess, free, unlink, or asynchronously repair.
Block custom publication and preserve native ownership evidence. Native
allocator/SEH faults are not normal rollback.

After successful detour return, native code owns the allocation, all `E` events,
all IgnoreSound allocations, callbacks, and links. The next parse or chart/shared
control-block destruction tears down exactly the counted native vector. Song
replacement, terminal events, list exit, and shutdown may invalidate only
mod-owned immutable publication state.

The committed token may retain:

```text
R, P, E, A
registry storage and exact song/profile identity
registry, policy, selection, route, lease, lifecycle, and activation generations
song key, preparation ordinal, descriptor hash, pending/active state
```

It must not retain or later dereference controller, control block, chart,
chart-row, side, vector header, allocation, event, callback, IgnoreSound, root, or
child pointers.

## Representative Single-Session Qualification Contract

One deterministic maximum profile can cover every newly authorized first-policy
shape:

```text
R source rows:          4099
dual-hand rows:         4093
monotone-only rows:        4
chord-only rows:           2
E native events:        4093*2 + 4 + 2 = 8192
P prefix events:        510*2 + 2 = 1022
T tail events:          8192 - 1022 = 7170
grouped child events:      2
A parentless actions:   8192 - 2 = 8190
```

Exact shape:

- chord-only rows 100 and 600;
- monotone-only rows 511, 512, 2000, and 2001;
- all other rows dual-hand and ungrouped;
- rows 511 and 512 use GroupIndex 1, proving a prefix-root to tail-child link;
- rows 2000 and 2001 reuse GroupIndex 1 after zero-group separators and share
  equal `TimeStr`, proving run-local ID reuse and equal-time RH grouping;
- all other rows use GroupIndex zero;
- tail chord-bearing rows include ordered IgnoreSound cardinalities one, two,
  and three, for example dual rows 700/701 and chord-only row 600;
- camera, strength, and dot remain zero.

The arithmetic is exact: the first 512 rows contain 510 dual rows plus two
single-hand rows, hence `P=1022`. The remaining 3587 rows contain 3583 dual rows
plus four single-hand rows, hence `T=7170`. Each two-row RH-only run contributes
one linked child, so `A=8190`.

A single reviewed, human-controlled runtime session must establish:

1. exact pre/reserve/post authority, one `512->8192` reserve substitution, and
   capacity 8192;
2. parser prefix `P=1022`, tail construction `T=7170`, final `E=8192`, and no raw
   growth;
3. monotone-only, chord-only, and dual-hand tail field/callback correctness;
4. one-, two-, and three-entry tail IgnoreSound ownership and validation;
5. prefix-tail link crossing, equal-time run, and GroupIndex-1 reuse;
6. displayed/list/action count 8190, result-counter sum 8190, and natural
   completion across all 8192 events;
7. replacement by an ordinary 481-event song, old immutable-token invalidation,
   native destruction, replacement playback, and clean exit.

No required tester delay or sleep is permitted. The exact readiness/admission
predicate either admits the activation or follows the recoverable native path.

## Resolved Research Blockers And Remaining Nonclaims

### Resolved by this evidence

- Chord and dual-hand compact order, side selection, and ordinals are directly
  recovered.
- Chord direct construction uses the already-qualified constructor/destructor and
  callback lifecycle.
- IgnoreSound string-to-qword equivalence, source order, native ordered-unique
  insertion, ownership, destructor cleanup, exact helper RVA, parser callsite,
  xrefs, and unique 138-byte 1.005 signature are established.
- Native group-run/root/link/prepend semantics and run-local byte reuse are
  established.
- Suppressing parser prefix groups and rebuilding one full stable-allocation link
  plan is implementation-authorized under exact validation and rollback gates.
- `R/P/E/A` count semantics and event-based capacity policy are established.
- Format 14 can carry and deterministically re-derive the generalized plan without
  a global cache-format invalidation.

These findings explicitly authorize a coherent Integration implementation for
the bounded verified-1.005 policy described here. They do not authorize a game
run, installation, or release before protected-boundary review and the canonical
human-controlled runtime gate.

### Implementation prerequisites, not new research blockers

- Add the catalog/runtime-spec entry and startup signature check for
  `piano_event_ignore_sound_insert` using the exact bytes above.
- Promote `piano_event_link` from signature-only evidence to a checked callable
  dependency for the generalized transaction.
- Implement one canonical immutable plan and split all row/event/action count
  consumers.
- Change the policy identity/generation, MIDI-scoped cache semantics, equal-frame
  storage, and compiler run-local group validator without changing format 14.
- Preserve first-failure diagnostics for helper, count, prefix-map, construction,
  IgnoreSound, link, rollback, and publication stages.

### Runtime-only gaps and nonclaims

- No chord, dual-hand, IgnoreSound, link, equal-time group, crossing group, or
  generalized replacement has yet run in the game.
- `result_counter_sum==A` is a required runtime observation; the exact counter
  increment instruction remains unrecovered.
- No recovery from OOM, fatal allocator behavior, C++ exception, arbitrary helper
  interruption, or SEH is claimed.
- No cross-thread quiescence beyond repeated synchronous identity checks is
  proved.
- Grouped-dual native semantics are described but generator production remains
  excluded; RH-only runs are the first policy.
- Camera-tail ownership, nonzero strength source/semantics, and nonzero DotType
  gameplay remain excluded.
- Assignment success for every generated FName domain remains a runtime gate;
  unresolved assignment sentinel 8 always fails closed.
- No capacity above 8192, source row above 8192, event count above 8192, 1.004
  compatibility, asynchronous native rollback, exact list-exit destruction
  instruction/thread, or release readiness is claimed.

### Precisely deferred fields

| Field | Why it remains outside this authorization |
| --- | --- |
| Camera | Camera events use the separate chart vector at `chart+0x70`. Tail append identity, relocation, element ownership, rollback, and native destruction are not recovered as one complete transaction. |
| Strength | The constructor parameter position is known, but the MIDI/source mapping and native update/scoring meaning of nonzero strength are not established. Generalized tails must pass `0.0f` and validate it. |
| DotType | Constructor storage at event `+0x4C` is known, but nonzero-dot timing, input, scoring, and completion behavior are not runtime-qualified. The first policy requires zero even though the field can be constructed. |
