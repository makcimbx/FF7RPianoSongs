# Chart admission: conditional read-only lookup evidence

## Scope and unresolved failure

Debug static investigation of the catalog-selected 1.005 Ghidra import
`/ff7rebirth_.exe-1.005`, image base `140000000`. This is research evidence,
not authorization to call additional native functions or relax another build.
The installed executable/artifact and original Sorairo Days failure are preserved
under the capture root referenced in `docs/CurrentStatus.md`. The failed final
recapture reports `manager_exclusion`; its exact contender remains unknown.

Skipping chart expansion is not cancellation: the state-9 entry already queued
audio and published its chart, and continues without an expansion-result check.
Earlier primary-action rejection does not by itself close a later admission race.

## Recovered lookup closure

All three catalog object vtables have exact leaf getters:
`+2B8 -> 141990720` reads `[object+58]`;
`+2C0 -> 141990710` returns `[object+58]+58`.

| Role | Object vtable | Mode-0 interface | Mode-0 lookup +28 | Mode-1 interface | Mode-1 lookup +28 |
|---|---|---|---|---|---|
| BGMList | 14673B9F8 | 14673B8D8 | 143EC3574 | 14673B878 | 1416BE674 |
| ScoreInfo | 1467A4EB8 | 1467A47F0 | 143F386F8 | 1467845E0 | 14412670C |
| PianoScore | 1467973A8 | 146795EA0 | 143E2E004 | 1467448A0 | 143F0DEAC |

Mode-0 readiness `140827C80` returns true. Mode-1 readiness `140B7ABEC`
tests `[interface+8] != 0`. Mode-0 lookups call respectively `143EC35C4`,
`143F38748`, `143E2E054`; inspected complete bodies traverse hash chains,
compare packed names and write only caller-local results. Mode-1 typed lookups
traverse serialized hash/index storage and call `140B7BA48`.

The singleton accessor's hot branch is exact:
`140B7BA55 CMP byte ptr [148F309F8],0`, `JZ` cold initializer,
then `LEA RAX,[1490D3C00]` and return. No TLS predicate is present. All indexed
writes to the guard set 1; no clearing write was found. The cold branch calls
initialization/allocators and is not qualified. Require hotness even if sampled
mode-1 readiness is false; readiness may differ when original executes.

Generic manager lookup resolves weak entries inline through GUObjectArray reads;
it does not assign serials. Its inspected stack probe has no engine calls or
synchronization. The fully qualified hot typed closure has no explicit engine
wait, allocator, callback or reentry. Memory faults/stack growth are not excluded.

## Why blanket relaxation is unsafe

Generic lookup visits all three groups and calls getters/readiness before finding
the selected key. A selected candidate or manager identity alone does not certify
all visited providers. Append can change the population after an unprotected
classification. Append also assigns weak serials and publishes count before
growth; remove/reset/reserve mutate arrays; name-helper can insert/reallocate the
global name map. All must remain conservative, including unknown lookup targets.

Current stock-baseline checks do not establish permanently closed populations.
Existing candidate/stock binding witnesses do not automatically pin every provider
or establish all backing/interface lifetime and immutability.

## Conditional population fence (superseded proposal; not an implementation route)

A proposed, not yet implemented protocol keeps registration exclusion unchanged.
Opaque lookups/mutators hold population-shared, retaining peer concurrency. A
potential pure lookup tries population-exclusive, classifies without allocations
or native calls, and retains it through original only on full qualification.
Unknown paths release exclusive before conservative original admission.
Memory-only validation uses its own exclusive try gate, never the population gate;
opaque/mutation paths share that validation gate, qualified pure lookup does not.

Required proof: every visited weak provider, getter/interface/lookup target,
retained backing lifetime, bounded valid hash traversal and hot singleton. Missing
proof means conservative fallback, not a capability granted by a diagnostic mask.
Registration bypass needs real writer ownership; unexpected nested mutation must
retain mutation admission through the outer call, not inherit generic TLS depth.
The remaining implementation obligation is complete eligible population ownership,
not another read-only leaf disassembly. A classifier that never admits actual
retained populations is not a behavioral fix.

Source services used by normal binding validation perform safe memory reads, not
native manager calls, but existing capture machinery allocates and must not be
reused as an allocation-free classifier. A pre-published identity view and fixed
scratch storage are needed. Normal full binding validation must remain intact.

