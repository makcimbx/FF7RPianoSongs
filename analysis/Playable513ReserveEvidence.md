# Playable Row 513 Scoped-Reserve Evidence

This file retains the static evidence and implementation envelope for one narrowly
defined experiment: reserve the final native event-vector allocation while the
verified 1.005 parser is expanding an exact 513-row, monotone-only chart; let the
parser construct the 512-event prefix; then construct one ungrouped tail event in
slot 512 and publish it by changing the native event count from 512 to 513.

It is reverse-engineering evidence, not a shipping contract, runtime validation,
or authorization to enable playable charts above 512 rows. The playable limit
remains governed by the product source and `../docs/ChartLimits.md`.

## Evidence Grades

- **Direct static evidence**: recovered from the verified executable's code,
  retained Ghidra output, or exact executable-byte reads.
- **Runtime evidence**: observed in a retained, immutable matching-build session.
  It establishes that session's call order and values, not unobserved executions.
- **Bounded inference**: follows from direct evidence under the restricted fixture
  invariants stated here, but has not been exercised in the game.
- **Runtime-unvalidated**: requires the focused in-game experiment before it can
  support a behavior or safety claim.

## Scope And Provenance

Repository source checkpoint:

```text
2085f1677ee1a8c53e087bc6e0b4d973a5cc7ce6
```

Verified executable identity:

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

The identity matches `../src/game/rva_catalog.json`. Every VA, RVA, byte
signature, callsite, vtable entry, and ABI statement below is scoped to this
exact 1.005 image. Nothing here derives or approves a 1.004 address.

Primary retained sources:

- [ChartEventAbiGhidra.txt](ChartEventAbiGhidra.txt): parser, constructor,
  deep-copy, link, destructor, and direct-call evidence.
- [ChartRowLimitEvidence.md](ChartRowLimitEvidence.md): source-accessor boundary,
  dynamic chart layout, ownership hazards, and diagnostic-only status.
- `../src/game/rva_catalog.json`: supported executable identity and existing
  catalog records.
- `../src/game/note_count.cpp`, `chart_patch.h`, and `chart_patch.cpp`: existing
  expand transaction, TLS, caller/owner checks, and native chart inspector.
- `../src/pipeline/chart_compiler.cpp`: retained prefix/tail and `TimeStr`
  generation at the source checkpoint.

Signature uniqueness below was measured by exact byte search within the verified
1.005 executable's `.text` section. “Minimum unique prefix” means unique in that
`.text` section, not an image-wide or cross-build uniqueness claim. All retained
masks are exact (`x`) masks.

## Restricted Fixture Contract

Every predicate is mandatory. A mismatch must leave the experiment disabled.

1. Exactly 513 authored rows.
2. Every row has exactly one nonempty monotone identity and no chord identity.
3. Every row is ungrouped (`GroupIndex == 0`).
4. Every row has empty `IgnoreSound` and no camera-switch cue.
5. Strength is the native default `0.0f`.
6. The first 512 rows produce exactly 512 native events.
7. Source row 512 is retained immutably in the runtime descriptor; diagnostic
   counts or hashes alone are insufficient.
8. No mixed-hand, chord, grouped, ignored-sound, camera-tail, or general
   greater-than-512 path may share this authority.

At the source checkpoint, the actual tail record is not retained in the runtime
descriptor and the effective publication/count policy remains 512. Those are
implementation prerequisites, not facts this evidence file changes.

## Native Chart And Event Layout

### Chart fields

| Offset | Type | Meaning and ownership |
| --- | --- | --- |
| `+0x30` | `float` | Maximum parsed row/event time. Mutated only at final tail commit. |
| `+0x34` | `float` | Elapsed chart time. Not changed by this experiment. |
| `+0x48` | `float` | Parser-selected frames-per-second divisor. Read after parsing; never guessed. |
| `+0x70/+0x78/+0x7C` | pointer/count/capacity | Eight-byte camera timer records. Unchanged for this fixture. |
| `+0x80/+0x88/+0x8C` | pointer/count/capacity | `0x90`-byte event vector. `+0x88` is the event count; there is no separate copied-row count. |
| `+0xA0` | byte | Active state. Not changed by this experiment. |
| `+0xA3` | byte | Parser-local previous `GroupIndex`; irrelevant because all fixture groups are zero. |

### Event fields

| Offset | Type | Meaning and ownership |
| --- | --- | --- |
| `+0x00` | pointer | Borrowed chart pointer. |
| `+0x08` | pointer | Borrowed monotone/chord side/controller pointer. |
| `+0x10` | pointer | Raw group-root pointer into the event allocation; must be null here. |
| `+0x18` | pointer | Raw child-chain pointer into the event allocation; must be null here. |
| `+0x20` | `uint32` | Parser event ordinal, not compact event-vector index. |
| `+0x24` | `float` | Native event time. |
| `+0x28` | `float` | Strength. |
| `+0x2C` | packed FName | Monotone or chord identity. |
| `+0x38/+0x40/+0x44` | pointer/count/capacity | Owned IgnoreSound vector; must be empty here. |
| `+0x48` | byte | Resolved input assignment; sentinel `8` means unresolved. |
| `+0x49` | byte | Resolved assignment/hand lookup path marker. |
| `+0x4A` | byte | Runtime result/state. Initially zero. |
| `+0x4B` | byte | NoteType. |
| `+0x4C` | byte | DotType. |
| `+0x4D..+0x4F` | bytes | Runtime activation/finalization state. Initially zero. |
| `+0x50..+0x8F` | 64 bytes | Owned callback/result state. |

## Helper ABI And Ownership Inventory

All functions use the Microsoft x64 ABI. Register arguments are `RCX`, `RDX`,
`R8`, and `R9`; subsequent constructor arguments use the standard 32-byte shadow
space and stack slots described below.

