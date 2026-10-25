# Older-build song HUD: bounded static recovery

2026-10-23. Integration prerequisite research, not gameplay or Release
qualification. Only this evidence file is authored. No source/catalog/generated
changes, builds, installs, native calls, process attachment/control, launches,
commits, workers or user-song changes were performed.

## Outcome and executable identity

**All eleven current HUD catalog additions have independently recovered older
import counterparts with unique, instruction-complete byte windows.** Their
consumed layouts and ordinary owning-call ABI agree with the current adapter.
The resource projection/ordered submission and read-only geometry/presentation
assumptions are supported below. These records are ready for lead integration as
`static_import_contract`, not `static_disk_contract` or older gameplay acceptance.

Canonical build: `ff7rebirth-steam-win64-68fd6fde`, game 1.004. Expected disk
identity remains owned by `src/game/rva_catalog.json`: size `126263624`, SHA-256
`76bd4d539878a8759433f1cd7c7b47d5e3abda0ef41572a664d2d606a9f09dc8`.

Every program-specific query explicitly selected Ghidra **`ff7rebirth_.exe`**,
which reports project path `/ff7rebirth_.exe-1.004`, PE, x86:LE:64, Windows,
image base `0x140000000`. Imported DOS/PE bytes independently show e_lfanew
`0x220`, AMD64, nine sections, PE32+, timestamp `0x68fd6fde`, SizeOfImage
`0x09bad000`, checksum `0x07877c99`, matching the older catalog profile.
No newer-build RVA delta was used to find a function.

**Disk gap:** the import's original executable path currently contains 1.005:
`D:/SteamLibrary/steamapps/common/FINAL FANTASY VII REBIRTH/End/Binaries/Win64/ff7rebirth_.exe`,
size `124317952`, SHA-256
`752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc`.
There is no sibling older-named or alternative ff7rebirth executable in that
directory; the referenced Ghidra project directory contains its project store,
not a separately authenticated older executable. No full imported-file hash was
obtained. Thus **older disk equality, raw/mapped disk uniqueness and original-file
SHA authentication are not established**. This bounded search does not assert an
older backup cannot exist elsewhere. Prior older bridge evidence explicitly has
the same import-only limit.

## Catalog-ready windows

All values in this table are **older-build RVAs**, not newer counterparts.
Byte windows were read from the explicit older import, checked against its
disassembly, and searched independently with `search_byte_patterns`. Every row
has exactly **one imported-program match**, at image base plus its listed RVA.
Use an all-`x` mask of exactly N bytes (no wildcards). All windows end at complete
instruction boundaries. C = direct callable, V = noncallable signature verifier;
none requires a hook. In particular V entries must never be invoked.

| Existing catalog ID | Kind | RVA | N | Exact bytes |
| --- | --- | --- | --- | --- |
| `song_hud_text_from_string` | C | `0x011dd508` | 26 | `48 89 5c 24 10 57 b8 60 00 00 00 e8 18 1a ed 00 48 2b e0 83 7a 08 01 48 8b f9` |
| `song_hud_text_item_construct` | C | `0x00e6db2c` | 30 | `48 89 5c 24 08 48 89 74 24 10 57 b8 20 00 00 00 e8 ef 13 24 01 48 2b e0 49 8b f8 49 8b f1` |
| `song_hud_text_destroy` | C | `0x00894970` | 24 | `40 53 b8 20 00 00 00 e8 b4 a5 81 01 48 2b e0 48 8b 59 08 48 85 db 74 0d` |
| `song_hud_text_submit` | C | `0x0095dc50` | 33 | `40 53 b8 20 00 00 00 e8 d4 12 75 01 48 2b e0 48 8b 02 4c 8b c2 48 8b d9 48 8b d1 49 8b c8 ff 50 18` |
| `song_hud_text_item_destroy` | C | `0x00e6dfd8` | 30 | `40 53 b8 20 00 00 00 e8 4c 0f 24 01 48 2b e0 48 8b d9 48 81 c1 50 01 00 00 e8 5a 54 cc 01` |
| `song_hud_text_scale_layout` | V | `0x05245d65` | 16 | `c5 fa 11 85 88 00 00 00 c5 fa 11 8d 8c 00 00 00` |
| `song_hud_text_foreground_layout` | V | `0x00e6df0f` | 38 | `c4 c1 78 10 01 48 8b 5c 24 30 48 8b c7 c5 fa 7f 47 28 c7 87 a0 00 00 00 00 00 80 3f c7 87 a4 00 00 00 00 00 80 3f` |
| `song_hud_font_measure` | C | `0x051d9b04` | 59 | `b8 38 00 00 00 e8 22 54 ed fc 48 2b e0 49 83 c8 ff 49 ff c0 66 42 83 3c 42 00 75 f5 48 8d 44 24 50 45 33 c9 48 89 44 24 28 c7 44 24 20 02 00 00 00 e8 c6 a2 00 00 48 83 c4 38 c3` |
| `song_hud_root_initialization` | V | `0x03c4ab34` | 33 | `45 33 ed 48 8b fa 4c 8b f1 48 85 d2 0f 84 7a 0c 00 00 4c 8d 61 08 49 8b cc e8 62 ad cb fd 83 c8 ff` |
| `song_hud_widget_opacity` | V | `0x04c98328` | 34 | `40 53 b8 40 00 00 00 e8 fc 6b 41 fd 48 2b e0 48 8d 54 24 20 48 8b d9 c5 f8 29 74 24 30 e8 76 6f cd fb` |
| `song_hud_widget_visibility` | V | `0x0096f134` | 35 | `48 89 5c 24 10 48 89 74 24 18 57 b8 20 00 00 00 e8 e7 fd 73 01 48 2b e0 48 83 b9 e8 00 00 00 00 48 8b f1` |

