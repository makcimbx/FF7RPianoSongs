# Piano UI presentation: bounded static evidence

## Scope and identity

Read-only Integration-support investigation at native checkpoint
`0ce16f2208d86971201640643e033fc95dbf481f`. This record does not qualify
gameplay or implement the count, difficulty, rank, or focus changes. Concurrent
count work and mutable validation/status documents were not used as native proof.

Queries explicitly selected these existing Ghidra programs; no analysis creation,
renaming, patching, or process operations were performed. Image base is
`0x140000000`; addresses below are VAs unless marked RVA.

| Build / program | Fresh static PE identity | Catalog executable SHA-256 |
|---|---|---|
| 1.005 / `ff7rebirth_.exe-9457d4`, project `/ff7rebirth_.exe-1.005` | PE at `+210`, timestamp `6a16ced2`, image `099d9000`, checksum `0769ea6e` | `752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc` |
| 1.004 / `ff7rebirth_.exe`, project `/ff7rebirth_.exe-1.004` | PE at `+220`, timestamp `68fd6fde`, image `09bad000`, checksum `07877c99` | `76bd4d539878a8759433f1cd7c7b47d5e3abda0ef41572a664d2d606a9f09dc8` |

The full imported executable hashes were not freshly computed. PE measurements,
explicit program selection, retained exact-build records, and newly recovered
call relationships are distinguished from catalog migration records.

## 1. Difficulty is repeated localized text, not the speed panel

**Do not repurpose `PianoListEntry.reserved` at entry+8 for difficulty.**
On 1.005, `1439A6D68` changes that integer using the speed keys, with lower bound
`ScoreInfo+71` and upper bound `panel.child_count-1`. `1439C21C8` renders the speed
panel: for each actual child, state is 2 when its index is at/below `ScoreInfo+71`,
otherwise 1/0 according to whether it is at/below entry+8. `140889F6C` stores the
state index at widget+13C, propagates it to Slate+318, and invalidates. Actual
brush/color assets were not inspected; these states must not be named arbitrary
difficulty icons. Selected-detail updater `1439A84E4` also invokes this speed path.

The actual list-item difficulty renderer is:

| Build | Item setup | ScoreInfo lookup CALL -> return |
|---|---|---|
| 1.005 | `1439A8C18` | `1439A8CF2 -> 1439A8CF7`, target `143951D5C` |
| 1.004 | `143C4F960` | `143C4FA3A -> 143C4FA3F`, target `143BFF0A4` |

Both resolve the difficulty child, localize one token, append it
`int32(ScoreInfo+6C)` times, and pass the resulting FString to the native text
setter. On 1.005 the token global is `149012840`; initializer `1405015E0` writes
`$minigame_piano_music_select_difficulty`. Child-name global is `1490127B8`.
The corresponding 1.004 globals are `1491DA790` and `1491DA708`; the recovered
1.004 renderer has the same loop and +6C load. Its existing native setter is
`140E77B78`; it need not be separately detoured to use this path.

The 1.005 loop checks +6C at `1439A8F26` (`44 39 68 6C`) and `1439A8F4F`
(`3B 58 6C`). There is **no fixed six-child panel bound in this renderer**.
Zero/nonpositive values produce an empty string; sparse labels are repeat counts,
not profile indices. This does NOT prove arbitrary large labels fit the widget
or are safe to feed to an allocating native loop. Current public nonnegative
labels and `native_scoreinfo_difficulty()`'s 1..6 clamp are different contracts.
A presentation-size policy for oversized labels needs an explicit product choice;
do not silently clamp them or invent a native panel capacity.

New independently unique, complete-instruction caller windows (one match per
explicit image; return is window+0C hex):

```text
1.005 RVA 039A8CEB, 21 bytes
48 8B D3 48 8D 4D D0 E8 65 90 FA FF B8 00 40 00 00 66 41 85 07
1.004 RVA 03C4FA33, 21 bytes
48 8B D3 48 8D 4D D0 E8 65 F6 FA FF B8 00 40 00 00 66 41 85 07
```

Source cause and smallest reuse boundary:

- `list_patch.cpp:310-330` clones native entries unchanged. This is not itself
  a reason to change their speed field.
- `list_patch.cpp:408-440,478-495` already owns initial item setup and profile
  refresh under `ScopedSongRenderContext`.
- `scoreinfo_overlay.cpp:404-451` admits only its cataloged menu-detail render
  role to the temporary private-row path. The list-item return above is missing
  from that role inventory (`scoreinfo_result_policy.h:19-27`).
- Add a distinct, exact list-item caller role to the EXISTING resolver owner;
  retain its row in the outer render scope and preserve source-wrapper checks.
  Give this presentation row the selected difficulty label under the agreed
  size policy. Do not globally relax result/native difficulty semantics or
  modify shared ScoreInfo assets. The current row builder clamps to 1..6
  (`scoreinfo_overlay.h:78-81`) and cannot be reused unchanged for zero/>6 labels.