| Entity | VA / RVA | ABI and effect | Failure behavior and independent anchors |
| --- | --- | --- | --- |
| `piano_score_expand`, `FUN_1439B9104` | `0x1439B9104` / `0x039B9104` | `void(chart, chart_row, arg3, arg4)`. Initializes the persistent chart and calls the parser. | Existing production hook target. Calls parser at `0x1439B9160`. The checked-in 1.005 catalog evidence is a migration record; parser semantics were recovered independently. |
| `piano_score_parser`, `FUN_1439B3608` | `0x1439B3608` / `0x039B3608` | `void(chart, chord_side, monotone_side)`. Clears/reserves vectors and parses source rows 0–511. | Direct caller `FUN_1439B9104` at `0x1439B9160`; retained in `ChartEventAbiGhidra.txt`. No reusable C++ unwind map was recovered. |
| strict FName Find, `FUN_140B517C4` | `0x140B517C4` / `0x00B517C4` | `void* fn(FNameValue* out, const wchar_t* text, int32 find_type)`. Use `find_type=0` (`FNAME_Find`), then require nonzero packed FName. | Returns an unresolved/zero FName for an unknown name; no name insertion is permitted. Current use is shown by `chart_patch.cpp::construct_fname_find`. Existing catalog policy is build-identity-only; this experiment requires the strict signature below. |
| event constructor, `FUN_143987734` | `0x143987734` / `0x03987734` | `Event* fn(Event* dst, Chart* chart, void* side, uint32 ordinal, float time, float strength, uint8 note_type, uint8 dot_type, uint64 fname)`. Initializes one `0x90` event and performs assignment lookups. | Returns `dst`; has no explicit failure result. Sentinel assignment `+0x48==8` means lookup failed. Parser callers: `0x1439B38C1` and `0x1439B393E`. It allocates no event-owned state. |
| callback builder, `FUN_143982EA0` | `0x143982EA0` / `0x03982EA0` | `void* fn(void* dst_0x40, Chart** captured_chart)`. Embeds the result callback and chart capture. Returns destination. | Allocation-free; leaves unused qwords untouched, so the destination must be zeroed. Parser callers: `0x1439B394B`, `0x1439B3998`. |
| result callback, `FUN_143991F24` | `0x143991F24` / `0x03991F24` | Thunk receiving captured chart and event arguments; dispatches to `FUN_1439A896C(captured_chart,event)`. | Installed by the callback builder at `0x143982ED4..0x143982EDB`. Runtime invocation is not validated by this investigation. |
| event destructor, `FUN_143988A04` | `0x143988A04` / `0x03988A04` | `void fn(Event* event)`. Destroys callback state at `+0x50` and frees IgnoreSound ownership at `+0x38`. | No result. Requires a zero, fully constructed, or proven partial event. Parser calls `0x1439B3B3E/0x1439B3B4A`; vector cleanup calls it at `0x143997830`. |
| event-vector reserve, `FUN_142AD3EF4` | `0x142AD3EF4` / `0x02AD3EF4` | `void fn(EventVectorHeader* header, int32 requested_capacity)`. Uses native allocator sizing for `0x90`-byte elements; `0` frees. | No status return. May relocate by raw byte copy. Parser reserve call `0x1439B36B3`; cleanup call `0x143997853`; 18 recovered code xrefs and no self-recursion. Allocator fault contract is unknown. |
| event-vector growth, `FUN_142913540` | `0x142913540` / `0x02913540` | `void fn(EventVectorHeader*)`. Requests approximately `count + floor(3*count/8) + 16` when capacity is nonzero. | Raw-relocates and does not rebase links. Parser calls `0x1439B39FA/0x1439B3AA2`. It must not execute for the restricted prefix after a successful final reserve. |
| event-vector destroy/free, `FUN_143997808` | `0x143997808` / `0x03997808` | `void fn(EventVectorHeader*)`. Destructs exactly `count` events, resets count, then reserves zero to free. | Parser call `0x1439B369F`. This experiment does not call it directly; normal parser/teardown lifecycle owns the committed vector. |

The prior detached-copy architecture's deep-copy helper is deliberately not part
of this normal path. Prefix events are constructed by the parser directly in the
final allocation.

## Exact Signature Candidates

These values are catalog-ready evidence candidates, not catalog edits. Masks are
the same length as their byte strings.

```json
[
  {
    "id": "fname_ctor",
    "rva": "0x00B517C4",
    "bytes": "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 B8 40 04 00 00 E8 A4 A3 61 01 48 2B E0 48 8B 05 1A B0",
    "mask": "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 21
  },
  {
    "id": "piano_score_expand",
    "rva": "0x039B9104",
    "bytes": "B8 28 00 00 00 E8 72 2A 7B FE 48 2B E0 8A 02 45",
    "mask": "xxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 9
  },
  {
    "id": "piano_score_parser",
    "rva": "0x039B3608",
    "bytes": "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D A8 28 FE FF FF B8 B0 02 00 00 E8 4F 85 7B FE",
    "mask": "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 38
  },
  {
    "id": "piano_event_construct",
    "rva": "0x03987734",
    "bytes": "48 89 5C 24 08 48 89 74 24 10 57 B8 40 00 00 00 E8 37 44 7E FE",
    "mask": "xxxxxxxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 19
  },
  {
    "id": "piano_event_destruct",
    "rva": "0x03988A04",
    "bytes": "40 53 B8 20 00 00 00 E8 70 31 7E FE 48 2B E0 48",
    "mask": "xxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 11
  },
  {
    "id": "piano_event_callback_build",
    "rva": "0x03982EA0",
    "bytes": "48 C7 01 00 00 00 00 48 8D 05 6A 01 AB 02 48 C7 41 10 00 00 00 00 4C 8D 41 28 48 89 41 20 48 8B",
    "mask": "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 12
  },
  {
    "id": "piano_event_result_callback",
    "rva": "0x03991F24",
    "bytes": "B8 28 00 00 00 E8 52 9C 7D FE 48 2B E0 48 8B 12 48 8B 09 48 83 C4 28 E9 2C 6A 01 00",
    "mask": "xxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 9
  },
  {
    "id": "piano_event_vector_reserve",
    "rva": "0x02AD3EF4",
    "bytes": "48 89 5C 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 B8 20 00 00 00 E8 6D 7C 69 FF 48",
    "mask": "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 29
  },
  {
    "id": "piano_event_vector_growth",
    "rva": "0x02913540",
    "bytes": "48 89 5C 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 B8 20 00 00 00 E8 21 86 85 FF 48",
    "mask": "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 29
  },
  {
    "id": "piano_event_vector_destroy",
    "rva": "0x03997808",
    "bytes": "48 89 5C 24 08 48 89 74 24 10 57 B8 20 00 00 00 E8 63 43 7D FE 48 2B E0 8B 71 08 48 8B D9 48 8B",
    "mask": "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 20
  },
  {
    "id": "piano_event_vector_reserve_call",
    "rva": "0x039B36B3",
    "bytes": "E8 3C 08 12 FF",
    "mask": "xxxxx",
    "minimum_unique_text_prefix": 5,
    "return_rva": "0x039B36B8"
  }
]
```

