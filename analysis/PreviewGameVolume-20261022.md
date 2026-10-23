# Preview following game volume: bounded static evidence

## Scope and disposition

Debug investigation, 2026-10-22. Read-only Ghidra import inspection and repository
reads only; no game launch, attachment, native invocation, installation, or memory
mutation. This file is evidence, not production authorization or mutable status.
No production source or catalog entries were changed by this investigation.

The game applies music and master settings to the SQEXSEAD `bgm` category's
Config layer. Both builds have a status-returning layer reader. However, **the
custom piano bank's ancestry to that category and a lifetime-safe sampling
boundary remain unqualified**. Do not put a native accessor in the preview input,
PCM loader, or render thread on the strength of this evidence.

## Executable identity and recovery

Canonical identity/address authority: [rva_catalog.json](../src/game/rva_catalog.json).
Ghidra project `D:/GhidraProjects/FF7R/ff7r`, explicit imported program paths
`/ff7rebirth_.exe-1.005` and `/ff7rebirth_.exe-1.004`, image base `0x140000000`.
The default import has a stale internal name; that name was not used as identity.

| Profile | PE timestamp | SizeOfImage | Checksum | Canonical SHA256 |
| --- | --- | --- | --- | --- |
| `ff7rebirth-steam-win64-6a16ced2` (1.005) | `6a16ced2` | `099d9000` | `0769ea6e` | `752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc` |
| `ff7rebirth-steam-win64-68fd6fde` (1.004) | `68fd6fde` | `09bad000` | `07877c99` | `76bd4d539878a8759433f1cd7c7b47d5e3abda0ef41572a664d2d606a9f09dc8` |

Imported PE headers independently matched timestamp/image/checksum: e_lfanew
`0x210` / `0x220`. Disk hashes were not independently remeasured in this task.
Recovery used `CategoryCtrl_GetVolume` string -> registration pair -> VM thunk
-> implementation separately in each import, not address deltas. All addresses
below are RVAs unless explicitly marked VA. No live-build qualification is claimed.

## Settings application, not an arbitrary attenuation

| Recovered role | 1.005 | 1.004 |
| --- | --- | --- |
| Options application | `010b9c38` | `01808c00` |
| Master-factor helper taking options pointer | `0360fdb8` | `038e0548` |
| Current-master wrapper | `0360fe0c` | `038e059c` |
| Category volume setter | `00a133bc` | `00b99764` |

In both options-application functions, option `0x18` stores the BGM byte at
options+`0x2e`, constructs FName `bgm`, and submits:

```text
masterFactor = (signed(options[0x1e5]) + 10) * 0.1
bgmConfigTarget = masterFactor * signed(options[0x2e]) * 0.1
categorySetter(bgm, bgmConfigTarget, 0.25f, 2)
```

Master option `0x1f` stores the UI value minus 10 and recomputes BGM, SE, voice,
song and speaker targets. These option IDs are separate from native category IDs.
Default enum table VA `1466ee7d0` pairs `EOptionCategory::BGMVolume` with `0x18`;
VA `1466ee840` pairs MasterVolume with `0x1f`. Default layer enum table at VA
`1464731d0` pairs `ESQEXSEADCategoryVolumeLayers::Config` with `2`.

The master helper has a conditional zero gate: options+`0x36d` nonzero and a
virtual predicate on the application object returning false. The older options
application's option `0x41` writes `1 - UIValue` there and reapplies the categories;
default strings include `EOptionCategory::BackgroundSound`. Exact virtual predicate
identity is not qualified here. Existing preview focus-loss cancellation must remain.

For valid UI settings, zero BGM or master yields zero target; full-scale values
yield unity. This is a linear settings conversion, not evidence that the entire
native audio pipeline has no other gain/curve. Saved/default values were not
recovered. The current-master wrappers have an internal unity fallback, but also
unguarded singleton dereferences: they are **not** a safe unavailable-state policy.
Do not parse saves or copy these private options offsets into production.

## Reader ABI, lookup and signatures

