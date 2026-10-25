# Gameplay card typography and complete-block geometry

Owner screenshots confirm the card draws but artwork covers the native note icon;
metadata is too small and cover proportions differ from the reference. The
reference has approximately 110px artwork at 942px viewport height. The running
candidate uses title-widget height times four, and unscaled SmallFont text.

## Static scaled-text recovery

Runtime-research worker, default-build static recovery only. The focused follow-up
below independently authenticates disk identity, exact instruction-complete
signatures and uniqueness. These are ready for canonical catalog admission, not a
claim that the catalog/source already consumes them. Generate headers, never
hand-edit them. Older-build support is unproved. No live process operation was used.

`K2_DrawText` string `0x1462b70f0` -> registration `0x146a732d0` -> FFrame thunk
`0x14507fa64`. The implementation is inlined in the thunk; do not call it with an
invented public ABI. It constructs a local native text item, sets uniform scale,
submits and destroys owning state synchronously:

| Anchor | Recovered contract |
| --- | --- |
| `0x14120773c` | `FText*(FText* destination, const FString* source)` retains/copies native text ownership; source string remains borrowed. |
| `0x140aee2cc` | `Item*(Item* storage, const Vec2* position, const FText*, UFont*, const LinearColor*)` owning Canvas text-item constructor. |
| `0x1407acd1c` | Temporary FText destructor, after item retains text. |
| `0x1417d9c0c` | `void(FCanvas*, Item*)` synchronous submission. |
| `0x140aee4e8` | Text-item destructor, not freeing caller storage. |

Base constructor `0x140aee350` initializes ScaleX/ScaleY at item `+0xa0/+0xa4`
to one. K2 thunk instructions `0x14507fe7d..0x14507fe85` independently assign the
parsed Scale there before submission. Stack item storage is `0x1b0` bytes;
temporary FText is `0x18` bytes. Existing shadowed helper uses the same native
construct/submit/destroy ownership pattern. No additional argument may be added
to that existing six-argument helper.

### Authenticated default-build handoff (2026-10-23)

The explicit Ghidra selector was `ff7rebirth_.exe-9457d4`, reporting project path
`/ff7rebirth_.exe-1.005`, PE x86:LE:64 Windows, image base `0x140000000`. The
installed executable was read **from disk only**, without opening a process:
`D:/SteamLibrary/steamapps/common/FINAL FANTASY VII REBIRTH/End/Binaries/Win64/ff7rebirth_.exe`.
Its identity matches catalog build `ff7rebirth-steam-win64-6a16ced2`:

- SHA-256 `752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc`;
- file size `124317952`; PE timestamp `0x6a16ced2`;
- SizeOfImage `0x099d9000`; checksum `0x0769ea6e`.

This is fresh disk authentication plus exact imported-byte correlation for the
windows below, not a full-file hash of Ghidra's imported program. No migration
record was used as independent ABI or byte evidence.

Every following window is wholly in `.text`, begins/ends at instruction boundaries,
and had exactly **one** match in each of: Ghidra's default-program byte-pattern
search, the entire disk file, and a PE-mapped disk image (headers plus all raw
sections copied to their VirtualAddress, remaining SizeOfImage bytes zero-filled).
The bytes at the expected RVA/raw offset equal the imported bytes. Masks are all
`x`, with exactly the listed byte length; no displacement is wildcarded.

| Purpose | RVA | Raw file offset | Bytes |
| --- | --- | --- | --- |
| FText from borrowed FString, callable entry, 26 bytes | `0x0120773c` | `0x01206d3c` | `48 89 5c 24 10 57 b8 60 00 00 00 e8 34 44 f6 00 48 2b e0 83 7a 08 01 48 8b f9` |
| Canvas text-item constructor, callable entry, 30 bytes | `0x00aee2cc` | `0x00aed8cc` | `48 89 5c 24 08 48 89 74 24 10 57 b8 20 00 00 00 e8 9f d8 67 01 48 2b e0 49 8b f8 49 8b f1` |
| FText destructor, callable entry, 24 bytes | `0x007acd1c` | `0x007ac31c` | `40 53 b8 20 00 00 00 e8 58 ee 9b 01 48 2b e0 48 8b 59 08 48 85 db 74 0d` |
| FCanvas item submit, callable entry, 33 bytes | `0x017d9c0c` | `0x017d920c` | `40 53 b8 20 00 00 00 e8 68 1f 99 00 48 2b e0 48 8b 02 4c 8b c2 48 8b d9 48 8b d1 49 8b c8 ff 50 18` |
| Canvas text-item destructor, callable entry, 30 bytes | `0x00aee4e8` | `0x00aedae8` | `40 53 b8 20 00 00 00 e8 8c d6 67 01 48 2b e0 48 8b d9 48 81 c1 50 01 00 00 e8 12 68 cc 01` |
| K2 Scale stores, NONCALLABLE instruction verifier, 16 bytes | `0x0507fe7d` | `0x0507f47d` | `c5 fa 11 85 88 00 00 00 c5 fa 11 8d 8c 00 00 00` |
| Foreground copy/default Scale, NONCALLABLE instruction verifier, 38 bytes | `0x00aee41f` | `0x00aeda1f` | `c4 c1 78 10 01 48 8b 5c 24 30 48 8b c7 c5 fa 7f 47 28 c7 87 a0 00 00 00 00 00 80 3f c7 87 a4 00 00 00 00 00 80 3f` |

