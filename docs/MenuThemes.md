# Menu Themes — Development Format

User installation, image preparation, controls and troubleshooting are documented
in [Themes Guide](../ThemesGuide.md), packaged as `ThemesGuide.md`. The material below
is the developer reference; native layouts, hooks and evidence do not belong in
the user guide.

The native theme feature is in development. Keyboard background replacement is
visually confirmed. Full-menu textures, palettes, animated row states and controller
cycling are implemented and installed in a reviewed development build; consolidated
physical rendering, recycling and persistence checks are still pending.
This is not a PAK format, and no Unreal asset cooking is required for the inputs.

## Files

Each theme has a portable ID (1–64 ASCII letters, digits, `_` or `-`) and its own
directory below a themes root. `original` is reserved for the unmodified menu and
requires no files. The runtime reads `Themes` beside the installed DLL.

```text
Themes/
  night/
    theme.ini
    background.png
```

```ini
[Theme]
Version=1

[Images]
MusicList/Bg_1=background.png

[Colors]
MusicList/Txt_Title_1=#E8EDFFFF
MusicListCell/Txt_Title@selected=#FFFFFFFF
```

The UTF-8 manifest accepts an optional BOM, blank lines and whole-line `;`
comments. Section and target names are case-sensitive. Colors are `#RRGGBBAA`:
RGB is sRGB, alpha is linear. Images are PNG files relative to the theme directory.
Paths cannot be absolute, contain `..`, or resolve outside that directory.

The offline plan compiler validates these selectors before native binding. A valid
selector is not proof that the current menu contains a compatible widget: native
identity, ownership, class and slot checks remain mandatory. Unknown or unsupported
targets must not count as applied. Missing overrides restore the original
appearance, never retain the previously selected theme's overrides.

### Current Selection Cursor

`Cursor/` addresses the current song-list cursor's own widget tree, not the
MusicList tree, a pooled song row, or the speed-arrow widgets. Binding follows
the current SelectList's strong CursorWidget edge and revalidates the positive
menu/list/cursor identities, class ancestry and current tree/containment. The
CachedCursor field is never an alternate authority. Existing owning image,
palette, original-capture and restoration rules still apply to each member;
replacing the provider invalidates the previous Subject and its affinities.

The authorized default-profile observation identified `Cursor_Left`,
`Img_BtnLight` and `HoverBase` under `Item_Cell_Light_C`. Their normal native
motion and alpha animation remain authoritative. Metadata for both profiles and
the observation limits are retained in
[`MenuThemeCursor-20261022.json`](../analysis/MenuThemeCursor-20261022.json).
CursorEffectName was None in that observation; replacing arbitrary named native
effects is not implemented or implied by these image selectors. Older metadata
is import-only, and physical appearance still requires focused qualification.

### Preview Palette

`[Colors]` additionally accepts the value-only roles `Preview/Background`,
`Preview/Border`, `Preview/Accent`, `Preview/Track` and `Preview/Text`. These are
not native widget selectors. The offline compiler converts their sRGB RGB once,
preserves alpha and keeps independent existing badge defaults for omitted roles.
Preview images, property suffixes, states and unknown roles are rejected.

Only a complete committed theme publishes the fixed palette snapshot, keyed by
the actual menu generation. Deferred or rejected choices do not change it; Original
publishes defaults. The renderer copies this value without native lookups and
suppresses a retired or mismatched owner's retained badge. Audio content, clock,
geometry and the existing native text shadow are unchanged. These color-only
controls are separate from cursor/native image bindings and are not a visual
qualification claim.

### Full-Menu Selectors — Source Integration

Selectors start with `MusicList/`, `MusicListCell/`, `SpeedIcon/` or `Cursor/`, followed by the
widget's leaf name. Row-template selectors apply to current admitted song rows,
not unrelated widgets sharing a name. Optional `@normal` and `@selected` states
are supported only for `MusicListCell`; selection comes from native row/selection
events, not from recognizing a color.

Image selectors use the primary brush by default. `.Normal`, `.Hovered`,
`.Pressed` and `.Disabled` select button brushes; `.0` through `.63` select an
ImageSet entry, subject to its actual current array bounds. Geometry, transforms
and native speed-index policy are not replaced.

Color selectors default to `ColorAndOpacity`. Explicit properties are
`.ContentColorAndOpacity`, `.BackgroundColor`, `.BottomColorAndOpacity`,
`.OutlineColorAndOpacity` and `.ShadowColorAndOpacity`; compatibility depends on
the actual widget class. Button-brush suffixes and numeric ImageSet suffixes in
`[Colors]` select brush tint. `.Brush` explicitly names the primary image in
`[Images]`; `.BrushTint` selects the primary brush tint in `[Colors]`.