## Actual-dispatch alternative: complete generic lookup contract

Read-only follow-up on 2026-10-23; no runtime implementation or catalog changes.
This section replaces population classification as the candidate implementation
direction. Source: explicit Ghidra `/ff7rebirth_.exe-1.005`, complete decompilation
and instruction queries for `14076A330`, including its noncontiguous cold blocks.
The function contains 395 instructions and eight CALL instructions. Ghidra's
reported address envelope includes other functions: it is NOT a contiguous body
or an acceptable signature window.

Executable identity is the catalog's `ff7rebirth-steam-win64-6a16ced2`: timestamp
`6a16ced2`, SizeOfImage `099d9000`, checksum `0769ea6e`, SHA256
`752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc`.
This is imported-program/catalog correlation, not a fresh installed-file hash.
Do not transfer these call sites or implementation contracts to 1.004.

### ABI and traversal

Catalog ID `dataobject_manager_lookup`, entry `14076A330` / RVA `0076A330`:

```text
RCX = manager (three consecutive 16-byte group headers)
RDX = full packed 64-bit name
R8  = writable uint64 association output
R9  = writable uint8 backing-mode output
RAX = borrowed row pointer, or null on an ordinary full miss
```

The normal ABI requires valid output storage. There are no optional-output null
checks. No group selector is passed. Traversal is group 0, then 1, then 2;
entries are visited in increasing index order. Each header is data/count/capacity;
each 16-byte entry is weak index, weak serial, then association at +8. A signed
nonpositive group count skips that group. Count/data are reread as traversal
progresses, not frozen into a population snapshot.

Weak resolution is inline, before dispatch, and repeated on a hit:

* Serial must be nonzero; index must be nonnegative and less than the live
  GUObject count at `14871EB4C`.
* Chunk table is read at `14871EB38`; chunk index is index >> 16, slot is the
  low 16 bits, FUObjectItem stride is 24 bytes.
* Item serial +10 must equal the entry serial; item flags +8 bits 28 and 29
  must both be clear; the provider is item.object at +0.
* No weak construction, serial assignment, callback, engine lock or resolver
  function is invoked by this sequence.

**Invalid weak entries are not ordinary misses.** First-resolution failures
branch to `14076A6C8` (RCX = 0), then `14076A424` loads `[RCX]`. This is an
unsafe-input fault, not a continue-to-next-entry branch. A replacement must not
silently turn it into skip/stock fallback. The native routine also has no
hash-chain budget or exception-recovery wrapper.

For each provider, load its vtable; inspect provider byte +54. Zero selects
object slot +2B8; nonzero selects +2C0. Call the selected getter with the same
provider as RCX; its result becomes the interface receiver. Load readiness
target from interface-vtable +8. If ready, load lookup target from
interface-vtable +28 and use `(interface, full_packed_name)`. A null row or false
readiness continues traversal; a nonnull row ends traversal.

### Readiness-to-lookup reload boundary

Fresh lead instruction inspection confirms that the interface vtable is reloaded
AFTER readiness, not reused across a potentially side-effecting readiness call:

```asm
14076A546  MOV RCX,R14
14076A549  CALL RDX
14076A54B  JMP 14076A45D
14076A45D  TEST AL,AL
14076A45F  JZ 14076A684
14076A465  MOV RAX,[R14]
14076A468  MOV R8,[RAX+28]
14076A46C  LEA RAX,[14078C74C]
14076A473  CMP R8,RAX
14076A476  JNZ 14076A550
```

The latter branch compares the freshly captured R8 against the other two
devirtualized lookup targets; if neither matches, it sets `(RCX=R14,RDX=RBX)`
and calls R8. Therefore a readiness callback replacing the interface vtable
changes the subsequent lookup target. The interface receiver R14 itself is
retained, not recomputed by calling the object getter again.

Source listing: approved Temp `ghidra-mcp/responses/response-yb937gc9.txt`,
initial function instructions only. The disassembly endpoint returned a huge
address envelope containing unrelated functions; do not treat that envelope as
a contiguous signature or read it as additional lookup body evidence.

### Complete call-site inventory