The root window is **33 bytes**, not a transplanted newer 32-byte length: it
includes the complete final `OR EAX,ffffffff`. The opacity/visibility window
lengths similarly follow their own complete instructions. Measurement is the
entire 59-byte wrapper. Identical foreground/Scale bytes were independently
located through older callers and measured unique; byte equality is not an
address-translation method.

## Independent recovery chain: text and measurement

Older name/registration recovery, all VAs:

| Name | String | Registration pair | FFrame thunk |
| --- | --- | --- | --- |
| `K2_DrawText` | `0x14642b9f0` | `0x146c0cf00` | `0x14524594c` |
| `K2_TextSize` | `0x14642b958` | `0x146c0cf50` | `0x145246598` |
| `GetRenderOpacity` | `0x1463d4318` | `0x1469f0440` | `0x144cf0c44` |
| `GetVisibility` | `0x1463b70e8` | `0x1469f0470` | `0x144cf0dcc` |

K2_DrawText is inlined in its FFrame thunk, **not a simpler callable with an
invented ABI**. Its final block independently establishes this owning sequence:

1. `0x145245d0e` calls `0x1411dd508` to construct FText at RBP `-0x30` from
   parsed FString. The helper's nonempty branch copies FString through
   `0x1409454d4`, creates owning text through `0x14122d45c`, assigns it through
   `0x140e6dde0`, cleans its private owners/string and sets FText flags
   `+0x10 |= 0x12`. Caller string storage remains borrowed and unchanged.
2. `0x145245d2a` calls `0x140e6db2c` with item RCX=RBP `-0x18`, RDX=position
   pointer, R8=initialized FText pointer, R9=font, fifth argument=foreground
   pointer. Constructor retains the FText owner (atomic strong increment at owner
   `+8`), stores its text/flags at item `+0x130` and font at `+0x148`.
3. `0x145245d33` destroys temporary FText through `0x140894970`; caller storage
   is not freed. Native strong/weak ownership is released normally.
4. `0x145245d65` / `0x145245d6d` store Scale into `[RBP+88]` / `[RBP+8c]`,
   item `+0xa0/+0xa4`; the verifier ends at `0x145245d75`.
5. `0x145245d90` calls `0x14095dc50(FCanvas*,Item*)`. It synchronously invokes
   item virtual `+0x18`; if Canvas `+0xa0==1`, it calls native flush
   `0x141604e40`. Return is void, not the old helper's metric/success flag.
6. `0x145245d99` calls `0x140e6dfd8`. It destroys item-owned state `+0x150`
   through `0x142b33450`, destroys retained FText `+0x130`, restores base
   vtables and **does not free caller item storage**. K2 immediately destroys
   after submission: no stack item or FText must survive a later render callback.

