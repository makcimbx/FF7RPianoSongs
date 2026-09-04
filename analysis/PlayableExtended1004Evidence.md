# Playable Extended-Chart Build 1.004 Static Evidence

This file retains the direct static evidence recovered for applying the existing
transactional extended-chart transport to the exact Steam build 1.004 image. It
is an engineering evidence record, not runtime qualification, a release claim,
or authority for a different executable. Build 1.004 must remain fail closed
until these records are integrated, reviewed, and exercised through a protected
human-controlled runtime session.

## Evidence Scope And Identity

The read-only Ghidra investigation was completed in runtime-research session
`ses_fa2bdcc79ffeRsCwMXFUyL9jq7`. It did not modify repository, Ghidra, game,
cache, or Git state.

```text
catalog build id: ff7rebirth-steam-win64-68fd6fde
game version:     1.004
SHA-256:          76bd4d539878a8759433f1cd7c7b47d5e3abda0ef41572a664d2d606a9f09dc8
image base:       0x140000000

comparison id:    ff7rebirth-steam-win64-6a16ced2
comparison build: 1.005
comparison SHA:   752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc
```

Evidence was established by matching decompiled semantics, call graphs, strings
or constants where available, exact call neighborhoods, structural offsets, and
unique executable bytes. Raw byte similarity to build 1.005 was not treated as
authority. Every function and address below is scoped to the exact 1.004 image.

## Recovered Transport Map

| Contract | 1.004 symbol | RVA | Evidence |
| --- | --- | ---: | --- |
| Persistent caller | `FUN_143c40db4` | existing catalog record | Exact call edge, controller ownership, capture construction. |
| Persistent score-expand call | call | `0x03C4120E`, return `0x03C41213` | Unique call bytes and semantic caller. |
| Score expand | `FUN_143c5fad8` | `0x03C5FAD8` | Existing catalog signature, body, parser call. |
| Score parser | `FUN_143c59e64` | `0x03C59E64` | Existing catalog signature, semantics, call graph. |
| Event-vector reserve | `FUN_142e2af84` | `0x02E2AF84` | Parser call, ABI/body, allocator chain, unique signature. |
| Reserve call | call | `0x03C59F0C`, return `0x03C59F11` | One image-wide call-byte match. |
| Event constructor | `FUN_143c318cc` | `0x03C318CC` | Existing catalog signature, two parser calls, field semantics. |
| Event deep copy | `FUN_143c317bc` | `0x03C317BC` | Existing catalog signature, owned deep-copy behavior. |
| Callback builder | `FUN_1423a4e30` | `0x023A4E30` | Sole parser caller, body, vtable/result publication. |
| Result callback | `FUN_1423ac0f0` | `0x023AC0F0` | Builder target and parentless-result forwarding. |
| Callback vtable | data | `0x05BF7E58` | Builder publication and all four slot semantics. |
| IgnoreSound insertion | `FUN_1408e4e48` | `0x008E4E48` | Exact parser call and matching ordered-unique set semantics. |
| IgnoreSound call | call | `0x03C5A2EB`, return `0x03C5A2F0` | Unique call bytes in chord context. |
| Event link | `FUN_143c35e90` | `0x03C35E90` | Existing catalog signature and exact link body. |
| Event destructor | `FUN_143c32c48` | `0x03C32C48` | Existing catalog signature and ownership cleanup. |
| Raw event growth | `FUN_142c76550` | `0x02C76550` | Parser append calls and relocation role. |
| Vector destroy | `FUN_143c3e99c` | `0x03C3E99C` | Count-driven destruction plus reserve-to-zero. |
| Strict FName find | `FUN_140882298` | `0x00882298` | Existing catalog identity, ABI/body, unique signature. |

The score-expand parser call is at `0x03C5FB34` with bytes
`E8 2B A3 FF FF`. Those bytes have three image-wide matches and must be
validated through the cataloged score-expand/parser identities and call
neighborhood, not as an independent exact signature.

Relevant parser calls are:

```text
constructor:      0x03C5A115, 0x03C5A1A3
callback builder: 0x03C5A1B0, 0x03C5A1FD
raw growth:       0x03C5A25F, 0x03C5A30E
deep copy:        0x03C5A276, 0x03C5A325
IgnoreSound:      0x03C5A2EB
event link:       0x03C5A37F, 0x03C5A38F
destructor:       0x03C5A3B4, 0x03C5A3C0
```

## Catalog-Ready Signatures

The following exact-build entries were missing from the 1.004 catalog at the
time of investigation. Masks contain one `x` per byte. Implementations must use
the catalog schema and validation rules already applied to the corresponding
1.005 records.

### Event-vector reserve

```text
RVA:   0x02E2AF84
bytes: 48 89 5c 24 10 48 89 4c 24 08 55 56 57 41 54 41 55 41 56 41 57 b8 20 00 00 00 e8 8d 3f 28 ff
mask:  xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

The 1.004 allocator retains the reserve ABI used by 1.005. The parser requests
512 native events. Its exact reserve call is:

```text
RVA:    0x03C59F0C
bytes:  E8 73 10 1D FF
return: 0x03C59F11
mask:   xxxxx
```

### Callback builder

```text
RVA:   0x023A4E30
bytes: 33 c0 4c 8d 41 28 48 89 01 48 89 41 10 48 8d 05 6c 29 85 03 48 89 41 20 48 8b 02 49 89 00 48 8d 05 03 30 85 03 48 89 41 20 4d 85 c0 74 0a 48 8d 05 8b 72 00 00 48 89 01 48 8b c1 c3
mask:  xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