| Recovered role | 1.005 | 1.004 |
| --- | --- | --- |
| Getter registration string VA | `145f6c120` | `146104870` |
| Registration pair VA | `146473a00` | `1465e8870` |
| Getter VM thunk | `02a82c6c` | `02de1c30` |
| Named getter | `018c66dc` | `0184e72c` |
| Status-returning layer reader | `018c678c` | `0184e7dc` |
| FName -> category-map index lookup | `0078cd70` | `008428f0` |
| Category map global | `06eef980` | `070bc7a0` |
| Numeric-ID -> category-object lookup | `00a136c4` | `00b8ea48` |

Recovered native contracts (not the VM thunks):

```cpp
float named_getter(uint64_t packed_fname, int32_t layer);
// RCX packed FName, EDX layer, result XMM0.
int32_t layer_reader(const int32_t* category_id, float* out, int32_t layer);
// RCX id pointer, RDX output pointer, R8D layer; EAX 0 on success, -1 on failure.
int32_t* map_find(void* category_map, int32_t* index_out, uint64_t packed_fname);
// RCX map, RDX index output, R8 packed FName, RAX index output pointer.
```

The named getter returns zero on missing context/name/category or reader failure,
so it conflates unavailable with mute. Its preliminary context helper
`0195e5b0` in 1.005 calls `019c5d9c`; that helper includes critical-section and
reference-count bookkeeping. It must not be described as a pure scalar read.

The lower reader does not invoke that context helper or enqueue audio commands.
It resolves the current category by numeric ID, validates the layer index, copies
the float and returns status. Failure need not write output: initialize output and
publish it only on success. Instruction proof at default VA `1418c67dd..1418c67ea`
calls the interpolator, stores XMM0 through RDI, and returns zero in EAX. Both named
getters' success and failure returns were independently checked to use XMM0.

The map lookup is read-only in the inspected body: hash-chain traversal, packed
FName equality, index or -1. The named getter then reads category ID from
`*(map.data + index * 0x14 + 8)`. Neither the map lookup nor lower reader establishes
map/category lifetime ownership. Do not cache their borrowed pointers or a numeric
ID across an unqualified reinitialization. No map lookup lock was established.

Entry prefixes below each matched one site image-wide in their own import:

```text
named_getter 1.005:
48 89 5c 24 08 57 b8 30 00 00 00 e8 94 54 8a 00 48 2b e0 c5 f8 29 74 24
named_getter 1.004:
48 89 5c 24 08 57 b8 40 00 00 00 e8 f4 07 86 00 48 2b e0 c5 f8 29 74 24
layer_reader 1.005:
48 89 5c 24 08 57 b8 20 00 00 00 e8 e4 53 8a 00 48 2b e0 8b 09 45 8b d8 48 8b fa e8 18 cf 14 ff
layer_reader 1.004:
48 89 5c 24 08 57 b8 20 00 00 00 e8 44 07 86 00 48 2b e0 8b 09 45 8b d8 48 8b fa e8 4c 02 34 ff
map_find, separately unique in both imports:
48 89 5c 24 08 48 89 7c 24 10 8b 41 08 48 8b fa 48 8b d9 3b 41 34 74 64 4c 8b 59 40 45 8b d0 41
```

The layer reader evaluates current interpolation, not just the final target.
Layer -1 multiplies local-category layers and base gain; it does not itself prove
parent-category, suspension/debug-mute, asset-volume or final-voice gain coverage.
Config already contains master: multiplying master again would double-attenuate.

## Existing FName helper

Use existing catalog ID `fname_ctor`, not hardcoded runtime name-pool indices:
`00b517c4` / `00882298`. Both imported bodies independently have the existing ABI
`void* (uint64_t* out, const wchar_t* text, int32_t find_type)` and forward find_type
to their internal name routines. Existing project find-only convention is 0:
[chart_patch.cpp](../src/game/chart_patch.cpp) `construct_fname_find`,
[extended_chart.cpp](../src/game/extended_chart.cpp) `resolve_name`, and
[native_audio_runtime.cpp](../src/game/native_audio_runtime.cpp) `construct_names`.
Proposed identity is `fname_ctor(&packed, L"bgm", 0)`, reject missing/None or unexpected
number, and retain only the packed value. No add-name call is needed.
The catalog deliberately validates this callable by build identity, not a stored
signature. Availability and calling-thread authority still need admission.