| Site | Native operation | Semantic call contract |
|---|---|---|
| `14076A34F` | direct `14216BB80` | stack probe, size 50; not manager work |
| `14076A431` | `[provider_vtable+2C0]` | RCX provider; RAX interface |
| `1424BA758` | `[provider_vtable+2B8]` | RCX provider; RAX interface |
| `14076A549` | RDX, captured readiness target | RCX interface; AL boolean |
| `14076A576` | R8, captured lookup target | RCX interface, RDX packed name; RAX row |
| `14076A49D` | direct `140B7BA48` | singleton accessor in inline lookup case |
| `14076A6ED` | direct `140B7BA48` | same, second inline case |
| `14076A7AC` | direct `140B7BA48` | same, third inline case |

There are two important compiler devirtualizations, not additional virtual calls:

* Readiness targets `140FEC7BC`, `140FEC424`, `141325848` are recognized by
  address and executed inline as `[interface+8] != 0`. All three complete leaves
  have bytes `48 83 79 08 00 0f 95 c0 c3`. Other targets execute at A549.
* Lookup targets `14078C74C`, `1410FDF64`, `1410FA388` are recognized by address
  and executed inline using serialized hash/index tables, row strides 1E0, 120,
  14 respectively. Complete decompilation of all three actual target functions
  matches those inline algorithms, including full-name matching, tagged relative
  pointers, miss behavior and singleton access. Their only non-stack-probe callee
  is `140B7BA48`. Other targets execute at A576.

Consequently, merely wrapping the four indirect CALL instructions leaves THREE
potential cold singleton initializations unprotected in the native inline body.
An actual-dispatch replacement can instead call each captured recognized target
once: its recovered body supplies the same computation, without copying these
three hash algorithms. Coldness must then classify the entire typed call as
opaque; do not partially execute it and retry.

### Outputs, effects and failure behavior

The only inline non-stack stores are:

| Address | Store |
|---|---|
| `14076A363` | `*association_out = 0` at entry |
| `14076A5EC` | hit association copied from the CURRENT group/index entry |
| `14076A65F` | `*mode_out = freshly_resolved_provider[54]` |

On a hit the native function rereads the manager entry and resolves its weak
provider again, rather than reusing the provider passed to virtual methods.
Second-resolution failure reaches `14076A653` with a null provider and then
reads byte +54; it is likewise a fault, not a successful alternate mode.
Return the already obtained row. On a full miss association remains zero and
the mode output is UNCHANGED. No map/group/object store occurs in the inline
traversal. Caller-output ownership/nonaliasing must remain part of admission:
the body does not check whether output pointers alias protected native memory.

Do not snapshot count/provider/association across opaque callbacks, reorder
groups, normalize nonzero mode to 1 in the final output, or replay original
after a dispatched operation. Unknown virtual targets remain real operations
under conservative mutation protection, including their side effects.

### Feasible bounded replacement, not a parser rewrite

The existing hook framework (`src/core/hooks.cpp:17-64`) is MinHook entry
detouring. No register-preserving indirect-call wrapper/mid-hook facility was
found in that boundary. Patching native call sites needs new machine thunks,
signatures, unwind/lifecycle handling and coverage of the inline direct helpers;
it is not a tiny existing mechanism.

A smaller candidate is a build-qualified outer traversal under the EXISTING
`dataobject_manager_lookup` hook, retaining registration admission throughout:

1. Reproduce the two inline weak resolutions and native iteration/output order.
2. At each actual dispatch capture `(target, receiver, semantic arguments)` once.
   Classify that captured target; invoke exactly that target, not a later vtable
   reread. Pure capability requires exact recovered target/prologue and, where
   applicable, the singleton hot guard. Anything else executes conservatively.
3. Use the recovered actual methods even for the compiler-devirtualized cases.
   Their complete bodies were checked; this avoids reimplementing hash parsers.
4. Unknown/cold calls acquire mutation admission before invocation, retained
   through the root lookup return. Nested mutation must acquire/inherit actual
   mutation authority, never infer it from generic entry depth. Actual product
   registration-writer ownership is a distinct bypass capability.
5. Unsupported executable/dispatch capability uses the conservative original
   route BEFORE starting replacement traversal. There is no whole-function
   fallback/retry after callbacks or cold work may have run.

Estimated implementation scope: roughly 100-180 lines of traversal, inline weak
resolver and dispatch wrappers, plus the separately owned gate/policy changes
and tests. This is a source-size estimate, not implemented/compiled evidence.
It requires independent unsafe-ABI review. Native invalid-input fault behavior
must not be accidentally converted into a successful fallback.