### Result callback

```text
RVA:   0x023AC0F0
bytes: b8 28 00 00 00 e8 36 2e d0 ff 48 2b e0 48 8b 12 48 8b 09 48 83 c4 28 e9 9c 34 8a 01
mask:  xxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

### Callback vtable

```text
RVA:   0x05BF7E58
bytes: 40 1d 0d 42 01 00 00 00 d0 5b 8d 41 01 00 00 00 c0 6d 0b 42 01 00 00 00 20 58 0b 42 01 00 00 00
mask:  xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

Decoded slot RVAs are build-specific and must not reuse 1.005 constants:

```text
0x020D1D40
0x018D5BD0
0x020B6DC0
0x020B5820
```

The slots implement callback-state copying/vtable installation, capture access,
virtual dispatch, and destruction/free behavior.

### IgnoreSound ordered-unique insertion

```text
RVA:   0x008E4E48
bytes: 48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 b8 20 00 00 00 e8 ce a0 7c 01 48 2b e0 48 8b 01 48 8b f2 48 63 79 08 48 8b d9 48 8d 14 f8 48 3b c2 74 1f 48 8b 0e 48 39 08 74 0b 48 83 c0 08 48 3b c2 75 f2 eb 0c 48 2b 03 48 c1 f8 03 83 f8 ff 75 17 8d 4f 01 89 4b 08 3b 4b 0c 7f 21 48 8b 13 8b c7 48 8b 0e 48 89 0c fa 48 8b 5c 24 30 48 8b 6c 24 38 48 8b 74 24 40 48 83 c4 20 5f c3 8b d7 48 8b cb e8 0c 52 ef ff eb d3
mask:  xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

The exact piano-parser call is:

```text
RVA:    0x03C5A2EB
bytes:  E8 58 AB C8 FC
return: 0x03C5A2F0
mask:   xxxxx
```

### Strict FName find

The existing 1.004 catalog identity is corroborated by this unique prefix:

```text
RVA:   0x00882298
bytes: 48 89 5c 24 10 48 89 6c 24 18 56 57 41 56 b8 40 04 00 00 e8 80 cc 82 01 48 2b e0 48 8b 05 46 75
mask:  xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

### Persistent score-expand call

```text
RVA:    0x03C4120E
bytes:  E8 C5 E8 01 00
return: 0x03C41213
mask:   xxxxx
```

## Layout, Ownership, And Allocator Parity

Direct 1.004 evidence confirms that the existing transaction algorithm can be
shared with 1.005:

```text
event size:                    0x90
event vector header:           chart +0x80/+0x88/+0x8C
maximum time:                  chart +0x30
parser FPS:                    chart +0x48
final GroupIndex:              chart +0xA3
controller chart/control:      controller +0xF48/+0xF50
captured controller:           chart +0x118
monotone/chord source sides:    chart row +0x18/+0x10
IgnoreSound vector:            event +0x38/+0x40/+0x44
callback state:                event +0x50..+0x8F
native reserve request:        512
possible parser prefix count:  1024
```

The constructor, compact ordering, source-slot ordinals, link semantics,
destructor ownership, reverse rollback order, and count-last publication match
the retained 1.005 contract. Event destruction first cleans callback state and
then frees IgnoreSound storage. Vector destruction iterates the published event
count in `0x90`-byte strides.

The controller capture at chart `+0x118` is established directly by the
persistent caller before score expansion: controller `+0xF48/+0xF50` provides
the chart and control block, the caller places the controller in its capture
block, and the block copy into chart `+0x110..+0x12F` publishes it at `+0x118`.
Neither score expansion nor parsing replaces it.

The reserve helper retains the 1.005 capacity calculation. Its large allocator
rounds allocations above `0x20000` to `0x10000`, reserves and commits the complete
extent, requires the committed address to equal the reserved base, and stores
metadata externally. The 1.004 Binned3 initializer has the same 74 size classes,
`0x2001` size-to-bin map, and `0x10000` large-allocation granularity. No segmented
or lazy event allocation was found. Native fatal/OOM behavior remains outside the
recoverable transaction contract, so the existing bounded 8192-event product cap
remains required.

## Integration Authority And Remaining Runtime Qualification

This evidence authorizes one fail-closed implementation batch that:

1. adds the missing build-specific catalog records;
2. selects immutable helper bytes, return addresses, FName signature, and callback
   vtable slots by exact executable identity;
3. retains every existing signature, caller, layout, capacity, ownership,
   lifecycle, rollback, and count-last gate;
4. shares the transport algorithm rather than introducing a 1.004 mutation branch;
5. leaves unknown or partially validated builds at native 512 behavior.

No catalog schema, event-plan, runtime-cache-format, tail-layout, or transaction
algorithm change is justified by the recovered evidence.

Build 1.004 is still missing protected runtime evidence for reserve substitution,
tail construction, chords, dual events, IgnoreSound cardinalities 1/2/3, links,
count-last publication, ordinary-chart replacement, completion, cleanup, and
rollback. A later human-controlled transactionally recoverable scenario must
qualify those behaviors before a release claim. OOM/SEH recovery, support above
8192 events, and asynchronous retention of native pointers remain unclaimed on
both builds.