## Actual custom-bank routing: narrower than a function named BGM

The production builder copies the fixed `kHeader` and only changes container/slot/
payload sizes before appending audio: [mabf_builder.cpp](../src/pipeline/mabf_builder.cpp),
`build_mabf_from_mode_hca`; source bytes in
[mabf_scaffold.h](../src/pipeline/mabf_scaffold.h). Header directory `musc` at `0x20`
points to chunk `0x50`; its first record relative offset at `0x60` is `0x20`, so
the Music record starts at `0x70`. Its byte +6 (`0x76`) is **0x1f (31)**.

Default static route, independently inspected from catalog anchor:

```text
sqexsead_play_setup 01249a44
 -> bgm_slot_set 01249ce0 -> 01249d10 -> 01249e58
 -> 01249fb4 (assign selected Music asset to BGM subobject)
 -> bgm_prepare 0124848c
 -> 01249350 for native Music bank kind 2
 -> 0124941c -> 00a023ac (MabFile::Music interface)
```

The Music interface's category getter is default `00a0e52c`: load record pointer
from interface-8; return record byte +6, or zero for missing record. Its exact
14-byte body `48 8b 41 f8 48 85 c0 74 04 8a 40 06 c3 c3` independently locates
older `00b972ac` uniquely; decompilation agrees, with data reference from older
VA `1457fad40`. This establishes a concrete serialized category field, **not** a
proof that category 31 is the final voice category or a child of `bgm`.

Category registration proves the missing link is data-dependent: default
`05531f24` constructs categories from ConfigFile records and links parents;
`05536de0` reads category ID at record+1, name through `0552b6d0`, and parent
through `05531f04`, storing parent at category+`0x90`. Native admission
`00a07a90 -> 01244d6c` resolves a voice category and recursively visits +`0x90`,
applying category gain to matching tokens. The actual category-config record for
31 and its ancestors was not recovered. **Neither the BGM wrapper name nor 31's
coincidental equality to the MasterVolume option ID proves this ancestry.**

## Thread, ownership and smallest proposed integration

[title.cpp](../src/game/title.cpp) `difficulty_input_thread_main` creates a dedicated
Windows thread; it calls `poll_dynamic_difficulty_input_impl`, which calls
`poll_song_preview`. The PCM loader in [song_preview.cpp](../src/game/song_preview.cpp)
is another detached worker. Neither is a demonstrated native getter boundary.

Existing owned `title::detail_update_detour` calls original PianoDetailUpdate and
then has a callback-gated post-original point. This is a **candidate** sampling
point, not a proved game-thread or periodic tick contract: static xrefs for native
`0399a334` / `03c41618` were vtable/unwind data, not a closed caller chain.
Likewise, title population is not proven to run every preview-start opportunity.
Do not add a second hook at either address.

Existing catalog IDs `native_game_thread_initialized`, `native_game_thread_id`
and their publication/role evidence in `MenuThemeObservation-20261020.json`
provide a way to verify actual thread identity. Existing
[menu_theme_observation.cpp](../src/game/menu_theme_observation.cpp) copies those
facts, native TLS role, GC and exit-purge facts. Those unrelated theme scopes do
not automatically grant audio-map lifetime ownership. Also, default category
teardown `055321a8`, called by `0552bd10`, destroys/frees the category array and
zeros its globals; the lower reader has no corresponding lifetime fence.

After admission is demonstrated, the smallest design is:

1. At an existing callback on the verified native game thread and live menu
   generation, find-only resolve `bgm`, resolve its category ID, call the status
   reader for Config=2, validate success/finite [0,1], and copy `{valid,gain,
   menu_generation}`. No native pointer or borrowed map record crosses threads.
