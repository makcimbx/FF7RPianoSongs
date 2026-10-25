# Older Runtime FontInfo HUD contracts

Bounded read-only static follow-up, completed 2026-10-24; filename retained as
requested for the 2026-10-23 HUD investigation. This covers only the nine new
local Runtime FontInfo contracts in `SongHudSharpTextAdapterEvidence-20261023.md`.
It does not repeat the eleven legacy HUD or stock-font-loader recoveries in
`SongHudOlderBuildEvidence-20261023.md`.

## Outcome and identity

**All nine new contracts have independently recovered older-import counterparts,
with instruction-complete unique windows and matching consumed ABI/layout.**
They are ready for lead catalog integration as `static_import_contract`, not
disk-authenticated or gameplay-qualified evidence. No inspected layout difference
requires an older-only branch in the current local FontInfo adapter.

All program-specific queries explicitly selected Ghidra `ff7rebirth_.exe`, which
reports `/ff7rebirth_.exe-1.004`, PE x86:LE:64 Windows, image base `0x140000000`.
The canonical build is `ff7rebirth-steam-win64-68fd6fde`; its expected identity and
the retained imported-header correlation are in the catalog and preceding older
HUD evidence (timestamp `68fd6fde`, image `09bad000`, checksum `07877c99`).

**The older disk limitation remains:** the import's original executable path now
contains the newer executable, and no independently authenticated older file or
full original-file import hash was available in the bounded prior search. This
pass uses that explicitly identified older import, not newer address deltas. It
does not claim old raw/mapped disk uniqueness, original-file SHA authentication,
or parity of the default run's observed loaded font objects.

## Catalog-ready windows

RVA values below are older-only, relative to `0x140000000`. Each exact sequence
was read from its recovered older location, checked against complete instructions,
and independently searched over the explicit older Ghidra program. **Each has
one match, at the listed RVA.** Use an all-`x` mask of exactly N bytes; there are
no wildcard displacements. C = callable; V = noncallable signature-only verifier.
No row installs a hook. In particular the optional destructor is a chain verifier,
not a new standalone cleanup call.

| Catalog ID | Kind | Older RVA | N | Exact bytes |
| --- | --- | --- | --- | --- |
| `song_hud_font_info_construct` | C | `0x008d3b2c` | 33 | `48 89 5c 24 08 57 b8 80 00 00 00 e8 f4 b3 7d 01 48 2b e0 48 8b 84 24 b0 00 00 00 48 8b fa 48 89 11` |
| `song_hud_item_measure` | C | `0x051e0f7c` | 34 | `48 89 5c 24 10 48 89 74 24 18 48 89 7c 24 20 55 48 8d 6c 24 80 b8 80 01 00 00 e8 95 df ec fc 48 2b e0` |
| `song_hud_canvas_dpi` | C | `0x00cc3b58` | 22 | `80 b9 94 00 00 00 00 0f 85 71 ab c4 01 c5 fa 10 81 a4 00 00 00 c3` |
| `song_hud_font_cache_dispatch` | V | `0x0095e13c` | 46 | `b8 28 00 00 00 e8 ea 0d 75 01 48 2b e0 48 8b 81 48 01 00 00 44 0f b6 50 30 45 85 d2 74 10 41 83 fa 01 75 05 e8 cb c7 ff ff 48 83 c4 28 c3` |
| `song_hud_font_info_layout` | V | `0x008d3b71` | 22 | `49 8b 01 48 89 41 40 44 89 41 48 c7 41 4c 00 00 00 00 c6 41 50 03` |
| `song_hud_font_optional_layout` | V | `0x051e0fd1` | 46 | `0f b6 4a 30 85 c9 0f 84 15 01 00 00 83 f9 01 0f 85 84 01 00 00 48 8d 87 50 01 00 00 80 78 58 00 74 16 48 8b d0 48 8d 4d 10 e8 59 2f 6f fb` |
| `song_hud_font_typeface_source` | V | `0x051e1011` | 64 | `44 8b 82 3c 01 00 00 48 8d 44 24 30 4c 8d 8a 40 01 00 00 48 89 44 24 20 48 8d 4c 24 58 c7 44 24 30 00 00 00 00 c5 f8 11 44 24 40 66 c7 44 24 34 00 00 48 c7 44 24 38 00 00 00 00 e8 db 2a 6f fb` |
| `song_hud_font_optional_init` | V | `0x00e6db8b` | 26 | `48 89 b3 48 01 00 00 48 8b 74 24 38 c6 83 a8 01 00 00 00 c7 43 14 02 00 00 00` |
| `song_hud_font_optional_destroy` | V | `0x02b33450` | 37 | `b8 28 00 00 00 e8 d6 ba 57 ff 48 2b e0 80 79 58 00 74 0d c6 41 58 00 48 83 c1 30 e8 28 9c c5 fd 48 83 c4 28 c3` |

