# Song HUD static prerequisites

Integration research, 2026-10-23. Read-only default Ghidra import
`ff7rebirth_.exe-9457d4`, project `/ff7rebirth_.exe-1.005`. These are observation
anchors, not admitted callables. Executable/build identity and production addresses
belong in `src/game/rva_catalog.json`. No new disk identity authentication or older
build recovery was performed. Existing migration records alone are not independent
recovery. No live operation was performed for this research.

## Canvas texture contract

- Existing shared PostRender owns all overlay drawing; extend it, never double-hook.
- `startup_cache_overlay.h` DrawTile argument 11 is the native rendering `FTexture`
  resource, not a Texture2D UObject or Slate brush. `0x145015cdc` forwards it to tile
  construction, substituting native white when null. Native Canvas at `0x14501c3ec`
  loads Texture2D `+0xa8` and passes it directly to the same constructor,
  `0x140b04c7c`; `0x142b7e41c` independently supplies that resource without adjustment.
- Use whole-image UV 0,0 to 1,1: inspected DrawTile hardcodes starting UV zero.
- `menu_theme_native` already owns fresh Prepared records, their rooted carriers,
  Texture2D, and reflected brush resource reference. Captured originals are NOT
  eligible for this new route. Creation remains inside its real qualified menu
  callback; PostRender must not fabricate menu CallbackAuthority.

For a **fresh immutable retained one-mip texture**, no new render fence is needed:

| Static anchor | Observation |
| --- | --- |
| `0x140e37a60` | Creates/publishes resource at Texture2D `+0xa8`, then dispatches initialization. |
| `0x140e3843c` → `0x140e37820` | Resource CPU fields later inspected by Canvas, including `+0x45`, are initialized before publication. |
| `0x1408f26f0` | Initializes directly when appropriate or queues resource virtual `+0x28` initialization. |
| `0x141665d84` → `0x14500ef64` → `0x142da489c` | Canvas flush queues captured commands to the same named rendering-thread target. Ordinary same-game-thread submission orders initialization before later Canvas consumption. |
| `0x1412337a0` | Canvas batch retains a RAW resource pointer, not an owning reference. |
| `0x140e381e8` | Resource release/replacement is a separate operation. |
| `0x1412d2ac4`, `0x1412d2ca0` | Existing fresh one-mip layout fails the examined streaming branch's required nonstreaming-mip comparison. |

Consequently retain the carrier, Texture2D and immutable resource for process
lifetime, call UpdateResource exactly once, and prohibit subsequent update,
release/destruction or streaming. A narrow bridge operation must freshly validate
its own Prepared graph, project the resource, and submit synchronously; no resource
pointer escapes or survives the operation. Missing resource/failed checks omit
artwork: no sleeps, extra UpdateResource, or native cleanup. Nonnull resource is not
a GPU-completion claim. The queue-order argument requires the established same
game thread and real PostRender scope, not a copied boolean/worker thread ID alone.

## Native gameplay title and geometry

- The container holding catalog `title_view_converter_activation_return` calls
  `0x1439b8cf8`, which resolves its component's weak widget at `+8`, locates typed
  `Pnl_MusicTitle` (EndCanvasPanel) and `Txt_MusicTitle` (EndTextBlock), assigns text,
  and sets panel visibility to zero. Name initializers: `0x1405027a0`, `0x140502770`;
  type-check/getters: `0x14155ed9c`, `0x14155ede0`, `0x1413bf058`, `0x1413bf09c`.
- Component `0x1439a3bbc` initialization stores the incoming widget at `+8` through
  native weak assignment (RDX remains incoming widget). Component is controller
  `+0x6f0`, so its root handle is controller `+0x6f8`. Generic show/hide
  `0x1414eb580` / `0x1414eb50c` resolve that handle and set visibility 0 / 2.
- This is not the Ready-menu handle. Concrete dynamic root UClass/Blueprint name
  remains unknown; do not invent it. The observed consumer requires a
  WidgetTree-owning user widget; fresh typed ancestry/forward containment are the
  usable contract, not an assumed concrete name.
- Capture positive weak root identity and playback-generation association inside
  an existing lifetime-qualified controller/chart callback. Never read a saved
  controller pointer in PostRender. Reuse current UObject identity primitives,
  fresh read-only WidgetTree/Slots/Content traversal and preview's bounded Slate
  paint borrowing/consistency rereads/root-to-output transform, not its Ready-menu
  owner or row-specific authority. Recheck identity/generation after capture;
  retain copied geometry only. Hide when evidence is unavailable.
- State-9 exit `0x1439a6344` calls `0x1439a1b90`, reversing the title animation.
  Visibility and cached geometry alone do not prove current paint opacity. Exact
  pause/fade presentation remains a focused in-game observation, not a memory
  safety or texture fence blocker. Play → pause → resume → results should compare
  current root/title/panel, ancestor visibility/opacity and actual appearance.

## Product boundaries

Offline source strings and CPU thumbnail live in SongDescriptor; no file parsing,
decode or cache construction belongs in game callbacks. Use a retained current
PlaybackSnapshot, not selection/cleanup leases, for gameplay content. The pure
`song_hud_overlay` renderer already accepts explicit bounds and measured font
metrics; native small-font DrawShadowedString does not support arbitrary scaling.
Font measurement admission is still an adapter responsibility. A screenshot's
guessed margins are not recovered native geometry. Runtime safeguards and scoped
manual acceptance remain in the product's canonical docs.