This requires caller catalog/generated policy plumbing, not another hook or
new icon widget. The native widget retains/copies the text through its normal
setter; the private ScoreInfo row remains owned until the outer setup returns.

## 2. List star and RESULT rank are different paths

`progress.cpp:285-302` returns literal U+2605 for the highest custom rank.
Its consumers are the LIST overlay in `list_patch.cpp:97-146,435-440,494`.
There is no demonstrated path from that literal to the RESULT screen.

Native 1.005 rank helper `1439A0E80` instead returns FString localization keys
`$minigame_piano_evaluation_C`, `_B`, `_A`, `_S`. `1439A0C48` compares the four
ScoreInfo thresholds and selects the last satisfied key; its lookup return
`1439A0C79` is already cataloged. Native list setup also uses these keys.

RESULT entry `14399A334` calls this helper at `14399A4CE`. It moves the rank key
into result-data offset **+1B0**, passes the data to `143968EA8` at controller+8E8,
which copies it to result-owner+120 through `1438FBD60` and requests state 1.
`1438FBD60` deep-copies the +1B0 FString along with the other result fields.
This establishes owned key transport, NOT the final resolved glyph/font pair.

`140AFBEF8`, the existing 1.005 list text setter, copies FString to widget+128,
updates text state +138, derives Slate presentation and invalidates. It is not
evidence that an arbitrary Unicode star has the required RESULT font mapping.
The older rank collection helper `143966A6C` / 1.004 `143C1264C` displays a
collection of piano records and uses SetStringText; it is not this RESULT sink.

**RESULT fix remains evidence-limited.** The actual localized `_S` value,
the concrete RESULT rank widget, and its effective font/rich-text style were
not recovered here. Changing list U+2605 cannot honestly be called a RESULT fix.
Reusing native rank keys is the justified list-format boundary, not proof of a
corrected result star. The bounded next evidence is the generic result consumer
of owner+120+1B0 and its widget/font/localization assets (or an explicitly
authorized observation of that exact sink); no global font/Unicode patch is
justified. No RESULT hook or new rank-helper ABI is recommended yet.

## 3. Last-played focus: native first-FName match loses custom identity

> **Correction after failed candidate `57ed12e`: the post-original helper-call
> recommendation in historical steps 4–5 below is SUPERSEDED.** The helper also
> registers native focus history; calling it again after native Open duplicates
> that transaction. Use the single-call interception boundary in the final
> section of this document. Do not implement the old “no extra detour” proposal.

Native open calls row publication and then selection restoration:

| Build | Open | Publish rows | Restore selection |
|---|---|---|---|
| 1.005 | `1439B8BF4` | `1439B0F94` | `1439B0FF0`, CALL `1439B8CC1` |
| 1.004 | `143C5F5C8` | `143C577F0` | `143C5784C`, CALL `143C5F695` |

Open writes its packed selected FName to widget+478. Both restore helpers have
ABI **void(widget*)**, RCX=the music-list widget. They validate selected index
at +480 against count+420 (invalid -> zero), then scan 12-byte entries at +418
for the **first** FName equal to +478. Zero +478 skips this scan. Custom clones
duplicate their stock backing FNames, so native lookup cannot restore their
distinct descriptor identity.

The helper does more than write +480: it resolves the live list child through
weak handle +3C8, passes the chosen index in R8D to the native selection operation
(`140FE3BBC` / `141551498`). On 1.005 the downstream `140FE3D44` writes the
internal selected index, requests focus/selection work, and conditionally invokes
the selected-index delegate. Merely calling the existing detail updater would
not establish this internal list selection. A same-index selection need not
emit another delegate; registry synchronization must account for that.

Fresh unique full-instruction entry windows (one match in each image):

```text
1.005 RVA 039B0FF0, 24 bytes
40 53 B8 30 00 00 00 E8 84 AB 7B FE 48 2B E0 8B 81 80 04 00 00 48 8B D9
1.004 RVA 03C5784C, 24 bytes
40 53 B8 30 00 00 00 E8 D8 76 45 FE 48 2B E0 8B 81 80 04 00 00 48 8B D9
```

### Historical ownership proposal — steps 4–5 superseded

These are design consequences of the recovered branches, not tested changes:

1. Store a presentation bookmark **song ID + difficulty label**, not an old
   visible index, descriptor pointer, backing FName, or profile vector index.
   Existing registry playback publication (`song_registry.cpp:468-482,543-563`)
   is an accepted-playback boundary; hovering and result-only persistence are
   not equivalent. It must survive cleanup without keeping a playback lease.