Direct destructor/callback dependencies retained for review:

```json
[
  {
    "name": "callback_state_destroy",
    "va": "0x14083F01C",
    "rva": "0x0083F01C",
    "bytes": "B8 28 00 00 00 E8 5A CB 92 01 48 2B E0 48 83 39 00 74 09 48 83 C1 10 E8 84 2C 02 00 48 83 C4 28 C3",
    "mask": "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 9
  },
  {
    "name": "ignore_vector_free",
    "va": "0x140764EA8",
    "rva": "0x00764EA8",
    "bytes": "B8 28 00 00 00 E8 CE 6C A0 01 48 2B E0 48 8B 09 48 85 C9 75 05 48 83 C4",
    "mask": "xxxxxxxxxxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 9
  },
  {
    "name": "callback_inline_clone",
    "va": "0x142805DA4",
    "rva": "0x02805DA4",
    "bytes": "48 8D 05 6D D2 C2 03 48 89 42 10 48 8D 42 18 48 8B 49 08 48 89 08 48 8D 0D B7 D2 C2 03 48 89 4A 10 C3",
    "mask": "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 6
  },
  {
    "name": "callback_vtable_destroy",
    "va": "0x1407A5680",
    "rva": "0x007A5680",
    "bytes": "B8 28 00 00 00 E8 F6 64 9C 01 48 2B E0 48 8B 01 33 D2 48 83 C4 28 48 FF",
    "mask": "xxxxxxxxxxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 9
  },
  {
    "name": "callback_inline_destruct",
    "va": "0x1427FCC44",
    "rva": "0x027FCC44",
    "bytes": "40 53 B8 20 00 00 00 E8 30 EF 96 FF 48 2B E0 48 8D 05 FE 63 C3 03 48 8B",
    "mask": "xxxxxxxxxxxxxxxxxxxxxxxx",
    "minimum_unique_text_prefix": 11
  }
]
```

The experiment need not call these transitive helpers directly. Their identities
bound callback clone/destruction behavior reached by the constructor/destructor
path. The event constructor also calls assignment lookup helpers
`FUN_143986324` and `FUN_14398639C`; those are internal constructor dependencies,
not separately callable experiment APIs.

## Callback Vtable And Event-State Invariants

The callback vtable-like object is at:

```text
VA  0x146433078
RVA 0x06433078
```

It contains relocated function pointers. Runtime validation must compare decoded
qwords to `module_base + RVA`; raw on-disk pointer bytes are not a valid ASLR-safe
signature.

| Slot | Expected pointer | Recovered role |
| --- | --- | --- |
| `+0x00` | `module+0x02805DA4` | Inline callback clone. |
| `+0x08` | `module+0x01958660` | Callback interface entry; exact downstream purpose not independently named. |
| `+0x10` | `module+0x007A5680` | Generic destroy dispatch. |
| `+0x18` | `module+0x027FCC44` | Concrete inline destructor. |

Every parsed prefix event and the completed tail must satisfy:

```text
event+0x50 == module+0x03991F24
event+0x60 == 0
event+0x70 == module+0x06433078
event+0x78 == chart
```

This concrete callback is inline and allocation-free. Its capture is the chart,
not the event's own address, so moving ungrouped/empty-IgnoreSound prefix events
would not create a callback self-pointer. The scoped-reserve architecture does
not move them at all.

## Parser Reserve Callsite And Capacity Semantics

Exact 1.005 instruction context:

```text
0x1439B3695  LEA  RSI,[R15+0x80]        ; event-vector header
0x1439B369C  MOV  RCX,RSI
0x1439B369F  CALL FUN_143997808          ; destroy/free old events
0x1439B36A4  MOV  EBX,0x200
0x1439B36A9  CMP  dword ptr [RSI+0x0C],EBX
0x1439B36AC  JGE  0x1439B36B8
0x1439B36AE  MOV  EDX,EBX
0x1439B36B0  MOV  RCX,RSI
0x1439B36B3  CALL FUN_142AD3EF4          ; reserve(header,512)
0x1439B36B8  CMP  dword ptr [R14+0x0C],EBX ; camera header, not event header
```

Context bytes:

```text
BB 00 02 00 00 39 5E 0C 7D 0A 8B D3 48 8B CE
E8 3C 08 12 FF
41 39 5E 0C 7D 0A 8B D3 49 8B CE E8 A8 90 81 FE
```

Within the parser, event capacity is referenced only at:

- `0x1439B36A9`: lower-bound check before reserve;
- `0x1439B39F2`: monotone append `count+1 <= capacity` check;
- `0x1439B3A9A`: chord append `count+1 <= capacity` check.

No subsequent equality-to-512 assumption was found. A normal-return reserve with
stored capacity at least 513 therefore leaves parser semantics unchanged and
prevents event growth for the 512-event prefix plus one tail. Camera reserve uses
a separate header and helper and is unchanged.

