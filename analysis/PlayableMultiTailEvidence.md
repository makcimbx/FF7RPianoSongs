# Playable Multi-Tail Scoped-Reserve Evidence

This file retains the bounded design evidence for extending the verified 1.005
row-513 scoped-reserve path to more than one native tail event. It is an
engineering evidence record, not a product contract, implementation, release
claim, or authorization to expose an arbitrary chart limit.

The recovered allocator evidence supports both the small- and large-allocation
branches. Request 910 is only the final small-pool request, not a native maximum.
The lead Integration decision retained here is a generic, explicitly bounded
513-through-8192 monotone-only substrate. The value 8192 is a product
resource/abuse policy and qualification boundary, not a theoretical allocator
or engine maximum.

## Evidence Classes And Identities

### Source identities

```text
multi-tail research baseline: 485c093b85e687a442d6a1f67977e5a2c361ee2a
large-branch retention baseline: 198ff82629cf20a255ca18151eab64d8c3a6d0e3
accepted row-513 checkpoint: 88f1f99ae4a6d52c88c07aaa4f8736384c29beb0
```

### Executable identity

Every native address and ABI in this file is scoped to this exact executable:

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

No 1.004 address, helper ABI, allocator result, or caller layout is derived by
this investigation. A 1.004 executable must fail closed before extended-chart
admission or reserve substitution.

### Accepted row-513 runtime evidence

```text
session:       20260902T175102Z-a761305b6d59
disposition:   Accepted / Development Keep
ASI SHA-256:   835bc4282fd20387b7759024352e3312712d1c7cb2b21651c1cb9e6b2eead945
log SHA-256:   35d22e441c489a0f2c0464de751709ad5c1067bd3346e57ceda19ee12922b9f0
game SHA-256:  752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc
```

The immutable session proved one exact `512 -> 513` reserve substitution, actual
capacity 540, a 512-event native prefix, one constructed tail, count-last 513,
exact playback promotion, a 513-event score total, natural completion,
replacement by an ordinary 290-event song, and clean teardown. It did not prove
multiple tails or another requested capacity.

### Classification

- **Direct static evidence**: matching-build disassembly/decompilation and the
  retained [ChartEventAbiGhidra.txt](ChartEventAbiGhidra.txt), including helper
  ABIs, field offsets, parser ordering, constructor/destructor behavior, and
  native shared ownership.
- **Runtime evidence**: only values and ordering observed in the immutable
  accepted row-513 session above.
- **Reference-source corroboration**: the local Unreal `FMallocBinned3` and
  `FSizeTableEntry::FillSizeTable` sources explain the runtime-initialized size
  tables used by the recovered helper. Those sources are tooling evidence, not
  executable identity by themselves.
- **Bounded inference**: product safety and workload conclusions combine direct
  helper/native-consumer evidence with checked arithmetic. Capacities above the
  accepted 513 request remain runtime-unvalidated where stated.
- Catalog entries are repository migration records unless this file identifies
  their semantics as independently recovered static evidence.

Primary supporting evidence is
[Playable513ReserveEvidence.md](Playable513ReserveEvidence.md), which owns the
accepted single-tail authority, callback, publication, and native-lifetime proof.

## Event Reserve Helper And Allocation Regimes

### Exact helper contract

`piano_event_vector_reserve`, `FUN_142AD3EF4`:

```text
VA:   0x142AD3EF4
RVA:  0x02AD3EF4
ABI:  void reserve(EventVectorHeader* header, int32 requested_capacity)
RCX:  event-vector header
EDX:  requested element capacity
event size: 0x90 bytes (144 decimal)
```

The parser's exact call is `0x1439B36B3`, returning at `0x1439B36B8`:

```text
call RVA:   0x039B36B3
return RVA: 0x039B36B8
native request: 512 elements
```

The helper:

1. Computes raw bytes as `requested_capacity * 0x90`.
2. Compares the active allocator's quantizer vtable slot with Binned3 target
   `FUN_141993F30` / RVA `0x01993F30`. A different allocator uses its virtual
   quantizer and is outside the calculated Binned3 contract below.
