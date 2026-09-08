# Changelog

## Unreleased

## 0.2.1 - 2026-09-08

- Improved left-hand participation in generated MIDI charts. Fresh bass notes and two-note
  accompaniment can now use filtered native chord inputs even when no complete chord is struck.
- A melody still sounding above the accompaniment can supply context, not just a melody note
  struck at the same time. Only fresh accompaniment is emitted at its original source onset;
  sustained notes are not retriggered.
- Partial voicings keep supported source notes in their original octave and suppress every
  extra native sound. No missing chord tones, fixed left-hand quota, or higher difficulty limits
  are introduced. Left-hand activity still depends on the arrangement and selected difficulty.
- Generated MIDI caches rebuild automatically once; no manual deletion is needed. Authored
  JSON charts and source audio are unchanged. Initial preparation can take longer.
- A focused 1.005 development playtest received positive feedback on sound and left-hand
  participation. Broader manual scenarios, 1.004 gameplay and the exact packaged artifacts
  remain unqualified; those limitations are accepted for this release.

## 0.2.0 - 2026-09-07

- Improved MIDI reduction to favor musically prominent source notes over extra ease among
  equally close density targets that fit the difficulty limits. Bounded alternative notes
  now compete during final selection instead of being discarded near a preliminary melody.
  Their original onset timing is preserved; difficulty and chart-size limits are not raised.
- Enabled automatic long-chart support on compatible 1.004 and 1.005 builds, up to 8192
  rows/events per profile after exact-build validation. Unsupported charts are rejected without
  truncation; long-chart input and feature combinations still have limited in-game coverage.
- Made failed required long-chart expansion withdraw the custom performance rather than leave
  a shortened chart playable. A rejected preflight restores and cancels preparation before any
  stock expansion; unresolved mutation remains blocked until owned cleanup.
- Fixed warm-cache reuse for a valid automatic-note group beginning at the last row of the
  512-row transport prefix, including additional authored profiles. Group topology is validated
  on the complete chart instead of treating the prefix as a standalone chart.
- Fixed authored extended-chart caches incorrectly applying the MIDI-only minimum lead-in
  to retained rows. Explicit chart timing remains unchanged.
- Added independent left/right note-value symbols, exact supported MIDI note values and
  key-signature-aware spelling, plus a complete resolved-chart export for manual authoring.
- Corrected the song list's full selected-profile input count for long charts and its difficulty
  symbols: one per level, capped at six, with the exact number still in the title.
- Restored the last played custom song and difficulty when reopening the list, using song and
  difficulty identities rather than old row indices.
- Corrected the return-focus implementation calling the game's focus-registration routine twice.
  The failed UI test could start vanilla music and charts after the first custom song; restoration
  now uses the single normal call during menu opening. The user reported successful repeat
  custom-song playback on the subsequent 1.005 development builds, not the packaged artifacts.
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
  Replacement lists cannot exceed the stock chord's sound count. A limited 1.005 development
  auditory/count/completion check passed; full input and transition qualification remains incomplete.
- Enabled the same chord-composition mechanism on 1.004 using independently checked native calls
  and structures. Existing songs with custom compositions rebuild their cache once; other songs
  are unaffected. Testing on 1.005 does not establish in-game behavior on 1.004.
- Corrected alternate C input limits in both authored JSON and generated MIDI to match the verified
  1.005 game table: C2–C6 and C#2–C#6. C7 already uses the high-C input without an alternate setting.
  Invalid boundary variants are now rejected or avoided during generation; affected caches rebuild.
  C-flat alternate inputs remain unsupported; no matching entry exists in that table.
- Fixed ordinary charts containing simultaneous right-hand notes and left-hand chords being
  limited by the number of JSON rows instead of the number of playable events. A focused 1.005
  development fixture retained and scored all seven events from five rows.
- Reworked the bundled song guide with a beginner path, complete gameplay examples, clearer MIDI
  timing controls, and instructions for editing generated charts. Kept the full field and chord reference.
- Affected song caches rebuild automatically after updating; manual cache deletion is not required.
- Release limitations: exact packaged-artifact gameplay is not newly qualified on either game build;
  1.004 and broader sound/input/lifecycle scenarios remain incompletely tested. The product owner
  explicitly accepted those limits for 0.2.0; successful automated checks do not replace them.

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
