# Gameplay HUD font observation — 2026-10-23

Debug, owner-authorized read-only Frida observation while a custom song played.
No Interceptor/Stalker, native calls, timers, input, memory writes, font/cache
mutation, installation or process control. Scripts performed bounded immediate
reads only. Copied facts are not native mutation/lifetime authority.

## Identity and method

Process `ff7rebirth_.exe`, PID 40140; Frida session
`session_40140_1792787754`. Mapped base `0x7ff6889d0000`, image size
`0x099d9000`, timestamp `0x6a16ced2`, checksum `0x0769ea6e` agree with the
canonical default build. Disk/import evidence and exact field/callable recovery
remain in `SongHudSharpTextEvidence-20261023.md` and
`SongHudStockFontEvidence-20261023.md`; this observation is not another disk hash.

The canonical locator admitted GUObjectArray at `0x7ff6910eeb28`, 397464 items.
The stock font-class slot held `0x7fed052c9900`. A bounded registry pass selected
objects whose exact class equals that UFont, checked object internal indices and
reread item object/serial before reporting. Five such objects were found, of
which three had Runtime cache type. No read exceptions were encountered.

A second bounded pass found exactly one object with all three admitted stock
font fields pointing to those registry-backed UFonts:

- engine candidate `0x7feee537b000`, index 160141, positive item serial 29734;
- class `0x7fed051b8280`; its raw class `+0x40` pointer equals the registered
  Engine class `0x7fed051b84c0` (a corroborating copied relation, not a new
  production inheritance-layout contract);
- Engine class slot was recovered from the actual stock Engine class thunk's
  RIP-relative load, rather than a guessed global Engine pointer.

## Decisive copied facts

| Admitted engine field | UFont pointer | FontCacheType | LegacyFontSize | Typeface FName64 |
| --- | --- | --- | --- | --- |
| Small `+0x50` | `0x7fece509c200` | Runtime (1) | 10 | 537222 |
| Medium `+0x70` | `0x7fece509c200` | Runtime (1) | 10 | 537222 |
| Large `+0x90` | `0x7fece509c200` | Runtime (1) | 10 | 537222 |

The common UFont has index 160194, item serial zero, public flags 64 and exact
class `0x7fed052c9900`. It therefore requires the existing item-backed-zero-serial
font snapshot/current-engine-field checks, not a fabricated positive weak handle.

Other exact UFont objects observed: index 160193 is Runtime, legacy size 8;
indices 21555 and 22380 are Offline; index 28836 is Runtime with legacy size 10.
None is promoted into an alternate runtime font provider by this enumeration.

## Consequence and limits

The installed renderer's logged SmallFont fallback is consistent with all three
loaded engine fields aliasing one font; field preference cannot increase source
size. A genuine loaded Runtime UFont is available, closing the source-cache-type
blocker in `SongHudSharpTextEvidence-20261023.md` for this observed run.

Implement the recovered local size-keyed FontInfo/item route with its genuine
typeface, native matching measurement and Canvas DPI, rather than magnifying
legacy-size geometry. Authenticate all new native/layout contracts first and
retain complete per-call authority/identity and conservative fault handling.
Actual raster sharpness, pixel alignment, Unicode and pause/transitions still
need focused appearance checks. No source change or visual success is established
by these reads, and no older-build observation is claimed.