3. For raw bytes at or below `0x20000`, quantizes through the active
   FMallocBinned3 small-pool tables.
4. Above `0x20000`, code at `0x142AD3F82..0x142AD3F9C` aligns the request upward
   with `DAT_148F38238`. Matching-build initializer `FUN_1443EF8EC` writes the
   literal `0x10000` to that global at `0x1443EF9C1`; this 64-KiB granularity is
   direct executable evidence.
5. Divides the quantized byte count by `0x90` to obtain the stored vector
   capacity. If the signed low-32-bit result would be less than the requested
   count, it substitutes `INT32_MAX`; it never deliberately publishes a smaller
   positive capacity.
6. Writes the stored capacity at header `+0x0C`, recomputes allocation bytes as
   `stored_capacity * 0x90`, and reallocates through the active allocator.

The event-vector header is separate from the event allocation. There is no
per-vector header or per-event metadata added to the helper's byte request.
Allocator slab/page metadata is allocator-owned and does not subtract from the
stored event capacity.

### Small-pool quantization evidence

The matching allocator algorithm uses:

```text
minimum alignment:       16 bytes
maximum small-pool size: 0x20000 bytes (128 KiB)
listed classes:          increasing classes through 28,672 bytes
large small-pool classes: 4 KiB increments through 131,072 bytes
```

The executable's size-class arrays are initialized at runtime and are zero in
the static image. The exact helper accesses those arrays using the same
size-to-index and reversed-size-table structure as the retained Unreal reference
source. That reference source corroborates the table-generation algorithm but is
not executable identity. Request 513 is runtime measured; the other small-pool
rows below are calculated pending runtime qualification.

### Recovered large-allocation branch

For the expected Binned3 allocator, requests whose raw bytes exceed `0x20000`
use:

```text
raw_bytes       = int64(requested_capacity) * 0x90
quantized_bytes = align_up(raw_bytes, 0x10000)
stored_capacity = floor(quantized_bytes / 0x90)
allocator_bytes = int64(stored_capacity) * 0x90
```

The specialized Binned3 realloc branch starts at `0x142AD42CC` and compares the
allocator realloc slot with `FUN_141993F90` / RVA `0x01993F90`.

- A fresh allocation calls `FUN_141993A30` / RVA `0x01993A30` at
  `0x142AD4406`.
- The malloc path enforces at least 16-byte alignment, aligns the requested bytes
  to that value, then rounds the OS extent upward to 64 KiB.
- `FUN_140E6CA00` reserves virtual address space and the malloc path commits the
  exact rounded extent with `VirtualAlloc(..., MEM_COMMIT, PAGE_READWRITE)`.
- The returned event pointer is the allocation base. Binned3 metadata is stored
  separately in allocator mapping/hash structures; there is no inline header to
  subtract from vector capacity.
- For an existing large allocation, metadata is resolved under the allocator
  lock with `FUN_141428A14`. An allocation may remain in place only when the new
  request still fits the same OS allocation class. Otherwise the helper allocates
  a replacement, copies `min(old_requested_bytes, new_requested_bytes)` through
  the native memcpy path, and frees the old allocation through
  `FUN_1419932C8` / RVA `0x019932C8`.
- Large free validates allocator canary `0x17EA5678`, retires the separate
  metadata, and releases the allocation through `FUN_140E6B02C`, which calls
  `VirtualFree(base, 0, MEM_RELEASE)`.

Reservation, commit, alignment, or pointer-validation failure reaches native
fatal/OOM helper `FUN_142217240`. No recoverable null return, C++ exception, or
SEH contract was recovered. Allocation failure is therefore not a mod rollback
case and is one reason for an explicit bounded product cap.

### Capacity table