This removes the population-ownership prerequisite because each receiver remains
within its native synchronous borrow and each invoked target is classified at
the actual use site. It does NOT create a GC/lifetime lease, permit raw-provider
retention, waive normal native caller ownership, or justify aliasing output
stores outside mutation protection. Inline protected-memory mutation through
non-owned output pointers must take the conservative route.

### Focused checks before implementation acceptance

Use a synthetic native-memory/target fixture, without game authority: group
order; count/data changes made by callbacks; false readiness; all mode values;
miss leaving mode untouched; hit re-resolution and full association; invalid
weak/index/serial/flag behavior; each devirtualized native target against its
actual method; hot/cold singleton routing; captured-target/vtable-change race;
unknown exactly-once side effects; nested mutation held until root return;
and simultaneous pure dispatch plus complete binding validation. No test or
runtime acceptance was performed in this investigation.

Affected catalog IDs: `dataobject_manager_lookup`, manager append/remove/reset,
name helper and group reserve; the typed lookup/interface and GUObject identities
used as evidence remain build-specific. No hook address or signature was changed.

## Older supported build: independently recovered actual-dispatch contract

### Identity, scope, and compatibility blocker

This section was recovered independently from Ghidra program
`/ff7rebirth_.exe-1.004` (internal name `ff7rebirth_.exe`, Windows x86-64,
image base `140000000`), not by applying a delta to newer addresses.
Bounded imported PE reads at `140000220` match canonical build
`ff7rebirth-steam-win64-68fd6fde`: timestamp `68fd6fde`, SizeOfImage
`09bad000`, checksum `07877c99`. The catalog's disk identity is 126263624 bytes,
SHA-256 `76bd4d539878a8759433f1cd7c7b47d5e3abda0ef41572a664d2d606a9f09dc8`.
No fresh installed-file hash or older in-game acceptance was obtained.

**Do not enable the current replacement unchanged for this build.** At this
inspection, `src/game/native_audio_lookup.h:35` selects newer object virtual
slots `2b8/2c0`. Older native lookup selects **`2b0/2b8`** for mode zero/nonzero.
The existing exact-build `NativeAudioLayout` already records this distinction.
The traversal/dispatch design otherwise matches the recovered older contract;
it needs exact-build slot inputs and older target capabilities, not a second
hash parser. Tests of both slot pairs remain lead-owned.

### Complete older outer lookup

Canonical `dataobject_manager_lookup` is RVA `008bd0c0`, VA `1408bd0c0`.
Complete decompilation plus existing-instruction searches cover 397 instructions
and eight CALLs, including a noncontiguous getter block. Do not use the function's
address envelope as a contiguous signature or dump.

ABI remains `row*(manager RCX, packed-name RDX, association-out R8, mode-out R9)`;
the returned row is borrowed, with no acquired ownership. Native normal caller
lifetimes/output ownership remain required. The replacement must neither retain
providers nor replay a partially executed lookup.

- Initialize the full association output to zero (`1408bd0fa`); mode is untouched
  until a hit. Groups are visited in order 0, 1, 2. Group headers and weak entries
  are each 16 bytes. Signed nonpositive counts skip a group. Data/count are live
  reads, including the back-edge count comparison at `1408bd41e` and group bound
  at `1408bd447`; do not snapshot them across native callbacks.
- Weak resolution uses global chunk-table pointer `1488eb4b8` and element count
  `1488eb4cc`, 65536 items/chunk, item stride 24. Require nonzero entry serial,
  nonnegative index below the current count, matching item serial at `+10`, and
  item flags `+8` with bits 28/29 clear. This is inline memory observation, not
  weak construction/serial assignment, and contains no native call or wait.
- Load provider vtable before reading provider mode byte `+54`. Zero mode calls
  captured object slot `+2b0`; every nonzero mode calls slot `+2b8`. The getter
  returns the interface receiver. Load its readiness target from vtable `+8`.
  After successful readiness, **reload the same receiver's vtable** at
  `1408bd21c` and capture lookup slot `+28` at `1408bd21f`. The readiness call
  may change that vtable; do not retain the old lookup target.
- Unknown readiness executes exactly once with interface in RCX; unknown lookup
  executes exactly once with that interface in RCX and original full name in
  RDX (`1408bd24f..255`). False readiness/null row continues traversal.
