# Song Format Reference

This guide explains how to create and edit `song.json`. It is included in every release archive;
you do not need the source code or developer tools. For installing the mod, see the [player guide](../README.md).

## Start here

Choose one way to make a song:

- **Generate from MIDI:** put your audio and matching MIDI in a song folder. Use the
  [MIDI-backed song example](#midi-backed-song), or let the mod create `song.json` on its first load.
  The mod chooses notes and difficulty levels; it does not convert audio into MIDI.
- **Write the gameplay yourself:** use the [minimal explicit chart](#minimal-explicit-chart), then
  add entries to `notes`. You need audio, but no MIDI. This is also how to port a hand-authored chart
  from another mod; that mod's original arrays are not accepted directly.
- **Edit a generated chart:** load it once, exit the game, and copy the generated
  [resolved song](#resolved-song-convenience-output) into the real `song.json`. You can then add
  [automatic note groups](#groups-and-dual-rows), [partial chords](#chords-and-ignore_sound),
  [custom chord sounds](#custom-chord-sounds), or [alternate C inputs](#pitch-spelling).

Only the fields marked **required** need to be supplied. Leave optional settings out until you
need them. A **profile** is a playable difficulty version of the same song, not another audio file.

### Editing JSON safely

1. Exit the game and keep a backup of `song.json` before editing. Use a plain-text editor and save
   as UTF-8 with the exact name `song.json`, not `song.json.txt`.
2. Use double quotes for names and text, a decimal point for numbers, and unquoted `true`/`false`
   for switches. Separate entries with commas, but do not put a comma after the last entry.
   JSON does not allow comments.
3. Copy a complete example first. Add note settings inside a note's `{ ... }`, not beside `title`.
   Field names are case-sensitive; unknown fields are rejected, including misspellings.
4. Restart the game to load changes. If the song is missing, check
   `End/Binaries/Win64/FF7RPianoSongs.log`. An invalid song is rejected rather than partly loaded.

Quick reference: [files](#folder-and-source-selection) · [song settings](#root-object) ·
[notes](#explicit-notes) · [profiles](#difficulty-profiles) · [MIDI timing](#midi-backed-songs) ·
[audio and volume](#audio-modes-loudness-and-gain) · [limits](#extended-charts-and-build-policy).

## Folder and source selection

```text
Music/My Song/
  song.json                        auto-created when absent; required afterward
  song.wav | song.mp3 | song.flac  choose exactly one audio file
  song.mode1.wav                  optional; .mp3 or .flac also accepted
  song.mode2.wav                  optional; .mp3 or .flac also accepted
  song.mid | song.midi              required only when JSON has no explicit notes
```

The folder belongs under `End/Binaries/Win64/Music/`. The `|` above means “choose one”; it is not
part of a filename. File names are matched case-insensitively, but only these names are recognized.
Keeping both `song.wav` and `song.mp3`, or both `song.mid` and `song.midi`, rejects the folder.
The same one-file rule applies separately to each optional mode. `song.mode0.*` is rejected:
`song.*` already supplies that mode. See [audio modes](#audio-modes-loudness-and-gain) before adding overrides.

Each audio file must be nonempty, no larger than 512 MiB, and no longer than 10 minutes when decoded.
The mod converts supported audio to 48 kHz stereo; you do not need to supply HCA or MABF files.

If `song.json` is absent, the mod creates a starter file using the folder name as the title and
enabling the metronome at level `0.12`. You still need matching MIDI, or must add your own notes.
An existing file is not replaced, even if it contains errors.

## Root object

The root is the outermost `{ ... }` object. Most songs need only `schema`, `title`, and either
MIDI or an authored `bpm` and `notes`/`profiles`. The table lists every accepted field; it is not
a list of settings you must fill in. All numbers must be finite.

| Field | JSON type | Status and default | Accepted value / interaction |
| --- | --- | --- | --- |
| `schema` | string or number | **required** | Exactly `"ff7rpianosongs.song.v2"`, `"v2"`, or numeric `2`. JSON schema remains v2. |
| `title` | string | **required** | Nonempty display title. |
| `bpm` | number | **required for authored notes/profiles** | 30–300 beats per minute. With MIDI, omit to use its tempo. |
| `difficulty` | integer | optional; default `1` | Difficulty label, 0–2147483647. Does not simplify authored notes. Forbidden with `profiles`; does not select a generated MIDI level. |
| `score_thresholds` | array of integers | optional; calculated per chart | Advanced score boundaries, for example `[0, 1000, 2000, 3000]`. Nonempty; values 0–2147483647 in ascending order, with equal values allowed. |
| `mode_change_combo_counts` | array of integers | optional; calculated per chart | Combo boundaries for changing audio modes, normally two values such as `[5, 10]`. Nonempty; values 0–2147483647 in ascending order, with equal values allowed. |
| `midi_audio_offset_seconds` | number | optional; automatic when omitted | -1 to 1. Manual total shift of MIDI note timing: positive is later, negative is earlier. Not added to the automatic estimate. |
| `midi_audio_alignment_seconds` | number | optional; estimated when omitted | -1 to 1. Advanced replacement for the MIDI/audio alignment estimate; normally leave out. See MIDI timing below. |
| `midi_minimum_lead_in_seconds` | number | optional; default `2` | 0–30. Omits MIDI attacks scheduled before this time; does **not** insert silence or delay the song. |
| `loudness_normalization` | boolean | optional; default `true` | Adjusts overall audio loudness toward the target below. |
| `loudness_target_lufs` | number | optional; default `-13` | -30 to -5 LUFS (average loudness). More negative is quieter. |
| `loudness_peak_ceiling_dbfs` | number | optional; default `-1` | -6 to 0 dBFS (peak limit for normalization). More negative leaves more headroom. |
| `gain_envelope` | array | optional; absent means unity/no envelope | 1–64 gain-point objects; see below. |
| `metronome` | object | optional; see defaults below | Closed metronome object; see below. |
| `notes` | array | optional | Your playable notes. Nonempty; cannot coexist with `profiles`. If neither is present, MIDI is required. |
| `profiles` | array | optional | 1–32 explicit profile objects. Mutually exclusive with root `notes` and root `difficulty`. |
| `chord_voicings` | object | optional; omitted means stock chord sounds | Song-wide replacements for chord sounds; requires explicit `notes` or `profiles` and the matching mod for game 1.004 or 1.005. See “Custom chord sounds”. |
| `diagnostic_extended_chart_fixture` | boolean | obsolete; omit | Accepted for old files, but has no effect. It does not unlock longer charts. |

Usually omit both threshold arrays. The mod calculates them for each chart from its required
player actions and difficulty. Automatic group followers do not add required actions; a note and
chord played together count separately. Supplied root threshold arrays apply to every profile.

## Explicit notes

`notes` is a nonempty array. Each `{ ... }` entry is one **row** of your chart. Keep rows in time
order; equal `beat` values are allowed for simultaneous actions.

- `pitch` is a single right-hand note; `chord_id` is a left-hand chord.
- `beat` is counted from zero at the beginning of the audio, not from one. At 120 BPM, one beat
  is 0.5 seconds, so `beat: 4` starts at 2 seconds. Fractions such as `4.25` are allowed.
- `duration_beats` is the duration in the same units, not an end timestamp. Leave room in your
  audio for the complete chart. To change the displayed note symbol, use the note-value fields.

| Field | JSON type | Status and default | Accepted value / interaction |
| --- | --- | --- | --- |
| `beat` | number | **required** | Finite and at least 0. |
| `duration_beats` | number | **required** | Greater than 0. Duration in beats; changing the note symbol does not change this duration. |
| `pitch` | string | optional | Exact pitch grammar below. At least one of `pitch` or `chord_id` is required. |
| `chord_id` | string | optional | Syntactically: `pca_` followed by one or more ASCII letters, digits, or underscores. Exact verified IDs are listed below. |
| `group_index` | integer | optional; default `0` | 0–255. Use the same nonzero value on consecutive rows for an automatic run after the first input. |
| `monotone_variant` | string | optional; default `"default"` | `default` or `alternate`; requires `pitch`. Selects the alternate high-C input where supported below. |
| `ignore_sound` | array of strings | optional | 1–3 different chord sounds to leave out; requires `chord_id`. Copy names from the selected custom composition, or the stock chord table when not overridden. |
| `monotone_note_value` | string | optional | Exact note-value token below; requires `pitch`. |
| `chord_note_value` | string | optional | Exact note-value token below; requires `chord_id`. |

Supply at least `pitch` or `chord_id`. Supplying both means both hands play at that time and counts
as two events toward the chart limit. A setting for a missing hand is an error.

### Pitch spelling

Write an uppercase note letter, optionally `#` for sharp or lowercase `b` for flat, then the
octave: `C4`, `C#4`, `Db4`. Naturals are authored as `C4`, not `Cn4`. Do not use the musical
symbols `♯`/`♭` or append `_2` to `pitch`.

The exact grammar is **`[A-G](#|b)?[0-9]`**, it is case-sensitive, and the sounding pitch must
be C1 through C7 inclusive:

| Written octave | Accepted spellings |
| --- | --- |
| 0 | only `B#0` (enharmonic C1) |
| 1 | all grammar spellings resolving into range; specifically `Cb1` is invalid |
| 2–5 | every spelling matching the grammar |
| 6 | every spelling matching the grammar, including boundary `B#6` (C7) |
| 7 | only `Cb7` (B6) and `C7` |
| 8–9 | none |

Pairs such as C-sharp and D-flat sound alike but use different spellings. The game preserves
`C#`/`Db`, `D#`/`Eb`, `F#`/`Gb`, `G#`/`Ab`, and `A#`/`Bb` as distinct note names. Other accepted
equivalents use the usual name for that sound; for example, ordinary `Cb4` becomes B3.

`monotone_variant: "alternate"` selects the high-C input variant (`_2` in original minigame
arrays), not a higher sounding octave or a different duration. It is available for **C2–C6 and
exact C#2–C#6**, including `B#1` through `B#5` as equivalents of natural C2–C6.

- C1 has no alternate. C7 already uses the game's high-C assignment with ordinary `"pitch": "C7"`;
  leave `monotone_variant` out for it. Requesting alternate for either boundary is an error.
- There is **no supported C-flat alternate**. `Cb4` can supply ordinary B3's sound, but not a
  high-C-flat input. The stock game table has no `Cb*_2` entry; ordinary B is not a substitute.
- C#1, C#7, every flat (including `Db4`), and every other pitch reject when alternate is requested.

These alternate mappings match the 1.005 game files. Their actual controls still need an in-game
check; compatibility of these mappings with 1.004 has not been independently confirmed.

### Note values

`monotone_note_value` changes the right-hand note symbol; `chord_note_value` changes the left-hand
symbol. Set either or both independently. They do not move the note or change `duration_beats`.

| JSON value | Displayed note |
| --- | --- |
| `whole` | Whole note |
| `dotted_whole` | Dotted whole note |
| `half` | Half note |
| `dotted_half` | Dotted half note |
| `quarter` | Quarter note |
| `dotted_quarter` | Dotted quarter note |
| `eighth` | Eighth note |
| `dotted_eighth` | Dotted eighth note |
| `sixteenth` | Sixteenth note |
| `dotted_sixteenth` | Dotted sixteenth note |

These are the only supported values. Without an override, durations of at least 2 beats display
a quarter note; shorter durations display an eighth note. This compatibility default is not a full
rhythmic transcription, so set an explicit value when the symbol matters.

### Chords and `ignore_sound`

Choose a `chord_id` from the table. For example, `pca_C` is C major and `pca_C_m` is C minor.
To play only part of a chord, add `ignore_sound` with the sounds you want to **remove**, not the
ones you want to keep. `pca_C` with `"ignore_sound": ["En2"]` leaves C and G.

The player still triggers the original chord input. Filtering changes the game's chord sounds,
not the notes already recorded in your audio file, and does not remove the scoring action.

Copy 1–3 different names exactly from the selected row below, or from that chord's
`chord_voicings` list when overridden. These names intentionally use
the game's spelling (`Cn2`, `En2`), unlike `pitch` (`C2`, `E2`). Names are case-sensitive and
octave-specific: `E2`, `En3`, and `en2` are not substitutes for `En2`. Omit `ignore_sound` to play
the full chord; an empty array is not accepted. Filtering all three sounds of a three-note chord
is allowed but leaves its input silent.

Names are matched exactly, not by sounding pitch: `Ab2` does not remove `Gs2`.
The table below describes **stock** chord sounds. A custom composition replaces the corresponding
list for this song; it does not add sounds to it.

| Chord ID | Stock sounds / allowed `ignore_sound` without an override |
| --- | --- |
| `pca_C` | `Cn2`, `En2`, `Gn2` |
| `pca_C_m` | `Cn2`, `Ds2`, `Gn2` |
| `pca_C_dim` | `Cn2`, `Ds2`, `Fs2` |
| `pca_C_sus4` | `Cn2`, `Fn2`, `Gn2` |
| `pca_C_7` | `Cn2`, `En2`, `Gn2`, `As2` |
| `pca_C_m7` | `Cn2`, `Ds2`, `Gn2`, `As2` |
| `pca_C_Maj7` | `Cn2`, `En2`, `Gn2`, `Bn2` |
| `pca_C_9` | `Cn2`, `En2`, `Gn2`, `Dn3` |
| `pca_Cs` | `Cs2`, `Fn2`, `Gs2` |
| `pca_Db` | `Db2`, `Fn2`, `Ab2` |
| `pca_Cs_m` | `Cs2`, `En2`, `Gs2` |
| `pca_Db_sus4` | `Db2`, `Gb2`, `Ab2` |
| `pca_Db_Maj7` | `Db2`, `Fn2`, `Ab2`, `Cn3` |
| `pca_D` | `Dn2`, `Fs2`, `An2` |
| `pca_D_m` | `Dn2`, `Fn2`, `An2` |
| `pca_D_dim` | `Dn2`, `Fn2`, `Gs2` |
| `pca_D_sus4` | `Dn2`, `Gn2`, `An2` |
| `pca_D_m7` | `Dn2`, `Fn2`, `An2`, `Cn3` |
| `pca_D_9` | `Dn2`, `Fs2`, `An2`, `En3` |
| `pca_D_m9` | `Dn2`, `Fn2`, `An2`, `En3` |
| `pca_Eb` | `Eb2`, `Gn2`, `Bb2` |
| `pca_Eb_dim` | `Eb2`, `Gb2`, `An2` |
| `pca_Eb_sus4` | `Eb2`, `Ab2`, `Bb2` |
| `pca_Eb_m7` | `Eb2`, `Gb2`, `Bb2`, `Db3` |
| `pca_Eb_9` | `Eb2`, `Gn2`, `Bb2`, `Fn3` |
| `pca_Eb_mM7` | `Eb2`, `Gb2`, `Bb2`, `Dn3` |
| `pca_E` | `En2`, `Gs2`, `Bn2` |
| `pca_E_m` | `En2`, `Gn2`, `Bn2` |
| `pca_E_dim` | `En2`, `Gn2`, `As2` |
| `pca_E_sus4` | `En2`, `An2`, `Bn2` |
| `pca_F` | `Fn2`, `An2`, `Cn3` |
| `pca_F_m` | `Fn2`, `Gs2`, `Cn3` |
| `pca_F_sus4` | `Fn2`, `As2`, `Cn3` |
| `pca_F_7` | `Fn2`, `An2`, `Cn3`, `Ds3` |
| `pca_F_m7` | `Fn2`, `Gs2`, `Cn3`, `Ds3` |
| `pca_F_9` | `Fn2`, `An2`, `Cn3`, `Gn3` |
| `pca_Fs` | `Fs2`, `As2`, `Cs3` |
| `pca_Fs_dim` | `Fs2`, `An2`, `Cn3` |
| `pca_Fs_7` | `Fs2`, `As2`, `Cs3`, `En3` |
| `pca_Gb_Maj7` | `Gb2`, `Bb2`, `Db3`, `Fn3` |
| `pca_G` | `Gn2`, `Bn2`, `Dn3` |
| `pca_G_m` | `Gn2`, `As2`, `Dn3` |
| `pca_G_dim` | `Gn2`, `As2`, `Cs3` |
| `pca_G_sus4` | `Gn2`, `Cn3`, `Dn3` |
| `pca_G_7` | `Gn2`, `Bn2`, `Dn3`, `Fn3` |
| `pca_G_Maj7` | `Gn2`, `Bn2`, `Dn3`, `Fs3` |
| `pca_G_9` | `Gn2`, `Bn2`, `Dn3`, `An3` |
| `pca_G_mM7` | `Gn2`, `As2`, `Dn3`, `Fs3` |
| `pca_Ab` | `Ab2`, `Cn3`, `Eb3` |
| `pca_Ab_m` | `Ab2`, `Bn2`, `Eb3` |
| `pca_Ab_dim` | `Ab2`, `Bn2`, `Dn3` |
| `pca_A` | `An2`, `Cs3`, `En3` |
| `pca_A_m` | `An2`, `Cn3`, `En3` |
| `pca_A_sus4` | `An2`, `Dn3`, `En3` |
| `pca_A_m7` | `An2`, `Cn3`, `En3`, `Gn3` |
| `pca_A_9` | `An2`, `Cs3`, `En3`, `Bn3` |
| `pca_Bb` | `Bb2`, `Dn3`, `Fn3` |
| `pca_Bb_m` | `Bb2`, `Db3`, `Fn3` |
| `pca_Bb_dim` | `Bb2`, `Db3`, `En3` |
| `pca_Bb_sus4` | `Bb2`, `Eb3`, `Fn3` |
| `pca_Bb_m7` | `Bb2`, `Db3`, `Fn3`, `Ab3` |
| `pca_B_m` | `Bn2`, `Dn3`, `Fs3` |
| `pca_B_dim` | `Bn2`, `Dn3`, `Fn3` |
| `pca_B_sus4` | `Bn2`, `En3`, `Fs3` |

Other `pca_*` names can pass the initial JSON check, but are not guaranteed to exist in the game.
The table is the supported reference for chord filtering; arbitrary new chords cannot be created
by inventing an ID.

### Custom chord sounds

A **voicing** is the list of notes that sound together. Use root `chord_voicings` to change that
list while keeping the original chord input. For example, `pca_C` can sound `Cn3`, `En3`, and
`Gn3` instead of the stock `Cn2`, `En2`, and `Gn2`. See the
[complete example](#custom-chord-composition-and-filtering).

- Put `chord_voicings` beside `title` and `bpm`, **not inside a note**. Each key is a chord ID
  from the stock table, and its value is an ordered list of sound names. Define each chord once.
- The definitions apply to this song and all its difficulty profiles. Other songs are unchanged.
  Chords not listed keep their stock sounds. Omit the object when not needed; an empty object is invalid.
- Supply at least one sound, but no more than the stock chord's number of sounds in the table:
  at most three for a three-note chord, or four for a four-note chord. Repeated sound names are invalid.
  You may change pitches/octaves or shorten the list, but cannot add extra sound slots.
- Use exact game sound names, not `pitch` spelling. Supported prefixes are `Cn`, `Cs`, `Db`,
  `Dn`, `Ds`, `Eb`, `En`, `Fn`, `Fs`, `Gb`, `Gn`, `Gs`, `Ab`, `An`, `As`, `Bb`, and `Bn`,
  each followed by an octave from `1` through `6`; `Cn7` is also supported. For example, use
  `Cn3`, not `C3`. Alternate input names such as `Cn3_2` are **not** sound names.
- Sound order matters: each position keeps the stock position's playing strength. Removing a
  sound with `ignore_sound` does not shift the remaining sounds into different strength positions.
  This setting does not edit your audio recording or change the required input, note symbols, or score.
- `ignore_sound` must now name members of the **replacement** list. If C uses `Cn3/En3/Gn3`,
  ignore `En3` to remove its middle note; `En2` is no longer a valid member. The existing limit
  of three ignored sounds remains. Filtering the entire replacement list is allowed when it fits
  that limit; the chord input still counts, but produces no chord sounds.

This feature supports **games 1.004 and 1.005**, using the matching mod build and explicit `notes` or `profiles`. A MIDI-only
song cannot use it: first [copy the generated chart into song.json](#resolved-song-convenience-output),
then add your definitions. An unsupported build or unavailable feature is
rejected rather than silently playing the stock composition.

Only chord events in the chart, including automatic group followers, use these definitions.
Free play and wrong-key sounds remain stock. Actual sound and transition behavior still require
focused in-game verification; do not treat successful file loading as that verification.

### Groups and dual rows

For a fast right-hand run, give two or more consecutive note rows the same `group_index`, from
1 to 255. The player hits the first note; the remaining notes follow automatically at their own
`beat` times. See the [automatic run example](#automatic-right-hand-run).

- Omit the field, or use `0`, for an ordinary independent input.
- A group must contain at least two consecutive rows. A single grouped row is an error.
- A different number or `0` ends the group. Reusing its number later starts a new independent run.
- Followers still count toward the chart's size, but do not require separate player actions.
- Groups can also contain chords or rows with both hands. A first row containing both `pitch`
  and `chord_id` still requires **both** inputs; grouping does not turn a two-hand root into one input.

Automatic MIDI generation never creates groups. Add them yourself after exporting the generated
chart if you want this gameplay. Long grouped charts have the additional limits below.

## Difficulty profiles

`profiles` is an array of 1–32 closed objects. Each object has exactly two **required** fields:

| Field | JSON type | Rule |
| --- | --- | --- |
| `difficulty` | integer | 0–2147483647; put profiles in increasing difficulty order with no duplicate labels. |
| `notes` | array | Nonempty explicit-note array using all rules above. |

Keep `bpm` at the root and remove root `notes` and root `difficulty`. Put each difficulty label
and its notes inside its own profile. The first profile is the default; labels can skip numbers.
All profiles share the song's audio, BPM, and root `chord_voicings`, if supplied.
An invalid authored profile rejects the song.
In the song's detail view, use D-pad left/right or the keyboard arrow keys to select a profile.

## MIDI-backed songs

When root `notes` and `profiles` are absent, exactly one `song.mid` or `song.midi` is required.
The generator tries difficulty labels 1 through 6; root `difficulty` does not select one of them.
Each level chooses a playable selection from the MIDI, not necessarily every original note.
Levels that cannot be generated within the supported limits are omitted, so some numbers may be missing.
The generator may choose stock chords, partial chords, and alternate C/C-sharp inputs, but never automatic
groups. It uses key signatures for sharp/flat spelling and exact supported MIDI lengths for note
symbols; other lengths use the default symbols described above. `chord_voicings` is for explicit
charts only; export and edit the generated chart before adding custom chord compositions.

### MIDI timing and overrides

- Start with the minimal example and leave both timing offsets out to use automatic alignment.
- To set a manual total shift, use `midi_audio_offset_seconds`: `0.1` schedules notes 0.1 seconds
  later than the MIDI timestamps, `-0.1` schedules them earlier. Explicit `0` means no shift;
  it does **not** mean automatic. Adjusting this value replaces, rather than adds to, the automatic shift.
- `midi_audio_alignment_seconds` is an advanced override for the alignment estimate. When no
  total offset is supplied, the mod uses this alignment plus its playback compensation. Normally
  leave this field out; an explicit total offset takes precedence for note scheduling.
- `midi_minimum_lead_in_seconds` removes attacks before the chosen start time (default 2 seconds).
  Lower it if you deliberately want early notes. It does not add an introduction to the recording.
- Omit `bpm` to use the MIDI tempo. Supplied `bpm` overrides it for chart timing.
- Supplied score/combo threshold arrays apply unchanged to all generated profiles. Omit them to
  let the generator select values for each level.

`midi_audio_offset_seconds`, `midi_audio_alignment_seconds`, and
`midi_minimum_lead_in_seconds` affect MIDI only. Once you supply explicit `notes` or `profiles`,
they do not shift that chart: edit its `beat` values instead.

## Metronome

The metronome adds a beat click to the base audio mode only. Omit it for no click, or use
`"metronome": { "enabled": true, "level": 0.12 }`. A newly auto-created starter enables it;
that is different from omitting it in a file you write yourself.

| Field | JSON type | Status/default | Accepted value / interaction |
| --- | --- | --- | --- |
| `enabled` | boolean | optional; default `false` | Adds clicks in Mode0 only. |
| `level` | number | optional; default `0.12` | Finite `[0,1]`; must be greater than 0 when enabled. |
| `beat_zero_offset_seconds` | number | optional; default `0` | -30 to 30. Moves the click grid's beat zero for explicit notes only; positive is later. Does not move notes or source audio. |

Do not put `beat_zero_offset_seconds` in a MIDI-backed song: its presence is an error, including an
explicit `0`, even when the metronome is disabled. Omit the field so MIDI uses its automatic or
manually configured timing.

## Audio, modes, loudness, and gain

The minigame switches among three audio modes as the player's performance changes; these are
not difficulty profiles. Supplying one `song.*` file is enough: all modes use it by default.
Optionally supply `song.mode1.*` and/or `song.mode2.*` for different arrangements at those tiers.
If either is missing, that mode uses the base `song.*`, not the other override.

Export all versions with the **same start, end, and sample-accurate duration**. Overrides must
have exactly the same decoded sample count after conversion to 48 kHz stereo, or the song is
rejected. Matching the displayed rounded duration in an audio player may not be enough, especially
with differently encoded MP3s; aligned WAV exports are the simplest choice. The mod does not pad
or trim mismatched versions. Metronome clicks are added to Mode0 only.

For volume, start with the default normalization settings. `gain_envelope` is optional automation
for making parts of the recording quieter or louder. It contains 1–64 points:

| Field | JSON type | Status | Accepted value |
| --- | --- | --- | --- |
| `time_seconds` | number | **required** | Seconds from the audio start, at least 0. Points must have strictly increasing times. |
| `gain_db` | number | **required** | -12 to 12 dB. `0` leaves volume unchanged; negative is quieter, positive is louder. |

Volume changes smoothly between points (linearly in amplitude). Before the first point and after
the last, its endpoint volume is held. The envelope applies to each audio version. Omit it for no
additional volume automation.

## Extended charts and build policy

The ordinary limit is 512 chart rows. On supported game builds, the mod automatically attempts
longer charts up to **8192 rows and 8192 events per profile**. One pitch or chord is one event;
a row containing both is two. Group followers still count toward these size limits.

There is no setting to unlock long charts. If the game's compatibility checks fail, the ordinary
limit remains and oversized charts are rejected, not shortened. For an ordinary chart of at most
512 rows, groups combined with more than 512 events are unsupported; reduce or split that chart.

Long-chart and independent left/right note-symbol support have limited in-game testing on 1.005
and are **runtime-untested on 1.004**. Grouped/filtered long-chart combinations and alternate-input
directions still need focused in-game verification. Test your complete song before sharing it.

## Resolved song convenience output

After a successful load, look for `.cache/resolved-song.json` inside your song folder. It contains
the complete playable chart, including generated difficulties, long charts, final note symbols,
and any root `chord_voicings`.
It is convenience output only: **the mod does not read it as your song configuration**.

To turn a generated chart into an editable chart:

1. Exit the game and back up your original `song.json`.
2. Copy `.cache/resolved-song.json` over `song.json`. Keep the same audio files.
3. Edit `notes`, or the notes inside each `profiles` entry, and restart the game.

Alternatively, copy a desired `notes` array or `profiles` array into your existing `song.json`.
Keep the exported `bpm` and `chord_voicings`, if present; remove the other chart form, and remove
root `difficulty` when using `profiles`. Copying only notes from a song with custom chord sounds
does not preserve those sounds. These explicit notes take precedence over any remaining MIDI file: future MIDI edits
will not regenerate them. Omitted generated levels stay absent; automatically calculated score
and combo settings may be recalculated for the edited chart.

Do not edit `.cache` directly or include it when sharing a song. The export is recreated on a
successful load if possible; inability to write it does not stop an otherwise valid song loading.

## Failure behavior

| Symptom | What to check |
| --- | --- |
| Song is missing | Read `FF7RPianoSongs.log`; check the exact filenames, JSON punctuation, and field names. |
| A group is rejected | Give at least two consecutive rows the same nonzero `group_index`. |
| A partial chord is rejected | Copy exact names from that chord's custom list when overridden, otherwise its stock table row; omit the field rather than using an empty array. |
| A custom chord composition is rejected | Check the matching mod build for game 1.004 or 1.005, explicit notes/profiles, the chord ID, exact sound names, and the stock chord's maximum sound count. |
| An alternate pitch is rejected | Check the supported spelling and octave in “Pitch spelling”; alternate is not available for every note. |
| Early MIDI notes are missing | Check the lead-in, timing offsets, and audio length. Generation also reduces notes for playability. |
| MIDI edits no longer change the chart | Remove explicit `notes`/`profiles` to return to MIDI generation, or edit those explicit notes instead. |
| Mode audio is rejected | Export all versions on the same timeline with exactly matching decoded lengths. |

Source edits normally invalidate the affected cache automatically. If a problem persists, exit
the game, delete only that song's `.cache/`, and retry. Invalid authored songs are rejected;
unsupported generated levels are omitted. Nothing is silently shortened to fit a chart limit.

## Examples

### Minimal explicit chart

Save this as `song.json` beside your audio. This complete example needs no MIDI. At 120 BPM,
the first note is at 2 seconds; replace or extend the notes to match your recording.

```json
{
  "schema": "ff7rpianosongs.song.v2",
  "title": "Minimal",
  "bpm": 120,
  "notes": [
    { "beat": 4, "duration_beats": 1, "pitch": "C4" }
  ]
}
```

### Automatic right-hand run

One right-hand input on C4 starts this three-note run. D4 and E4 keep their own scheduled times;
G4 is a separate input because its group is omitted.

```json
{
  "schema": "ff7rpianosongs.song.v2",
  "title": "Automatic Run",
  "bpm": 120,
  "notes": [
    { "beat": 4, "duration_beats": 0.25, "pitch": "C4", "group_index": 7 },
    { "beat": 4.25, "duration_beats": 0.25, "pitch": "D4", "group_index": 7 },
    { "beat": 4.5, "duration_beats": 0.25, "pitch": "E4", "group_index": 7 },
    { "beat": 6, "duration_beats": 1, "pitch": "G4" }
  ]
}
```

### Partial chord and alternate C input

The first row uses the alternate C4 input. The next row asks for C major but leaves out E,
so only C and G sound from the game's chord. Your recorded audio is unchanged.

```json
{
  "schema": "ff7rpianosongs.song.v2",
  "title": "Input And Harmony",
  "bpm": 120,
  "notes": [
    { "beat": 4, "duration_beats": 1, "pitch": "C4", "monotone_variant": "alternate" },
    { "beat": 6, "duration_beats": 1, "chord_id": "pca_C", "ignore_sound": ["En2"] }
  ]
}
```

### Custom chord composition and filtering

Use the matching mod build for game 1.004 or 1.005. The first C-major input sounds C/E/G in octave 3. The second uses the same input
but leaves out E. The last still requires the C-major input, but all three chord sounds are muted.
Your source audio is unchanged throughout. The same definition also applies to any difficulty
profiles you add at the root instead of `notes`.

```json
{
  "schema": "ff7rpianosongs.song.v2",
  "title": "Custom Chord Sounds",
  "bpm": 120,
  "chord_voicings": {
    "pca_C": ["Cn3", "En3", "Gn3"]
  },
  "notes": [
    { "beat": 4, "duration_beats": 1, "chord_id": "pca_C" },
    { "beat": 6, "duration_beats": 1, "chord_id": "pca_C", "ignore_sound": ["En3"] },
    { "beat": 8, "duration_beats": 1, "chord_id": "pca_C", "ignore_sound": ["Cn3", "En3", "Gn3"] }
  ]
}
```

### Dual row, chord filtering, note values, and grouping

Advanced combination: the first row requires both hands, with different note symbols. The second
row is a group follower, not a separate required input.

```json
{
  "schema": "v2",
  "title": "Authored Features",
  "bpm": 96,
  "notes": [
    {
      "beat": 0,
      "duration_beats": 2,
      "pitch": "Db4",
      "chord_id": "pca_Db",
      "ignore_sound": ["Fn2"],
      "monotone_note_value": "dotted_quarter",
      "chord_note_value": "whole",
      "group_index": 7
    },
    {
      "beat": 2,
      "duration_beats": 1,
      "pitch": "C#4",
      "monotone_variant": "alternate",
      "group_index": 7
    }
  ]
}
```

### Explicit difficulty profiles

```json
{
  "schema": 2,
  "title": "Profiles",
  "bpm": 120,
  "profiles": [
    { "difficulty": 1, "notes": [{ "beat": 0, "duration_beats": 1, "pitch": "C4" }] },
    { "difficulty": 4, "notes": [
      { "beat": 0, "duration_beats": 1, "pitch": "C4" },
      { "beat": 1, "duration_beats": 1, "pitch": "G4" }
    ] }
  ]
}
```

### MIDI-backed song

Place `song.mid` (or `song.midi`) and your required `song.wav`, `song.mp3`, or `song.flac`
beside this JSON. This minimal example leaves timing alignment automatic and has no metronome:

```json
{
  "schema": "ff7rpianosongs.song.v2",
  "title": "Generated MIDI"
}
```

### Metronome and gain envelope

```json
{
  "schema": "v2",
  "title": "Click And Gain",
  "bpm": 100,
  "metronome": { "enabled": true, "level": 0.12, "beat_zero_offset_seconds": 0 },
  "gain_envelope": [
    { "time_seconds": 0, "gain_db": -6 },
    { "time_seconds": 2.5, "gain_db": 0 }
  ],
  "notes": [{ "beat": 0, "duration_beats": 1, "pitch": "C4" }]
}
```

### Mode audio filenames

```text
Mode Audio Example/
  song.json
  song.flac         # Mode0/base
  song.mode1.wav    # Mode1 override
  song.mode2.mp3    # Mode2 override
```

Mode1 or Mode2 falls back to `song.flac` when its override is absent. Adding `song.mode0.wav` is
invalid: `song.mode0.*` is rejected.
