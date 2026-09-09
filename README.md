# FF7RPianoSongs
<!-- current-release-version: 0.2.2 -->

**Version 0.2.2** · Windows x64 · MIT

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
    song.json                      (settings and optional hand-authored notes)
    song.wav                       (required audio; .mp3 or .flac also accepted)
    song.mid                       (or song.midi; needed unless JSON supplies notes)
    song.mode1.wav                 (optional alternate audio; .mp3/.flac also accepted)
    song.mode2.wav                 (optional alternate audio; .mp3/.flac also accepted)
```

The package contains no music. To make your first song, put matching audio and MIDI in a song
folder; the mod creates a starter `song.json` if it is missing. Audio alone cannot generate notes.
For hand-authored charts, automatic note runs, partial chords, and alternate C inputs, start with
the bundled [Song Format guide](docs/SongFormat.md#start-here). It includes complete JSON examples
and the full settings/chord reference.

## Use

- Custom songs appear alongside the default songs.
- The first load of new or edited songs can take longer while audio and charts are prepared.
- For a song with several profiles, use D-pad left/right or the keyboard arrow keys in its detail view. Scores are stored separately for each actual profile.
- If a song is missing, it was rejected safely. Check `End/Binaries/Win64/FF7RPianoSongs.log` for the reason.

## Settings

Edit `End/Binaries/Win64/FF7RPianoSongs.ini` only while the game is stopped.

| Setting | Values | Purpose |
| --- | --- | --- |
| `General.Enabled` | `0` or nonzero | Disables or enables the mod. |
| `General.LogLevel` | `debug`, `info`, `error` | Sets log detail. Default: `info`. |
| `Advanced.RebuildAudioCache` | `0` or nonzero | Rebuilds every song cache on the next run. Set it back to `0` afterward. |

The dot in the table separates an INI section from its setting; do not type `General.LogLevel`
as a key. For example, to enable detailed logging, change the existing entry to:

```ini
[General]
LogLevel=debug
```

To rebuild one song, exit the game and delete only that song's `.cache/` directory. Never distribute
or copy `.cache/` data between machines or mod versions. To edit an automatically generated chart,
follow [Editing a resolved song](docs/SongFormat.md#resolved-song-convenience-output), rather than
editing cache files directly.

## Update, Roll Back, or Remove

- **Update:** exit the game, replace the ASI, compare the new INI with yours, and keep your `Music/` folders. Delete caches only when the changelog asks for it. After the game itself updates, take the download for the new game version.
- **Roll back:** with the game stopped, restore the previous ASI and matching INI, then delete generated song caches.
- **Remove:** with the game stopped, delete `FF7RPianoSongs.asi`. Optionally remove its INI, log, score INI, and only the custom song folders you no longer want. Do not remove the ASI loader if another mod uses it.

## Troubleshooting

**The mod does not load**

- Confirm Ultimate ASI Loader or another compatible x64 ASI loader is installed. A working layout normally includes a proxy DLL such as `End/Binaries/Win64/xinput1_3.dll`.
- Confirm `FF7RPianoSongs.asi` is directly in `End/Binaries/Win64/` and `General.Enabled` is nonzero.
- Check `FF7RPianoSongs.log`. If this download does not match your game version, the mod will not run; the log shows the mismatch. Install the matching download, or wait for one to be published.

**A song is missing or changed after an edit**

- Check the fixed names and limits in [Song Format](docs/SongFormat.md).
- Keep one main audio file, not both `song.wav` and `song.mp3`. Supply matching MIDI or explicit
  JSON notes. Optional mode recordings must have exactly the same decoded duration as the main audio.
- Exit the game before editing files or deleting that song's `.cache/`, then retry.
- Temporarily set `General.LogLevel=debug`, reproduce once, then restore `info`.

**Some MIDI notes have no playable input**

- A generated difficulty selects only part of the MIDI. Notes outside C1–C7 are also excluded;
  the diagnostics report them. This leaves the audio recording unchanged and does not move
  notes into another octave. See [MIDI range handling](docs/SongFormat.md#midi-notes-outside-the-supported-range).
- A song needs supported notes and at least one playable generated difficulty. Custom chord
  compositions do not extend the available pitch range or automatically revoice MIDI.

Logs may contain song names and local paths. Remove private data and copyrighted inputs before sharing them.

## Compatibility and Limitations

- Each download supports exactly one Windows x64 game build. Updating or rolling back the game can require a different download.
- [Custom chord compositions](docs/SongFormat.md#custom-chord-sounds) support the matching
  1.004 and 1.005 builds with explicitly authored charts. A limited auditory/count/completion
  check passed on a 1.005 development build; full sound/input/transition checks and 1.004
  gameplay remain unqualified. Free-play and wrong-key sounds are not replaced.
- Longer charts and independent left/right note symbols have limited in-game testing on 1.005
  and remain **runtime-untested on 1.004**. Not every combination of grouping, chord filtering,
  and alternate input has been checked in-game.
- The ordinary chart limit is 512 rows. Compatible builds can automatically accept longer charts,
  up to 8192 rows/events per profile; see [chart limits](docs/SongFormat.md#extended-charts-and-build-policy)
  for how two-hand rows and groups count. Unsupported charts are rejected, not shortened.
- Replace, remove, or update the ASI only while the game is stopped. Live unload/reload is unsupported.
- Supply WAV, MP3, or FLAC audio within the [song limits](docs/SongFormat.md#folder-and-source-selection).
  Pre-encoded HCA/MABF files and audio looping are not supported authoring inputs.
- Retry, abort, and unusual exit paths are not comprehensively tested. Keep backups of your song files.
- For 0.2.2, incomplete manual scenario coverage and the absence of new in-game checks on the
  exact packaged artifacts are accepted release limitations. Development results do not qualify
  every song, feature combination, or another game build.

## Reference

- [Song Format](docs/SongFormat.md)
- [Changelog](CHANGELOG.md)
- [MIT License](LICENSE)
- [Third-Party Notices](THIRD_PARTY_NOTICES.md)
- [Contributing](https://github.com/makcimbx/FF7RPianoSongs/blob/main/CONTRIBUTING.md)
- [Security Policy](https://github.com/makcimbx/FF7RPianoSongs/security/policy)