## Independent recovery chain

The start was the **already recovered older text-item constructor's actual
vtable**, `0x1457c0d48`, not a shape/delta translation of newer functions. Its
measurement virtual `+0x38` points to `0x1451e0f7c`; emission virtual `+0x40`
points to `0x14095e13c`. Following those bodies independently yields the FontInfo
constructor, native copies/destruction, character-list lookup and Canvas DPI.

The three direct callable ABIs are ordinary Windows x64:

```cpp
void* construct_font_info(void* uninitialized_payload, void* retained_font,
    int32_t size, const uint64_t* genuine_typeface, const Outline20* outline);
Vec2f* measure_item(void* initialized_item, Vec2f* output, float font_scale);
float canvas_dpi(void* borrowed_fcanvas);
```

### Constructor, typeface and payload

Constructor `0x1408d3b2c` receives destination RCX, FontObject RDX, signed size
R8D, full FName64 pointer R9 and the fifth borrowed Outline20 pointer on the
stack. Its own frame loads that pointer at `0x1408d3b3f` from RSP `+0xb0`.
The destination is returned in RAX. Its actual stores establish:

| Local FontInfo field | Offset / semantics |
| --- | --- |
| FontObject | `+0`, borrowed current retained font |
| FontMaterial | `+8`, initialized null |
| OutlineSettings | `+0x10..+0x2f`, full 32-byte native input copy |
| CompositeFont shared pair | `+0x30/+0x38`, initially null |
| Typeface FName64 | `+0x40`, full eight-byte copy from R9 |
| Requested size | signed int32 `+0x48` |
| Letter spacing | `+0x4c`, initialized zero |
| Native fallback policy | byte `+0x50`, initialized three |

With a nonnull FontObject the constructor may initialize/query the real
FontProviderInterface class at `0x1492ec568`, resolve its interface using
`0x140885660`, and invoke interface virtual `+0x10`. It is an owning native
game-thread operation, not a read-only accessor or worker-safe constructor.

The older measurement fallback independently supplies the real UFont typeface:
`0x1451e1011` loads the explanatory legacy size from UFont `+0x13c`;
`0x1451e101d` forms R9 = UFont `+0x140`; `0x1451e104c` calls the constructor.
The constructor copies the pointed-to eight bytes, proving the same genuine
FName64 source used by the current adapter. The fallback also initializes native
Outline20 with zero first 16 bytes and black/alpha-one last 16 bytes. Do not
fabricate a face name or mutate either legacy size or typeface.

### Runtime dispatch and native measurement

`0x14095e13c` reads the item's genuine UFont at item `+0x148`, then cache-kind
byte UFont `+0x30`. Zero enters the baked/offline emitter `0x1451dd004`; one
enters runtime emitter `0x14095a930`. Unknown values do not enter either normal
emission route. This is actual older branch semantics, not an assumed enum value.

Measurement `0x1451e0f7c` independently makes the same cache-kind split. It reads
optional payload item `+0x150` and initialized byte payload `+0x58`, i.e. item
`+0x1a8`. When present it copies through owning native helper `0x1408d3f58`;
otherwise it constructs the legacy fallback above. It then copies into its
private measure FontInfo, acquires the native renderer measure service through
`0x1411de6cc` from `0x149247ea0+0x20`, and invokes service virtual `+0x10`
with the item's FText, that same FontInfo and supplied font scale. The actual
third argument is a float: `0x1451e0fcd VMOVAPS XMM6,XMM2`, later supplied to
the native measure service. It multiplies resulting x/y by item `+0xa0/+0xa4`,
releases all temporary owners, and returns the output pointer.