2. The existing poller consumes only the copied fact and passes gain to the
   generic core PCM API. Master is not multiplied again. A valid zero is mute;
   unavailable is a separate state, not unity or a fabricated coefficient.
3. Invalidate on generation/ownership loss. Preserve existing cancellation,
   fade and focus-loss behavior. Prove refresh coverage before retaining a value
   across settings changes; do not invent expiry sleeps as ownership proof.

This is a conditional sketch, not an instruction to implement unsafe calls now.

## Minimal next observation and stop conditions

Stop static integration here: unclosed category ancestry and audio lifetime are
concrete gaps. A future **explicitly authorized** observation-only diagnostic can
extend the existing detail callback (no duplicate hook), recording bounded facts:
current/native thread IDs, menu generation, audio initialized/shutdown state,
map/category identity, actual current piano token category and its named parents,
Config layer fields, and sample opportunities on menu entry/row change. The first
pass should use bounded safe-memory copies, not additional native accessor calls;
derive a candidate gain offline. Status-reader invocation belongs to a later
admitted step after thread and lifetime evidence, not to this first observation.
Do not start by calling the getter from the input worker or debugger.

Observe normal start and preview menus with BGM 10/5/0 and master 10/5/0; confirm
whether the expected targets are 1/.5/0 and .25 for 5+5, and preserve zero versus
unavailable in the copied-state projection. Compare actual piano ancestry with the
`bgm` map entry. Stop for
thread mismatch, teardown/reinitialization, category drift, missing ancestry,
unreadable ownership, or callback silence that prevents refresh coverage.
Do not mutate gain, setters, category data, or playback in that observation.

Recovery: retain source and existing ASI, normal game exit before any authorized
replacement; no same-process unload/reload. No installation is authorized by
this record. Exact per-build executable and signatures must pass before any
future observation or implementation.

Affected catalog anchors: `sqexsead_play_setup`, `bgm_slot_set`, `bgm_slot_play`,
`bgm_prepare`, `piano_audio_state_tick`, `fname_ctor`, `piano_detail_update`,
`piano_title_population`, and native thread observation IDs. Existing default
audio/UI migration records remain migration records; new reader candidates
above are independently recovered static evidence, not newly approved entries.

## Bounded read-only Frida snapshot source

Follow-up source: [PreviewGameVolumeSnapshot.js](PreviewGameVolumeSnapshot.js).
The lead reported explicit authorization for bounded Frida observation and an
existing attachment with default-profile module headers and disk SHA256 verified.
This worker did not attach, load the script, or perform live operations. The source
is not evidence that a live snapshot succeeded. The earlier static-only disposition
above remains distinct from that subsequent authorization.

The diagnostic is **1.005 only** and makes no native calls, writes, hooks, timers,
name-pool modifications, or playback changes. It rechecks PE identity on each sample;
lead-owned disk-hash authentication remains a prerequisite. No entry signature is
required because no code entry is invoked or hooked. Do not reuse its new data
offsets on 1.004 without independently recovering those globals/layout consumers.

Default globals independently established by category init/destruction and lookup:

| RVA | Read |
| --- | --- |
| `091d8320` | signed int32 category-pointer-array count |
| `091d8328` | pointer to array of 8-byte category pointers; holes may be null |
| `091d8378` | int32 shutdown state; init stores 0, shutdown stores 1 |
| `091d8388` | byte initialized latch; init stores 1, shutdown clears after destruction |

Default category layout, from `05536de0`, `00a136c4`, `01244d6c`, and readers:

| Offset | Read |
| --- | --- |
| `38` | volume-envelope-array pointer |
| `48` | float local base gain, separate from Config layer |
| `60`, `64` | int32 category ID, configured parent ID |
| `6c` | int32 suspend count (not folded into Config gain) |
| `78` | borrowed narrow config-name pointer, not an FName |
| `80` | int32 number of allocated volume-envelope records |
| `90` | parent-category pointer |