2. Keep native stock behavior: a confirmed STOCK activation must supersede a
   previous custom bookmark. Use the existing exact activation-getter caller
   authority, not every stock getter/hover. Currently the no-descriptor branch
   returns before that caller classification (`selection.cpp:467-488`).
3. After exact pre-open catalog adoption, resolve the ID in current immutable
   storage and the difficulty by label. Missing song -> native stock reopening;
   removed profile -> current declared default, not nearest/old-index guessing.
   Current selected-profile preferences store indices (`song_registry.cpp:21-28,
   365-412`), while `commit_catalog:148-153` preserves them across replacement:
   remap by difficulty at that boundary or store labels internally. No second
   registry or generation counter is needed.
4. Extend the EXISTING menu-open owner after original open returns but BEFORE
   `publish_ready` and `notify_session_ready` (`menu_session_authority.cpp:170-189`).
   Validate exact Opening generation, live widget serial/index, current catalog,
   managed array pointer/count/capacity and target row. Existing list preparation
   and owner checks are in `list_patch.cpp:552-658,671-703,816-850`.
5. Within that synchronous boundary, a scoped +478=0 plus resolved +480 lets the
   recovered native restore helper apply the exact index without the duplicate
   FName scan. Restore only the temporary +478 field under the same identity and
   generation; leave the intentional native selection. Do not assume that seeding
   +480 BEFORE original open survives all native row-publication callbacks.
   A cataloged helper call suffices; no extra detour is required. Native calls
   remain outside registry/menu locks; retain current immutable storage locally.
6. Verify/synchronize the final registry selection through the existing selected-
   index owner, including its same-index/no-delegate case; refresh detail and the
   restored item using their existing render scopes. Publish Ready only with this
   final target, not a transient first stock row. Existing coordinator stores one
   deferred direction (`profile_list_coordinator.cpp:53-65`) and checks bound
   identities before draining (`103-189,371-416`). Ensure its final readiness
   identity is the restored item; an already-bound superseded intent must be
   explicitly rejected, never retargeted silently. No delay, poll count, or second
   intent queue is justified.

If identity/tuple authority is absent, do not write or guess. Before mutation,
use the established recoverable native fallback/rejection. After a scoped native
write, failed restoration is a menu mutation failure, not successful focus; reuse
the existing terminal menu ownership path. This does not promise recovery from
native allocation faults, arbitrary reentrant destruction, or live ASI unloading.

## Handoff and validation boundary

Smallest owners: difficulty -> existing ScoreInfo resolver/caller policy and
list render scopes; focus -> existing registry preferences, menu-open owner,
selection/list coordinator plus a cataloged native restore helper. No count,
audio, chart construction, or new hook framework changes are supported here.
Generated output should be regenerated by the lead, never edited manually.

Before acceptance, focused checks should cover list-item caller isolation and
private-row lifetime; labels 0/sparse/oversized with an explicit presentation
policy; reordered/removed songs and profiles; stock activation after custom;
same-index reopening; immediate profile/activation input without sleeps; stale
widget/catalog rejection and scoped-field restoration. Native focus and icon
appearance on both builds still need authorized in-game observation. RESULT
glyph correction needs the missing sink/font evidence above before implementation.

## RESULT follow-up: concrete sink and remaining asset/observation boundary

This append-only follow-up leaves the difficulty/focus sections frozen. It uses
the same 1.005 identity and explicitly selected Ghidra program
`ff7rebirth_.exe-9457d4`; no runtime calls, production changes, or catalog changes
were made. The concrete text sink is now recovered. The resolved S glyph and
its active font/symbol assets are still not established, so this is not a fix
recommendation or an assertion that the mod causes a font mismatch.

### Recovered result state and widget binding

The generic result component constructed by `1439529A8` has vtable `146619670`.
Its native initializer `14395E718` locates **`ResultWindow`**, then binds twelve
named text children into the pointer array at result-owner+78. Initializer
`1404FACE0` constructs the names at global `1490110A0`:
index 6 = `Txt_RankTitle`, **index 7 = `Txt_Rank`** (`1490110D8`). Thus the
rank text pointer is **result-owner+B0**. These are native-owned widget pointers,
not new mod-retained UObject references or asynchronous mutation authority.

The decompiler incorrectly eliminated much of `14395E718` as unreachable.
Its complete 164-instruction listing independently retains the binding loop:
name base `1490110A0`, destination base owner+78, indexed 8-byte entries, and
CALL `1412A88E8` at `14395E7F9`. The instruction listing also installs state table
`146629480`; this is a state table, not another widget vtable.

State table records have stride 20 hex:

- State 1 entry `143959630`, update `14395B448`: create/populate generic result
  rows, then request state 2. The entry's `/Game/Menu/Billboard/Common/` lookup
  is a separate billboard resource, not proof of the rank font.
- State 2 entry **`143959240`** supplies rank data to the text sink.