## Complete Fail-Closed Reserve Gate

A shared reserve-helper detour is viable with the existing MinHook-based raw RVA
hook infrastructure, but `FUN_142AD3EF4` is shared by unrelated callers and may
run on other threads. Every nonmatching call must invoke the original trampoline
with exactly its original arguments.

Substitute `requested_capacity=513` only when all of these predicates hold:

1. Exact verified 1.005 executable identity and every required signature pass.
2. Outer `piano_score_expand` call is the persistent call at
   `0x143999F4E`, with return/caller gate `0x143999F53`
   (`persistent_chart_expand_caller`). The call instruction is
   `E8 B1 F1 01 00`.
3. Reserve detour `_ReturnAddress()` is exactly `module+0x039B36B8`.
4. Requested capacity is exactly 512.
5. `header == chart+0x80` and the header is exactly
   `{data=null,count=0,capacity=0}` after parser reset.
6. Dedicated TLS context is active, `original_inflight` is true, and nesting
   depth is exactly one.
7. TLS selection, route, lease, song, policy, registry, descriptor, direct
   native controller, wrapper, chart-row, fixture-hash, and source-row
   identities still match the admitted transaction. The controller identity is
   the synchronous callback capture qualified below, not the retained completion
   UObject observation.
8. TLS declares final generated event count 513 and the exact restricted fixture.
9. The per-transaction reserve-hit count is zero; a second matching hit fails
   closed and must not authorize a tail.
10. A process-global exclusive extended-transaction claim is owned by this
    transaction; no competing extended expansion is admitted.

The current `ChartAudioExpandTlsSnapshot` does not retain the expected wrapper,
chart-row/header address, fixture identity, target capacity, hit count, or reserve
result. A dedicated trivially copyable TLS reserve context is required; those
facts must not be inferred inside the shared helper detour.

After the trampoline returns normally, record the actual pointer/count/capacity.
Tail authority requires `data != null`, `count == 0`, and `capacity >= 513` at
that point, followed by the complete post-parser prefix validation below. No
experiment path may attempt to recover from a native allocator fault and continue
the parser with an uncertain header.

## Prefix Validation After Original Expansion

Before writing slot 512, revalidate transaction identity, then require:

### Vector/chart predicates

- Header address is still exactly `chart+0x80`.
- Data pointer equals the recorded successful-reserve allocation.
- `count == 512` and `capacity >= 513`.
- Allocation extent is readable/writable for at least `capacity * 0x90` bytes.
- Chart and wrapper identities still equal the admitted persistent object.
- `chart+0x48` is finite and greater than zero.
- `chart+0x30` is finite and equals the maximum accepted prefix row time within
  the native float comparison semantics.
- Camera header remains the parser-produced prefix header; the experiment has no
  camera-tail authority.

### Each prefix event `i` in `[0,511]`

- Address is exactly `data + i*0x90`.
- `+0x00 == chart`.
- `+0x08 == *(void**)(chart_row+0x18)` for the monotone side/controller.
- `+0x10 == 0` and `+0x18 == 0`.
- `+0x20 == 2*i`; the parser attempts monotone then chord construction and advances
  the ordinal for both, even when the empty chord is not appended.
- `+0x24` is finite, nondecreasing, and matches the corresponding descriptor
  `TimeStr` decoded with the live `chart+0x48` divisor.
- `+0x28 == 0.0f`.
- `+0x2C` is the exact packed FName resolved for that descriptor monotone.
- IgnoreSound header `+0x38/+0x40/+0x44` is empty.
- `+0x48 != 8`; assignment resolution succeeded.
- `+0x49` is the constructor-resolved lookup-path/hand value, not a synthesized
  value.
- `+0x4A == 0`, NoteType/DotType match the descriptor at `+0x4B/+0x4C`, and
  `+0x4D..+0x4F` are zero.
- Callback state matches the four callback invariants above.

Any mismatch leaves the parser-built prefix at count 512. The larger coherent
capacity is safe to retain: consumers use count, and the next parser reset or
normal chart destructor frees the complete allocation through native lifecycle.

## Restricted Row-513 Constructor Recipe

Resolve the immutable tail monotone string with strict `FNAME_Find` before event
construction. Require a nonzero packed FName; do not add an unknown name.

Zero all `0x90` bytes at `tail = data + 512*0x90`, then invoke
`FUN_143987734` as follows:

| Constructor input | Exact source/value |
| --- | --- |
| `RCX` / destination | `tail` |
| `RDX` / chart | Persistent `wrapper`/chart admitted to the expand transaction |
| `R8` / side | `*(void**)(chart_row+0x18)`; outer expand supplies this same value to parser `R8` for monotones |
| `R9D` / ordinal | `1024` (`2 * zero-based source row 512`) |
| stack `+0x20` / time | Native float decoded from tail `TimeStr` as specified below |
| stack `+0x28` / strength | `0.0f` |
| stack `+0x30` / NoteType | Immutable tail descriptor NoteType |
| stack `+0x38` / DotType | Immutable tail descriptor DotType |
| stack `+0x40` / FName | Strictly resolved packed monotone FName |

Constructor result checks:

- returned pointer is `tail`;
- chart, side, ordinal, time, strength, FName, NoteType, and DotType match inputs;
- group links and IgnoreSound ownership remain empty;
- runtime state bytes remain initial;
- assignment `+0x48 != 8` and `+0x49` was resolved by the constructor;
- callback ownership remains empty before callback building.

The constructor itself calls `FUN_143986324`, falling back to
`FUN_14398639C`, to resolve input assignment/hand metadata. The writer must not
invent either byte.

### Exact tail time

Parser evidence:

```text
0x1439B36DE  call runtime float-setting getter
0x1439B36E3  store returned frame-rate divisor at chart+0x48
0x1439B379A  convert TimeStr first component through _wtof, then float
0x1439B37B5  convert TimeStr second component through _wtof, then float
0x1439B37BF  second_component / chart[+0x48]
0x1439B37D9  float(first_component) + quotient
0x1439B383C..0x1439B3851  max(old_max,row_time), stored at chart+0x30
```