| Request | Raw bytes | Quantized/OS bytes | Stored capacity | Helper bytes | Slack | Evidence |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 513 | `0x12090` / 73,872 | `0x13000` / 77,824 | 540 | `0x12FC0` / 77,760 | 64 | Runtime observed and accepted. |
| 520 | `0x12480` / 74,880 | `0x13000` / 77,824 | 540 | `0x12FC0` / 77,760 | 64 | Calculated; same exact class as accepted 513. |
| 798 | `0x1C0E0` / 114,912 | `0x1D000` / 118,784 | 824 | `0x1CF80` / 118,656 | 128 | Calculated; not runtime-qualified. |
| 910 | `0x1FFE0` / 131,040 | `0x20000` / 131,072 | 910 | `0x1FFE0` / 131,040 | 32 | Calculated final small-pool request. |
| 911 | `0x20070` / 131,184 | `0x30000` / 196,608 | 1365 | `0x2FFD0` / 196,560 | 48 | First large-branch request. |
| 1024 | `0x24000` / 147,456 | `0x30000` / 196,608 | 1365 | `0x2FFD0` / 196,560 | 48 | Large branch. |
| 2048 | `0x48000` / 294,912 | `0x50000` / 327,680 | 2275 | `0x4FFB0` / 327,600 | 80 | Large branch. |
| 4096 | `0x90000` / 589,824 | `0x90000` / 589,824 | 4096 | `0x90000` / 589,824 | 0 | Large branch. |
| 8192 | `0x120000` / 1,179,648 | `0x120000` / 1,179,648 | 8192 | `0x120000` / 1,179,648 | 0 | Large branch. |

For clarity:

- Requests 513 through 540 occupy the same exact 77,824-byte allocation class;
  stored capacity is 540 and `540 * 0x90 == 0x12FC0`, leaving 64 class bytes.
- Requests through 910 remain in the same **small-pool allocator regime** as
  accepted 513, but not necessarily the same size class.
- Request 798 maps to capacity 824 (`824 * 0x90 == 0x1CF80`), leaving 128 class
  bytes. It is a scenario-driven target, not a supported limit.
- Request 910 is only the final small-pool request. Crossing to request 911
  changes allocator regime but does not change the vector ABI or ownership.
- Request 8192 is the lead-selected bounded product safety policy. It is not a
  recovered native maximum and does not authorize a larger request.

Every candidate must require the exact 1.005 executable and record the actual
returned capacity. The generic correctness gate requires
`target_count <= capacity <= 8192`, exact tracked allocation identity, and a
proved writable extent. Large-branch requests can additionally compare against
the direct formula above; the maximum-bound qualification requires exact
`8192 -> 8192`. A mismatched named-fixture capacity fails closed.

### Remote numeric and metadata hazards

The vector request, count, and capacity fields are signed 32-bit. Multiplication
by `0x90` is performed in 64-bit arithmetic. At `INT32_MAX`, the 64-KiB-aligned
quotient has low value `0x80000000`; the helper's signed comparison substitutes
`INT32_MAX` and requests `INT32_MAX * 0x90` bytes. This does not overflow 64-bit
arithmetic, but it is not a usable allocation.

Binned3's recovered large-allocation metadata stores requested bytes and OS
extent in 32-bit fields. The final event request whose rounded OS extent remains
representable below 4 GiB is 29,825,706:

```text
request 29,825,706: raw/helper 0xFFFEFFA0, OS extent 0xFFFF0000
request 29,825,707: raw        0xFFFF0030, OS extent 0x100000000
```

The second request cannot be represented in the recovered OS-size metadata.
This is a remote structural hazard, not a support limit. OOM, native result
counters, per-frame work, duration, and product abuse controls become relevant
many orders of magnitude earlier.

## Lead Integration Decision: Bounded Monotone Substrate

This section is an Integration design decision made from the recovered evidence;
it is not itself a recovered native fact or a runtime acceptance claim.

Implement one generic target-count path with this explicit policy bound:

```text
native parser prefix: 512
minimum target:       513
maximum target:       8192
tail count:           target_count - 512
```

Do not encode exact 520, 798, 1024, or 8192 construction branches. The descriptor,
preflight, reserve transaction, construction loop, rollback, committed token, and
count-facing consumers take one checked immutable target count. A future policy
may lower or raise the cap without redesigning native ownership, but no value
above 8192 is admitted without new evidence and qualification.

For every admitted source row:

1. Exactly one nonempty monotone FName and an empty chord FName.
2. `GroupIndex == 0`.
3. Empty IgnoreSound slots.
4. No camera cue.
5. Native strength `0.0f`.
6. A finite parser-equivalent time.
7. No second native event for the row.

Chord, mixed-row, grouped, camera, and IgnoreSound profiles do not inherit this
authority at any count.

### Generic monotone tail records

The native parser constructs source rows 0 through 511. The extension constructs
source rows 512 through `target_count-1` in their same-index final event slots.

For tail source row `r`:

```text
destination: allocation + r * 0x90
chart:       exact synchronous chart
side:        *(chart_row + 0x18), the parser's monotone side
ordinal:     2 * r
time:        float(seconds) + float(frames) / live chart FPS
strength:    0.0f
NoteType:    immutable descriptor row NoteType
DotType:     immutable descriptor row DotType
FName:       strict FNAME_Find result for the descriptor monotone ID
```

Representative ordinal boundaries are:

```text
row:      512  519  797  1023  8191
ordinal: 1024 1038 1594 2046 16382
```

Each tail must be zeroed before calling `FUN_143987734` / RVA `0x03987734`.
After construction require:

- returned pointer equals the final slot;
- chart, monotone side, ordinal, time, strength, FName, NoteType, and DotType
  exactly match the preflight record;
- group links and IgnoreSound ownership are empty;
- assignment at `+0x48` is not sentinel `8`;
- lookup path at `+0x49` is native-resolved and is `0` or `1`;
- the assignment occurs in the parser-built prefix and the lookup-path behavior
  matches its prefix exemplar;
- runtime state bytes remain initial.

Then construct callback state with `FUN_143982EA0` / RVA `0x03982EA0`, passing a
capture of the exact chart, and require the callback invariants retained in
`Playable513ReserveEvidence.md`. Direct construction in final slots is valid only
because the final allocation already exists and the slots remain outside the
native vector count.

Tail-only pitches or assignment behavior absent from the prefix remain
unqualified. Supporting arbitrary generated-note domains requires separate
assignment-table evidence or focused runtime qualification; it must not be
inferred from a non-sentinel byte alone.

## Multi-Tail Transaction

### Preflight before native mutation

Before acquiring mutation authority:

1. Retain immutable registry storage and exact song/profile identity.
2. Require current playable policy generation and descriptor hash.
3. Require `513 <= target_count <= 8192`, a 512-row prefix, and exactly
   `target_count-512` contiguous retained tail rows.
4. Validate every prefix and tail row against the restricted shape.
5. Check all count, offset, multiplication, and ordinal arithmetic.
6. Strictly resolve all tail FNames without inserting names.
7. Parse and validate all tail times using the parser's operation order; retain
   source strings and recompute with the live FPS before construction.
8. Materialize bounded transaction-local tail specifications before entering the
   original expansion. Allocation/preflight failure takes the native 512 path.

Checked bounds include:

```text
target_count >= 513
target_count <= 8192 by explicit product resource policy
target_count * 0x90 is checked in 64-bit arithmetic
tail_count == target_count - 512
max ordinal == 2 * (target_count - 1) for monotone-only rows
```

At target 8192, the final monotone ordinal is 16382, far below the `uint32_t`
field limit.

### Synchronous authority and final reserve

Reuse the accepted row-513 authority envelope:

- exact persistent score-expand caller, returning at RVA `0x03999F53`;
- exact guarded selection/admission storage, generations, route, lease, song
  key, lifecycle epoch, activation generation, and preparation ordinal;
- depth-one original-inflight TLS and exclusive global claim;
- exact chart, chart row, embedded event header at chart `+0x80`, and monotone
  side at chart-row `+0x18`;
- raw native controller at chart `+0x118`, reciprocal
  `controller+0xF48 == chart`, and nonnull control block at
  `controller+0xF50`;
- exact reserve request 512, exact return RVA `0x039B36B8`, pristine header, and
  one reserve hit.

Substitute the exact final target count once. The reserve must happen before the
parser constructs its prefix. This gives the parser and every tail one final
allocation; no event-vector growth or raw relocation is permitted afterward.

