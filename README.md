# FF7RPianoSongs
<!-- current-release-version: 0.1.1 -->

**Version 0.1.1** · Windows x64 · MIT

[![Windows CI](https://github.com/makcimbx/FF7RPianoSongs/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/makcimbx/FF7RPianoSongs/actions/workflows/windows-ci.yml)

FF7RPianoSongs adds user-authored piano songs to Final Fantasy VII Rebirth.

This is an unofficial community project and is not affiliated with or endorsed
by Square Enix.

## Requirements

- Final Fantasy VII Rebirth on Windows x64. Each download supports one game build and the game version is part of its file name, so pick the download that matches your installed game. A download built for a different game version refuses to run rather than guessing.
- An x64 ASI loader. The recommended setup is [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader), which provides the proxy that loads `.asi` files. Download its `xinput1_3-x64.zip` build and put `xinput1_3.dll` in `End/Binaries/Win64/`. The loader selects its behaviour from its own filename and upstream ships a separate build per supported name, so use that build rather than renaming another one. Without an ASI loader, `FF7RPianoSongs.asi` will not load. If a compatible x64 ASI loader is already installed, keep it and do not add a second proxy DLL.

## Install

1. Exit the game.
2. If no compatible x64 ASI loader is installed, install Ultimate ASI Loader from the link above.
3. Copy this package's `End/` folder into the game directory and merge it with the existing `End/` folder.
4. Put each song in `End/Binaries/Win64/Music/<Song Name>/`.
5. Start the game. Valid songs appear in the piano-song list.

Expected layout:

```text
End/Binaries/Win64/
  xinput1_3.dll                    (ASI loader proxy; another proxy name works too)
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

- **Update:** exit the game, replace the ASI, compare the new INI with yours, and keep your `Music/` folders. Delete caches only when the changelog asks for it. After the game itself updates, take the download for the new game version.
- **Roll back:** with the game stopped, restore the previous ASI and matching INI, then delete generated song caches.
- **Remove:** with the game stopped, delete `FF7RPianoSongs.asi`. Optionally remove its INI, log, score INI, and only the custom song folders you no longer want. Do not remove the ASI loader if another mod uses it.

## Troubleshooting

**The mod does not load**

- Confirm Ultimate ASI Loader or another compatible x64 ASI loader is installed. A working layout normally includes a proxy DLL such as `End/Binaries/Win64/xinput1_3.dll`.
- Confirm `FF7RPianoSongs.asi` is directly in `End/Binaries/Win64/` and `General.Enabled` is nonzero.
- Check `FF7RPianoSongs.log`. A game build this download was not built for fails closed instead of guessing addresses, and the log reports the executable identity it expected next to the one it found. Install the download that matches your game version, or wait for one to be published.

**A song is missing or changed after an edit**

- Check the fixed names and limits in [Song Format](docs/SongFormat.md).
- Use exactly one supported audio source and valid JSON.
- Delete that song's `.cache/` and retry.
- Temporarily set `General.LogLevel=debug`, reproduce once, then restore `info`.

Logs may contain song names and local paths. Remove private data and copyrighted inputs before sharing them.

## Compatibility and Limitations

- Each download supports exactly one cataloged Windows x64 game build. Updating or rolling back the game can require a different download.
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