At `14395931C` it executes the following complete 17-byte window:

```text
4C 8D 83 D0 02 00 00  B2 07  48 8B CB  E8 4F B5 00 00
LEA R8,[RBX+2D0]     MOV DL,7 MOV RCX,RBX CALL 14396487C
```

This is exactly result-owner+120+1B0 from the piano result producer, not the
custom list's U+2605 string. `14396487C(owner, uint8 index, FString* source)`:

```text
if source.num > 1 and owner.text_widgets[index] != null:
    copy = native_FString_copy(source)          // 1408D5950
    native_SetStringText(widget, copy)         // 140E8B948
```

Instruction proof: widget load `48 8B 5C C1 78` (owner+78+index*8);
CALL `1408D5950` at `1439648A7` = `E8 A4 10 F7 FC`;
CALL **`140E8B948`** at `1439648B2` = `E8 91 70 52 FD`.
These windows were read directly, not uniqueness-scanned installation signatures.
No new RESULT hook/helper catalog entry is proposed from them.

### Native localization and symbol handling, not raw Unicode substitution

Fresh inspection of `1439A0E80` confirms that C/B/A/S are still the four
`$minigame_piano_evaluation_*` keys. The RESULT producer and the stock producer
are the same original native piano result routine. The inspected mod path
supplies private ScoreInfo/progress values; it has no demonstrated write to
`Txt_Rank`, result-data+1B0, the localized S value, or this widget's font.
Different thresholds can change the chosen rank; they do not establish a font
replacement. Runtime equality of the final custom/stock contexts is unmeasured.

`140E8B948(widget, owned FString*)` copies into widget+128 through `140E8CAA0`,
clears the alternate text source at +138 through `140E8CE04`, updates Slate
text fields +2C8/+2D8/+2E0 when widget+220 exists, calls **`141121DB0`**, and
invalidates. It finally destroys the supplied native temporary FString through
`140764EA8`. This is a consuming native call; a borrowed `std::wstring` view
must not be passed to it as if it were the existing non-consuming list setter.

`141121DB0(SlateText)` provides the missing localization stage:

1. Reads/copies the text attribute at Slate+2C8.
2. If it begins with `$`, calls the active service at `DAT_149186158`, virtual
   slot +50, with the key and a context name derived from Slate+568. A returned
   FString replaces the key. A still-unresolved `$` string is emptied; the
   decompiled branch does not deliberately print the raw key as a squiggle.
   The two `$` checks are independently present at `141121FE9` (`66 83 3A 24`)
   and `14112209F` (`66 83 38 24`).
3. Uses the active text/font resource at Slate+2F0 (and its selected effective
   resource) for layout. With the native markup option enabled it performs
   `<icon=...>` replacement through `1411B306C` and further tag processing
   through `1411B32F8` before laying out glyphs.
4. `1411B306C` looks up the icon name in that resource's map at +88, whose
   records have stride 20 hex. A nonzero UTF-16 value at record+10 is appended.
   Therefore an icon tag can depend on a resource-specific code-unit mapping;
   a visually similar arbitrary Unicode character is not equivalent proof.

Neither the current contents returned for `_S` nor the active font/resource
object and its icon-map entry are serialized in these static code reads.
No claim is made that `_S` actually contains an icon tag, that its glyph is
U+2605, or that a particular font is wrong. The common renderer has no recovered
S-only branch that explains the report independently of those missing values.

### Bounded retained-index check and disposition

The known exact pakchunk3 manifest was reused, not regenerated or expanded into
an archive-wide search: 8,991 entries, SHA-256
`ab74cb88468d81e1ade21164dd090f6165ac4ee0d34f2ebdd22190bda88e1ed3`.
The focused result-named Menu/UI query and its scope are retained at
`C:/Users/makci/AppData/Local/Temp/opencode/ff7rp-result-rank-audit/inventory.py`
and `inventory.json`. It identifies
`/Game/Menu/Billboard/Common/U_Com_Billboard_Result_Piano`, chunk
`983a116966369fc600000002`, but this is not an identified `ResultWindow/Txt_Rank`
font/localization package. It was deliberately not extracted as a speculative
substitute. No binary assets or full copyrighted tables were added to the repo.
Other retained manifest directories inspected contained legacy audio manifests
or were empty. This is not evidence that the required asset is absent from the
installed game or other containers.

**Stop boundary:** the native sink and localization/symbol mechanism are proven;
the actual S content and effective widget resource are not. No safe RESULT
correction, global font change, replacement Unicode, or extra hook follows yet.
Because no new mutation seam is recommended, this follow-up does not claim new
1.004 RESULT ABI/signature parity; the previously verified difficulty/focus
addresses for both builds are unaffected.