`05536de0` explicitly stores `"Master"` for ID0 and otherwise the result of
`0552b6d0`. That latter helper returns a pointer into a config record: version>1
uses the uint16 offset at record+2, version1 uses record+0x10, version0 returns null.
Thus bounded ASCII names can identify `bgm` directly here, without constructing
an FName or decoding the UObject name pool. Non-ASCII, unreadable or unterminated
names remain unresolved. The diagnostic prints pointers and IDs as well as names.

Volume-envelope stride is `0x30`. The native layer reader uses
`array + (layer + 1) * 0x30`; Config=2 therefore reads **record3 at array+0x90**,
not record2. Diagnostic admission requires at least four records, even though the
native reader's weaker index check was observed. Envelope fields are float start
`+8`, target `+c`, elapsed `+10`, duration `+14`, and int32 curve `+18`.

Default interpolator `0079f870..0079f97f` instructions establish:

```text
duration == 0 -> target
elapsed < 0   -> start
otherwise t = elapsed / duration, current = start + (target-start) * weight
curve 0: t
curve 1: 1 - (1-t*t)^3
curve 2: 1 - (1-t)^2
curve 3: t*t
curve 4: 2^t - 1
curve 5: 2 - 2^(1-t)
```

There is no ratio clamp inside this reader. Unknown curves leave the native
weight zero; the diagnostic instead returns null for unknown-curve approximation
while retaining raw fields. JS `Math.fround` follows the observed scalar arithmetic
steps, but `Math.pow` is not a claim of bit-identical native `powf`. These are
`currentApprox` values at the copied elapsed time, with no wall-clock extrapolation.
They are not final-voice/parent/mute gain, nor permission to multiply master again.

Loading through the lead's existing authorized session sends **one** immediate
result. `rpc.exports.snapshot()` permits at most seven further explicit samples;
there is no background polling. Results include `records`, `category31Chain`,
`bgm` addresses, `terminal`, and tri-state `reachesBgm`. Each record includes native
ID/name/parent identity, base gain, suspend count, layer count, and Config raw fields
plus `currentApprox`. Missing/duplicate category31 or unresolved bgm yields no
affirmative route conclusion. Loops, missing parent or ID mismatch and depth caps
are explicit terminal outcomes.

Reads use bounded `NativePointer.readVolatile` and
try/catch. Frida's [official API](https://frida.re/docs/javascript-api/) recommends
`readVolatile` for memory that other threads may make inaccessible; no unsafe
fallback is used if unavailable. Caps: 256 category slots, 32 envelope records,
96 name bytes, 16 parent levels, eight samples, and 4096 bytes per read. The script
rejects duplicate pointers and rechecks slot arrays, category identity fields and
engine state around the copy. Unreadable/drifting data returns `unavailable`;
uninitialized/shutting-down/null state returns `not_ready`; a zero Config value
remains a valid zero. Successful output is deliberately `observed_nonatomic`:
readable memory and repeated identity do not prevent all races or ABA replacement,
and envelope/name fields may change during a snapshot. This is diagnostic evidence,
not a synchronization or native-lifetime guarantee.

Offline checks run without Frida: `node --check` passed; an assert-based Node VM
memory fixture exercised platform/PE admission, category31->bgm ancestry, valid
zero versus unready, parent loops, oversized category count, duplicate pointer,
unreadable envelope, wrong timestamp and snapshot cap. Pure envelope tests covered
duration zero, negative elapsed, each known curve, unclamped ratio, unknown curve
and NaN. No live observations or gain-following claims follow from these checks.

## Authorized live observations (2026-10-22)

The lead authenticated the running default-profile PE and installed executable
SHA256 against `src/game/rva_catalog.json`; the user authorized Frida observation
only, without DLL replacement, native accessor calls or gain/playback mutation.
The first full probe timed out; the user confirmed normal game operation and then
restarted the game and reconnected Frida. Do not reuse that first session.

