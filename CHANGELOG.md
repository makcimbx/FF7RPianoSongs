# Changelog

## Unreleased

## 0.4.1 - 2026-10-29

### Fixes

- Long charts above 512 rows no longer fall back to the first native song: the
  long-chart transaction checked song selection against an unrelated activation
  counter and rejected it once the two drifted apart.
- Grouped charts of at most 512 rows but more than 512 events (common for generated
  MIDI levels 4-6) now play through the long-chart path instead of being skipped.
- A committed long chart no longer waits for an audio-publication signal that
  native audio stopped sending, which kept it `pending` indefinitely.
- The result screen again receives the custom song's title, difficulty and score
  thresholds: it rejected every custom song because live playback always carries
  a cleanup lease with the same token, which the check mistook for an unrevoked
  previous playback.

## 0.4.0 - 2026-10-25

### New features

- Added a gameplay song card beside the native title: optional `cover.png`,
  album, BPM, prepared duration and chart/arrangement preparer (`author`).
  Artwork has rounded corners and a fine frame; tempo, time and preparer have
  pictograms. Missing artwork or credits do not prevent playback.
- Added piano-menu themes for the song menu's images, colors and preview badge;
  **Original** keeps the game's styling. Install themes under
  `End/Binaries/Win64/Themes/`. Ctrl+F6 cycles themes (`Theme.KeyboardFunctionKey`),
  or hold L3 and tap L2/R2 on a gamepad; the last applied theme is remembered
  (`Theme.Selected`). The package includes the `evangelion` and `steins-gate`
  example themes and the Themes Guide for installation, controls and authoring.
  Menu themes and song-card artwork are independent.

### Improvements

- Use native Runtime-font raster sizing where available instead of enlarging
  tiny cached glyphs. Fit metadata to the available screen space, with compact
  gaps and priority for tempo, duration and preparer over long album names.
- Preserve song-card identity through callback-free pause while rechecking the
  current song and visible HUD before drawing; stale content is withdrawn.
- Song previews now follow the game's master and music volume, including mute,
  and the native piano attenuation. Previews stay silent when those settings
  cannot be read safely. This replaces the 0.3.1 separate-stream volume limit.
- Reduced verification overhead when loading already-prepared songs at startup
  (Windows CNG SHA-256 with a portable fallback, fewer repeated reads). Every
  cached artifact is still fully verified.
- Permit full binding checks inside the game's read-only chart lookup, intended to
  reduce spurious custom-chart activation rejections. One bounded 1.005 run logged
  none; 1.004 is covered by automated tests only.

### Updating song files

- Song JSON remains schema v2. Optional display credits are `album` and `author`;
  remove `artist` from experimental song files. It is no longer accepted.
- BPM and duration are not cover-image tags; duration is computed automatically.
  Changing only `cover.png` does not require rebuilding audio/chart caches.
- No source audio, MIDI or score reset is required for these display changes.
  Edited `song.json` files can trigger normal cache preparation on the next load.

### Validation limits

- Song-card appearance and sharper text have owner feedback on a 1.005
  development build. The complete pause/results, resolution/UI-scale and Unicode
  matrix is not established. The 1.004 contracts were recovered independently,
  but the new card/font path has not been verified in that game build.
- Custom-theme menu performance under the final scheduling policy, close/reopen
  restoration, gamepad switching and 1.004 theming are not fully verified in-game.
  Startup and chart-activation changes are not measured in-game improvements, and
  chart activation is not a complete fix for every reported chart/audio mismatch.
  Preview volume on 1.004 and parity with every native mix effect are unverified.
- Exact packaged-artifact gameplay and the full per-build manual matrix are not
  established. The owner explicitly accepts these gaps for 0.4.0; see
  `docs/CurrentStatus.md` in the source repository.

## 0.3.1 - 2026-10-20

### Improvements

- Redesigned the startup song-cache overlay in the native menu style: Cloud rides an
  animated chocobo along the progress track toward a Gysahl Green. It shows how many
  songs are checked, ready, and skipped, plus the time left while songs are rebuilt,
  and fades out after the ready notice.
- Count native-audio admission failures as skipped songs, rather than reporting
  an empty Music folder when no prepared song can be admitted.

### Validation limits

- Fresh visual checks of the redesigned loading overlay, exact packaged-artifact
  gameplay and the full per-build manual matrix are not established. The owner
  explicitly accepts these gaps for 0.3.1; see `docs/CurrentStatus.md` in the
  source repository. Automated checks do not qualify the in-game appearance.

## 0.3.0 - 2026-10-20

### New features

- Switched the default distribution to a standalone `D3DCOMPILER_43.dll` containing
  the complete mod and its loader. A separate ASI loader is no longer required.
  Supported alternative DLL names are listed in the player guide; install only
  one copy and remove the previous product ASI when upgrading.
- Replaced stock-audio source substitution with per-song native audio containers
  and registrations. The game now owns playback, pause and stop transitions;
  generated containers are cached automatically outside the game directory.