- On a hit, reread the current manager entry's association, store it at
  `1408bd380`, then load current group storage again at `1408bd383` and resolve
  that entry's weak provider anew. Copy this fresh provider's mode byte to the
  caller output at `1408bd3f1`; return the previously found row. Full miss returns
  null with association zero and unchanged mode.
- Invalid weak resolution is **not** a native skip. The first failure path sets
  RCX to zero at `1408bd518`, then rejoins provider dereference at `1408bd1b8`.
  The second failure path rejoins mode dereference at `1408bd3e5`. Preserve
  native fault semantics rather than inventing a successful fallback.
- The complete MOV memory-destination audit found only stack saves/locals and
  the three caller-output stores above. No inline manager/map/UObject mutation
  occurs. Output aliasing into protected native memory is not made safe by this
  observation; retain the conservative non-owned-output path.

| CALL VA | Actual operation |
|---|---|
| `1408bd0e4` | Stack probe `1420aef30` |
| `1408bd1c5` | Captured provider getter `[vtable+2b8]`, RCX provider |
| `14289a742` | Captured provider getter `[vtable+2b0]`, RCX provider |
| `1408bd204` | Captured readiness RDX, RCX interface |
| `1408bd255` | Captured lookup R8, RCX interface/RDX full name |
| `1408bd281`, `1408bd476`, `1408bd53d` | Singleton `1408bb424` in compiler-inlined lookup cases |

Recognized readiness targets are `14116c404`, `1411c09a4`, `1412d38d4`;
each complete body is `return interface[+8] != 0`. Recognized lookup targets are
`140842740`, `140842438`, `14118dc94`, with row strides `1e0`, `120`, `14`.
Their complete actual method bodies reproduce the respective native inline
hash/index paths. Calling the actual captured method once is equivalent to that
inline computation, subject to the same borrow and hot/cold admission contract.
Protecting only the four indirect CALL sites would miss the three cold singleton
possibilities in these inline cases.

### Role dispatch closure and ownership limits

The object vtables below were read at the older slots, not inferred from a
signature resemblance. All three have `+2b0 -> 1418ff300` (return `[object+58]`)
and `+2b8 -> 1418ff2f0` (return `[object+58]+58`). These are complete five/nine-byte
leaves. The BGM constructor `14415450c` independently establishes its backing and
interface vtables; the other role vtables are also canonical catalog identities.

| Role | Object vtable | Mode-0 backing vtable / lookup | Mode-1 interface vtable / lookup |
|---|---|---|---|
| BGMList | `1468afce0` | `1468af970` / `14415602c` | `1468af7f0` / `14164f848` |
| ScoreInfo | `14691c2a0` | `14691bf30` / `1441cae8c` | `1468fbdf0` / `1443b6bb0` |
| PianoScore | `14690f808` | `14690d718` / `1440c14c4` | `1468b8538` / `1441a06f8` |

All mode-0 readiness slots `+8` target constant-true leaf `1408b07b0`; all
mode-1 readiness slots target `1408bdda4`, testing receiver `+8`. Lookup slot is
`+28` in both interface forms. Complete mode-0 wrappers call only stack probe
and their hash child: BGM `14415607c`, ScoreInfo `1441caedc`, PianoScore
`1440c1514`. Children contain no calls; they traverse full packed-name hash
chains and write only the local output index. Entry strides/next offsets are
`a8/a0`, `98/90`, `c8/c0`; wrappers return entry+8 or null.

Complete mode-1 methods, including the three recognized inline targets, read
serialized tagged-relative map/bucket/row storage. They compare both name halves,
walk 20-byte name-index records (next `+c`), and return row base+index*stride or
null. The only non-stack-probe callee is `1408bb424`. Role row strides are
`84/78/b0`; recognized generic-inline strides are `1e0/120/14`. No additional
shared writes, engine callback, or reentry was found in these complete closures.
This does not close arbitrary provider populations: only the **actual captured
target and receiver** may be classified at each dispatch; unknown targets must
execute under conservative mutation admission, not be dropped.

### Exact hot/cold singleton and lifetime qualification

`1408bb424` tests byte `1490fd2e8` at `1408bb431`; nonzero returns singleton
`14929a780` without a TLS/header check. Zero branches to `14289a72a`, calls
initializer `1424548e4`, then stores one to the guard at `14289a736`. These agree
with existing catalog `native_bootstrap_srw_pool_complete` / pool identities.
The 38-byte complete hot body is recorded below, including stack probe.