Thus local requested size must be measured through this item, with item Scale
one, rather than multiplying old SmallFont unit widths. This consumed ABI and
the current size-fit policy do not need an older-only adjustment.

### DPI and size-keyed raster source

Runtime emission at `0x14095a9c8` obtains `float canvas_dpi(FCanvas*)` from
`0x140cc3b58`, preserves XMM0 in XMM6 at `0x14095a9f4`, then passes XMM6 as
XMM2 at `0x14095aafa` to character-list lookup `0x1408d3c80` (CALL
`0x14095ab0f`). RDX is the owning FontInfo, R9 the actual native settings at
`0x1470dfe70`. This directly connects font size and the same native DPI used
by rendering; no Windows DPI or unit-scale assumption is involved.

The DPI getter checks Canvas byte `+0x94`. Clear returns float `+0xa4`; set
branches to `0x14290e6d6`, which loads native float one from `0x1460a26f4`
and returns. The 22-byte gate above includes the actual branch displacement.

The recovered character-list lookup `0x1408d3c80` owns a FontInfo copy, hashes its
font/settings/scale, and compares the requested Size in cache-key `+0x48` and
raster scale in cache-key `+0x78` before reusing a character list. On a miss it
constructs the native list through `0x1447d0448`, wraps/stores native shared
ownership, and releases temporary owners. A new Size therefore selects native
size-keyed glyph work rather than merely magnifying the previous glyph geometry.
This is native raster/cache behavior, not certification of particular glyph ink.

### Optional ownership, initialization and cleanup

Older ordinary item construction stores the genuine UFont at item `+0x148`
(`0x140e6db8b`) and clears initialized byte `+0x1a8` (`0x140e6db97`). The new
adapter can therefore construct directly into the uninitialized local payload
`+0x150` and mark `+0x1a8` only after successful native construction. Payload size
`0x58`, existing item storage `0x1b0`/FText `0x18`, and current alignas(16)
storage are consistent with the independently recovered native uses. No raw
owning FontInfo or item may be memcpy-cloned.

Native copy `0x1408d3f58` copies the payload and increments a nonnull shared
CompositeFont controller's strong count at `+8`. Existing older item destructor
`0x140e6dfd8` invokes `0x142b33450(item+0x150)` before releasing FText. The
optional destructor tests/clears payload `+0x58` and calls `0x14078d098` on
shared pair `+0x30`. No standalone optional cleanup is needed or proposed.
The ordinary synchronous measure/submit/owning-item-destruction sequence remains
valid. Existing authority loss/native-fault poisoning with uncertain owners must
remain unchanged; these signatures do not manufacture native lifetime authority.

## Integration boundary and remaining gaps

- All nine selected-build keys above may be added by the lead with import-only
  evidence. Their current capability gate already requires the complete set;
  partial admission must continue to preserve legacy fallback rather than invoke
  unproved sharp-font operations.
- Consumed local payload/optional/typeface/cache/DPI offsets and call ABIs match
  the current adapter. Existing qualified SmallFont identity and frame checks,
  copied cache-kind/typeface rechecks, matching DPI, Scale one, native size-fit
  measurements and poison semantics still apply. No new hook, stock loader call,
  global/UFont/widget mutation, asset pipeline or atlas machinery is required.
- The newer observation of a populated Runtime SmallFont is **not** older source
  availability evidence. Older Runtime availability and actual appearance remain
  unobserved; the existing Offline/unsupported fallback policy must remain.
- Old executable disk authentication/raw-mapped scans and older gameplay, glyph
  sharpness, fallback quality or pause/UI-scale acceptance remain unproved. They
  do not need to delay the lead's independent default-build candidate.

Recovery recipe: select the older import explicitly; use the older item vtable
slots above; follow native measurement/runtime dispatch to constructor, cache and
DPI; follow the already-recovered item destructor to optional destruction. Read
the listed complete byte windows and run exact whole-program pattern searches.
Do not derive any of these records by shifting a newer address or wildcarding
away an unresolved ABI/layout difference.

Only this new evidence file was authored. No production/catalog/generated edits,
builds, live reads/attachment/control/launches, native invocation, installation,
commits, subagents or user-song changes occurred. Ghidra automatically retained
one oversized disassembly response outside source; it is not a required evidence
artifact or an authored runtime change.