In the new process, isolated identity, module-global state and two-byte
`readVolatile` probes succeeded. Removed redundant full-memory-map range queries
from the bounded snapshot reader; accessibility and exact read length are checked
by `readVolatile` itself. One-shot snapshots now complete and unload, with no
hooks/timers. This bounds an observation, not a proof of coherent native state.

Observed 64 initialized category slots, shutdown zero. Category **ID31 is named
`bgm` itself**, parent ID1 named `music`, with a null parent above `music`;
both local base gains were 1 and suspension counts 0. This establishes the
scaffold category's BGM route in this running profile.

| User-confirmed setting/state | `bgm` Config start/target/currentApprox | Parent `music` Config |
| --- | --- | --- |
| Initial post-restart observation (slider values not asserted) | 1 / 1 / 1 | 1 |
| Master10 / Music5, returned to piano song list | 0.5 / 0.5 / 0.5 | 1 |
| Master5 / Music5, returned to piano song list | 0.25 / 0.25 / 0.25 | 1 |
| Master5 / Music0, returned to piano song list | 0 / 0 / 0 | 1 |

These read-only snapshots establish the observed setting response, not preview
parity: the installed preview still plays unity PCM. Safe production copying and
independent review remain outstanding. All successful one-shot scripts reported
unloaded; no hooks, timers or native getter calls were installed. The user was
asked to restore their preferred settings after the final zero observation.
Do not promote
the native named getter to an arbitrary input-thread call.

## Fixed artifact gain: actual playback consumers (both profiles)

Separate bounded static follow-up, 2026-10-22: no additional live operations,
native calls, workers, or production/catalog edits by this researcher. The explicit
Ghidra imports and PE correlation above apply. Default audio catalog migration
records were starting anchors, not independent proof of the new consumers. Older
counterparts below were recovered through their actual BGM/Music call paths and
vtables, not by translating addresses. These are playback evidence, **not new
callable APIs** or native lifetime authority.

### Source authority and all custom modes

The fixed property comes from `kQualifiedPrefix` in
[native_music_export.cpp](../src/pipeline/native_music_export.cpp): bytes at prefix
`0x64` are `cd cc 4c 3f`, float32 **0.800000011920929**. `validate_inputs` pins the
prefix; `build_native_music_export` preserves these bytes while changing durations
and wrapper extents. Older projection copies the same qualified Volume to output
`0x6b`; the supported older donor representation places it at `0x63`. The older
input validator's fixed-byte comparison does not exempt this Volume field.

Reflected identity is SoundWave `Volume`, not a guessed float: default property
record VA `1463b3200` names `Volume` and stores native offset `0x274` at record+`0x24`.
The retained [older asset profile](NativeAudioAssetProfile-OlderBuild-20261017.json)
independently records the older Volume property at VA `146527ac8`, native offset
628=`0x274`, with the separate Pitch and Duration entries disambiguated.

The second fixed gain is in checked-in
[mabf_scaffold.h](../src/pipeline/mabf_scaffold.h), `kHeader`: Music record starts at
bank offset `0x70`; its float `+0x28` is bank offset **`0x98`**, bytes
`c3 f5 08 3f`, float32 **0.5350000262260437**. This is a real native gain consumer
below, not an inference from the field's numeric appearance.

Coverage is the current product's qualified generated artifacts, not arbitrary
external Music/MABF files:

- [song_repository.cpp](../src/pipeline/song_repository.cpp) constructs all three
  `resolved_modes` (clean base and authored/fallback mode1/2), then `build_audio_cache`
  calls [audio_artifact_builder.cpp](../src/pipeline/audio_artifact_builder.cpp)
  `build_audio_mabf` for the song.
- That builder encodes each resolved mode to HCA, reusing equal payloads where
  applicable; an optional metronome guide replaces only mode0 PCM. Every successful
  path then calls `build_mabf_from_mode_hca`.