The first five records are direct callables, never hooks. The last two are
signature-only verifiers and must never be invoked. They permit a narrow owning
item adapter on this build; omit the capability on builds without independent
equivalents. At this research baseline the catalog has no admitted scaled-text
callable: its existing shadowed-string helper and unit-scale measurement do not
magically acquire a scale argument.

### Exact argument, foreground and layout proof

All entries use the ordinary Windows x64 ABI. These effective types describe the
recovered pointer/register contract, not a source-level Unreal overload:

```cpp
FText24* from_string(FText24* out, const FString16* borrowed_source);
Item1b0* construct(Item1b0* out, const Vec2f* borrowed_position,
    const FText24* borrowed_text, void* borrowed_font,
    const LinearColor4f* borrowed_foreground);
void destroy_text(FText24* initialized_text);
void submit(void* borrowed_fcanvas, Item1b0* initialized_item);
void destroy_item(Item1b0* initialized_item);
```

`FString16` is pointer at `+0`, signed int32 count at `+8`, capacity at `+0xc`.
Supply a bounded, terminated UTF-16 string and count/capacity including its NUL.
The nonempty `from_string` path calls native FString copy `0x1408d5950`, creates
owning text and cleans its private copy; it does not steal or free caller storage.
The adapter's string must remain unchanged through that synchronous call.

At constructor entry RCX is item storage, RDX points to the two-float position,
R8 points to initialized FText, R9 is UFont, and the fifth pointer argument is
at entry RSP `+0x28`. In the native constructor's own frame,
`0x140aee2ea` loads that pointer from RSP `+0x50` into R8, then calls base
constructor `0x140aee350` at `0x140aee2f2`. The base preserves the color pointer
in R9 (`0x140aee370`) and the item pointer in RDI. Its verified tail is:

```text
140aee41f  VMOVUPS XMM0, [R9]                ; all four foreground floats
140aee424  MOV RBX, [RSP+30]
140aee429  MOV RAX, RDI
140aee42c  VMOVDQU [RDI+28], XMM0            ; item foreground RGBA, including alpha
140aee431  MOV DWORD [RDI+a0], 3f800000       ; ScaleX=1
140aee43b  MOV DWORD [RDI+a4], 3f800000       ; ScaleY=1
```

There is no RGB-only copy, alpha replacement, premultiplication or sRGB conversion
in this assignment. The later native draw method `0x14120a0a8` reads the foreground
vector from item `+0x28` and multiplies its alpha (`+0x34`) by the native Canvas
alpha for ordinary emission. Caller alpha therefore participates in the native
color path; it is not forced to one. Pass the existing linear RGBA value.

The position is copied from the supplied Vec2 pointer to item `+8/+0xc`.
The base constructor zeros the center-X/center-Y bytes at `+0x88/+0x89` and
outline byte `+0x8a`. The simple path is therefore native uncentered/unoutlined
positioning; do not invent baseline corrections or write additional flags.
Only the local Scale pair needs changing for the requested proportional draw.

K2's exact scale handoff is `0x14507fe3b` (`LEA RCX,[RBP-18]`) followed by
constructor CALL `0x14507fe42`. After temporary-text destruction it sets RDX to
that same item at `0x14507fe63`. Stores `0x14507fe7d` and `0x14507fe85` target
`[RBP+88]` and `[RBP+8c]`, respectively: item `+0xa0/+0xa4`. Both stores finish
at `0x14507fe8d`; they are not a callable setter. The native caller then submits
at `0x14507fea8` and destroys the item at `0x14507feb1`.