The smallest discriminating human evidence is a full RESULT screenshot plus
rank crop for a stock S result and a custom S result with the same language,
installation, and UI settings; an A result and the corresponding list-rank crop
help identify whether the report concerns `Txt_Rank`, a list overlay, or the
billboard. Same malformed shape on stock/custom points to shared presentation
or its intended stylization, not proof of custom rank-string corruption.
Correct stock S but malformed custom S narrows the next authorized observation
to the two inputs at this exact sink: key/resolved UTF-16 including any tags and
the effective text/font resource (including icon mapping). No such observation
or game launch was performed here, and changing the installed artifact is not
required by this research disposition.

### Optional numeric difficulty note from the shared text path

Plain decimal text does not enter the leading-`$` lookup branch. A numeric label
can therefore reuse the EXISTING list item's text-setting owner and its native
conversion/invalidation path; it does not require a RESULT detour or a new font.
For the existing 1.005 `EndTextBlockSetText` path (`140AFBEF8`), keep the borrowed
FString storage alive for the synchronous copy; do not confuse that ABI with
the consuming `140E8B948` call above. Retain exact live item/widget identity and
the current render snapshot as already required by the list owner. This is an
ownership/path observation, not an implementation or a claim of a new numeric
setter signature for 1.004. The display threshold/capacity policy and visual fit
remain the difficulty owner's decision; no arbitrary cutoff was derived here.

## 1.004 follow-up: non-consuming list text setter — implementation-ready

This append resolves the missing 1.004 `end_text_block_set_text` catalog evidence
for the approved numeric presentation of difficulty labels above six. It does
not modify the earlier sections, implement presentation, or resolve RESULT's
separate glyph question. No production/catalog/generated edits, builds, tests,
native calls, process operations, or Ghidra mutations were performed.

### Exact identity and independent recovery

Every 1.004 query explicitly selected `ff7rebirth_.exe`; fresh program metadata
reports project `/ff7rebirth_.exe-1.004`, PE, x86:LE:64/windows, base
`140000000`, 373972 functions, and no overlays. Fresh static bytes at
`140000220` again identify timestamp `68fd6fde`, SizeOfImage `09bad000`, and
checksum `07877c99`. They match catalog build
`ff7rebirth-steam-win64-68fd6fde` and its retained executable SHA-256
`76bd4d539878a8759433f1cd7c7b47d5e3abda0ef41572a664d2d606a9f09dc8`.
The full imported executable hash was not freshly computed.

Recovery followed **1.004 native item setup `143C4F960`**, not an RVA delta:
it resolves the difficulty text child through `140803390(item, &DAT_1491DA708)`,
builds its text by repeating the localized difficulty token, and calls
**`140E77B78`**. The same native routine uses this setter for title and rank,
including a borrowed global FString for the unplayed rank. The lookup helper
uses the item's named-child cache; its cache-miss path resolves the child and
checks its type through `1413FA0DC` before caching it. That is the actual
difficulty child, not the speed panel or the generic RESULT text sink.

### Catalog candidate and ABI

Add this build to the EXISTING `end_text_block_set_text` /
`EndTextBlockSetText` entry; retain `signature_only`, the existing consumer and
ownership policy. No new hook, consuming setter, or bridge is required.

```json
"ff7rebirth-steam-win64-68fd6fde": {
  "rva": "0x00e77b78",
  "signature": {
    "bytes": "40 53 b8 50 00 00 00 e8 ac 73 23 01 48 2b e0 83 7a 08 00 4c 8d 89 28 01 00 00 4c 8b d2",
    "mask": "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  }
}
```

The **29-byte complete-instruction entry window** was read directly and scanned
image-wide in this explicit program: exactly **one match, `140E77B78`**.
It ends after `MOV R10,RDX`; it does not cut the following RIP-relative LEA.
The existing 1.005 entry `140AFBEF8` was separately decompiled for semantic
comparison, not used to synthesize the 1.004 bytes.

ABI: **`void setter(widget*, const FStringView*)`**, Microsoft x64:
RCX = native text widget, RDX = borrowed FString header, no result consumed.
Header layout is data pointer +0, int32 Num +8, int32 Capacity +C; text is
well-formed NUL-terminated UTF-16 (Num includes the terminator when nonempty).
Caller storage must remain valid throughout the synchronous call, but is
neither transferred to native ownership nor retained after return.

### Ownership proof, including both cleanup sites

Complete 47-instruction setter inspection agrees with its full decompilation:

1. Compare incoming UTF-16 with widget-owned FString at +128/+130. Equal text
   returns without retaining the argument or rebuilding presentation.
2. On change, CALL **`140A3A0BC(widget+128, originalArgument)`** at `140E77BD6`
   (`E8 E1 24 BC FF`). Entry saves the argument in R10; `140E77BD0` restores it
   to RDX, and `140E77BD3` puts widget+128 in RCX.