- Added audio-to-MIDI transcription for piano recordings without `song.mid` or
  explicit JSON notes. The bundled muscriptor model generates `song.mid` once;
  subsequent loads reuse it. Existing authored notes and supplied MIDI take priority.
- Added autoplay for stock and custom songs, toggled with Ctrl+F8 or same-controller
  L3+R3 while seated at the piano. Assisted plays still show their score and rank,
  but do not replace personal records, even if autoplay is disabled before finishing.
- Added custom-song previews: automatic after a short highlight dwell, manual
  Ctrl+F7/R3 control, and manual/off settings. A fading row-attached badge shows
  song time, excerpt position and an equalizer; preview stops before gameplay,
  on menu exit or focus loss. Native controller stick clicks also support devices
  read directly by the game, such as DualSense without XInput emulation.

### Improvements and fixes

- Improved generated MIDI charts with source-exact left-hand accompaniment,
  simultaneous two-hand inputs and better source-note coverage. Difficulty timing
  and fast register changes now use stock-derived constraints; these changes do
  not guarantee lower fatigue or retain every source note.
- Disabled the metronome in newly created song templates by default.
- Separated saved-score identities by song and profile to prevent INI-name
  collisions. Ambiguous legacy `.difficulty.<number>` records remain in the old
  INI for manual recovery rather than being assigned to the wrong song.
- Improved startup recovery when the native object registry grows during initial
  authentication, without accepting ambiguous or changed-storage snapshots.
- Rejected excessively nested song JSON and authored note onsets beyond the
  source audio, including on first preparation rather than only cache reload.
- Corrected nested list-adoption contention introduced by autoplay's controller
  callback, and added more specific chart-selection rejection diagnostics.
- Added separate native-audio, autoplay and preview contracts for game builds
  1.004 and 1.005. Each archive still supports only its named game build.
- Song caches rebuild automatically as needed; keep source songs and scores.
  The first preparation, especially audio transcription, can take longer.

### Validation limits

- The product owner reports that the current development version was tested and
  works. This is not a per-scenario or per-build report for the newly generated
  archives. The owner accepts missing exact packaged-artifact gameplay and the
  full per-build manual matrix for this version; see `docs/CurrentStatus.md`
  in the source repository. Preview audio uses a separate stream, not the game's
  music-volume mixer. Live unload/reload remains unsupported.

## 0.2.3 - 2026-09-24

- Expanded the exact game-derived chord inventory from 64 to all 170 assignments for
  authored chord compositions and `ignore_sound`, including `pca_F_dim`. Replacement
  sounds still cannot exceed the original chord's sound slots.
- Completed the existing MIDI chord-quality mappings across all twelve roots and
  made the additional native chords available to exact partial accompaniment.
  Existing mappings and the fail-closed partial-match checks remain in place.
- Added native one-third and one-sixth note values, including dotted variants, for
  hand-authored charts and exact MIDI-duration selection. Existing named values keep
  priority when durations coincide.
- Generated MIDI caches rebuild automatically for the changed selection policy;
  authored song sources, cache layout and saved scores do not need conversion.
- The user reported successful play on a 1.005 development build. Individual new
  chords and fractional note-value behavior were not exhaustively checked in game.
  New 1.004 gameplay checks, exact packaged-artifact playtests and broader manual
  scenarios are absent and explicitly accepted limitations for this release.

## 0.2.2 - 2026-09-09

- Generated MIDI charts now preserve additional simultaneous notes from the selected note's
  exact source tick, track and channel as automatic group followers. One root input triggers
  the group; the extra sounds do not require extra button presses. Nearby arpeggio notes and
  sustained notes are not merged or retriggered, and source pitches are not transposed.
- Followers retain source voicing while avoiding duplicate pitches and sounds already covered
  by selected left-hand accompaniment. All sounds count toward complete chart limits; oversized
  profiles are rejected rather than silently shortened.
- Reduced MIDI preparation overhead by sharing immutable search-row data instead of repeatedly
  copying it. Isolated cold preparation improved from 124.4 to 41.7 seconds for Sorairo Days and
  from 27.7 to 11.6 seconds for Zanarkand on the test host. The optimization preserved complete
  cache artifacts byte-for-byte; these measurements are not a per-song time guarantee.
- Song preparation remains limited to two concurrent workers to avoid the increased load of
  the experimental four-worker setting. Warm caches remain reusable.
- Updating from 0.2.1 automatically rebuilds generated MIDI caches once for the new groups;
  manual deletion is unnecessary. Authored JSON charts and source audio are unchanged.
- A bounded 1.005 development run completed Sorairo Days Lv.4 with grouped extended events,
  the expected required-action total, score persistence and cleanup. Broader group timing/input
  scenarios, new 1.004 gameplay checks and exact packaged-artifact qualification remain
  incomplete and are explicitly accepted release limitations.

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
