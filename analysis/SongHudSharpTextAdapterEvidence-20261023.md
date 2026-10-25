# Local Runtime FontInfo adapter — 2026-10-23

Integration static/read-only follow-up to `SongHudSharpTextEvidence-20261023.md`
and owner-authorized `SongHudFontObservation-20261023.md`. Observation establishes
availability only: runtime still borrows the qualified current SmallFont field
with positive or item-backed-zero-serial identity; no observed pointer is embedded.
Medium/Large selection is retired because all three observed fields alias one font.
The catalog's "song_hud_engine_font_fields" is retained as research-only historical
typed-loader proof for both builds, not a current runtime consumer or invocation.

## Narrow remaining layout proof

Explicit static default import `ff7rebirth_.exe-9457d4`, path
`/ff7rebirth_.exe-1.005`, base `0x140000000`. Native item measure
`0x145019964`, Runtime legacy fallback at `0x1450199f9..0x145019a38`, loads
R8D from UFont+`0x13c`, loads R9 with **address UFont+`0x140`**, and calls the
same FontInfo constructor `0x140b08498`. That constructor copies the full 64-bit
value pointed to by R9 into FontInfo+`0x40`. This independently admits a copied
genuine typeface FName64 read at UFont+`0x140`; no invented None/face name, guessed
title-widget cast, or legacy-size write is used.

Ordinary owning item constructor `0x140aee2cc` writes UFont at item+`0x148` and
clears optional initialized byte+`0x1a8` at `0x140aee32b..0x140aee344`.
Existing admitted item destructor `0x140aee4e8` calls `0x1427b4d18(item+0x150)`
before releasing FText. This optional destructor tests/clears payload+`0x58` and
calls `0x140793670(payload+0x30)` to release its shared CompositeFont owner pair.
No standalone optional destructor is called by the adapter.

Additional instruction-complete NONCALLABLE windows, all exact/unique in the
named import (whole-program byte search), raw executable and PE-mapped image:

| Catalog ID | RVA | Bytes |
| --- | --- | --- |
| `song_hud_font_typeface_source` | `050199f9` | `44 8b 82 3c 01 00 00 48 8d 44 24 30 4c 8d 8a 40 01 00 00 48 89 44 24 20 48 8d 4c 24 58 c7 44 24 30 00 00 00 00 c5 f8 11 44 24 40 66 c7 44 24 34 00 00 48 c7 44 24 38 00 00 00 00 e8 5f ea ae fb` |
| `song_hud_font_optional_init` | `00aee32b` | `48 89 b3 48 01 00 00 48 8b 74 24 38 c6 83 a8 01 00 00 00 c7 43 14 02 00 00 00` |
| `song_hud_font_optional_destroy` | `027b4d18` | `b8 28 00 00 00 e8 5e 6e 9b ff 48 2b e0 80 79 58 00 74 0d c6 41 58 00 48 83 c1 30 e8 38 e9 fd fd 48 83 c4 28 c3` |

Disk is the installed default `End/Binaries/Win64/ff7rebirth_.exe`, SHA256
`752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc`,
124317952 bytes; PE identity remains timestamp `6a16ced2`, image `099d9000`,
checksum `0769ea6e`. First six new gates reuse exact authenticated windows from
the preceding sharp-text evidence, not inferred address shifts.

## Size/DPI/ownership policy

- Runtime emission obtains Canvas DPI at `0x14120a73c`, saves XMM0 in XMM6 at
  `0x14120a768`, then passes XMM6 as XMM2 at `0x14120a86e` to its native character
  list lookup `0x140b085ec` (call at `0x14120a883`). Item measurement receives
  the identical DPI as its third float argument, not a guessed Windows DPI.
- Native currentness includes the root/playback/engine/GC/font field and identity;
  sharp work additionally rereads Runtime cache type and genuine typeface around
  native operations. DPI is checked against this frame's measurement before each
  owning text transaction. No native getter is called from a worker.
- Every measured/drawn string owns an FText/item transaction. Construct local
  FontInfo directly at item+`0x150` with integer size and native default Outline20,
  mark local+`0x1a8` only after successful construction, then measure OR submit,
  and let native item destruction release both FontInfo and retained FText.
- Fit a bounded integer size (1..128) to copied native pixel-height measurements
  using a probe containing sanitized complete metadata plus `Mg`. An initial
  upper request bound of ceil(target/DPI) limits cache requests; correctness uses
  actual measured fit, not a presumed points-to-pixels multiplier. Sizes may fit
  conservatively rather than maximizing beyond that bound. Pure layout receives
  actual matching width/height callbacks and existing `max_scale=1`. Because the
  measured height fits the target, pure scaling resolves exactly to one; the native
  adapter also rejects any other scale. No fractional post-raster enlargement or
  new public metrics interface is needed.
- Native faults or authority loss while owners remain poison and suppress cleanup;
  an invalid copied measurement is rejected only after healthy owner destruction.
  The read-only DPI getter runs before owners exist; its failure does not poison.
- Unsupported/incomplete capability or Offline font keeps the existing legacy
  adapter with explicit `hud_submitted_*_blur_unresolved` outcome. Unknown cache
  values and unsafe/stale facts omit. New capability is absent on older builds;
  no address/layout equivalence is assumed. Existing older legacy contracts remain.

No build, game observation/attachment, invocation, install, process control or
commit was performed by this worker. Actual sharpness, Unicode fallback,
pixel placement, opacity, native DPI/UI scale and immediate/delayed pause/resume
still need the lead's independent review and focused in-game check.