3. `140A3A0BC` reads source data/count, assigns the destination count, resizes
   DESTINATION storage through `14133717C`, then `memcpy(dst.data, src.data,
   Num*2)`. It neither assigns the source pointer into the destination nor
   destroys/modifies the source header. The resize helper receives destination
   and size/capacity, not the source buffer.
4. Setter clears alternate source fields widget+138/+13C/+140. If Slate+220
   exists, `14098EDDC(widget, stackTemporary)` derives text from the widget's
   owned state. `141081AF0(Slate, stackTemporary)` updates text, rebuilds, and
   invalidates. These internal helpers differ structurally from 1.005, but the
   external borrowed-input contract is the same.
5. The setter's destructor CALL **`14085FC64` at `140E77C31`** cleans **its own
   `[RSP+20]` conversion temporary**, NOT original RDX. Instruction
   `140E77C2C: 48 8D 4C 24 20` proves the argument. That destructor releases the
   temporary's auxiliary owner at +18 and its native FString. The original
   supplied FString is not passed to it or another destructor.

Native caller cleanup provides independent corroboration. Exact complete
35-byte window at **`143C4FC9C`**:

```text
48 8D 55 B0 48 8B CE E8 D0 7E 22 FD
48 8B 4D B0 48 85 C9 74 05 E8 1A FC B4 FC
48 8D 4D C0 E8 31 FB B4 FC
```

It passes its local FString header `[RBP-50]` in RDX and the difficulty widget
in RCX; **CALL `143C4FCA3 -> 140E77B78`, return `143C4FCA8`**. Only afterward
the CALL at **`143C4FCB1`** frees that local string's data through native free
`14079F8D0`, followed by destruction of the separate localized token through
`14079F7F0` at `143C4FCBA`. The caller's buffer therefore does not become widget
storage. The complete caller instruction scan covered 232 instructions without
truncation. This is not the consuming RESULT `SetStringText` contract.

### Minimal integration and remaining checks

- Lead can add the build entry with this independent evidence reference and
  regenerate catalog-derived files; do not hand-edit generated data or add a
  second text hook. Unknown/mismatched images remain unavailable/fail-closed.
- Reuse the existing list-item/profile-refresh owner, exact live widget/item
  identity, and owning render snapshot. Pass a bounded, terminated numeric
  FString view that stays alive until this synchronous call returns. The native
  widget owns its copy and its subsequent normal replacement/destruction.
- No shared font/asset mutation, native allocator interoperability for the
  caller's buffer, retained raw widget pointer, or special restoration journal
  is required by this setter contract. It is not authority to call a stale or
  wrong-type widget; existing lifetime checks remain required. Arbitrary native
  OOM/SEH/reentrant destruction recovery is not claimed.
- Focused implementation checks: generated 1.004 address/signature resolution;
  same borrowed-view ABI for both builds; source lifetime/termination; numeric
  update during item setup and profile refresh; unchanged stock/default path.
  Later authorized in-game checks must confirm numeric labels above six,
  transition back to repeated icons, clipping/layout, and reopen/profile
  refresh. No such build, harness, or gameplay check was run in this research.

**Disposition: this concrete 1.004 setter blocker is closed at the static ABI,
call-site, ownership, and exact-signature level.** It is ready for the existing
runtime worker's integration, not a claim that the presentation has been built
or visually qualified. RESULT-star research is unchanged.

## Focus regression correction: intercept the ONE native Open restore call

### Scope, provenance, and confidence

This read-only follow-up corrects the earlier post-Open proposal, not the
accepted-playback bookmark or stable song/profile identity contract. Source
references below describe failed candidate
`57ed12e95abe3e787d1078399345d97ccc4d56ec`, compared with accepted base
`0ce16f2208d86971201640643e033fc95dbf481f`. Only this evidence file was changed;
no native hook was installed, no catalog/generated/production code changed,
and no build, test, game run, or commit was performed.

All new static calls explicitly selected the program; no address-delta mapping:

| Build | Program / project | Fresh PE check (base `140000000`) |
|---|---|---|
| 1.005 | `ff7rebirth_.exe-9457d4` / `/ff7rebirth_.exe-1.005` | PE at +210: timestamp `6a16ced2`, image `099d9000`, checksum `0769ea6e` |
| 1.004 | `ff7rebirth_.exe` / `/ff7rebirth_.exe-1.004` | PE at +220: timestamp `68fd6fde`, image `09bad000`, checksum `07877c99` |

The exact executable SHA identities are those recorded at the start of this
document/catalog; the complete imported executable hashes were not freshly
computed. Complete function-scoped MOV/CALL searches each covered all 56 Open
instructions without truncation. Caller bytes were read directly, then each
full pattern was searched image-wide and had exactly one match.