Effective ABI is the same five pointer/register constructor and ordinary Windows
x64 calling convention described in the newer typography evidence. FString is
pointer/count/capacity at `+0/+8/+0xc`; the terminated code-unit count includes
NUL. The native stack layout has FText size `0x18`, item size `0x1b0` (item RBP
`-0x18`, next cookie RBP `+0x198`). Native item alignment is 8; the existing
adapter's `alignas(16)` buffers suffice. Use typed native calls with ordinary
Win64 stack alignment/shadow space, not manual register dispatch or copied owners.

Older base constructor `0x140e6de40` copies position to item `+8/+0xc`, zeros
center-X/center-Y `+0x88/+0x89` and outline `+0x8a`, and constructs native
internal state at `+0x40/+0xa8`. Constructor `0x140e6db2c` loads the fifth
foreground pointer at `0x140e6db4a` and calls the base at `0x140e6db52`. The
verified base tail `0x140e6df0f..0x140e6df35` copies **all RGBA**, including
alpha, from `[R9]` to item `+0x28`, then initializes Scale `+0xa0/+0xa4` to
one. Native draw method `0x14095dfa4`, reached through older item vtable
`0x1457c0d48+0x18`, reads foreground `+0x28`, alpha `+0x34`, multiplying
the latter by Canvas alpha. No RGB-only copy, alpha-one override, guessed UFont
field, shared Canvas-transform change or shadow enabling is needed.

K2_TextSize constructs the same `0x28` measure-info and calls `0x1451d9b04`.
That wrapper counts UTF-16 code units, sets R9=0 and stack limit mode=2, and calls
`0x1451e3e00`. The body reads font `+0x18`, Scale `+0x10/+0x14`, spacing
`+0x20/+0x24`, writes width/height `+8/+0xc`, and calls actual older glyph/
kerning helpers `0x1451dfed0`, `0x1425e2014`, `0x1425e1f04`. This proves the
existing **unit-scale** measurement ABI; nonunit ink/fallback behavior is not
newly qualified. Preserve the current uniform metric/draw-scale policy.

The present SmallFont field also has older reflected metadata: string
`0x145ef18d0`, property record `0x146448ed8`, Object property kind `0x12`,
one element, offset `0x50`; its class getter indirection is `0x144cbec74`.
Further decompilation of that getter's builder `0x145267eec` timed out, so no new
font-class/init API is proposed from it. This supports the current strong field's
offset, not a newer font-choice feature. **Any separately added font contract
needs its own older recovery**; the parallel newer-font work is not covered here.

## Controller root, native title and presentation

Started from the older cataloged activation container `0x143c40db4` and controller
destructor `0x143c32970`, not newer addresses. They independently operate on the
embedded UI component at controller `+0x6f0`. Destructor follows it to
`0x143c32cf4`, which publishes component vtable `0x1467a4968`; virtual `+8`
identifies initializer **`0x143c4ab04`**.

The initializer's verified `0x143c4ab34` window preserves incoming RDX widget,
sets RCX to component `+8`, and calls native weak assignment `0x1419058b4`.
Therefore the gameplay-root weak handle is controller **`+0x6f8`**, not the
different menu handle at component `+0x7a0`. No concrete runtime Blueprint class
name is inferred. Continue positive weak identity, typed current tree ownership,
fresh containment and current playback validation; no retained raw controller read
in PostRender is justified by this offset alone.

The actual older activation container converts the title through `0x140f6c09c`
and calls title setter **`0x143c5f6cc`**. That setter resolves component `+8`
with `0x141905998`, looks up typed panel/title through `0x1413c0cac` /
`0x1413c2470`, assigns the string with `0x14098eaf4`, and shows the panel.
Name initializers `0x140501c20` and `0x140501bf0` independently publish
**Pnl_MusicTitle** / **Txt_MusicTitle**, globals `0x1491da868/0x1491da870`.
The older setter's visibility virtual slot is **`+0x280`**, not newer `+0x288`.
The HUD's read-only traversal does not call that slot; do not add a shared-slot
fallback. Existing strong WidgetTree/Slots/Content traversal remains applicable.