```ini
[Images]
MusicList/Bg_1=background.png
MusicList/Bg_2=frame.png
MusicListCell/MainMenu_Btn.Hovered=row-hover.png
SpeedIcon/SpeedIcon.2=speed-score-covered.png

[Colors]
MusicList/Txt_Title_1=#E8EDFFFF
MusicList/Pnl_HighScore.ContentColorAndOpacity=#FFFFFFFF
MusicListCell/Txt_Title@normal=#CAD0DCFF
MusicListCell/Txt_Title@selected=#FFFFFFFF
MusicListCell/Txt_Rank.OutlineColorAndOpacity=#2A3454FF
```

This is the full-menu source contract, not an acceptance claim for the installed
development build. Animated palette handling preserves
native fades by multiplying theme alpha by incoming native alpha; original-state
restoration must use the latest admitted untinted source, including owning
SlateColor references. Shared native animation assets are not edited.

Native animation can read its current color back from the UObject property.
Leaving themed RGB in that field can therefore contaminate a later partial-channel
animation update and the restoration baseline. The qualified setter contracts allow
separate untinted UObject source and themed Slate presentation; the corrective
implementation preserves genuine native updates and owning references without a second
setter call or RGB-equality heuristics. This does not reconstruct already polluted
animation caches. See
[`MenuThemeColorFeedback-20261021.json`](../analysis/MenuThemeColorFeedback-20261021.json);
clean-process visual validation remains required.

Non-owning LinearColor emission refreshes its baseline from the currently
qualified genuine UObject property, including native direct writes that bypass
the intercepted setters. It cannot revive forwarded stale sources or adopt a
temporary themed property after uncertain native completion. Owning SlateColor
and Bottom attributes retain their separate ownership protocols.

Repeated qualification and Subject checks may share one private uninterrupted
read/data-write phase of the actual callback. A phase ends before every native
setter, owning helper or other potentially reentrant operation; subsequent native
access must reacquire current authority and containment. No proof survives a
callback, yield, generation, retired invocation or changed source revision. This
does not cache GC/thread/TLS facts across native calls or remove the containment
classifier's consistency rereads.
The optional `CallbackAuthority::phase_current` callback is a metadata-only
revocation guard, never standalone admission. Without it, adapters retain full
qualification per use; the palette Reader only supplies a candidate authority.
Native read-only classification may share full callback qualification inside its
own stack-bound phase, not target identity or containment results. Entry/exit
owner, membership and tree consistency reads remain fresh; nested entry or
revocation invalidates the phase instead of resurrecting its prior proof.

Button states and ImageSet entries belonging to the same admitted subject use
grouped owning transactions with one renderer refresh. Palette retirement filters
registered subjects in one pass. These reduce repeated work; they do not make native
UI operations atomic, move them to arbitrary worker threads, or establish a measured
opening/closing latency improvement. Partial native faults retain fail-closed
ownership and do not permit optimistic rollback through an unavailable bridge.
An untouched, non-owning passive source with no owned attribute needs metadata
detachment rather than a restoring setter; owning captures still require their
qualified destruction and reentry checks.

Same-association row refresh retains non-Bottom palette policies before native
setup and forces one restricted-row emission after brush synchronization, avoiding
an extra restoring setter round. Bottom attributes still restore before setup and
reapply afterward; fresh brush baselines and transactional rollback remain required.

Preparation shares freshly admitted Subjects for compatible properties of the same
current widget and retains dependency work across genuinely deferred admission. Class hints
avoid redundant probes but do not establish identity or authority. Rebinding,
replacement and new generations invalidate the corresponding descriptors; owning
operations still validate their current targets and original affinities. These
changes reduce repeated reconstruction, not the need for fresh native baselines.

Song-row reconciliation uses row-owned indices into the existing binding, palette
and combination records. A rebind replaces that row's contribution without resetting
unrelated requested-theme progress; row-only lookup, restoration and repair do not
rebuild the whole physical inventory. These indices are metadata, not retained
authority: each association still receives fresh Subjects, tickets, affinity and
baselines. Repairing a committed row cannot admit an incomplete requested program;
whole-theme type, collision and completeness checks remain required before switching.

Choice application projects prototype combinations and palette policies into
callback-local direct lookups, preserving canonical aliases and semantic-state
precedence. No projection survives row rebinding or menu reopening. Normal work
finishes a finite semantic unit rather than stopping at an elapsed-time budget:
one explicitly requested program, or the exact physical Song row being set up.
Neither unit has constant execution time. Many exposed rows, large programs or
slow hardware may still stall input or rendering; these are not frame-time guarantees.