The pristine-header requirement (`data == nullptr`, `count == 0`, `capacity ==
0`) is especially important across the large branch: the admitted parser call
uses the fresh-allocation path, so no existing event is copied or relocated. The
persistent caller remains suspended until the detour finishes. This excludes
same-thread caller replacement but does not prove cross-thread quiescence.

Accordingly, parser-prefix construction, private tail bookkeeping, reverse
destruction, maximum-time update, count-last publication, synchronous rollback,
next-parser reset, and chart/shared-control-block destruction are identical in
the small- and large-allocation regimes.

All nonmatching reserve calls forward their original arguments unchanged.

### Parser prefix and tail construction

After the original score expansion returns, revalidate all synchronous authority
and require:

```text
header address == chart + 0x80
data == the allocation returned by the intercepted reserve
count == 512
capacity == the exact matching-build result for target_count
capacity >= target_count
allocation extent covers capacity * 0x90 bytes
controller/chart/header relation remains exact
all 512 prefix events match immutable descriptor and ABI invariants
```

Keep native header count exactly 512 while constructing tails. Maintain a private
transaction-local `constructed_tail_count`; do not increment the native count for
cleanup bookkeeping.

For each tail:

1. Zero the final slot.
2. Call the native constructor.
3. Mark the slot destructor-owned in private transaction state as soon as the
   constructor has entered the proven destructor-safe state.
4. Validate all constructor fields.
5. Build and validate callback state.
6. Advance only the private constructed count.

After all tails validate, recompute maximum time over prefix and tails. Preserve
the original parser maximum for rollback.

### Reverse destruction and publication

If any tail fails before native count publication:

1. Restore any changed maximum/group/link state.
2. Destruct exactly the privately recorded constructed tails in reverse order
   with `FUN_143988A04` / RVA `0x03988A04`.
3. Zero their slots.
4. Leave native count 512. The coherent larger capacity remains native-owned.

On success:

1. Write and re-read the final maximum time.
2. Revalidate the exact header, allocation, prefix, tails, authority, and terminal
   generation.
3. Write native event count `target_count` **last**.
4. Re-read count, capacity, maximum time, first/last tail, and immutable identity.
5. Publish the pending immutable token under terminal-generation serialization.

If terminal or publication rejection occurs after count-last but before the
detour returns, and synchronous identity remains exact:

1. Restore count to 512 first.
2. Restore maximum/group/link state.
3. Destruct tails in reverse order.
4. Zero the tail slots.

If exact ownership cannot be re-proved, do not destruct, free, or asynchronously
repair native state. Block custom audio/publication and preserve native ownership
for its ordinary teardown. Native allocator faults and arbitrary SEH are not
claimed recoverable.

### Terminal ordering

Terminal recording and committed-token publication must remain serialized by the
same committed-state mutex and monotonic failed-generation watermark:

- failure terminal N before publication causes publication N to reject and the
  synchronous transaction to roll back;
- failure terminal N after publication invalidates only token N;
- stale N cannot block or invalidate N+1;
- successful AudioPublished/ExpandFinished outcomes do not advance the failure
  watermark.

No asynchronous rollback, tester wait, sleep, or polling count is authorized.
Early input either obtains the exact guarded admission or follows the recoverable
native 512 path.

## Native Ownership After Count-Last

After successful detour return, the game owns the allocation and all counted
events:

- The next parser calls `FUN_143997808` / RVA `0x03997808`, destructs exactly the
  current count, resets count, and reserves zero to release storage.
- Chart destructor `FUN_143988A30` / RVA `0x03988A30` destroys callback regions,
  chart state, the event vector through `FUN_143987C18`, and the camera vector.
- The persistent controller owns the chart through its native shared pair at
  controller `+0xF48/+0xF50`; zero strong references invoke chart destruction
  through the control block.

Replacement expansion, abort, list exit, and shutdown invalidate only mod-owned
immutable publication state. They must not inspect, destruct, free, or restore a
previously committed native vector.

The committed token may contain only:

```text
immutable registry storage
exact song and profile pointers within that storage
registry generation
policy generation and policy identity
descriptor hash
target event count
selection generation
route generation
lease generation
song key
route lifecycle epoch
activation generation
preparation ordinal
pending/active publication state
```

It must not retain or later dereference controller, control block, chart,
chart-row, side, vector header, allocation, event, callback, or group-link
pointers.

## Native And Product Limits

### Recovered native representation and workload facts

- Event-vector count and capacity are signed 32-bit fields. The reserve request
  is signed 32-bit and its byte multiplication uses 64-bit arithmetic.
- Event ordinal is `uint32_t`; monotone ordinal `2*row` remains representable for
  every row admitted by the 8192 policy.
- `piano_chart_update`, `FUN_1439BA628` / RVA `0x039BA628`, reads the dynamic
  signed event count, computes `base + count*0x90`, and traverses every event
  twice. This proves dynamic count consumption, not acceptable frame cost.
- Native score calculation `FUN_1439917F4` consumes four `uint16_t` result
  counters and returns a signed weighted result. Target 8192 fits an individual
  counter; counts beyond 65,535 can wrap a category counter. Runtime weights and
  the complete signed-score overflow domain remain unqualified.
- Registry, active-count, page-marker, and persisted score/count values use
  signed 32-bit representations.
- The product duration override accepts only finite durations in `(0,600]`
  seconds. Audio input is independently bounded to ten minutes. Float time
  representation is sufficient for the proposed fixture but does not establish
  arbitrary long-chart timing behavior.

### Product-imposed limits

- The existing accepted-input ceiling of 1024, runtime-cache note safety bound of
  8192, and chart-event diagnostic walker bound of 2048 are repository policies,
  not recovered native engine limits.
- The 2048 diagnostic walker cannot safely inspect the returned capacity 2275
  for an exact-2048 request without being widened. An 8192 candidate requires all
  diagnostic count/capacity arithmetic to use the same explicit 8192 policy and
  checked allocation extents.
- Event-vector cost is `144*target_count` bytes, but this excludes native
  callback/reference allocations. Exact 8192 uses a 1,179,648-byte vector and
  makes chart update process up to 16 times the stock parser event count.

The lead-selected 8192 cap is therefore an explicit memory/work/abuse boundary,
not a conclusion that native code is safe up to that value or unsafe at 8193.
Allocator proof alone does not qualify higher per-frame work, UI/result behavior,
score accumulation, completion, replacement, or teardown.

## Future Group-Link Design

This section retains parser evidence for future work. It does not authorize group
support in the bounded monotone substrate.

`piano_event_link`, `FUN_14398DB4C` / RVA `0x0398DB4C`, receives root/predecessor
in RCX and child in RDX. It:

```text
child+0x10 = root
child+0x18 = root's former child
root+0x18  = child
```

The parser tracks only the immediately preceding byte GroupIndex at chart
`+0xA3` and a run-local root pointer:

- zero clears the run;
- a new nonzero byte selects the current event as root, preferring monotone when
  both events exist;
- the same nonzero byte on the following row links the current event or events to
  that root;
- a different nonzero byte starts a new root.

With one final event allocation and one event per row:

- **Wholly-prefix run:** validate native root/parent/child links; do not rebuild it.
- **Crossing row 511/512:** derive the prefix root index from the immutable source
  run, validate every native prefix link, and link the first tail child to that
  stable root.
- **Wholly-tail run:** retain a transaction-local root index for the first tail
  event and link later tail events after all construction succeeds.

Use indices until the final allocation is known. Apply links only after all tails
are constructed; no reserve/growth may follow. Before mutation, snapshot every
prefix root child and chart `+0xA3`. Rollback restores root children and the prior
group byte, clears tail links, and then destructs tails in reverse order.

Native semantics are run-local rather than globally unique by byte value. A byte
ID may be reused after a zero or different immediately preceding ID. Cycling IDs
1 through 255 is therefore compatible with the parser only if each intended run
is separated or receives a byte different from the adjacent prior run; two
adjacent intended runs with the same byte would merge.