For a descriptor `TimeStr` `seconds_frames`, reproduce the same single-precision
operation order:

```text
float row_time = float(seconds) + float(frames) / live_chart_frame_rate;
```

The current descriptor `time_str` is sufficient; no guessed frame rate or new
compiled time field is required. `chart_compiler.cpp` currently emits the string
from a 60-Hz offline frame calculation, but native decoding must still use the
live, validated `chart+0x48` value. Hard-coding 60 or the parser's fallback is not
equivalent.

### Callback embedding

The constructor does not call the callback builder. With `Chart* captured = chart`,
call:

```text
FUN_143982EA0(tail+0x50, &captured)
```

Then require the complete callback-state invariants. A parser-style temporary
event plus deep-copy is unnecessary and adds another ownership/failure boundary;
direct construction is safe only because the destination is the final stable
slot, remains outside `count`, and was zeroed first.

## Partial-Construction And Cleanup Boundaries

Direct disassembly proves this ordering in `FUN_143987734`:

1. It writes chart/time/strength, null links, side, and ordinal.
2. It zeroes IgnoreSound ownership at `0x14398778C..0x143987790`.
3. It writes NoteType/DotType and zeroes runtime state.
4. It zeroes callback ownership at `0x1439877AD..0x1439877B1`.
5. It stores FName and assignment sentinel `8`.
6. Only then does it call assignment lookups at `0x1439877C2` and
   `0x1439877E2`.

Direct callback-builder ordering:

```text
0x143982EA0  callback function = 0
0x143982EAE  external state pointer = 0
0x143982EBA  temporary/purecall vtable
0x143982EC1  captured chart
0x143982ECB  final vtable
0x143982EDB  callback function = FUN_143991F24  ; published last
```

Destructor-safe state machine:

| State | Count owns slot? | Permitted cleanup |
| --- | --- | --- |
| Zeroed, constructor not entered | No | Zero/no action. |
| Constructor entered or returned | No | `FUN_143988A04(tail)` is safe because owned fields were pre-zeroed and constructor allocates none. |
| Callback builder interrupted before final function write | No | Destructor sees callback function zero; other callback bytes are ignored. |
| Callback builder completed | No | Destructor dispatches through the complete final callback state. |
| Count committed to 513 | Yes | Native vector/parser/chart lifecycle owns destruction; do not separately destruct without first restoring count to 512 at a proved synchronous rollback point. |

This proves cleanup at native instruction boundaries for the known event-owned
fields. It does not prove that continuing the process after an arbitrary allocator
fault or unrelated engine fault is safe.

## Count-Last Commit And Synchronous Rollback

Retain original `max_time` and do not change event pointer or capacity after the
parser returns.

1. Construct and validate the complete uncounted tail.
2. Revalidate exact transaction, header pointer/data/count/capacity, prefix, tail,
   chart identity, and source identity.
3. Write `chart+0x30 = max(original_max_time, tail_time)`.
4. Re-read and validate max time.
5. Write event count `chart+0x88 = 513` **last**.
6. Re-read count, capacity, max time, and tail identity before returning from the
   expand detour.

Synchronous rollback before the expand detour returns:

- Before count commit: restore max time if changed, destruct the tail, then zero
  the slot.
- After count commit but before detour return: only if chart/header/data and all
  transaction identities remain exact, restore count to 512 first, restore max
  time, destruct the now-uncounted tail, and zero it.
- If external drift makes ownership uncertain, do not free or guess. Block the
  custom route and retain native ownership evidence.

After successful return, no asynchronous rollback is authorized. Native ownership
has committed:

- the next parser invocation calls `FUN_143997808`, which destroys count 513 and
  frees the single allocation;
- native chart destruction walks the current event count, destroys all 513
  events, and frees the vector as qualified below;
- mod abort, song switch, or list exit invalidates only immutable publication
  identity and must not free or dereference retained native addresses;
- no original allocation was detached or retained, so there is no orphaned block
  and no custom lifecycle journal to drain.

The exact native release instant and thread for list exit were not recovered.

A failed experiment that leaves a coherent capacity at least 513 but count 512 is
safe under recovered native semantics. The unused slot is not destructed because
it must already have been cleaned; native reserve-zero later frees the allocation.

## Hook Admission And Shutdown

The reserve helper is shared. Its detour must be installed transactionally after
all helper/callsite signatures verify and must maintain the original trampoline
for unconditional forwarding.

A reserve callback needs callback accounting even when it is not part of the
extended TLS transaction. Shutdown must prevent a trampoline or detour from being
removed while an unrelated reserve callback is forwarding.

Required shutdown ordering:

1. Close admission for new extended transactions.
2. Prevent new outer expand transactions and disable the outer expand hook as the
   hook coordinator requires.
3. Disable the reserve hook so new native calls bypass it.
4. Drain both outer-expand and reserve-detour in-flight callback counts.
5. Verify no TLS/global extended transaction remains owned.
6. Remove hooks and clear original function pointers/state.

Do not assume the outer expand lease accounts for reserve calls on other threads.
The reserve hook requires its own always-counted callback lifetime, including its
forward-only path. A nested reserve call on the admitted expand thread may use the
same coordinator only if reentrancy and shutdown drain semantics are explicit.

## Quiescence Evidence And Limits

Direct same-thread evidence:

```text
FUN_143999AF4
0x143999F4E  CALL FUN_1439B9104
0x143999F53  first caller instruction after expansion returns
0x143999F62  first recovered post-call chart write, chart+0x4C
```

The expand detour's post-original section runs before `0x143999F53`. The first
recovered chart-update path is separate: `FUN_1439B5290` calls
`FUN_1439BA628` at `0x1439B540B`. No same-thread chart consumer can interleave
between original expansion return and the detour's tail transaction.