The native member resolver groups capture, immutable composition and optional
tinting for current Subjects. Property-only and next/prior brush preflight groups
share owner, containment and retained-resource checks only inside uninterrupted
private read phases; each member still proves exact current affinity. Allocation,
native helpers and reentry end sharing, and final containment consistency checks
remain. Resolver outputs publish together as owning values, never borrowed brush
pointers or reusable proof tokens. It performs no texture uploads: whole-program
type/collision validation and explicit upload admission still precede composition.
Carrier-name caching retains only constant FName values, not object/lifetime facts.

### Performance diagnostics

The theme backend has persistent bounded `[menu_theme_perf]` schema-1 diagnostics.
Metric/counter IDs and histogram boundaries are defined in
`src/game/menu_theme_metrics.h`; they are diagnostics, never native authority.
Capture records hook totals separately from actual Original/native helper calls,
validation and traversal work, preparation and row lifecycle. Ready-to-commit,
pending-row and message-queue delays are latency samples, not CPU execution time.

The recorder uses fixed thread-local nested scopes, bounded generation banks and
try-lock publication. It counts dropped roots rather than blocking gameplay. No
extra UObject read or production predicate is performed solely to collect a
metric. Native SEH wrappers use trivial explicit stamps. Completed snapshots are
drained by the existing poll service; formatting and one batched logger write per
window occur outside native/runtime protected locks, not in individual animation
setter callbacks. Histograms, failed-helper counts and bounded slow callback
contexts distinguish averages from spikes without per-call text logging.

`QualifyCallback` is partitioned into palette/Scope routes, executable mapping,
owned row/selection/window-frame checks and native-facts capture/classification.
The facts subtree separates synchronized session copy, shared identity access,
dynamic reads and package/class snapshots. These scopes surround existing work;
they do not perform extra validation. Newly instrumented children move time out
of the parent's **self** column, not out of the application. Compare inclusive
parent time and the self-time partition, never old and new parent self alone.
`Facts*` measurements are not `Native*` helper calls. RAII scope failure counts
do not count boolean admission rejections.

Caller mapping admission remains fresh on every qualification. A resident-image
fast path must prove current image allocation ownership and valid current page
protection; allocation-level protection/commit totals are not page evidence.
Nonresident, unavailable or ambiguous queries retain the full `VirtualQuery`
fallback, without touching/locking the page or caching memory facts. Copy-on-write
image pages remain eligible. Runtime's readable-executable policy and owned
row/selection's execute-only allowance must not be conflated. Private-frame,
return-slot, lifetime and post-native qualification checks remain unchanged.
Only optional API availability may be cached. The queries are observations, not
an atomic mapping/protection snapshot or a lease against later changes.

