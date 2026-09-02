# Playable Multi-Tail Scoped-Reserve Evidence

This file retains the bounded design evidence for extending the verified 1.005
row-513 scoped-reserve path to more than one native tail event. It is an
engineering evidence record, not a product contract, implementation, release
claim, or authorization to expose an arbitrary chart limit.

The only next implementation scope supported here is exact 513 and exact 520,
with one monotone event per source row. The 798-row case is a subsequent bounded
experiment target. The allocator ceiling of 910 events is not a product promise.

## Evidence Classes And Identities

### Source identities

```text
multi-tail research baseline: 485c093b85e687a442d6a1f67977e5a2c361ee2a
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
- **Bounded inference**: the calculated 520, 798, and 910 capacities and the
  multi-tail transaction below combine the direct helper logic with the matching
  allocator table algorithm. They remain runtime-unvalidated where stated.
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
2. For raw bytes at or below `0x20000`, quantizes through the active
   FMallocBinned3 small-pool tables.
3. Above `0x20000`, enters the large-allocation/granularity branch.
4. Divides the quantized byte count by `0x90` to obtain the stored vector
   capacity. If that would be less than the request, it uses its fail-fast
   overflow disposition rather than a smaller usable capacity.
5. Writes the stored capacity at header `+0x0C`, reallocates/copies through the
   native allocator path, and preserves the existing vector prefix when called
   on a nonempty vector.

The event-vector header is separate from the event allocation. There is no
per-vector header or per-event metadata added to the helper's byte request.
Allocator slab/page metadata is allocator-owned and does not subtract from the
stored event capacity.

### Quantization evidence

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
source. Therefore 513 is runtime measured; the other rows in the table are
calculated static/reference results pending their named runtime experiments.

### Capacity table

| Request | Raw event bytes | Quantized class | Stored capacity | Evidence/disposition |
| ---: | ---: | ---: | ---: | --- |
| 513 | `0x12090` / 73,872 | `0x13000` / 77,824 | 540 | Runtime observed and accepted. |
| 520 | `0x12480` / 74,880 | `0x13000` / 77,824 | 540 | Calculated; same exact class as accepted 513. |
| 798 | `0x1C0E0` / 114,912 | `0x1D000` / 118,784 | 824 | Calculated bounded target; not runtime-qualified. |
| 910 | `0x1FFE0` / 131,040 | `0x20000` / 131,072 | 910 | Calculated final small-pool request; 32 class bytes remain. |
| 911 | `0x20070` / 131,184 | large branch | not applicable here | First request beyond the small-pool threshold. |

For clarity:

- Requests 513 through 540 occupy the same exact 77,824-byte allocation class;
  stored capacity is 540 and `540 * 0x90 == 0x12FC0`, leaving 64 class bytes.
- Requests through 910 remain in the same **small-pool allocator regime** as
  accepted 513, but not necessarily the same size class.
- Request 798 maps to capacity 824 (`824 * 0x90 == 0x1CF80`), leaving 128 class
  bytes. It is a scenario-driven target, not a supported limit.
- Request 910 is a hard matching-build branch ceiling, not a recommended chart
  size. Request 911 and every larger request are outside this design.

The first multi-tail candidate should require the exact 1.005 executable and
record the actual returned capacity. For exact 513 and 520, an unexpected value
other than 540 is a fail-closed allocator-class mismatch, not permission to
continue merely because the capacity is numerically large enough.

## First Implementation Scope

Only these two source/prefix/tail shapes are admitted:

```text
513 / 512 / 1
520 / 512 / 8
```

For both shapes every source row must satisfy:

1. Exactly one nonempty monotone FName and an empty chord FName.
2. `GroupIndex == 0`.
3. Empty IgnoreSound slots.
4. No camera cue.
5. Native strength `0.0f`.
6. A finite parser-equivalent time.
7. No second native event for the row.

No 514-through-519 profile, general `<=520` profile, chord, mixed row, group,
camera, or IgnoreSound profile inherits this authority.

### Exact 520 tail records

The native parser constructs source rows 0 through 511. The extension constructs
source rows 512 through 519 in final event slots 512 through 519.

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

The exact ordinals are:

```text
row:      512  513  514  515  516  517  518  519
ordinal: 1024 1026 1028 1030 1032 1034 1036 1038
```

Each tail must be zeroed before calling `FUN_143987734` / RVA `0x03987734`.
After construction require:

- returned pointer equals the final slot;
- chart, monotone side, ordinal, time, strength, FName, NoteType, and DotType
  exactly match the preflight record;
- group links and IgnoreSound ownership are empty;
- assignment at `+0x48` is not sentinel `8`;
- lookup path at `+0x49` is native-resolved and is `0` or `1`;
- for the first bounded candidate, the assignment occurs in the parser-built
  prefix and the lookup-path behavior matches its prefix exemplar;
- runtime state bytes remain initial.

Then construct callback state with `FUN_143982EA0` / RVA `0x03982EA0`, passing a
capture of the exact chart, and require the callback invariants retained in
`Playable513ReserveEvidence.md`. Direct construction in final slots is valid only
because the final allocation already exists and the slots remain outside the
native vector count.

Tail-only pitches or assignment behavior absent from the prefix remain
unqualified. Supporting arbitrary generated-note domains requires separate
assignment-table evidence or focused runtime qualification; it must not be
inferred from a non-sentinel byte alone in the first candidate.

## Multi-Tail Transaction

### Preflight before native mutation

Before acquiring mutation authority:

1. Retain immutable registry storage and exact song/profile identity.
2. Require current playable policy generation and descriptor hash.
3. Require one of the two exact source/prefix/tail shapes above.
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
target_count is exactly 513 or 520 for the first implementation
target_count <= 910 for every native reserve path
target_count * 0x90 is representable and <= the proved regime bound
tail_count == target_count - 512
max ordinal == 2 * (target_count - 1) for monotone-only rows
```