GetRenderOpacity thunk reaches `0x144c98328`. It calls pair getter
`0x14096f2c0` and reads current Slate **`+0x1c8`** at `0x144c98354`; its
no-Slate fallback reads UObject `+0xc4`. The HUD intentionally uses live Slate,
not that fallback. The pair getter independently confirms preferred UWidget
`+0xe8/+0xf0`, fallback `+0xd8/+0xe0`, positive strong count controller `+8`.

GetVisibility thunk reaches `0x14096f134`. Its body confirms Slate cached byte
**`+0x1b0`**, binding pointer/count **`+0x1b8/+0x1c0`**. A binding may call
native delegates at virtual `+0x30/+0x48`; preserve the HUD's rejection of bound
visibility rather than evaluating it. Older constants `0x1470dfed0..4` are
exactly **`04 11 02 01 19`**: Hidden, SelfHitTestInvisible, Collapsed,
HitTestInvisible, Visible. Existing acceptance of `19/11/01` and bounded live
ancestor-alpha multiplication therefore has matching older layout/semantics.
No UObject visibility fallback or pause-state guess is introduced.

## Geometry: reuse retained independent older proof

Do not rediscover or translate already-qualified metadata. The older entries for
`preview_root_shared_getter`, `preview_root_layout_writer`, `preview_screen_to_ui`,
`preview_reference_scale`, `preview_widget_geometry`, `preview_slate_paint_writer`,
`preview_root_pointer`, `preview_root_controller` already retain independent older
recovery in the catalog and `song_preview_anchor.h` evidence. They establish:

- Root shared pair with signed strong count `+8`; native root layout origin
  `+0x318/+0x31c`, render extent `+0x320/+0x324`, output extent `+0x328/+0x32c`,
  viewport scale `+0x330`.
- Paint geometry at Slate `+0x90`, size 56, affine components `+0x1c..+0x30`;
  incoming LTRB cull `+0x100`; weak parent pair `+0x38/+0x40`, requiring
  **strong** count `+8`, not weak `+0xc`.
- Same native inverse transform and reference scale
  `max(.01,min(renderWidth/1920,renderHeight/1080))`; preserve zero-origin/full
  borrowed Canvas admission, four-corner conversion and consistency rereads.

The title and already-typed Pnl_MusicTitle can supply separate text and complete-
block rectangles through this path; panel geometry needs no new native field.
Whether the authored panel includes every visible note-icon pixel, ultrawide
alignment and fade appearance remain visual checks, not guessed offsets.

Strong receiver/tree/slot/class containment, rooted EndImage/brush retention,
transient one-mip BGRA8 factory/upload layout and exactly-once update are already
independently recovered for this profile in
`MenuThemeNative-OlderBuild-20261020.json` and the associated catalog entries.
Those are reused **as older import evidence**, not promoted to disk/gameplay proof.

## Resource projection and native batch lifetime: new older follow-through

The existing older UpdateResource entry **`0x140c1f380`** leads to general update
`0x140c1f710`. It releases an old resource separately through `0x140c1f618`,
creates through texture virtual **`+0x2e0`**, publishes at Texture2D **`+0xa8`**,
then dispatches initialization. UpdateResource itself is older virtual
**`+0x2d8`**; these are not newer slot numbers.

Reading the actual older texture vtable `0x1457adcc0+0x2e0` yields creator
`0x140c1f478`, which calls resource constructor `0x140c1f084` and base
`0x140c1ee88`. Those construct the resource's CPU fields before publication,
including the Canvas-inspected `+0x45` flag. Native streaming-policy calculation
`0x140c1f15c` and nonstreaming-mip counter `0x140c1f330` independently exclude
the examined streaming branch for the bridge's one-mip resource: its required
`nonstreamingMips < totalMips` comparison is false. Existing factory
`0x1451a8dc8` has one mip; retain the established count/capacity distinction.

Initialization dispatcher **`0x1409e7ef8`** invokes resource virtual `+0x28`
directly or queues it (including its native deferred-list route). Its queued task
uses dispatcher **`0x1420b8090`**, which sets desired named thread from
**`0x1470b9a38`**. Canvas flush **`0x141604e40`** captures the completed command
list and submits through **`0x1451d80cc`**. That directly executes when appropriate
or dispatches through **`0x14219e9f0`**, independently confirmed to set the
**same** named-thread target `0x1470b9a38` and dispatch via native task scheduler
`0x149104960`. Ordinary same-game-thread init-before-draw ordering thus matches
the fresh immutable resource contract; no newly guessed ready fence is needed.