Failed session `20260907T085725Z-ef9b9fc8976c` has log SHA256
`8a9cb4910b8961b9f0f249f1518da41c26d61280e8a719bf212bafb7b23ab365`.
First custom admission/handoff succeeded (979–989), playback published
(1019–1020), completed, and cleanup resolved (1587–1597). After bookmarked reopen,
Multi Difficulty Manual Test reserved and confirmed (1627–1628), but Close
already saw an erased reservation (1629), and persistent admission failed
(1634), followed by enabled native fallback (1641 onward). Nexus and ZZ Check 01
repeat this pattern. The lead finalized Rollback; this research changes no
installed artifact or session disposition.

The independently recovered 1.005 native defect is **duplicate focus-history
registration**, not merely a redundant index write:
`Open -> 1439B0FF0 -> 140FE3BBC/140FE3D44 -> 1414A481C -> 14083C3FC`.
The last helper appends a source-widget/previous-focus record (stride28, native
header40/count48/capacity4C), without same-source deduplication. Native Close
uses `1439A1AF4 -> 1416995C8 -> 14083BF7C` to remove matching records and restore
intermediate previous focus. Candidate `menu_session_authority.cpp:171,179`
executed original Open and then `list_patch.cpp:848–853` called restore again.
Restoring only widget+478 did not undo that second native focus transaction.

This explains the new side effect and its first-play/bookmarked-reopen boundary.
**The exact last reflected notification/getter/revoke call inside failing Close
was not recorded.** Do not promote that unobserved last hop into a captured
trace. The expanded focus-manager chain was recovered on 1.005; this pass proves
the corresponding single native Open call boundary on both builds without
claiming a second focus-manager audit or corrected gameplay.

### Exact catalog candidates and ABI

Keep existing `piano_list_restore_selection` / `PianoListRestoreSelection`
addresses and complete 24-byte entry signatures from section 3. Change its
integration from an additional callable invocation to ONE entry hook owned by
the existing menu-session module. No mid-function patch, register bridge, or
global focus-manager hook is needed. Native ABI remains `void(widget*)`, RCX
the music-list widget; call its trampoline with that same widget.

Suggested new signature-only authority ID:
`piano_list_restore_selection_caller` / `PianoListRestoreSelectionCaller`.
It identifies a caller WINDOW, not the helper entry and not a patch site:

| Build | Window RVA | Restore CALL RVA | Helper RVA | Exact return RVA |
|---|---|---|---|---|
| 1.005 | `039b8cae` | `039b8cc1` | `039b0ff0` | **`039b8cc6`** |
| 1.004 | `03c5f682` | `03c5f695` | `03c5784c` | **`03c5f69a`** |

Both windows are 24 bytes / six complete instructions, all bytes exact
(mask `xxxxxxxxxxxxxxxxxxxxxxxx`). Restore CALL is window+13 hex;
**return is window+18 hex**, including the five-byte CALL:

```text
1.005 @1439B8CAE (one image-wide match)
48 8B C8 E8 DE 82 FF FF 48 8B CF E8 3E BD E7 FC 48 8B C8 E8 2A 83 FF FF

1.004 @143C5F682 (one image-wide match)
48 8B C8 E8 66 81 FF FF 48 8B CF E8 06 63 CA FD 48 8B C8 E8 B2 81 FF FF
```

Decoded sequence on each build:

```text
MOV RCX,RAX
CALL publish_rows            ; 1439B0F94 / 143C577F0
MOV RCX,RDI                  ; RDI = &list_member.weak_widget (+7A0)
CALL resolve_weak_widget     ; 1408349FC / 141905998
MOV RCX,RAX                  ; exact native widget argument
CALL restore_selection      ; the ONLY direct restore call in this Open
```

There is no loop or second restore call in either Open. Its guard can skip the
whole body when the list is invalid/already active; **do not synthesize a missing
restore call after return**. An unexpected nested call must not consume the
outer frame's projection; it retains ordinary native forwarding and is not
evidence that another custom restore should be attempted.

### Publication, active state, and native remainder

Before the signature window, Open writes list-member+7D8 = `0001`
(1.005 `1439B8C4F`; 1.004 `143C5F623`), resolves list-member+7A0, and writes its
original incoming packed FName into widget+478 (`1439B8CA2` / `143C5F676`).
`publish_rows` has returned before the intercepted CALL. It validates the child
weak handle widget+3C8, publishes widget+420 as native row count, and invokes
native row refresh:

| Build | Count setter | Row refresh |
|---|---|---|
| 1.005 | `1409703C4` | `1409703F0` |
| 1.004 | `1410EA154` | `1410E5A10` |

This proves ordering, **not that every virtualized item exists** or that a void
publisher cannot skip an invalid child. The current managed array must already
be published and owned at interception: validate the existing `Opening` session,
live widget identity, catalog storage/revision, and exact +418/+420/+424 managed
array tuple with the existing list owner (`list_patch.cpp:816–846`). Do not treat
the call address alone as publication success or patch a raw stock/unowned array.
Target row must resolve by current song ID/difficulty and lie in that exact
managed custom range. No pre-Open index seeding assumption is required.