- [mabf_builder.cpp](../src/pipeline/mabf_builder.cpp) copies the same `kHeader` for
  all songs and all three HCA slots. Only header fields `0x0c`, `0x424`, `0x428`,
  `0x448` are varied. Its release/structural validator compares the entire header
  after admitting only those four fields; **gain `0x98` is pinned even on cache
  validation**, independently of mode payload equality or authored overrides.
- [native_audio_cohort.cpp](../src/pipeline/native_audio_cohort.cpp)
  `build_native_audio_cohort` passes `request.mabf` through the same selected-profile
  Music exporter. Both exporters copy the replacement bank without altering its
  Music gain. Thus mode1/2 and either supported executable profile retain this
  common asset-times-bank baseline.

HCA encoding consumes resolved PCM without baking this `.8 * .535` factor into
the samples. These are native container/playback multipliers; do not attenuate
the HCA payload a second time. This does not equate distinct authored mode content
or metronome guide loudness with the clean preview's content.

### Asset Volume -> Music volume envelope

All function addresses in this table are RVAs:

| Role | 1.005 | 1.004 |
| --- | --- | --- |
| Normal BGM start/state transition | `012495ec` | `01212cb4` |
| Multiply BGM gain by selected asset Volume | `0124976c` | `01212b38` |
| Enqueue token volume command | `00a12698` | `0082d238` |
| Native command consumer | `019d4440` | `007a95e4` |
| Music primary vfunc +0x38 volume setter | `01241ee4` | `0182bbb0` |
| Curved envelope setup helper | `01242200` | `01214a94` |

Normal BGM start passes slot+`0x4c` gain (assignment initializes it to unity), zero
fade, and the BGM subobject at slot+`0xc0` to the multiply helper. That helper's
ABI is **RCX=subobject, XMM1=gain, XMM2=fade**. It reads selected Music asset pointer
from subobject+`0x28`, refreshes float subobject+`0x68` from **asset+`0x274`**, copies
the player token at subobject+`0x48`, and submits `gain * assetVolume`, fade and -1.

Default instruction VA `1412497b0` is
`VMULSS XMM1,XMM7,[RCX+0x68]`; call VA `1412497c3` reaches `140a12698`.
Older independently has the same multiply at VA `141212b7c`, then call
`14082d238` at VA `141212b8f`, with the asset+`0x274` load at VA `141212b6b`.
Both wrapper bodies preserve gain and fade in XMM7/XMM6. BGM preparation also
copies the asset Volume when creating its player; the start-time refresh is the
decisive consumer, rather than merely that cached copy.

Token-volume enqueue ABI is **RCX=token pointer, XMM1=gain, XMM2=fade, R9D=curve**.
For Music token type8 it queues command `0xe`: token at command+8, target gain at
`+0x10`, fade at `+0x14`, curve at `+0x18`. Both recovered consumers resolve the
token and dispatch command0xe to native player **vfunc+0x38**.

That Music virtual setter configures **player+`0x1a8`**, not an unrelated player
field. Default instruction VA `141241f79` and older VA `14182bc45` each explicitly
form `RCX = player + 0x1a8`. Their setup helpers retain the curve and forward the
target/fade to dynamic-envelope setup (default `01243394`, older `01214af4`).
Default setup stores target at envelope+`0xc`, duration at `+0x14`, and current
start at `+8`. Older helper/caller structure matches the same envelope layout.

### Bank gain -> base-envelope mixer -> actual child voice

| Role | 1.005 | 1.004 |
| --- | --- | --- |
| Music constructor | `01242920` | `012161c4` |
| Music primary vtable (data RVA) | `05659400` | `057fade0` |
| MabFile::Music interface vtable (data RVA) | `055f6318` | `057fad28` |
| Interface +0x30 bank-gain getter | `012440dc` | `0121586c` |
| Primary +0xb8 base-gain getter | `01243fb4` | `01215744` |
| Curved envelope evaluator | `01244044` | `012157d4` |
| Child-volume propagation | `01243bb4` | `01215314` |
| Child-voice volume application | `019d71f4` | `0093ce30` |