Inclusive wall time contains nested scopes; self time excludes instrumented direct
children, not scheduling pauses or uninstrumented engine work. It includes some
instrumentation overhead and is not a CPU/GPU/FPS measurement. Completed windows
may omit unfinished scopes at process exit. Generation zero is unattributed work;
copied row/setup/generation context is not a lifetime lease. The parser and focused
human comparison scenario are documented in
[In-Game Validation](InGameValidation.md#focused-menu-theme-performance-capture-debug).

Replacement PNGs retain the target's native drawing contract. The observed record
`Deco` uses a Box brush with half margins: its midpoint pixels can stretch into
strips across the record panel. Use nine-slice-safe endcaps and transparent gutters
around both sampling seams, not a centered whole-image illustration. The speed
arrows are resource-less Pie images whose native angular mesh already forms their
shape. Palette-only arrow overrides preserve that geometry; a pre-cut arrow PNG
can be clipped or mirrored again, and its color still multiplies the widget tint.

Palette registration during preparation is passive: it must not call an unchanged
setter on Original. Text bottom color additionally has a native Slate attribute
set/unset state, separate from `FSlateColor.Mode`; setting its color activates that
attribute even for an unchanged value. Exact restoration therefore requires its
owning color and complete attribute baseline, including binding/set state, not just
the RGBA value. The independently recovered contracts are retained in
[`MenuThemeBottomAttribute-20261021.json`](../analysis/MenuThemeBottomAttribute-20261021.json).
Rendered restoration still requires the focused in-game check.

### Native Brush Geometry

Image replacement swaps only the brush resource; ImageSize, draw type, margins,
tiling and tint stay native. Slate sizes Box (nine-slice) corners from the bound
texture's pixel dimensions times the margin, not from ImageSize, so a supersampled
Box PNG enlarges its corners by the same factor. In-game review on 2026-10-21
confirmed this: 4x Box PNGs drew 4x corners, 1x PNGs matched native geometry.
Image-draw brushes stretch the whole texture into the native slot, so they can be
authored at higher resolution. Values below come from the read-only runtime
snapshot (`menu-style-runtime-*.json`); margins are 0.5 unless listed.

| Selector | Draw | ImageSize | Native color / notes |
| --- | --- | --- | --- |
| `MusicList/Shadow` | Box | 129x100 | dark, alpha 0.8; window drop shadow |
| `MusicList/Bg` | Box | 80x79 | white; covered by `Bg_1` |
| `MusicList/Bg_1` | Box | 14x14 | window fill |
| `MusicList/Bg_2` | Box | 128x128 | outer frame |
| `MusicList/Bg_Pettern` | Image | 375x335 | alpha 0.3; watermark behind the list |
| `MusicList/ListBg` | Box, EndTiledImage | 16x16 | grey, alpha 0.2; despite the name, a title-height box inside every row, not a list texture. Its stretched centre pixel tinted every row (identified 2026-10-22 by elimination with colour-coded cell images and the centre-pixel colour); the bundled examples hide it with alpha 0 |
| `MusicList/Base1_4` | Box | 32x32 | near-black; title bar under `Pnl_Title` |
| `MusicList/Base1` | Box | 32x32 | near-black; rank header under `Pnl_TargetScore` |
| `MusicList/Shadow_3` | Box | 18x19 | margins L 0.758, T 0.5, R 0.3, B 0.5; info frame with left bar |
| `MusicList/UnderLine1_1`, `_2` | Image | 32x32 | resource-less, grey 0.5; `Pnl_Info` separators |
| `MusicList/UnderLine1_3` ... `_6` | Image | 32x32 | resource-less, grey 0.5; rank-table separators |
| `MusicList/UnderLine1`, `UnderLine2` | Image | — | orange, alpha 0.7; `Pnl_HighScore` lines |
| `MusicList/BaseImg` | Image | 160x165 | orange, alpha 0.314 |
| `MusicList/Deco` | Box | 55x60 | orange; `U_Result_WinDeco` |
| `MusicListCell/EndImage` | Box | 16x16 | teal; under `Pnl_Light` (content alpha 0 on unselected rows) and alpha 0 on the selected row, so not visible |
| `MusicListCell/MainMenu_Btn` states | Box | 16x16 | full-row background; BackgroundColor alpha 0.5, 0 on the selected row |
| `SpeedIcon/SpeedIcon.0` ... `.2` | Image | 18x11 | entry tints black 0.5 / blue / white |

Theme alpha multiplies these native alphas, so they are ceilings: authored art
for `Bg_Pettern` and `BaseImg` must be brighter than its intended look.
`MusicListCell/Pnl_Light.ContentColorAndOpacity` (native gold, alpha 1 only on
the selected row) multiplies the selected-row art beneath it: unchanged, a
replaced `EndImage_172` rendered muddy teal, and white restored it. The
unselected-row background is the current `MainMenu_Btn` state brush, not
`EndImage`; a transparent `.Normal` left rows without background under the notes
line in the 2026-10-21 review, so themes should supply an opaque `.Normal` and
expect at most 50% coverage. That review also showed an unidentified title-height
band over unselected rows. `EndImage_172`,
`Icon_New` and `Txt_Difficulty` were absent from that snapshot, but
`EndImage_172` replacement rendered as the selected-row background in game.

The bundled `steins-gate` and `evangelion` examples exercise every selector above
and passed `program_admission result=ready` and
`stage=apply result=applied_complete_program`. Their sources are generated art;
the generator is not part of the product.

### Common Widget Names

These names were observed in this menu; binding still checks the current owner,
native class and strong containment rather than trusting a name alone.

| Scope | Useful targets |
| --- | --- |
| `MusicList` | `Bg`, `Bg_1`, `Bg_2`, `Bg_Pettern`, `ListBg`, `Shadow`, `Shadow_3`, `Deco`, `BaseImg`, `Base1_4`, `Base1`, `UnderLine1_1` ... `UnderLine1_6`, `UnderLine1`, `UnderLine2`, `Arrow_Left`, `Arrow_Right` |
| `MusicList` text | `Txt_Title_1`, `Txt_Info1`, `Txt_Info2`, `Txt_Info3`, `Txt_Time`, `Txt_Volume`, `Txt_Alert`, `Txt_HighScore`, `Txt_HighScoreTitle`, `Txt_Rank`, `Txt_TargetScore`, `Txt_RankS`, `Txt_ScoreS` (also A/B/C rank and score variants) |
| `MusicList` panels | `Pnl_All`, `Pnl_ListWindow`, `Pnl_Title`, `Pnl_Info`, `Pnl_SpeedList`, `Pnl_HighScore`, `Pnl_TargetScore` |
| `MusicListCell` | `EndImage_172`, `EndImage`, `Icon_New`, `MainMenu_Btn`, `Txt_Title`, `Txt_Rank`, `Txt_Difficulty`, `Pnl_Light`, `Pnl_Content` |
| `SpeedIcon` | `SpeedIcon` (`EndImageSet`, including indexed brush entries) |

`Bg_Pettern` is the game's spelling. Row selectors apply to current song-row
instances, not one pooled object or a guessed array-position/song-index mapping.
The observed piano silhouette uses `MusicList/BaseImg`; overriding the background
does not replace that separate image. Supply an explicit image override to replace
the artwork. `BaseImg_1` is a different, previously resource-less brush.

`SpeedIcon` applies uniformly to current speed rows, not a song index or a
normal/selected song-row state. Binding uses current strong list membership and a
local membership epoch; native speed changes keep their own image-selection
policy. Ordinary speed updates do not rebuild those image entries. Independent
row refreshes require reconciliation at qualified menu/selection/theme boundaries,
not reuse of saved row pointers. See the retained speed lifecycle in
`analysis/MenuThemeClasses-20261021.json`.

Speed image index `2` covers ordinals at or below the score's recorded speed;
otherwise `1` covers ordinals at or below the current configured speed, and `0`
is unfilled. It is not a song-row selected state. Use distinguishable artwork for
all three entries. PNG transparency/padding affects the visible mark; replacement
keeps the authored brush size, transform and pivot rather than resizing the UI
to the PNG's pixel dimensions.

### Texture Color Policy

Native construction already enables sRGB for theme Texture2D pixels. The stock
importer's separate `bNotOfflineProcessed` flag controls offline-processing
resource policy, not gamma or alpha premultiplication. Fresh uploaded textures
must match that policy before their first resource update; adjacent flags remain
unchanged. Independently recovered metadata and the generated native true-setter
for both profiles are retained in
[`MenuThemeTexturePolicy-20261021.json`](../analysis/MenuThemeTexturePolicy-20261021.json).
This initialization contract is not a claim of measured visual equivalence.

## Preparation And Failure Behavior

`core::load_theme` decodes PNGs through Windows Imaging Component into owned,
top-down BGRA8 pixels with straight alpha, and copies color values. 16-bit inputs
are converted to 8-bit output. It does no game-memory access, native game calls or
GPU work. Call it outside game callbacks. Failure leaves the previous output
unchanged, with an error string.

Preparation bounds the manifest to 64 KiB, each image file to 32 MiB, and image
dimensions to 4096×4096. There is no separate image/color entry-count quota.
Encoded image
bytes and estimated decoded pixel bytes each have a 128 MiB per-theme budget.
Header checks cover dimensions, color/depth combinations and compression, filter
and interlace fields before decoding. WIC decoding errors are rejected; WIC may
recover damaged inputs, so this is not a strict PNG conformance checker. The
encoded user file is not passed to a game decoder.

## Selection Persistence And Controls

The existing mod configuration can store the selected ID:

```ini
[Theme]
Selected=night
KeyboardFunctionKey=6
```

The default is `original`. Invalid IDs fall back to `original` when read;
`save_selected_theme` rejects invalid IDs and reports write failure without
rewriting other settings. The runtime must call it only after a successful theme
application, not merely after file preparation or a switch request.

The chosen interaction is hotkey cycling, not a separate theme picker. The
configuration reserves Ctrl+F6 by default; `KeyboardFunctionKey=1..12` selects
Ctrl+F1..F12, `0` disables the keyboard chord, and out-of-range values fall back
to `6`. Keyboard cycling is visually confirmed for the two test backgrounds on
the default game build; this does not qualify the full theme format.

The approved gamepad extension holds L3 as modifier: tap L2 (Xbox LT) for the
previous theme, or R2 (Xbox RT) for the next. Both directions wrap through
`original` and the sorted theme IDs. Holding a trigger must not repeat; pressing
both together suppresses switching until neutral. This extension uses the same
generation-bound request and game-thread dispatch, not a second mutation path.
Controller input is implemented for XInput and the existing native digital-key
fallback; physical-controller acceptance is pending. Keyboard `0` does not disable
controller input. Existing R3 preview and L3+R3 autoplay gestures are unchanged.

### Runtime Coverage

The completed source composition accepts main-menu, song-row and SpeedIcon
programs: Image/Pie/Tiled brushes, indexed ImageSet entries, Button states and
tints, and the documented text/panel/image/button palette properties. Song-row
normal/selected policies follow native semantic events. Speed rows use current
strong membership and local epochs, preserving native speed-index policy.
Unknown or incompatible targets reject the whole candidate rather than silently
counting a partial theme as applied. Omitted properties restore their originals.

The file inventory is prepared at startup; adding or editing files requires a
restart in this implementation. Asset bytes, dimensions, format and actual memory
admission are checked. There must be no separate quota on themes, retained
brushes, palette sources or future menu openings. Retained records, row
associations and palette sources now use stable dynamically grown storage;
allocation/byte-accounting failures remain explicit, without speculative reserves
for hypothetical future opens.

Physical preparation is demand-driven: it services the committed or explicitly
requested program, not every inactive theme. Cold Original does not materialize
custom rows, capture their brushes or register their palette sources merely
because themes exist on disk. Logical compilation is not physical admission;
native target incompatibility may first be reported when that program is requested.
Changed-row baselines and complete candidate preflight remain required. Current
installation and measured results belong in [Current Status](CurrentStatus.md).
Native work remains confined to the owning game-thread callbacks; the metadata
dispatch mechanism is not a CPU/GPU utilization quota or standalone native authority.

File decoding happens before game-callback admission. Native originals and
immutable compositions require qualified game-thread callbacks; the correction
must leave Original available immediately while custom resources are prepared.
Only Original or a complete physically admitted program can be committed. A
request for an unprepared custom program starts preparation without changing the
applied theme; repeated pending input must not restart that work. Cancellation
stops servicing the cancelled candidate. A pending or rejected program is not an
invisible applied position, and no request is persisted merely because it is
pending.

An explicit theme request services that complete demanded program in its actual
qualified window dispatch, not the first unrelated pending row. Song setup and
same-association refresh service only their exact physical contribution after
Original, retaining pre-Original restoration and fresh post-Original baselines.
They do not advance unrelated pending rows or another requested program. Neither
path prepares inactive themes or drains `work_pending` in a readiness retry loop.

There is no continuation timer or automatic successor-message chain for normal
preparation. Genuine missing receivers, Speed readiness without a dedicated
notification, qualified cleanup and failed posting retain coalesced metadata
tickets and the existing input-poller fallback. Later actual setup/selection or
qualified dispatch rechecks readiness; elapsed time never establishes admission.
There is at most one current ticket, and cancellation/session revocation cannot
revive old work. No native work or authority runs on the poller. QPC measurements
remain diagnostic, not preparation-budget decisions.

Opening is a separate policy: the persisted custom program is prepared and
transactionally committed within the actual qualified Ready callback, cold or
warm, without artificial slice/timer pauses. This is finite existing preparation,
not a readiness retry loop; it may briefly block opening. It does not run before
Ready, prepare inactive themes, reuse stale row authority or bypass owning/native
checks. Missing receivers, rejected programs or lost authority leave Original
available and use existing safe deferred handling. Only actual leftover work may
request a wake after the Ready Scope and runtime lock retire. Opening Original
still performs no custom materialization.

The existing keyboard poller samples the configured chord like the preview
shortcut. A press queues one owner/generation-bound ticket; holding the chord
does not repeatedly cycle. No native or UI work runs on the poller. A private
message on the existing owned window hook consumes the ticket and prepares the
requested program if needed, publishing only after complete admission under a
live dispatch frame, current subclass, hook lease and native/session checks.
Focus loss, failed posting, stale ownership and retirement
reject the request rather than replaying it in a later menu. The direct
PlayerInput switching path is removed, avoiding both UMG key-delivery dependence
and duplicate application. Request/dispatch diagnostics correlate the ticket;
only a successful application is a switch. Close restoration runs before the
session begins closing. Actual owning
callback scopes, current session and native facts are required; snapshots alone
do not authorize these operations. Only successful application can update the
selected ID. Native visual behavior and persistence still require the focused
manual scenario in `InGameValidation.md`.

Qualified row-setup, direct-refresh and selection scopes reconcile recycled rows.
Routine recycling restores and rebinds only the affected row; unrelated styled
subjects stay intact. Selection updates changed policies without repainting
unchanged brushes. Focus loss cancels gesture intent, not qualified maintenance
or the current visible theme. Newly discovered Speed members receive a non-saving
maintenance reapplication. Close avoids duplicate palette restoration, and warm
opening reuses immutable resources while still validating current targets.
Palette interception retains latest untinted native animation values, with
owning restoration and stale-source rejection. Transitions preflight members
before uploads and restore the prior composition on recoverable failure; lost
authority never permits stale rollback. The complete implementation passes
focused offline tests and review, but full-menu rendering, recycling and restart
persistence still require the consolidated manual scenario. Earlier visual
acceptance covers keyboard background switching only.

## Native Texture Bridge

`game/menu_theme_native` accepts prepared BGRA8 pixels. Configuration authenticates
the executable profile and canonical address policies without invoking game code.
Creation requires a caller-qualified ordinary menu callback covering the current
game thread, live session, outside-GC phase and shutdown exclusion through cleanup.
The runtime supplies owning callback scopes. Earlier background execution was
visually confirmed; full-menu native execution remains separately unqualified.

Each immutable texture is retained through a fresh rooted native `EndImage` and
its reflected brush reference. Native brush construction, owning assignment and
destruction are used instead of copying shared-owner bytes. A fresh single mip is
validated before upload; authority and the retained graph are rechecked around
native operations and cleanup. Compromised operations disable further bridge work.

Retained images and failed allocation attempts consume a bounded process-lifetime
budget. Switching must not reupload, evict, unroot or destroy these textures.
Borrowed brushes cannot escape their qualified callback. A successful bridge
result means upload was dispatched, not that a theme was applied or rendered.
Fake-native tests do not establish real GC survival or renderer readiness.

### Named Image Lookup — Static Contract

The cataloged `end_image_find_named_child` accepts the cache-owning
`EndUserWidget*` receiver and a borrowed `FName64*`, returning a borrowed
`EndImage*` or null. Native piano binding makes the current Ready widget the
`MusicList` receiver; the retained instance mapping identifies its `Bg_1` child.
Pass the leaf name, not `MusicList/Bg_1`. Do not use an arbitrary UWidget receiver.

This is **not read-only**: a miss searches the receiver's WidgetTree, checks
EndImage ancestry and inserts the result (including null) into its cache at
`+0x2d0`. A hit does not repeat the class check. A future caller must qualify its
current callback before lookup and revalidate returned identity, actual class,
name, current menu ownership and admission afterward; no receiver fallback or
pointer-only acceptance. Default-profile native `Icon_New` lookup and menu binder
were independently decompiled in the explicit default program. The exact
instruction-complete lookup prefix is unique and matches the authenticated disk;
the older counterpart is independently import-verified only. This static contract
does not establish runtime lookup or brush-application success.

For both supported profiles, the receiver's inherited **UUserWidget.WidgetTree**
is a strong reflected Object pointer at `+0x1d8`; the tree's **RootWidget** is a
strong Object pointer at `+0x28` (WidgetTree native size `0x30`). Default property
records are `0x14625c258` and `0x146268978`; older records are `0x1463cfc10` and
`0x1463dd0b8`. Both have native property kind `0x12`, not WeakObject `0x0e`.
Default UUserWidget registration `0x1406c8400 -> 0x144acd3d4` uses class params
`0x145e0ee80` and properties `0x14625bea0`; older registration
`0x1406c43e0 -> 0x144ce8b40` uses params `0x145fa4440` and properties
`0x1463cf870`. These are static metadata anchors, not callable entries.

Target checks must compare the **forward** current tree reference with the
target's Outer, not merely follow Outers backward: validate positive current
receiver/target handles, `target.Outer == tree`, `tree.Outer == receiver`, and
`receiver.WidgetTree == tree`. Capture/recheck the tree's RootWidget as well,
before mutation and after potentially reentrant native operations. This rejects
a cached child of an old tree still outered to the same receiver. A zero-serial
tree remains diagnostic-only; the current live owner's strong field explains
tree retention, not a fabricated weak lease. Neither Outer nor these fields
prove target containment/retention through the root. Do not use editor-only
`AllWidgets`, assume the tree pins arbitrary cached targets, or apply these
UUserWidget offsets to an EndImage (its `+0x1d8` is a Slate pointer).

The bounded panel-slot containment path was independently recovered for both
profiles (offsets hexadecimal):

| Strong reflected field | Offset | Default property record | Older property record |
| --- | --- | --- | --- |
| UWidget.Slot | `28` | `146260af0` | `1463d5070` |
| UPanelSlot.Parent | `28` | `1462517a0` | `1463c63b8` |
| UPanelSlot.Content | `30` | `146251770` | `1463c6360` |
| UPanelWidget.Slots | `108` | `146251b10` | `1463c6720` |
| Slots inner Object pointer | stride `8` | `146251b40` | `1463c6820` |

Pointer properties are kind `0x12`; the array is kind `0x16`, with pointer/count/
capacity at `+0x108/+0x110/+0x114`. PanelWidget derives Widget (native size `0x120`);
PanelSlot size is `0x38`. Default class params/property arrays are
`Widget 145e0f300/14625fa10`, `PanelSlot 145e0cb80/146251760`,
`PanelWidget 145e0cc20/146251b00`; older counterparts are
`Widget 145fa5290/1463d4bf0`, `PanelSlot 145fa2c50/1463c6390`,
`PanelWidget 145fa2a20/1463c6750`. Native child-access evidence
`0x140923214` (default) / `0x140970340` (older) corroborates count/stride/Content;
these are evidence anchors, not added callables. Default property records matched
authenticated disk bytes; older remains import-only.

The implemented containment check requires actual PanelSlot/PanelWidget ancestry,
`child.Slot == slot`, `slot.Content == child`, `slot.Parent == parent`, and the
same slot at a checked index in bounded `parent.Slots`. Walk to the **current**
tree.RootWidget, rejecting cycles, nulls, invalid arrays or exhausted bounds.
Root equality ends the walk; do not require root.Slot to be null. Reread every
edge's identities/fields/array header/index plus current tree/root/receiver/target
and session after building the path and before mutation after reentrant calls.
The forward strong Slots/Content chain explains target retention; backwards
links alone do not. No borrowed path survives a callback, and ancillary zero
serials are still not weak leases. The bounded checker has independent source
review; fake-native checks do not qualify its actual game execution.

The no-call native ancestry read ABI and base-class slots are retained in
[`MenuThemeContainment-20261020.json`](../analysis/MenuThemeContainment-20261020.json).
For both profiles, `Class+0x30` stores an ancestry-array pointer and `Class+0x38`
stores a signed **last index**, not a count. After identity/type/package checks,
require bounded nonnegative `baseLast <= actualLast` and
`actualArray[baseLast] == address(baseClass+0x30)`, **not** the base object or the
pointer stored in that field. Reject inaccessible/overflowing metadata and
reread both indices, array/entry, actual class and published base-slot pointer.
The independently recovered complete native predicate authenticates the read
layout only; it is never called. The two published UClass slots are authenticated
through immutable RIP-load windows, not mutable pointer-value signatures;
missing/null slots mean unavailable, never invoke their getters to initialize.
Base classes must be actual PanelSlot/PanelWidget UClasses under `/Script/UMG`,
not names alone. All five records are data-only or noncallable signature-only,
with default disk/import verification and older import-only attribution.

### Fresh mip capacity correction

The first background run stopped before upload/application because the bridge
incorrectly required both mip count and pointer-array capacity to equal one.
Native factories instead create **one mip** using an initially empty array whose
first growth requests at least four pointers (32 bytes), then applies allocator
size quantization. Thus count remains one while capacity is at least four, not
necessarily exactly four. The default factory/growth path was checked against
authenticated disk/import code; the older counterpart was independently checked
in its import and retained in the older-build evidence JSON.

The corrected reader accesses only pointer element zero and does not iterate or
allocate from capacity. It preserves exact count, dimensions, format, byte count,
flags, lock and pointer checks and requires header stability across materialization.
Diagnostics retain already-read count/capacity and the first failing read/predicate;
logging performs no additional native reads. The failed run did not record its
actual capacity, so this static correction does not itself establish runtime success.

### Observation Boundary

The separate read-only Debug observer runs in the existing exact menu-open
callback, after the native original returns and the session publishes `Ready`.
It samples before/after the existing session-ready notification, not from a new
hook, tick, background worker or WndProc. It does not create textures, invoke
native name lookup, acquire a collector lock or apply a theme.

The `menu_theme_observation` catalog records distinguish mutable facts from
immutable, noncallable verification windows:

- `native_game_thread_initialized` and `native_game_thread_id` are published by
  `native_game_thread_publish_site`. Startup fallback is not accepted. The native
  UObject constructor's thread-role predicate additionally reads role `2` from
  the native TLS block; `native_uobject_thread_role_site` authenticates the actual
  body read: current-thread `GS:[0x58]`, literal native vector slot `0`, then DWORD
  block offset `0x398`. A missing vector/block is unavailable; neither the mod's
  TLS index nor the executable's PE TLS index is a fallback selector.
- `native_garbage_collecting` is the actual collection-scope flag reached by
  `native_gc_flag_exchange_site`, not a collection request or timer.
- `native_uobject_exit_purge` is a late shutdown byte referenced by
  `native_exit_purge_publish_site`. The transient package is removed earlier, so a
  clear byte alone cannot exclude shutdown. Package slot/membership/root facts and
  session identity must also be observed and rechecked.

Site signatures authenticate code bytes, never the current values of mutable
globals. RIP references must match their cataloged globals. Missing profile
evidence or failed reads disable only the observation, not the native menu.
Package FNames are copied as raw IDs without calling native name lookup; this
does **not** verify the strings `Package` or `/Engine/Transient`. Zero-serial
membership remains diagnostic-only. Even stable healthy before/after facts do not
prove uninterrupted GC exclusion, early-exit exclusion or safe reentrancy, and
must not be promoted into a texture-creation authorization token.

The independently recovered read profiles and instruction windows are retained
in `analysis/MenuThemeObservation-20261020.json`. The default profile has exact
disk/import byte equality and uniqueness; the older profile has imported-program
evidence only. Neither profile has runtime qualification from these static reads.

Older-build static research is retained in
[`MenuThemeNative-OlderBuild-20261020.json`](../analysis/MenuThemeNative-OlderBuild-20261020.json),
including independently recovered mappings and layout differences. The bridge
entries are integrated into the canonical catalog and generated per-build trees;
additional palette/ImageSet candidates remain research only. Imported program
evidence is not older-disk equality or runtime validation.
