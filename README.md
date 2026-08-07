# FF7RPianoSongs
<!-- current-release-version: 0.1.0 -->

**Version 0.1.0** · Windows x64 · MIT

[![Windows CI](https://github.com/makcimbx/FF7RPianoSongs/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/makcimbx/FF7RPianoSongs/actions/workflows/windows-ci.yml)

FF7RPianoSongs adds user-authored piano songs to Final Fantasy VII Rebirth.

This is an unofficial community project and is not affiliated with or endorsed
by Square Enix.

## Requirements

- Final Fantasy VII Rebirth on Windows x64, using the supported game build.
- An x64 ASI loader. The recommended setup is [FFVIIHook - INI and dev console unlocker](https://www.nexusmods.com/finalfantasy7rebirth/mods/4), which provides the proxy that loads `.asi` files. Without an ASI loader, `FF7RPianoSongs.asi` will not load. If a compatible x64 ASI loader is already installed, keep it and do not add a second proxy DLL.

## Install

1. Exit the game.
2. If no compatible x64 ASI loader is installed, install FFVIIHook from the link above.
3. Copy this package's `End/` folder into the game directory and merge it with the existing `End/` folder.
4. Put each song in `End/Binaries/Win64/Music/<Song Name>/`.
5. Start the game. Valid songs appear in the piano-song list.

Expected layout:

```text
End/Binaries/Win64/
  xinput1_3.dll                    (FFVIIHook proxy; another loader may differ)
  FF7RPianoSongs.asi
  FF7RPianoSongs.ini
  FF7RPianoSongs.log              (generated)
  Music/<Song Name>/
    song.json
    song.wav | song.mp3 | song.flac
    song.mid | song.midi          (optional)
```

The package contains no music. See [Song Format](docs/SongFormat.md) to create or check a song.

## Use

- Custom songs appear alongside the default songs.
- For a song with several profiles, use D-pad left/right or the keyboard arrow keys in its detail view. Scores are stored separately for each actual profile.
- If a song is missing, it was rejected safely. Check `End/Binaries/Win64/FF7RPianoSongs.log` for the reason.

## Settings

Edit `End/Binaries/Win64/FF7RPianoSongs.ini` only while the game is stopped.

| Setting | Values | Purpose |
| --- | --- | --- |
| `General.Enabled` | `0` or nonzero | Disables or enables the mod. |
| `General.LogLevel` | `debug`, `info`, `error` | Sets log detail. Default: `info`. |
| `Advanced.RebuildAudioCache` | `0` or nonzero | Rebuilds every song cache on the next run. Set it back to `0` afterward. |
| `Experimental.ExtendedCharts` | `0` or nonzero | Diagnostic only; it does not raise the playable chart limit. Keep it at `0`. |

To rebuild one song, exit the game and delete only that song's `.cache/` directory. Never distribute or copy `.cache/` data between machines or mod versions.

## Update, Roll Back, or Remove

- **Update:** exit the game, replace the ASI, compare the new INI with yours, and keep your `Music/` folders. Delete caches only when the changelog asks for it.
- **Roll back:** with the game stopped, restore the previous ASI and matching INI, then delete generated song caches.
- **Remove:** with the game stopped, delete `FF7RPianoSongs.asi`. Optionally remove its INI, log, score INI, and only the custom song folders you no longer want. Do not remove FFVIIHook if another mod uses it.

## Troubleshooting

**The mod does not load**

- Confirm FFVIIHook or another compatible ASI loader is installed. The FFVIIHook layout normally includes `End/Binaries/Win64/xinput1_3.dll`.
- Confirm `FF7RPianoSongs.asi` is directly in `End/Binaries/Win64/` and `General.Enabled` is nonzero.
- Check `FF7RPianoSongs.log`. An unsupported game build fails closed instead of guessing addresses.

**A song is missing or changed after an edit**

- Check the fixed names and limits in [Song Format](docs/SongFormat.md).
- Use exactly one supported audio source and valid JSON.
- Delete that song's `.cache/` and retry.
- Temporarily set `General.LogLevel=debug`, reproduce once, then restore `info`.

Logs may contain song names and local paths. Remove private data and copyrighted inputs before sharing them.

## Compatibility and Limitations

- Only the cataloged Windows x64 game build is supported.
- Playable charts are limited to 512 rows.
- Replace, remove, or update the ASI only while the game is stopped. Live unload/reload is unsupported.
- Looped HCA and arbitrary CRI encoder profiles are unsupported.
- Audio conversion has bounded validation for the supported piano profile, not universal encoder parity.
- Unqualified retry, abort, active-process exit, and unusual teardown paths may require scenario-specific testing.

## Reference

- [Song Format](docs/SongFormat.md)
- [Changelog](CHANGELOG.md)
- [MIT License](LICENSE)
- [Third-Party Notices](THIRD_PARTY_NOTICES.md)
- [Contributing](https://github.com/makcimbx/FF7RPianoSongs/blob/main/CONTRIBUTING.md)
- [Security Policy](https://github.com/makcimbx/FF7RPianoSongs/security/policy)