For the older import, constructor VA `141216202` loads the actual primary vtable
`1457fade0`, and VA `141216209` stores it into `[this]`. Its primary +0xb8 pointer
at VA `1457fae98` is `141215744`; +0x38 at VA `1457fae18` is `14182bbb0`.
Constructor also installs the MabFile::Music interface at player+`0xa0`; its
vtable+0x30 pointer at VA `1457fad58` is `14121586c`. These pointer edges establish
the old roles independently of code-layout similarity.

Bank getter ABI: **RCX=Music interface, float result XMM0**. Both versions dereference
record pointer at interface-8, return float **record+`0x28` for record version>=3**,
otherwise unity. Explicit loads are default VA `1412440e9` and older VA
`141215879`: `MOVSS XMM0,[RAX+0x28]`. Current scaffold record version is `0x0f`,
so it takes the `.535` branch in both imports.

Base-gain getter ABI: **RCX=Music player, float result XMM0**. Both call that
interface+0x30 getter, multiply four curved envelope values starting at player+
`0x1a8`, stride `0x40`, and multiply three stored float factors at `+0x948/+0x94c/
+0x950`. The first envelope is the asset-derived volume command above. Older
instruction VA `14121577b` calls interface+0x30; VA `141215790` calls the curved
evaluator inside that four-iteration loop. This closes asset and bank factors
into the same base value, rather than finding two unrelated gain-like fields.

Both child propagators call primary **vfunc+0xb8**, combine section and per-track/
transition values, and pass the result to child-voice gain application with
**RCX=voice, XMM1=gain**. Older direct calls are VA `14121545c`, `14121553a` and
two retained tail blocks, all targeting `14093ce30`. Both child-voice functions
clamp to [0,1], reject invalid numeric/state cases, and apply through backend
vfunc+0x30 (with an alternate backend-state route). This is actual output-volume
propagation, not just queued command storage. No part of this chain was invoked
by the researcher.

### Scaffold parsing check and bounded conclusion

Read-only Python parsed `kHeader` hex and followed the same record-offset rules:
Music chunk `0x50`, Music record `0x70`, sole section record `0xf0`. Result:

```text
Music float +0x28: bank0x98, c3 f5 08 3f -> 0.5350000262260437
Section float +0x30: bank0x120 -> 1.0
Indexed track records: bank0x180, 0x1a0, 0x1c0; version3, float+0x10 -> 1.0 each
Qualified asset float: prefix0x64, cd cc 4c 3f -> 0.800000011920929
Known fixed product, before float32 result rounding: 0.42800002735853226
Float32 product: 0.4280000329017639
```

The read-only parser/assert check was rerun after the older counterpart recovery:
it followed the scaffold's relative section/track tables, asserted both pinned
gain byte sequences, section and all three indexed track unity fields, and the
float32 product. Evidence whitespace and local-link checks passed. No build,
gameplay or listening checks were performed in this fixed-factor follow-up.

The shared fixed artifact baseline is therefore **approximately `.428` on both
supported builds**, in addition to the user-settings Config gain. It is legitimate
artifact-derived attenuation, not an arbitrary preview coefficient. Config alone
or Config*.8 omits a demonstrated native factor. Do not apply master twice.

This is not a complete final-mix or audible-parity claim. Other dynamic envelopes,
crossfades, category layers, per-track/material behavior and backend effects were
not comprehensively qualified; stop this fixed-factor investigation here.

Smallest source API: reuse the lead-owned `native_music_fixed_gain()` now present
in `native_music_export.cpp/.h`, which decodes the two authoritative pinned byte
fields instead of repeating `.428`, `.8` or `.535` in game/core code. Keeping its
two format offsets named/shared within pipeline would be sufficient if desired;
no generic native mixer reconstruction, descriptor schema or extra configuration
is indicated for this fixed profile. Runtime consumes that offline scalar plus
its independently copied Config fact. The researcher did not edit that helper,
its tests, CMake, or any runtime file.