At the hard 910 ceiling, the monotone ordinal would be 1818 and a hypothetical
chord ordinal 1819, both far below `uint32_t` limits.

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

All nonmatching reserve calls forward their original arguments unchanged.

### Parser prefix and tail construction

After the original score expansion returns, revalidate all synchronous authority
and require:

```text
header address == chart + 0x80
data == the allocation returned by the intercepted reserve
count == 512
capacity == 540 for the first 513/520 implementation
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

## Future Group-Link Design

This section retains parser evidence for future work. It does not authorize group
support in the first multi-tail implementation.

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
change explicitly before cycling is enabled. Group zero remains mandatory for the
first 513/520 implementation.

## Deferred Shapes And Direct Blockers

### Chord and mixed rows

The parser uses chart-row `+0x10` as the chord side and ordinal `2*row+1` for a
chord candidate. The same constructor can statically construct one chord event.
This ABI is direct evidence, but no appended chord tail has runtime qualification.

Mixed rows may produce two events while consuming one source row, so source row
count no longer equals target event count. They also invoke monotone-preferred
group-root behavior and chord IgnoreSound handling. Both chord and mixed rows are
excluded from the first implementation. Empty-IgnoreSound, one-event chord rows
require a separate focused qualification before general either-hand support.

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
  already validates exact `520/512/8` diagnostic retention.
- `SongDifficultyProfile`, however, projects only one optional tail note, and the
  runtime eligibility path admits only exact `513/512/1`.

The first implementation needs an immutable owning tail vector in the runtime
profile/descriptor, contiguous indices beginning at 512, exact target count, and
hash coverage for every source and compiled tail field. `chart_notes` remains the
512-row native prefix.

Policy identity must distinguish diagnostic-only 520 retention from playable
520 authority. A descriptor/cache produced under the diagnostic-only policy must
not silently acquire mutation authority under the playable policy.

A runtime-cache format or pipeline-version bump is **not established merely by
this evidence**:

- Exact 520 data is already represented by the format-14 counted payload.
- The binary encoding need not change merely to project those eight retained rows
  into an immutable runtime vector.
- Existing cache validation already has policy identity/generation and semantic
  hashes that may provide the correct invalidation boundary.
- A global policy/version change may unnecessarily reject ordinary 512-row
  caches whose serialized semantics did not change.

The Integration implementation must inspect the exact cache acceptance keys and
version ownership before changing them. It must:

1. Change the playable policy identity for exact 520.
2. Prove whether that policy is currently embedded in all cache entries or only
   extended-profile validity.
3. Preserve ordinary 512 cache compatibility when its payload and semantics are
   unchanged, or document why the existing key design makes selective reuse
   unsafe.
4. Bump pipeline or cache format only if the serialized contract, trusted count
   domain, semantic hash, or reader/writer compatibility actually changes.

General retention up to 798 would expand the trusted tail bound from 8 to 286.
Although the existing encoding is counted, that larger validator domain may
justify a later explicit format/version boundary. It is not part of the first
513/520 implementation decision.

Do not reuse the current offline 1024 diagnostic-input constant as native reserve
authority. Native playable hard ceiling, bounded product/experiment target, and
offline diagnostic input are separate policies.

## Next Runtime Qualification

No additional observation-only run is required before a reviewed, reversible
exact-520 Integration candidate. The mutation experiment still requires the
normal explicit lead/game authorization.

### Exact 520 scenario

Use the existing fixture:

```text
title: Extended Chart Read-Only Diagnostic 520
shape: 520 C4 monotone rows, group 0, no chord/IgnoreSound/camera
audio: retained 70-second silent sidecar
```

Start the selected fixture immediately through the normal readiness predicate;
do not add or require a delay. Early input must either receive exact guarded
admission or follow the recoverable native behavior. Let all 520 events traverse
the chart to natural completion, then start the ordinary 290-event song and
verify chart, score, and audio recovery before list exit.

Required structured markers, emitted once per stage rather than per frame:

```text
candidate: source=520 prefix=512 tail=8 target=520 policy_generation/hash
authority_pre: caller/admission/TLS/controller/chart/header exact
reserve: requested=512 substituted=520 hits=1 capacity=540 allocation_exact
parser_post: count=512 capacity=540 prefix_valid=512
tails: requested=8 constructed=8 first_row=512 last_row=519
       first_ordinal=1024 last_ordinal=1038 callbacks=8
