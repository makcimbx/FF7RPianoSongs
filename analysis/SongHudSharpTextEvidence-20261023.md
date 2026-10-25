# Remaining gameplay metadata blur — bounded native recovery

2026-10-23, Debug; read-only log, repository, explicit default Ghidra import and
authenticated executable-on-disk research. Only this evidence file is authored.
No process attachment/read/control, native invocation, installation, build, source
or catalog change, commit, worker, font/atlas mutation, or user-song edit occurred.

## Outcome

**A size-keyed native runtime-font route exists. It is not another stock-field
selection tweak, and is conditional on an actually available Runtime UFont.**
The frozen log does not establish that font-cache type. Do not claim an
unconditional crisp-text fix, or cast the native title's custom font into UFont.

The smallest next discriminator is a bounded copied-fact diagnostic in the
existing qualified HUD frame: selected font's admitted cache-type byte, existing
unit metrics, requested draw scale and identity. If it is Runtime (1), the local
FontInfo/item path below is statically implementable without another hook or
custom atlas. If it is Offline (0), this route cannot turn its baked glyphs into
runtime glyphs; a genuine retained Runtime source remains a concrete blocker.
The current investigation performs no such live observation or installation.

## Actual remaining failure

Frozen input:
`C:/Users/makci/AppData/Local/FF7RPianoSongs/captures/song-hud-font-fallback-20261023-224849/observed-game.log`.
Line 2670 reports `render_font/small_font_fallback_blur_unresolved`, `detail=80`;
line 2671 reports `bridge_submitted/hud_submitted`. Configuration is admitted at
line 38. This establishes selection fallback and successful adapter submission,
not cache type, rasterization size, glyph atlas resolution or sharpness. In
particular **80 is not evidence of an 80px or 8px font height**. The owner reports
fuzzy text and sharp icons. Repeating Medium/Large reflection or assuming those
slots are better would not address this observed result.

Current source creates an owning Canvas text item from selected UFont, changes
only local Scale `+0xa0/+0xa4`, submits and destroys. This preserves native
ownership but does not change the glyph source size selected by the native font
cache. Existing stock-font evidence explicitly left that source quality unknown.

## Independent native dispatch and typed field

The existing text-item vtable `0x1455fb328` has emission virtual `+0x40` pointing
to `0x14120a240`. That function reads the item's Font pointer at `+0x148`, then
the Font's byte `+0x30`:

- 0 calls offline emission `0x1450153d8`;
- 1 calls runtime emission `0x14120a6a4`;
- other values do not take either rendering branch.

This is not a guessed UFont offset. The `FontCacheType` property record
`0x1462dd4d8` names string `0x1462dbcb8`, has enum kind `0x1e`, dimension 1 and
offset `0x30`, with enum thunk `0x1450a3734`. Enum parameters `0x146f45120`
point to two entries at `0x146a7b308`: `EFontCacheType::Offline=0`,
`EFontCacheType::Runtime=1`. Keep the existing typed UFont/engine-field/current-
frame checks around any read. The value is not a lifetime token.

For a Runtime UFont the ordinary path builds FontInfo using native constructor
`0x140b08498`, defaulting its size from UFont `+0x13c` and typeface from `+0x140`.
The `LegacyFontSize` property record `0x1462dda20` independently names
`0x1462dbc68`, has integer kind `0x03`, dimension 1 and offset `0x13c`.
This is explanatory evidence; the proposed adapter need not write/read that size
or any other UFont field beyond the newly qualified cache-type read.

## Native size-keyed local FontInfo path

The existing item already contains optional FontInfo storage at `+0x150`:
`FSlateFontInfo` payload length `0x58`, followed by its initialized byte at item
`+0x1a8`. Runtime emission `0x14120a6a4` and native item measurement
`0x145019964` both test that byte. If set, they copy the supplied FontInfo through
`0x140b088c4` instead of reconstructing it at the UFont legacy size.

Recovered effective constructor ABI (ordinary Windows x64):

```cpp
void* construct_font_info(void* uninitialized_info58, void* borrowed_font_object,
    int32_t requested_size, const uint64_t* borrowed_typeface_fname,
    const void* borrowed_outline_settings20);
```

The constructor returns the destination. RCX is destination, RDX FontObject,
R8D size, R9 typeface-FName pointer, fifth argument pointer is at entry RSP+`0x28`.
It writes:

| FontInfo field | Offset | Construction |
| --- | --- | --- |
| FontObject | `00` | borrowed supplied object |
| Material | `08` | null |
| OutlineSettings | `10..2f` | native 32-byte input copy |
| CompositeFont shared pair | `30/38` | null initially |
| TypefaceFontName | `40` | supplied full FName64 |
| Size | `48` | supplied signed int32 |
| LetterSpacing | `4c` | zero |
| Fallback policy | `50` | native default 3 |

The ordinary runtime caller supplies default OutlineSettings: zeros in its first
16 bytes and linear black RGBA `(0,0,0,1)` in its final 16 bytes. It supplies the
UFont's native typeface name; using a different/None typeface is not established
as equivalent to the current face. Retain that native typeface through a separately
qualified field read or a valid existing FontInfo copy; do not invent face names.
The constructor may query/initialize the native FontProviderInterface class and
query the supplied object's interface. It belongs inside actual game-thread,
outside-GC admission with post-native revalidation, never on a worker.

This does more than enlarge old glyph vertices: runtime emission gets the native
character list via `0x140b085ec`. Its key compares the copied FontInfo's Size at
key `+0x48`, along with face/object/composite/outline data and rasterization scale.
A different Size therefore obtains a different native size-keyed character list,
constructed on a miss through `0x14455d5a4`, before geometry emission. Native glyph
lookup is `0x14120b0f4`. The adapter should use the desired FontInfo size and local
item Scale `(1,1)`, not magnify a list generated at legacy size. This is a recovered
native raster-cache mechanism, not proof of a particular screenshot's appearance.

### Matching native measurement

`0x145019964` is the item's own measurement implementation (vtable `+0x38`):

```cpp
Vec2f* measure_item(void* initialized_item, Vec2f* out, float font_scale);
```

RCX=item, RDX=output, XMM2=font scale; RAX returns output. Disassembly preserves
XMM2 into XMM6 at `0x1450199b5`; do not pass the third argument as an integer.
The runtime branch uses the same optional FontInfo, obtains native font-measure
service from the native application/renderer, calls its virtual `+0x10` with the
item FText and copied FontInfo, and applies the item's local Scale to the result.
Thus the old unit SmallFont width multiplied by geometry scale is **not** a valid
measurement replacement for a new FontInfo size.

Pass the same native Canvas DPI scale as the draw path. Native getter
`0x1409edd00` has effective ABI `float(void* fcanvas)`: when Canvas byte `+0x94`
is clear, it returns float `+0xa4`; otherwise its cold branch returns one.
Both ordinary item drawing and runtime emission call it. Do not add Windows DPI
or assume the borrowed Canvas scale is always one. Measurement may populate the
normal native caches; this is not a read-only UObject accessor or a worker API.

### Owning item sequence and limits

Use the already recovered `0x18`-byte FText and `0x1b0`-byte item with native
construction/destruction. After successful ordinary item construction with the
same retained Runtime UFont, construct FontInfo directly in its still-uninitialized
optional payload at `item+0x150`, then mark only that **local** optional initialized
byte after constructor success. Measure/submit within the same admitted frame,
with all native boundaries revalidated; the existing native item destructor
destroys the optional before releasing FText. No engine font, widget, UFont,
global size, Canvas transform or persistent asset is changed.

Ownership is not memcpy-safe: FontInfo copy `0x140b088c4` increments its shared
CompositeFont owner at payload `+0x38`. Optional destructor `0x1427b4d18` tests
payload `+0x58`, clears it, then releases the pair at payload `+0x30` through
`0x140793670`. The existing item destructor already invokes this optional
destructor. Its initial constructor byte is false. Preserve poisoned-adapter
handling after uncertain construction/admission; do not destroy an unproven owner.
Use existing `alignas(16)` item storage; payload is naturally 8-aligned. No new
standalone destruction API is necessary for the healthy owning-item sequence.

**Do not bypass the cache-type dispatcher or item eligibility with a null/fake
UFont**, even though part of runtime emission can copy explicit FontInfo. The
normal native item path requires the actual UFont, and its Offline branch ignores
this runtime override. Availability of a suitable current Runtime source, and
preserving its actual typeface, are the remaining integration prerequisites.

## Current title font is not a proven direct Canvas source

The qualified title is an EndTextBlock, not an ordinary UTextBlock. Its registered
`SetFont` thunk `0x143d2d2d4` extracts an object argument and reaches setter
`0x14112b810`, storing it at widget `+0x1e0`; the existing Slate receiver is at
`+0x220`. Slate setter `0x140885580` stores that object at Slate `+0x2f0` and
rebuilds through `0x141121db0`. That rebuild follows custom font/locale/control
data (including font object `+0x5d0` weak state and `+0xf8` glyph/layout input),
macro/button handling and custom text layout. This is not a recovered
FSlateFontInfo member getter. It does not justify treating widget `+0x1e0` as
UFont, passing a guessed EndFont pointer to Canvas, or cloning title-widget state.
No small native custom-title emission/measurement ABI was established in this
bounded pass. That route is a concrete unresolved contract, not the selected fix.

