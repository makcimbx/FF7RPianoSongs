# Missing gameplay song card: authorized memory observation

Mode: Debug. The owner reported no song card, remained in an unpaused song and
explicitly authorized Frida observation. The observer performed bounded memory
reads only: no Interceptor/Stalker, native calls, writes, timers, gameplay input,
installation or process control. Snapshots are observations, not native lifetime
authority or proof that the product callback consumed the same values.

## Identity

- Native process PID 47540, Frida session `session_47540_1792775252`.
- Main module is the installed `End/Binaries/Win64/ff7rebirth_.exe`.
- Disk size/hash and mapped timestamp/image size/checksum match the default
  executable in `src/game/rva_catalog.json`; base was `0x7ff71a9a0000`.
- The installed product was not replaced. Its preceding frozen log is retained at
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/song-hud-missing-20261023-200314/observed-game.log`.
  That log admits HUD configuration and custom playback, but has no subsequent
  HUD capture/render outcome; the original runtime has silent early returns.

## Copied observations

The frozen log supplies chart/wrapper `0x7fecb4c2ec50`. Its documented controller
capture at `+0x118` was read, then authenticated against the catalog state table
and its count before further controller fields were examined.

| Fact | Observed value |
| --- | --- |
| Controller | `0x7fef54c07510` |
| Controller state-table count | 14 |
| State-table pointer | `0x7ff7205625f0`, exact catalog target |
| Current state / pending | 9 / null |
| Current chart | `0x7fecb4c2ec50` |
| Controller title-root weak index / serial | 276118 / 92894 |
| Root UObject | `0x7fedf55b1240` |
| Root internal index / item serial | 276118 / 92894 |
| Root public flags | 8 |
| Root WidgetTree (`+0x1d8`) | `0x7feb542ce640` |
| WidgetTree Outer | `0x7fedf55b1240`, matches receiver |
| Tree RootWidget (`+0x28`) | `0x7fecd4969410` |
| RootWidget Outer | `0x7feb542ce640`, matches tree |
| Native game-thread initialized / ID | 1 / 8376 |
| Native GC / exit-purge flags | 0 / 0 |

The canonical GUObjectArray locator found `0x7ff71b1d43e9`; its decoded,
header-admitted array was `0x7ff7230beb28`, with 394537 elements / seven chunks.
The weak index resolved to the same root and positive serial as above. The
controller weak fields were unchanged in the later engine/tree snapshot.

## Conclusion and limits

This run rules out an absent/nonpositive title-root weak handle **at these
observations**. It also finds the expected controller play state and basic
receiver/tree ownership, rather than a menu-root substitution. It does not
establish product playback-token equality, callback execution, native callback
thread/TLS admission, actual named-title containment, paint visibility/opacity,
font measurement or successful Canvas submission. Do not weaken those guards
based on this snapshot. The next discriminator is the product's silent
capture/render first-failure path, not guessed coordinates or delayed readiness.