All 132 indexed direct guard writes were checked: 102 encode immediate one;
the remaining 30 register stores, in 18 enclosing functions, decompile to one.
No indexed clear was found. This is indexed static evidence, not an arbitrary
alias-write impossibility or a GC/lifetime lease. The pool is static name-hash
storage, not a per-lookup UObject acquisition. Keep normal native singleton
lifetime authority; no teardown/reinitialization or cached-pool replay is added.

The complete cold initializer was inspected: SRW initialization, allocations,
TLS/CRT initialization guards, name construction and duplicate-name diagnostics
are present (including an error path calling PostQuitMessage). It is **opaque**,
not a pure call. A sampled false readiness must not excuse a cold mode-1 target.
Hot classification requires the actual initialized guard and exact recovered
target; otherwise conservatively protect the entire actual typed call.
Stack probe `1420aef30` has no engine call, lock acquisition or explicit wait:
it reads `GS:[10]` and probes stack pages. Stack growth/faults remain possible.

### All 15 new optional lookup capability mappings

These are exact imported entry bytes, all-`x` masks of the displayed byte count.
They are complete instructions (entire leaf where short, complete entry prologue
otherwise), not image-wide uniqueness claims. Duplicate readiness leaf bytes are
intentional: capability requires exact target address **and** signature/build.
Existing typed ScoreInfo/PianoScore mode-1 catalog entries remain unchanged.
Do not infer runtime enablement or callable ownership from this evidence table.

| Catalog ID | Older RVA | Exact entry signature |
|---|---|---|
| `lookup_backing_getter` | `018ff300` | `48 8b 41 58 c3` |
| `lookup_serialized_getter` | `018ff2f0` | `48 8b 41 58 48 83 c0 58 c3` |
| `lookup_ready_true` | `008b07b0` | `b0 01 c3` |
| `lookup_ready_serialized` | `008bdda4` | `48 83 79 08 00 0f 95 c0 c3` |
| `lookup_ready_inline_bgm` | `0116c404` | `48 83 79 08 00 0f 95 c0 c3` |
| `lookup_ready_inline_score` | `011c09a4` | `48 83 79 08 00 0f 95 c0 c3` |
| `lookup_ready_inline_piano` | `012d38d4` | `48 83 79 08 00 0f 95 c0 c3` |
| `lookup_mode0_bgm` | `0415602c` | `40 53 b8 20 00 00 00 e8 f8 8e f5 fd 48 2b e0` |
| `lookup_mode0_score` | `041cae8c` | `40 53 b8 20 00 00 00 e8 98 40 ee fd 48 2b e0` |
| `lookup_mode0_piano` | `040c14c4` | `40 53 b8 20 00 00 00 e8 60 da fe fd 48 2b e0` |
| `lookup_mode1_bgm` | `0164f848` | `48 8b c4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 55 41 56 41 57 b8 20 00 00 00 e8 c5 f6 a5 00 48 2b e0` |
| `lookup_inline_bgm` | `00842740` | `48 8b c4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 55 41 56 41 57 b8 20 00 00 00 e8 cd c7 86 01 48 2b e0` |
| `lookup_inline_score` | `00842438` | `48 8b c4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 55 41 56 41 57 b8 20 00 00 00 e8 d5 ca 86 01 48 2b e0` |
| `lookup_inline_piano` | `0118dc94` | `48 8b c4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 55 41 56 41 57 b8 20 00 00 00 e8 79 12 f2 00 48 2b e0` |
| `lookup_hot_singleton` | `008bb424` | `b8 28 00 00 00 e8 02 3b 7f 01 48 2b e0 80 3d b0 1e 84 08 00 0f 84 ec f2 fd 01 48 8d 05 3b f3 9d 08 48 83 c4 28 c3` |

### Handoff / focused acceptance gap

Only this evidence record was edited. No runtime, catalog, generated source or
test change, native call, debugger attachment, build or launch was performed.
Lead should parameterize the older `2b0/2b8` getter slots, supply the older
capability targets/hot guard/GU globals, and exercise the existing synthetic
exactly-once fixture with both profiles. Specifically retain readiness-vtable
reload, hit-time association-before-second-resolve ordering, unknown/cold calls
and native fault behavior. A newer-slot fixture must fail for older selection.
Independent ABI/unsafe-memory review and focused game acceptance remain required;
source equivalence and imported PE agreement alone are not older runtime proof.