commit: max_time_valid=1 count_last=520 terminal_generation_exact=1
publication: target=520 pending_then_active exact_playback=1
identity: target=520 descriptor/token/policy exact
score: native_counter_sum=520
completion: natural target=520
replacement: prior_target=520 invalidated ordinary_count=290
cleanup: custom_absent=1 exit_cleanup_complete=1 aggregate_exit_complete=1
```

Every failure marker must identify the first failed predicate and include target,
constructed count, header count/capacity, rollback outcome, activation generation,
and terminal order. Acceptance requires no unresolved ownership, external drift,
unexpected reserve hit, rollback failure, terminal mismatch, or stale-token reuse.

### Subsequent 798 experiment

Only after exact 520 is accepted, use a deterministic 798-row monotone-only,
one-event-per-row fixture:

```text
target:           798
tail count:       286
calculated capacity: 824
last monotone ordinal: 1594
```

It must repeat natural completion, replacement, ordinary-song recovery, and
teardown qualification. “Megalovania 798 distinct candidate frames” is a
task-supplied practical target, not an independently retained repository metric.
The actual song is not a valid first capacity fixture if it requires chord,
group, IgnoreSound, camera, mixed-row, or tail-only assignment behavior.

Acceptance of 520 does not qualify 798; acceptance of a synthetic monotone 798
does not qualify those deferred chart shapes or make 798 a public product limit.

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

- playable exact 520 or 798 behavior;
- capacities above the accepted 540 at runtime;
- any public row limit, including 798 or 910;
- arbitrary pitch assignment or tail-only FName behavior;
- chord, mixed-row, grouped, IgnoreSound, or camera tail safety;
- native allocator failure, C++ exception, or SEH recovery;
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