Older DrawTile **`0x1451dd908`** forwards its eleventh argument as a native
rendering resource; null selects native white, starting UV is zero. Tile ctor
**`0x1411dd468`** stores that pointer at tile `+0x58`; virtual draw
**`0x14095c358`** passes it into native batching `0x14095d14c` as the resource
key (batch field `+0x238`). The completed batch therefore requires external
resource retention, not merely stack item lifetime. Creator/release and batch
paths justify the existing process-lifetime rooted carrier/Texture2D/immutable
resource policy; a nonnull pointer alone is not GPU completion. Further batch
allocator decompilation at `0x14095d4f4` timed out; no native batch-owner API is
proposed or required by this read-only raw-resource submission contract.

Release `0x140c1f618` separately queues/directly executes release through
`0x1424d7c30` / `0x1425cdc70`, then clears texture `+0xa8`. Do not reupload,
release, destroy or evict Prepared resources at switches. Project/recheck `+0xa8`
inside the existing qualified synchronous bridge operation only; omit artwork on
missing/failed proof. Native fault/uncertain construction preserves poisoning and
suppresses unsafe cleanup, exactly as the newer owning adapter.

## Source assumptions and bounded integration handoff

At inspection, `configure_song_hud` consumes selected catalog signatures and the
admitted preview root pair; it does not literally require the newer build ID.
`menu_theme_native` compares the supplied profile to the compile-selected target
and requires all seven isolated text signatures. **Missing older catalog keys are
the immediate gate.** No HUD-specific layout divergence was found in the inspected
current assumptions: root `+0x6f8`, chart/controller/control-block ownership,
item storage/Scale/foreground, font measure, geometry, presentation and resource
projection agree. Existing profile-specific bridge virtual differences must stay
profile-specific; no new native slot calls are needed.

Lead may integrate the eleven rows as older-build keys with this evidence path,
regenerate through the normal catalog workflow and run affected older-target
checks. No claim about those unrun checks is made here. Newer-font selection is a
separate worker's scope; **later-added font IDs are outside these eleven rows**
and need separate older evidence before enabling that dependency.

Recovery recipe: explicitly select `/ff7rebirth_.exe-1.004`; confirm imported PE
headers; follow the four registered names to thunks and their actual callees;
follow the cataloged older controller destructor to component vtable/initializer;
derive Scale from that thunk's item base and stores; compare each complete window
and run explicit older-program byte-pattern searches. Follow the already-cataloged
older UpdateResource through its own creation slot and both task dispatch paths,
not address deltas. When an authenticated older disk is available, compare these
windows and scan raw/mapped disk bytes against the canonical older SHA before
claiming disk verification. No game attach or runtime experiment is needed for
that next static check.

Remaining gaps are older disk authentication, later font-choice parity and actual
appearance/lifecycle qualification. They do not require delaying the independent
newer-build visual iteration or rediscovering the proven menu bridge.

## Follow-up: stock engine-font fields (same date)

This closes older parity for the subsequently added **single** catalog contract
`song_hud_engine_font_fields`, described by
`SongHudStockFontEvidence-20261023.md`. The eleven earlier rows were not revisited.
Only this section is authored; no catalog/generated/runtime/bridge change or live
operation accompanies it. All queries explicitly selected older Ghidra program
`ff7rebirth_.exe` (`/ff7rebirth_.exe-1.004`). Provenance remains the imported PE
identity above for build `ff7rebirth-steam-win64-68fd6fde`: **no authenticated
older disk, original-file SHA or raw/mapped disk scan is newly claimed**.

### Typed field equivalence

Independent older metadata, not neighboring-slot inference:

| Field | Name string VA | Object-property record VA | Offset |
| --- | --- | --- | --- |
| SmallFont | `0x145ef18d0` | `0x146448ed8` | `0x50` |
| MediumFont | `0x146444028` | `0x146448e78` | `0x70` |
| LargeFont | `0x146443fc8` | `0x146448a40` | `0x90` |

Each record has native property kind `0x12` (Object), dimension one, the stated
field offset and the same type thunk **`0x144cbec74`**. That thunk calls
**`0x145267eec`**, now successfully decompiled: it registers
`/Script/Engine.Font`, size `0x1d0`, class slot `0x14933a690`. This resolves the
earlier optional type-builder timeout; these are actual **UFont object fields**.