Current compiler/MIDI policy deliberately rejects historical ID reuse and limits
group shapes. That policy, descriptor validation, action counting, and tests must
change explicitly before cycling is enabled. Group zero remains mandatory for
the entire 513-through-8192 monotone substrate.

## Deferred Shapes And Direct Blockers

### Chord and mixed rows

The parser uses chart-row `+0x10` as the chord side and ordinal `2*row+1` for a
chord candidate. The same constructor can statically construct one chord event.
This ABI is direct evidence, but no appended chord tail has runtime qualification.

Mixed rows may produce two events while consuming one source row, so source row
count no longer equals target event count. They also invoke monotone-preferred
group-root behavior and chord IgnoreSound handling. Both chord and mixed rows are
excluded from the bounded monotone substrate. Empty-IgnoreSound, one-event chord
rows require separate focused qualification before general either-hand support.

### IgnoreSound

The parser calls `FUN_1407FDAA4` / prospective RVA `0x007FDAA4` with:

```text
RCX: event-owned qword-vector header at event + 0x38
RDX: pointer to one qword value
effect: find existing value or append, growing the qword vector if required
```

Event deep copy clones this vector and event destruction releases it. The helper
ABI is recovered, but its production signature/catalog identity, exact semantic
qword conversion, and partial-growth rollback contract are not qualified.
IgnoreSound must remain empty. Future support requires separate catalog/signature
evidence and a focused ownership experiment.

### Camera cues

Camera timers use the independent eight-byte vector at chart `+0x70`, reserve
helper `FUN_1421CC770` / RVA `0x021CC770`, and growth helper
`FUN_14218D854` / RVA `0x0218D854`. Tail append order, semantic record fields,
and rollback have not been completely qualified. Camera cues remain excluded.

## Descriptor, Pipeline, And Cache Implications

Current retained pipeline evidence already owns more than one tail offline:

- `DiagnosticChartRetention` stores complete source rows, compiled tail rows,
  source-row indices/count, native-prefix count, and descriptor hash.
- Runtime-cache format 14 serializes counted source and compiled-tail vectors and
  uses a self-imposed 8192-note safety bound.
- `SongDifficultyProfile`, however, projects only one optional tail note, and the
  runtime eligibility path admits only exact `513/512/1`.

The Integration implementation needs an immutable owning tail vector in the runtime
profile/descriptor, contiguous indices beginning at 512, exact target count, and
hash coverage for every source and compiled tail field. `chart_notes` remains the
512-row native prefix.

Policy identity must distinguish historical exact-513/exact-520 retention from
generic playable 513-through-8192 authority. A descriptor/cache produced under a
diagnostic-only policy must not silently acquire mutation authority.

A runtime-cache format bump is not required by the selected design:

- Format 14 already encodes counted source and compiled-tail vectors with 32-bit
  lengths and has an 8192-note defensive bound.
- The serialized field layout does not change when validators admit a larger
  counted tail and project it into an immutable runtime vector.
- Extended profiles retain policy identity/generation and semantic hashes, which
  must cover every retained row and the selected target count.
- Ordinary 512-row payload semantics do not change and must remain cache
  compatible.

The Integration cache/policy work must therefore:

1. Widen bounded source/tail validation to 8192/7680 without changing the
   format-14 binary layout.
2. Change the special extended policy identity so old diagnostic or narrower
   profiles cannot gain playable authority.
3. Preserve ordinary 512 cache acceptance when its payload and semantics are
   unchanged; do not use a blind global invalidation.
4. Reject malformed counts, noncontiguous source rows, incomplete hashes, and
   policy/target mismatches before descriptor publication.

If implementation discovers that existing acceptance keys cannot isolate the
extended policy from ordinary profiles, that is a design blocker to resolve at
the key boundary, not authorization for global invalidation. A future serialized
field change may require a format bump; widening the current counted validator
alone does not.

## 2026-09-02 Accepted Maximum-Bound Qualification

