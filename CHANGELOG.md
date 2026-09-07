# Changelog

## Unreleased

- Made generated difficulty density and growth targets preferences rather than rejection quotas.
  Bounded selection seeks a valid full-song chart without requiring the preferred number of inputs;
  actual difficulty-load, source-note and chart-size checks remain in force.
- Fixed MIDI generation rejecting otherwise usable songs because some notes fall outside C1–C7
  when long charts are available. Unsupported pitches are excluded with diagnostics; remaining
  notes still need to form a playable chart. No automatic octave changes or audio edits are made.
- Clarified runtime logging: waiting for the game window, inapplicable long-chart checks and
  successful cleanup transitions no longer look like failed operations. Genuine ownership,
  identity and release failures remain visible; a deferred cleanup is not reported as completed.
- Added song-local chord compositions for explicit charts on games 1.004 and 1.005: keep the original chord
  input while replacing its sound list, and apply `ignore_sound` to that replacement. Definitions
  are shared across the song's difficulties; other songs and free-play sounds remain unchanged.
  Replacement lists cannot exceed the stock chord's sound count. In-game qualification is pending.
- Enabled the same chord-composition mechanism on 1.004 using independently checked native calls
  and structures. Existing songs with custom compositions rebuild their cache once; other songs
  are unaffected. Testing on 1.005 does not establish in-game behavior on 1.004.
- Corrected alternate C input limits in both authored JSON and generated MIDI to match the verified
  1.005 game table: C2–C6 and C#2–C#6. C7 already uses the high-C input without an alternate setting.
  Invalid boundary variants are now rejected or avoided during generation; affected caches rebuild.
  C-flat alternate inputs remain unsupported; no matching entry exists in that table.
- Fixed ordinary charts containing simultaneous right-hand notes and left-hand chords being
  limited by the number of JSON rows instead of the number of playable events. Focused in-game
  verification of the fix is still pending.
- Reworked the bundled song guide with a beginner path, complete gameplay examples, clearer MIDI
  timing controls, and instructions for editing generated charts. Kept the full field and chord reference.

## 0.1.3 - 2026-09-01

- Added up to 32 manually authored difficulty profiles per song, each with its own notes,
  while preserving the existing single-chart format.
- Kept the first authored profile as the default chart and exposed every
  profile through the existing in-game difficulty selector with its actual
  sparse difficulty label.
- Improved compatibility with some non-standard MIDI exports; affected song caches rebuild automatically.
- Fixed songs becoming unselectable after returning to and immediately reopening the piano list
  while songs were still being prepared, including repeated list-return handling.

## 0.1.2 - 2026-08-09

- Added optional `song.mode1.*` and `song.mode2.*` recordings for the game's audio tiers.
  Missing versions use the main `song.*` recording; all supplied versions must have matching lengths.
- Corrected generated metronome audio so clicks are present only in Mode0;
  Mode1 and Mode2 now remain clean unless their own authored source differs.
- Fixed newly prepared songs appearing in the list but becoming unselectable after returning to it.
- Improved validation of multi-mode audio and cached song files.

## 0.1.1 - 2026-08-09

- Added separate downloads for Final Fantasy VII Rebirth 1.004 and 1.005, with checks that prevent
  a download from running on the wrong game build.
- Placed custom songs after the player's actual native piano-song rows instead
  of assuming a fixed original-song count, including progressive catalog
  replacement without duplicate or missing rows.
- Improved menu usability while songs are being prepared, transitions between custom and default
  songs, and error reporting.
- Improved per-game-version package checks and rollback verification.

## 0.1.0 - 2026-07-31

- First standalone release.
- Added custom songs from hand-authored JSON or MIDI, automatic audio processing, and caching.
- Added reproducible release archives and checksums.
- Declared the first-party MIT license and removed bundled music examples.
- Cataloged supported game builds explicitly and published one archive per
  supported game build, named after the game version it supports.

Compatibility and remaining limitations are documented in the
[player guide](README.md#compatibility-and-limitations).