Older parameter-block links additionally corroborate the owner: the engine
property-array pointer stored at `0x146038848` points to `0x146446310`, whose
entries include those Small/Medium/Large property records. The same parameter
block includes getter thunk `0x142b5db30 -> 0x14266d1dc`, registering
`/Script/Engine.Engine`, size `0xe48`, class slot `0x14933a520`.

### Actual older stock load path

Rather than translate the newer initializer address, recovery followed xrefs to
the independently recovered older Font class slot `0x14933a690`. Inspection of
its reader **`0x14265d8f0`** identified the typed stock-font loader; actual direct
callers identify initializer **`0x1451c6ec8`**. Its disassembly proves:

| Configured name | Destination | Name conversion CALL | Typed loader CALL |
| --- | --- | --- | --- |
| engine `+0x58` | SmallFont `+0x50` | `0x1451c8017` | `0x1451c8027` |
| engine `+0x78` | MediumFont `+0x70` | `0x1451c8049` | `0x1451c8059` |
| engine `+0x98` | LargeFont `+0x90` | `0x1451c807e` | `0x1451c8091` |

Each conversion targets **`0x140b7ab58`**; its returned temporary string is
supplied to loader **`0x14265d8f0`**, then destroyed through `0x14079f7f0`.
The loader returns immediately if the destination is already populated. Otherwise,
for a nonempty configured name it uses the Font class slot (with native class
initialization if needed), calls native typed loader **`0x140d7179c`**, and stores
the result into the supplied destination. A missing/failed configured name invokes
the supplied fallback and assigns that result. These three calls supply fallback
`0x14265e100 -> 0x14265d280`. Exact fallback asset names are not inferred.

**The HUD calls none of these loaders, class initializers or fallbacks.** It
borrows already-populated reflected fields under the existing synchronous frame,
engine-field and object-identity checks. A null field remains unavailable; its
name does not justify loading a font or fabricating a replacement pointer.

### Catalog-ready noncallable verifier

Existing ID: `song_hud_engine_font_fields`. Older RVA **`0x051c803d`**, VA
`0x1451c803d`; signature-only/call-site verifier, optional, **no hook and never
callable**. The complete **89-byte** Medium/Large load window ends at
`0x1451c8096` (exclusive), immediately after the LargeFont loader CALL. Its own
disassembly establishes every boundary; do not transplant the newer window's
relative CALL displacements.

```text
48 8d 4f 78 48 89 45 df 48 8d 55 e7 e8 0a 2b 9b fb
48 8d 57 70 4c 8b c0 4c 8d 4d d7 e8 92 58 49 fd
48 8d 4d e7 e8 89 77 5d fb 48 8d 45 b7 48 89 45 df
48 8d 8f 98 00 00 00 48 89 75 d7 48 8d 55 e7 e8 d5 2a 9b fb
48 8d 97 90 00 00 00 4c 8b c0 4c 8d 4d d7 e8 5a 58 49 fd
```

Mask is **89 `x` characters**, no wildcards. Explicit older-program
`search_byte_patterns` over these exact bytes returned **one match**,
`0x1451c803d`. This is instruction-complete unique **imported-program** evidence;
there is no older disk equality or disk uniqueness claim. A search with the newer
window's CALL displacements masked found no candidate and was not used as proof;
the successful recovery came from the older class-slot/loader/caller chain above.

### Integration boundary

The consumed offsets Small/Medium/Large `0x50/0x70/0x90` and populated-field
semantics match the current source selection. Lead can add this older key as a
`static_import_contract` referencing this section and regenerate normally. No
source-layout divergence requiring an older special case was found for this
additional contract. Retain actual unit-Mg comparison, Small winning ties, selected
font field/identity rechecks, unit metrics, and the current no-magnification policy
for an improved stock source. The signature is not permission to invoke loaders.

This supersedes the earlier pending-font-parity gap **for this one current ID**.
It does not establish configured asset names, atlas resolution, per-glyph visual
quality or runtime acceptance, nor automatically cover future font contracts.
Older disk authentication remains outstanding. No newer-candidate visual work
needs to wait on those separate qualifications.
