# FF7RPianoSongs
<!-- current-release-version: 0.2.3 -->

**Version 0.2.3** · Windows x64 · MIT

[![Windows CI](https://github.com/makcimbx/FF7RPianoSongs/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/makcimbx/FF7RPianoSongs/actions/workflows/windows-ci.yml)

FF7RPianoSongs adds user-authored piano songs to Final Fantasy VII Rebirth.

This is an unofficial community project and is not affiliated with or endorsed
by Square Enix.

## Requirements

- Final Fantasy VII Rebirth on Windows x64. Each download supports one game build and the game version is part of its file name, so pick the download that matches your installed game. A download built for a different game version refuses to run rather than guessing.
- The standalone DLL package includes the mod and its proxy loader in one file.
  No separate ASI loader or loader-INI changes are required for this package.
- Install the game's prerequisites, including the Microsoft Visual C++ x64 runtime.
  The genuine Windows/System32 DLL for the selected proxy name must be available.
- The mod needs write access to `Music/<Song Name>/.cache/` and
  `%LOCALAPPDATA%/FF7RPianoSongs/native_audio/`.

## Install

1. Exit the game.
2. Copy `D3DCOMPILER_43.dll` and `FF7RPianoSongs.ini` from the package into `End/Binaries/Win64/`.
3. Remove or move out any previous `FF7RPianoSongs.asi` or other copy of this mod's DLL. Do not overwrite another mod's proxy; see the alternatives below.
4. Put each song in `End/Binaries/Win64/Music/<Song Name>/`.
5. Start the game. Valid songs appear in the piano-song list.

Expected layout:

```text
End/Binaries/Win64/
  D3DCOMPILER_43.dll               (the complete mod; default filename)
  FF7RPianoSongs.ini
  FF7RPianoSongs.log              (generated)
  Music/<Song Name>/
    song.json                      (settings and optional hand-authored notes)
    song.wav                       (required audio; .mp3 or .flac also accepted)
    song.mid                       (or song.midi; needed unless JSON supplies notes)
    song.mode1.wav                 (optional alternate audio; .mp3/.flac also accepted)
    song.mode2.wav                 (optional alternate audio; .mp3/.flac also accepted)
```

### Alternative DLL Names

If `D3DCOMPILER_43.dll` is already occupied by another mod, rename **our DLL** to
one of these unused names:

`d3d9.dll`, `d3d11.dll`, `dsound.dll`, `winmm.dll`, `dxgi.dll`, `xinput1_3.dll`.

Use only one copy. The selected name must be a normal startup import of your game
executable; an arbitrary name or late-loaded DLL is not supported. Never rename,
overwrite or remove another mod's proxy to make room.

Older **ASI packages** are an alternative installation: they need a compatible
x64 ASI loader and `FF7RPianoSongs.asi` beside the executable. Do not rename an ASI
to a proxy DLL or install both editions together.

The package contains no music. To make your first song, put matching audio and MIDI in a song
folder; the mod creates a starter `song.json` if it is missing. Audio alone cannot generate notes.
For hand-authored charts, automatic note runs, partial chords, and alternate C inputs, start with
the bundled [Song Format guide](docs/SongFormat.md#start-here). It includes complete JSON examples
and the full settings/chord reference.

## Use

- Custom songs appear alongside the default songs.
- The first load of new or edited songs can take longer while audio and charts are prepared.
- Native audio containers are generated automatically. Do not install generated
  pak files manually; prepared songs appear only after native admission succeeds.
- For a song with several profiles, use D-pad left/right or the keyboard arrow keys in its detail view. Scores are stored separately for each actual profile.
- Older score sections ending in `.difficulty.<number>` are retained but not imported automatically: their names can also belong to another song. New scores use separate song/profile identities; ambiguous old records require manual attribution.
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

The native backend also owns immutable generated containers under
`%LOCALAPPDATA%/FF7RPianoSongs/native_audio/`. They are separate from the song's
authoring files and need no manual installation. Do not edit or delete them while
the game is running. Source changes produce separate content identities.

## Update, Roll Back, or Remove

- **Update:** exit the game, replace this mod's DLL (or ASI for that edition), compare the new INI with yours, and keep your `Music/` folders. Keep only one edition/copy. Delete caches only when the changelog asks for it. After a game update, take the matching download.
- **Roll back:** with the game stopped, restore the previous mod binary and matching INI, then delete generated song caches.
- **Remove:** with the game stopped, delete only this mod's DLL or `FF7RPianoSongs.asi`. Optionally remove its INI, log, score INI, and custom song folders you no longer want. Leave other mods and their loaders alone.
- Generated `native_audio/` containers may be removed after the game is fully
  closed. Do not remove other files in the product's LocalAppData directory as
  cache cleanup; they can contain retained diagnostics and rollback evidence.

## Troubleshooting

**The mod does not load**

- Confirm this mod's DLL is directly in `End/Binaries/Win64/`, uses the default or a supported unused name, and `General.Enabled` is nonzero. Do not leave a second copy or this mod's ASI installed.
- For an ASI package, confirm its compatible x64 ASI loader and `FF7RPianoSongs.asi` are installed instead.
- Check `FF7RPianoSongs.log`. If this download does not match your game version, the mod will not run; the log shows the mismatch. Install the matching download, or wait for one to be published.
- If the main log is absent, include `FF7RPianoSongs.proxy-startup-failure.log` in your report if it exists.
- A native-bootstrap rejection is not a slow cache build. Keep the first named
  failure in the log and report it; do not change global loader settings or
  repeatedly launch in an attempt to bypass the guard.

**A song is missing or changed after an edit**

- Check the fixed names and limits in [Song Format](docs/SongFormat.md).
- Keep one main audio file, not both `song.wav` and `song.mp3`. Supply matching MIDI or explicit
  JSON notes. Optional mode recordings must have exactly the same decoded duration as the main audio.
- Exit the game before editing files or deleting that song's `.cache/`, then retry.
- Temporarily set `General.LogLevel=debug`, reproduce once, then restore `info`.
- `prepared` and native `ready` log entries establish artifact/runtime admission,
  not correct notes or every playback transition. Report a wrong chart, denied
  activation or failed song switch even when the cache finished successfully.

**Some MIDI notes have no playable input**

- A generated difficulty selects only part of the MIDI. Notes outside C1–C7 are also excluded;
  the diagnostics report them. This leaves the audio recording unchanged and does not move
  notes into another octave. See [MIDI range handling](docs/SongFormat.md#midi-notes-outside-the-supported-range).
- A song needs supported notes and at least one playable generated difficulty. Custom chord
  compositions do not extend the available pitch range or automatically revoice MIDI.

Logs may contain song names and local paths. Remove private data and copyrighted inputs before sharing them.

## Compatibility and Limitations

- Each download supports exactly one Windows x64 game build. Updating or rolling back the game can require a different download.
- [Custom chord compositions](docs/SongFormat.md#custom-chord-sounds) use explicitly
  authored charts on matching 1.004 and 1.005 builds. Free-play and wrong-key sounds
  are not replaced.
- The ordinary chart limit is 512 rows. Compatible builds can automatically accept longer charts,
  up to 8192 rows/events per profile; see [chart limits](docs/SongFormat.md#extended-charts-and-build-policy)
  for how two-hand rows and groups count. Unsupported charts are rejected, not shortened.
- Replace, remove, or update this mod's DLL/ASI only while the game is stopped. Live unload/reload is unsupported.
- Supply WAV, MP3, or FLAC audio within the [song limits](docs/SongFormat.md#folder-and-source-selection).
  Pre-encoded HCA/MABF files and audio looping are not supported authoring inputs.
- Keep backups of your song files and scores before updating.

## Reference

- [Song Format](docs/SongFormat.md)
- [Changelog](CHANGELOG.md)
- [MIT License](LICENSE)
- [Third-Party Notices](THIRD_PARTY_NOTICES.md)
- [Contributing](https://github.com/makcimbx/FF7RPianoSongs/blob/main/CONTRIBUTING.md)
- [Security Policy](https://github.com/makcimbx/FF7RPianoSongs/security/policy)