### Storage, synchronous ownership and completion

- Reserve separate local `0x18`-byte FText and `0x1b0`-byte item storage. K2's
  FText starts at RBP `-0x30`, item at RBP `-0x18`, and the next stack cookie is
  RBP `+0x198` (item storage ends there). The constructor writes its final byte
  at item `+0x1a8`; do not infer a smaller allocation from that last write.
- Use `alignas(16)` for both opaque storage buffers, which satisfies the observed
  native uses without needing a guessed C++ type. The native K2 item itself is
  only 8-byte aligned: its RBP is 16-aligned and item is RBP `-0x18`. There is
  **no** evidence that item storage must be 16-aligned; the stronger local
  alignment is simply sufficient. Preserve normal Win64 call-stack alignment,
  shadow space and stack arguments by using real typed calls, not hand-built
  register dispatch. Vec2/LinearColor pointers refer to ordinary float values.
- The item constructor retains the FText's native owner. Destroy the temporary
  FText after successful item construction, exactly as K2 does, and never memcpy
  an owning FText/item. The item owns its retained text and its internal layout
  state, not the caller's stack storage or UFont.
- Submit consumes the initialized item synchronously through item virtual
  `+0x18`, and may perform the native Canvas flush. Its return type is **void**,
  not the old shadowed-string metric. The K2 caller immediately destroys its item
  after submit returns; no item/text buffer must survive until a future render
  callback. This is native batch ownership, not a claim of GPU completion.
- Destroy only successfully constructed native objects; on a native fault or
  uncertain partial construction retain the existing poisoned-adapter policy and
  suppress unsafe cleanup. Healthy calls finish both native destructions before
  leaving the already-qualified HUD draw frame. The existing font/root/current-
  playback checks remain; these new signatures do not authorize worker use or
  manufacture callback authority. Shadow enabling is not required for this path.

### Recovery / validation boundary

Repeat from the default catalog identity: follow the K2_DrawText name registration
to its FFrame thunk, inspect the final construct/scale/submit/destroy block, follow
the constructor into its base, and compare the seven exact windows above against
the authenticated PE's RVA-to-section mapping. Scan each complete byte sequence
over the full raw file and zero-filled mapped image; retain a single match at the
listed offsets. Repeat Ghidra `search_byte_patterns` with the explicit program.
This establishes the addresses/ABI/layout without attaching to a process or
assuming additional arguments. No source/catalog/generated files, game process,
installed binary, configuration or runtime artifact were modified by this pass.

The only authored change is this evidence handoff. Production admission and its
focused fake-native tests belong to the implementation worker; actual scaled
fallback/Unicode appearance remains a manual check. No older-build or Release
qualification, and no pause investigation, is claimed.

Use isolated local item scale, never mutate UFont or shared Canvas transforms.
No native pointers/text-item storage escape the synchronous qualified draw. Native
fault or uncertain partial construction must poison admission and suppress unsafe
cleanup. Existing admitted unit-scale font measurements can be multiplied by the
same uniform scale for width/height; nonunit ink/fallback appearance remains a
focused manual check. Shadow enabling is a separate native contract, not assumed.

## Complete-block geometry

The native bridge already resolves typed, contained `Txt_MusicTitle` and
`Pnl_MusicTitle`, and passes title as row and panel as list to
`read_preview_anchor`. `map_preview_anchor_geometry` transforms both rectangles;
the current result discards `list_pixels`. Expose that already-validated rectangle
as a copied result; `inherited_cull` is not a substitute. Anchor artwork before
the panel, retaining separate text bounds for metadata. No new object lookup,
field or hook is needed. Do not pass panel as both row/list (proper ancestry is
required). Panel inclusion of the visible note icon still needs visual checking.

## Planned pure-renderer boundary

Lead owns pure overlay h/cpp and its selftest. `SongHudAnchor` will append
`std::optional<SongHudRect> title_block` (no unsafe geometry fallback for artwork).
`SongHudDrawText` adds a final uniform `float scale` to the existing text callback
arguments and returns the existing layout metric. Unit font metrics remain unit;
the renderer multiplies measurements and requests the same draw scale. Native
bridge public runtime-facing signature can remain unchanged; its checked HUD
text callback uses the isolated native adapter. Shared startup/preview callbacks
and fonts must remain unchanged.