The persistent owner stores the chart pointer at owner `+0xF48` before calling
expansion. Therefore global pointer unreachability and cross-thread quiescence are
not proved. The bounded inference is that native initialization is exclusive:
the original parser itself destroys, reallocates, and incrementally updates the
published chart without synchronization. The tail operation remains inside that
same call interval and does not extend mutation past expansion return. Runtime
thread-affinity evidence is still required before a stronger claim.

## Allocator Evidence, Risk, And Nonclaims

Native request sizes:

```text
512 * 0x90 = 0x12000 = 73,728 bytes
513 * 0x90 = 0x12090 = 73,872 bytes
```

Both requests use `FUN_142AD3EF4` and remain below its `0x20001` large-allocation
branch. The substitution adds no second allocation and increases the mandatory
parser request by 144 bytes. It may nevertheless select a different allocator
size class; runtime-initialized size tables prevented a static proof that both
requests round identically.

The reserve helper returns no status, and its callers assume success. Static
evidence does not establish whether OOM returns null, raises a C++ exception,
raises SEH, or invokes a fatal engine allocator path. Consequently:

- normal-return predicate failures are transactionally recoverable;
- native allocator failure is not claimed recoverable;
- C++ `catch (...)` is not assumed to catch SEH under `/EHsc`;
- an SEH handler must not attempt to continue with an uncertain vector header;
- “513 is equivalent to 512 under OOM” is expressly not claimed.

The scoped substitution is materially narrower than detached-copy because it
does not allocate a second full vector or retain an original allocation. If the
project requires recovery from native OOM/fatal allocator behavior, the experiment
remains blocked.

## Generalization Is Not Established

Reserving the exact final event count before parser construction would, in
principle, keep parser-created group links stable because no later event-vector
growth would occur. This is only a bounded architectural inference.

It is not approval to reserve 1,024 or 2,048 events or to append general tails:

- larger requests eventually enter a different allocator branch;
- chord, IgnoreSound, group, camera, and mixed-event tail construction require
  additional ownership and semantic qualification;
- the camera vector is separate;
- downstream dynamic-count evidence has not been runtime-qualified above 512;
- the generated source accessor limit remains 512 rows.

The first fixture must request 513 and append exactly one monotone event.

## Implementation And Runtime Nonclaims

This evidence does **not** establish that:

- the current repository contains the required reserve hook, TLS context, catalog
  records, immutable tail carriage, or 513 publication policy;
- a playable row 513 has run successfully;
- UI, scoring, completion, teardown, subsequent-song behavior, or row-513 audio
  has been validated;
- cross-thread chart access is impossible;
- allocator or arbitrary helper faults are recoverable;
- any 1.004 helper address or ABI matches 1.005;
- any chart other than the exact restricted fixture is safe.

Required runtime observations remain: reserve substitution hit exactly once;
capacity is at least 513; parser prefix count is exactly 512; tail prompt and
judgement occur; score/combo and completion include row 513; abort, natural
teardown, and a subsequent song destroy/rebuild the vector without stale state,
leak, double destruction, or fallback-route corruption.

## 2026-09-02 Pre-Expansion Authority Follow-up

This follow-up retains one later, bounded investigation of why an exact eligible
513-row activation still expanded to 512 events after descriptor retention,
selection admission, and reserve-hook availability had been established. It does
not replace the ABI and ownership evidence above.

Follow-up source checkpoint:

```text
2368bc362729f4c212b7aa3afb6bc57af9f1483d
```

The executable identity remained exactly:

```text
catalog build id: ff7rebirth-steam-win64-6a16ced2
game version:     1.005
PE timestamp:     0x6a16ced2
SizeOfImage:      0x099d9000
PE checksum:      0x0769ea6e
file size:        124317952
SHA-256:          752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc
image base:       0x140000000
```

Evidence sources and classifications:

- **Runtime evidence**: immutable session
  `C:/Users/makci/AppData/Local/FF7RPianoSongs/runtime-gates/20260902T150238Z-f519abcb0c7b`,
  principally `log-after.log`. The session was rolled back after capture.
- **Direct static evidence**: read-only Ghidra inspection of the matching 1.005
  program `ff7rebirth_.exe-1.005`, plus the source checkpoint named above. No
  Ghidra program state was changed.
- **Bounded inference**: the corrected authority envelope and lifecycle rules
  below. They are constrained by the exact caller and session evidence but have
  not run in the game.
- `rva_catalog.json` entries remain repository migration records. Their presence
  is not classified as independent recovery of the call semantics below.

### Deterministic circular dependency

The retained session established this order:

1. At `18:06:26.533`, `completion_timing_init_detour` observed native owner
   `0x7fefcee96b90`. The chart pointer was null both before and after the original
   call, and playback was absent. The resulting retained observation had owner
   generation 1, registry generation 0, empty song/profile identity, and no chart.
2. At `18:06:36.933..18:06:36.946`, the target fixture became current/selected
   and then entered guarded activation. Selection generation 38, route generation
   1, lease generation 1, exact song key, chart patching, and chart/audio
   admission all succeeded while playback remained unpublished.
3. The expansion received wrapper `0x7fed2eb19750`, chart row
   `0xf613b7e750`, and persistent caller return RVA `0x03999F53`.
4. Pre-expansion extended authority capture rejected the retained observation:
   its registry generation was 0, its chart had not been read or bound, wrapper
   equality failed, and no wrapper UObject identity was available.
5. No extended transaction, reserve substitution, tail construction, or count
   commit followed. Native expansion returned event count/capacity `512/512`.
   Playback publication occurred only later.

The source explains that order. `completion_timing_init_detour` is the sole native
producer of `RetainedChartOwnerObservation`. It records a chart only when the
owner's chart changes across that callback and binds song/profile identity from
the current playback snapshot. `retained_chart_owner_observation_impl` can lazily
bind a chart only after current **playback** matches the retained song/profile.
`begin_extended_chart_transaction`, however, requests that observation before
calling the original expansion. In the observed first-activation lifecycle there
is no intervening producer and playback cannot be published until later
PlaySetup. The observation therefore cannot become current in time to authorize
the expansion that it is being asked to authorize.