After the restore return, neither Open directly writes widget+478/+480 nor
publishes rows/calls restore again. The remaining calls are:

- 1.005 `1408A5B8C -> 1427C60F8 -> 140FE2E80`, and
  `141176618 -> 141167124 -> 141167208`.
- 1.004 `140806940 -> 142B3E818 -> 141550C38`, and
  `140DC7F60 -> 140DCA0BC -> 140DCA260`.

The first branch sets manager mask bit1 at+50 and, on changed enabled state,
updates input state through `140FE32E4` / `141550D40` (pressed-key state and
input mode). The second builds and forwards a named native request with unit
float parameters. Neither inspected branch performs another list restore or
directly writes this list's selection. They are not a second focus transaction.
This is not a guarantee against arbitrary callbacks/input/object destruction:
**revalidate final widget/session/array/selected index after full original Open
returns, before Ready**. If it drifted, do not repair it by calling restore again.

### Corrected existing-owner orchestration

1. Existing Open owner adopts the catalog, establishes `Opening`, and begins the
   existing profile-list session. Push a nested-call-safe RAII TLS frame around
   **original Open only**. It carries owning session/catalog/target context and
   a local invocation/outcome flag, not a new registry, lifecycle epoch or queue.
   A nested/ineligible Open must shadow, not accidentally inherit, outer authority.
2. At restore ENTRY, capture `_ReturnAddress()` there (not in a downstream C++
   helper). Require exact build-specific return above, the current non-consumed
   Open frame, RCX == its exact live widget, and the established Opening/list/
   managed-array/target checks. Claim that frame before calling native code.
   Ordinary unrelated calls, no bookmark, removed target, or failed pre-mutation
   eligibility forward their original helper exactly once without projection.
3. Reuse `restore_last_played_menu_focus`'s managed-list/identity checks and
   `restore_menu_focus_fields` journal **as interceptor action**: save +478,
   temporarily set it to zero, set +480 to the resolved target, and invoke the
   **helper trampoline once**, not its hooked public address. Native selection,
   synchronous delegates, and the sole focus-history registration run in their
   original position inside Open. Restore only temporary +478 while exact
   ownership still holds; +480 is the intentional native selection.
4. The current bool return from `restore_last_played_menu_focus` is not an
   original-call contract: several `true`/NotApplied paths never invoke its
   supplied callback. The adapter must explicitly distinguish “not applied,
   original not entered” from “original entered/applied/failed”, or track entry
   in the supplied closure. Do not skip the sole stock call on an early true
   return, and do not call it a second time after an applied/failed invocation.
   Post-mutation ownership/restoration failure uses the existing terminal menu
   path, not speculative native retry or a claim of successful restoration.
5. Reuse existing selected-index/detail synchronization and coordinator intent
   reconciliation, including same-index/no-delegate handling. No native call
   while registry/menu/list locks are held. Keep descriptor storage pinned only
   for the scoped operation; retain no native widget ownership beyond the
   existing session. Restore temporary fields before leaving the interceptor.
6. After original Open returns, perform final identity/selection/outcome checks,
   publish Ready, then drain the existing deferred direction. The post-Open
   phase **must not call the restore helper**. A skipped native Open or failed
   projection is not repaired with a synthetic extra focus registration.

The existing `RawRvaHook` entry/trampoline mechanism suffices; install/disable/
rollback/drain remain owned by the existing menu-session hook transaction.
Catalog changes are the existing helper's hook ownership/install policy plus
the two signature-only caller windows above; generated data is regenerated,
not hand-edited. Unknown/signature-mismatched builds do not acquire custom
projection authority. No selection-admission guard weakening, global focus
stack edits, forced reservation replay, or audio-lifecycle changes are justified.

### Required production-used regression and nonclaims

Exercise the actual Open/interceptor orchestration, not only the field-journal
template: first no-bookmark Open; accepted playback; bookmarked same-index and
different-index reopen; one native history registration; selection of a DIFFERENT
custom song; Close; preserved reservation; subsequent custom persistent claim.
Also cover unrelated caller/no-TLS, nested frames, native-Open skipped body,
pre-mutation fallback, post-mutation failure without a second native call,
catalog/profile reorder/removal, and immediate input before Ready. Assert total
helper/trampoline calls across original Open plus interceptor, not a mock's
local callback count in isolation.

**Disposition: single-call boundary is implementation-ready on both exact
builds at static ABI/caller/order level.** The failed candidate's observed last
revoke branch remains untraced, and this correction has not been built or
gameplay-qualified. A future authorized run must demonstrate the second custom
admission/audio publication after reopening; loader Passed is insufficient.