The maximum-bound scenario was accepted after one failed, non-mutating diagnostic
run exposed a parser-ordering defect. Session
`20260902T194536Z-9f7510c0c596` retained the complete `8192/512/7680` cache but
published only the native prefix. Static follow-up proved that the candidate read
`chart+0x48` before the parser initialized the current frame rate. Checkpoint
`827a7c8d6800b4a47330c74539a88a22610a1bd2` moved FPS-dependent time validation
after parser return while retaining complete tail-time validation before the first
tail constructor. The failed candidate performed no extended native mutation and
was rolled back.

The corrected focused Development session is:

```text
session:       20260902T202554Z-ba1ee1e9374f
checkpoint:    827a7c8d6800b4a47330c74539a88a22610a1bd2
artifact sha:  c1cfeb1f16d207ea1dd6268822dee4c4ba76584469493abdd7d9b7d3ee4aba6c
game sha:      752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc
catalog:       ff7rebirth-steam-win64-6a16ced2
disposition:   Accepted / Development Keep
```

The fixture contained 8192 default `C4` monotone rows with one event per row,
no groups, chords, camera, IgnoreSound, dots, or strength state. Rows 0 through
8190 occupied consecutive native frames 30 through 8220; the final sentinel was
at frame 8249 / 137.483 seconds in a 139-second sidecar. Immediate input was
accepted without a prescribed wait.

The immutable log establishes:

```text
candidate: source=8192 prefix=512 tail=7680 target=8192
authority: selection/admission/TLS/controller/chart/header exact
reserve: requested=512 substituted=8192 hits=1 capacity=8192
parser_post: count=512 capacity=8192 prefix_valid=512
tails: requested=7680 constructed=7680 first_row=512 last_row=8191
commit: max_time=137.483 count_last=8192 rollback=none
publication: target=8192 lifecycle/playback exact
result: counters=8191,1,0,0 total=8192
completion: natural target=8192
replacement: prior target invalidated, ordinary_count=481
cleanup: custom_absent=1 exit_cleanup_complete=1 aggregate_exit_complete=1
```

No failed predicate, unresolved ownership, external drift, corruption, rollback,
or terminal rejection occurred in the accepted chain. The user observed prompts
throughout the fixture and normal chart/audio behavior in the subsequent ordinary
song. The exact-513 and exact-8192 accepted sessions qualify both endpoints of the
shared restricted count-driven mechanism; intermediate counts use the same checked
path but were not separately exercised in game.

## Physical-Chart-First MIDI Substrate

This design can later carry a bounded physical chart without truncating at row
512 because the descriptor retains physical tail rows and runtime construction
uses their original row indices and ordinals. The future generator may assign
byte GroupIndex values per contiguous run and reuse them after a valid boundary.

This evidence does not implement or approve that policy. MIDI generation must
first guarantee one eligible event per physical row, bounded target counts,
qualified hand/assignment behavior, and explicit run-local group validation.
Runtime construction remains descriptor-driven and must not parse MIDI or invent
group policy inside game callbacks.

## Nonclaims And Recovery Boundaries

This evidence does not establish:

- a distinct in-game scenario for every intermediate count such as 520, 798, or
  1024, although both shared-path endpoints 513 and 8192 are accepted;
- any runtime capacity above the accepted 8192-event allocation;
- safety, acceptable resource use, or product behavior above the lead-selected
  8192 policy bound;
- arbitrary pitch assignment or tail-only FName behavior;
- chord, mixed-row, grouped, IgnoreSound, or camera tail safety;
- recoverable native allocator failure, C++ exception, or SEH handling;
- universal performance or UI behavior for arbitrary dense charts beyond the
  accepted deterministic 8192 fixture;
- cross-thread chart quiescence while the persistent caller is suspended;
- exact native C++ class names or RTTI identities;
- exact list-exit destruction instruction, thread, or timing;
- 1.004 ABI/address compatibility;
- asynchronous rollback after the score-expand detour returns.

Normal predicate failure before reserve forwards native request 512. Failure
after a successful enlarged reserve but before count-last leaves count 512 after
destructor-safe tail cleanup. Failure after count-last may roll back only while
the exact synchronous transaction still owns and revalidates every native
identity. Otherwise custom publication is blocked and native lifecycle retains
ownership.