This is a deterministic circular dependency for the observed activation, not a
cache, descriptor, fixture, startup-order, or reserve-hook loss. It is not a
universal claim that every native first activation must have the same callback
history. It proves that playable admission cannot depend on that history. A
retained observation from an older activation is also not a substitute: the
global is explicitly cleared only at duration shutdown, may contain a null or
previous wrapper, and does not itself own the current selection/admission lease.
It may support later diagnostics only after current identity is independently
proved.

### 2026-09-02 Native chart ownership and observation follow-up

This later result supersedes the UObject requirement and observation-only next
step in the earlier owner-recovery hypothesis. It uses clean source checkpoint:

```text
c95bac83814a83b162b5efe42b107ffe7e444bae
```

Evidence sources and classifications:

- **Runtime evidence**: immutable, rolled-back session
  `C:/Users/makci/AppData/Local/FF7RPianoSongs/runtime-gates/20260902T162602Z-633a77abe6d8`.
  Its observation ASI SHA-256 was
  `5ade619d84e9c777643028f7b1f766fa90de1aea369126cc2bbe7916442b5420` and
  `log-after.log` SHA-256 was
  `4946014397b43f633f9a3cc6b72ddce7886e7c48e3d64a388f1d33ec17be9b9f`.
- **Direct static evidence**: read-only Ghidra inspection of matching program
  `ff7rebirth_.exe-1.005`, retained ABI evidence above, and source at the named
  checkpoint. Ghidra state was not changed.
- **Bounded inference**: same-thread lifetime, cross-callback separation, and the
  corrected Integration state transition below. These claims remain scoped to
  the exact executable identity at lines 717-725.

#### Native chart construction, shared ownership, and callback capture

`piano_score_expand` RCX is the actual non-UObject native chart/parser object.
The persistent caller `FUN_143999AF4` constructs and owns it as follows:

1. The native controller arrives in RCX and is saved in RDI at `0x143999B4A`.
2. `FUN_14079F980` allocates `0x140` bytes. Allocation offsets `+0x08` and
   `+0x0C` receive strong and weak counts `1/1`, and allocation offset zero
   receives control-block table `PTR_FUN_14662E320`.
3. `FUN_1421FFCDC` (`0x1421FFCDC`, RVA `0x021FFCDC`) constructs the chart
   in-place at allocation `+0x10`, using controller `+0xF30` as its source or
   context. A separate path, `FUN_1439A0744`, constructs the same chart type on
   the stack, expands it, inspects its vector, and destroys it. The chart type
   therefore neither is nor intrinsically requires a UObject or shared allocation.
4. The persistent controller's shared pair is controller `+0xF48` for the chart
   object and `+0xF50` for its control block. At `0x143999E46` the new chart is
   published into that pair; `FUN_140DD5E94` transfers/releases the prior control
   block.
5. Before expansion, the caller installs a 64-byte callback closure at chart
   `+0xF0..+0x12F`. The writes at `0x143999E80` and `0x143999EAF` place the raw
   native controller in the closure at chart `+0x118`. Callback target
   `FUN_143991DB4` uses that capture to call `FUN_1439B9340(controller+8)`.
   The capture is a borrowed native controller pointer, not a UObject handle,
   reference count, completion owner, or independently retained owner.
6. Immediately before `0x143999F4E`, RCX is reloaded from controller `+0xF48`,
   RDX is the chart row, and R8/R9 are residual rather than authority. The exact
   call returns at `0x143999F53` (RVA `0x03999F53`).

The controller relation is therefore:

```text
chart+0x118       -> raw native controller callback capture
controller+0xF48 -> chart object
controller+0xF50 -> native shared-pointer control block
```

The earlier interpretation was wrong only in identifying this controller as the
completion UObject. A safe transaction-local reciprocal read of
`(*(chart+0x118))+0xF48 == chart` is meaningful. No UObject identity predicate
belongs on either the chart or this captured native controller.

Direct teardown evidence establishes ordinary native ownership. Chart destructor
`FUN_143988A30` (RVA `0x03988A30`) destroys the `+0xF0` and `+0xB0` callback
closures, state at `+0x90`, the event vector at `+0x80` through
`FUN_143987C18`, camera/vector state at `+0x70`, and finally resets the embedded
table. Control-block destructor `FUN_143995700` invokes that chart destructor on
control block `+0x10`. The shared-pointer release path decrements the strong
count, destroys the chart at zero, then releases the weak/control block. The raw
callback capture does not extend controller lifetime by itself.

#### Successful observation and the failed UObject model

The immutable session observed the exact first-activation interval without
changing reserve arguments or chart memory:

- Completion timing first observed completion owner `0x7fefc9f37050` with null
  chart before and after its original call and with playback absent. This is the
  separate late `RetainedChartOwnerObservation` path.
- Fixture admission succeeded for selection generation 41, route generation 1,
  lease generation 1, activation generation 1, and song key
  `3028907728447002289`. The score-expand wrapper was `0x7fed29b85790`.
- Before original expansion, exact caller and guarded selection/admission
  authority were true. `wrapper+0x118` was readable and nonnull; the embedded
  `wrapper+0x80` header relation/read was exact. The captured pointer failed
  UObject identity, and the observation candidate consequently did not attempt
  its reciprocal read.
- The reserve callback was accounted, received requested capacity 512, and
  forwarded 512 unchanged as required by observation-only mode.
- After original expansion, the same captured pointer was stable, caller and
  admission authority remained exact, and the same embedded header contained
  count/capacity `512/512`. UObject identity remained invalid.
- PlaySetup and playback publication occurred only afterward. The later completion
  observation then read the same chart through its independent completion-owner
  path.

Thus the first failed predicate was the incorrect UObject model, not an unreadable
or unstable chart/controller capture, header mismatch, admission loss, or reserve
callsite mismatch. The session did not mutate or prove a 513 event.