## Exact default-program/disk windows

Program explicitly selected: `ff7rebirth_.exe-9457d4`, reporting
`/ff7rebirth_.exe-1.005`, base `0x140000000`. Installed executable was read from
disk only at `D:/SteamLibrary/steamapps/common/FINAL FANTASY VII REBIRTH/End/Binaries/Win64/ff7rebirth_.exe`.
Fresh SHA256 `752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc`,
size `124317952`, timestamp `0x6a16ced2`, SizeOfImage `0x099d9000`, checksum
`0x0769ea6e` match the canonical default build. No full imported-file hash claim.

Each window below is instruction-complete and exact, with one match each in the
explicit import, entire raw file and zero-filled PE-mapped image. Extracted disk
bytes match import bytes. Masks are all `x` of the listed length. Only the first
two and the DPI getter are candidate callables; the others are noncallable layout
verifiers. None is added to the production catalog by this research.

| Purpose | RVA / raw file offset | N | Exact bytes |
| --- | --- | --- | --- |
| FontInfo construction | `00b08498 / 00b07a98` | 33 | `48 89 5c 24 08 57 b8 80 00 00 00 e8 d8 36 66 01 48 2b e0 48 8b 84 24 b0 00 00 00 48 8b fa 48 89 11` |
| Item's own measurement | `05019964 / 05018f64` | 34 | `48 89 5c 24 10 48 89 74 24 18 48 89 7c 24 20 55 48 8d 6c 24 80 b8 80 01 00 00 e8 fd 21 15 fd 48 2b e0` |
| Native cache-type dispatch hot block | `0120a240 / 01209840` | 46 | `b8 28 00 00 00 e8 36 19 f6 00 48 2b e0 48 8b 81 48 01 00 00 44 0f b6 50 30 45 85 d2 74 10 41 83 fa 01 75 05 e8 3b 04 00 00 48 83 c4 28 c3` |
| Local FontInfo face/size/defaults | `00b084dd / 00b07add` | 22 | `49 8b 01 48 89 41 40 44 89 41 48 c7 41 4c 00 00 00 00 c6 41 50 03` |
| Measurement cache-type/optional-layout branch | `050199b9 / 05018fb9` | 46 | `0f b6 4a 30 85 c9 0f 84 15 01 00 00 83 f9 01 0f 85 84 01 00 00 48 8d 87 50 01 00 00 80 78 58 00 74 16 48 8b d0 48 8d 4d 10 e8 dd ee ae fb` |
| Canvas DPI scale hot body | `009edd00 / 009ed300` | 22 | `80 b9 94 00 00 00 00 0f 85 e9 04 b6 01 c5 fa 10 81 a4 00 00 00 c3` |

Recovery: follow current text-item vtable emission to its actual cache-type
dispatch, follow Runtime draw and measurement to their optional FontInfo branches,
then follow their native FontInfo constructor and character-list cache key. Check
the typed FontCacheType/LegacyFontSize metadata and enum entries instead of
assuming object offsets. Repeat exact-byte searches against the explicit import
and authenticated PE mapping. Existing catalog IDs affected by integration are
the `song_hud_text_*`, `song_hud_font_measure`/font-selection and owning-item
adapter families; no address is suitable for a second hook.

## Focused next checks / honest blocker

1. In the existing qualified callback, log cache type and existing font/scale
   facts once. This can accompany the next reviewed candidate; no attachment,
   alternate font load or wait is required. The frozen log cannot answer it.
2. If Runtime, preserve the genuine face and exercise owning optional FontInfo
   construction, native measurement, submit/destruction and authority/fault paths
   with focused adapter fixtures. Admit the new exact contracts before invocation.
3. Inspect actual metadata at its intended pixel size, Unicode fallback and changed
   UI scale; cache-list sizing is not per-glyph visual certification.

If only Offline UFonts are available, do not ship this as a blur fix or keep
changing engine field preference. A currently retained Runtime UFont/FontInfo
provider, or an independently recovered custom-title emission contract, is still
needed. No such provider is proven by the frozen log or the stock-font-field
inventory. No older equivalent was pursued while that default-source gap remains;
do not transfer this new conditional capability to the older build by assumption.