#### Corrected synchronous authority envelope

The smallest fail-closed Integration authority is one synchronous transaction:

**Before original expansion**

1. Require the exact persistent caller, nonnull chart/chart-row, eligible exact
   513 descriptor, current policy/hash, depth-one original-inflight chart/audio
   TLS, and exclusive global claim.
2. Require guarded selection/audio admission identity: immutable registry storage,
   song/profile pointers, selection generation, route/lease generations, song
   key, route lifecycle epoch, activation generation, and preparation ordinal.
3. Read a nonnull native controller from chart `+0x118`; safely require
   controller `+0xF48 == chart`. Bind controller, chart, chart row, and the exact
   embedded event-header address `chart+0x80` only into the thread-local
   transaction. Do not inspect UObject metadata or native reference counts.
4. A stale or null `RetainedChartOwnerObservation` is not mutation authority.

**Inside the reserve callback**

1. Revalidate the same TLS/generation/admission/controller/chart relation, exact
   header, global claim, one-hit state, requested 512, and exact parser reserve
   return RVA `0x039B36B8`.
2. Require the embedded header to be pristine after parser reset, then substitute
   513 and retain the normal-return allocation/capacity result.
3. Every nonmatching call forwards the original request unchanged.

**After original expansion, before detour return**

1. Revalidate capture stability, reciprocal controller relation, admission/TLS
   identities, chart/chart-row/header addresses, and the recorded allocation.
2. Require native count 512, capacity at least 513, and all prefix/event/time
   invariants above before touching slot 512.
3. Construct and validate the tail, update maximum time, and publish native count
   513 last. Publication of the mod's pending token must succeed for the same
   activation generation before the synchronous transaction is considered
   successful.

The persistent caller cannot execute its post-call instructions until the detour
returns, and its shared member already owns the chart. The parser reserve callback
and finish phase are nested within that same call. This directly excludes
same-thread replacement during the interval. Cross-thread replacement remains a
nonclaim; repeated reciprocal/header checks narrow but do not atomically eliminate
that risk.

No pointer from this envelope may outlive the synchronous transaction. After a
successful count-last commit, the immutable committed token may retain only:

- registry storage and exact song/profile pointers;
- registry, policy, and selection generations plus descriptor hash;
- route generation, lease generation, song key, and route lifecycle epoch;
- activation generation and preparation ordinal.

It must not retain or later dereference the native controller, chart, chart row,
event header, vector allocation, callback capture, or a synthetic owner generation.
Later completion hold/inspection may use the separately qualified completion
UObject observation, but absence of that downstream authority must not invalidate
an already proven synchronous 513 commit.

#### Native ownership and rollback after count-last

Before count 513, cleanup follows the destructor-safe state machine above. If
publication is rejected while the detour still has exact synchronous identity,
restore count to 512 first, restore maximum time, destruct the now-uncounted tail,
and zero it. If ownership has drifted, block the custom route and do not guess.

After successful publication, native code owns the allocation and all 513 events:

- another parse calls `FUN_143997808(chart+0x80)`, destructs exactly the current
  event count, zeros count, and reserves zero to free/reset the allocation;
- replacement of the controller shared pair releases the old chart when its
  native strong count reaches zero;
- chart destruction invokes the callback/vector/event teardown chain above.

Mod terminal, list-exit, replacement, and shutdown paths must only invalidate the
immutable token. They have no authority for asynchronous native rollback or free.
The exact list-exit release instruction, release thread, and timing were not
recovered, and ordinary completion was not shown to destroy the chart immediately.

#### Terminal-generation serialization

Source review proves a check/publication ordering gap: a failed activation terminal
can arrive before `publish_committed_513`, find no committed record, and be lost.
The race itself was not observed at runtime.

Terminal recording and publication must share the committed-state serialization.
A monotonic failed-terminal generation watermark, paired with the existing
nonwrapping activation generation, gives the required ordering:

- failure terminal N before publication records N; publication N rejects and
  performs the synchronous rollback above;
- publication N before failure terminal N creates the token; that terminal then
  invalidates the exact token without dereferencing native memory;
- stale terminal N cannot invalidate or block newer generation N+1;
- AudioPublished and ExpandFinished do not record a failure watermark.

Supersession must notify this same state transition. Replacement expansion retires
the old immutable token before admission and binds only its new synchronous chart.
Shutdown closes admission, drains outer/reserve callbacks, clears transaction and
immutable publication state, and leaves native allocations to native ownership.
No wait, polling count, or tester delay is introduced: early input either satisfies
all predicates or follows the native 512 path recoverably.

#### Disposition, nonclaims, and next Integration proof

No further observation-only run is required before a focused Integration edit.
The runtime session establishes capture readability and pre/post stability, while
direct static evidence establishes its native type, reciprocal relation, shared
ownership, and destructor chain. The first Integration run must still prove:

1. synchronous authority exact pre/reserve/post, including controller reciprocal
   equality and embedded-header identity;
2. one exact reserve substitution from 512 to 513 and validated returned capacity;
3. parser prefix count 512, complete prefix/tail/callback validation, max-time
   update, and count-last commit 513;
4. immutable pending-to-active publication only after exact playback promotion;
5. `[extended_chart_identity] status=proven` from selection/admission plus
   synchronous native authority, without completion-owner or UObject dependence;
6. terminal-before-publication rollback, terminal-after-publication invalidation,
   replacement/list-exit retirement, and clean native teardown;
7. first activation, warm-cache retry, early-input fallback, an ordinary 512 chart,
   and diagnostic 520 isolation without timing waits.

No cache payload or policy change follows from this result. No recovered catalog
RVA changes. Production use of chart `+0x118` and controller `+0xF48/+0xF50`
must remain gated to this exact executable and retain/startup-validate the caller
instructions establishing the callback capture and shared publication.

This result does **not** establish native class names, cross-thread quiescence,
exact controller-destruction/list-exit timing, allocator-fault recovery, a
successful playable row 513, or compatibility with another executable build.
