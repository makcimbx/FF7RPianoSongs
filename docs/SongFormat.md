# Song Format Reference

This is the complete user-facing reference for one custom-song folder. It is bundled as
`docs/SongFormat.md` in every release archive. JSON object field sets are closed: unknown fields are rejected
at the root, profile, note, metronome, and gain-point levels. Parsing, compilation,
or final validation failure rejects the song (or omits an independently generated MIDI profile)
without clipping or partial publication. Check `FF7RPianoSongs.log` for the reason.

## Folder and source selection

```text
Music/My Song/
  song.json                         auto-created when absent; required afterward
  song.wav | song.mp3 | song.flac   exactly one required base/Mode0 source
  song.mode1.wav|mp3|flac           optional Mode1 override
  song.mode2.wav|mp3|flac           optional Mode2 override
  song.mid | song.midi              required only when JSON has no explicit notes
```

Names are matched case-insensitively, but only the names above are recognized. More than one
supported file for a role is ambiguous and rejects the folder. `song.mode0.*` is rejected because
Mode0 always uses the required base source. Missing Mode1 or Mode2 audio falls back directly to
the base source. Every supplied override must decode to the same logical frame count as the base.
Each source audio file is limited to 512 MiB; decoded audio must be nonempty, at most 10 minutes,
and is normalized to 48 kHz stereo. Both `song.mid` and `song.midi` together are rejected.

If `song.json` is absent, loading atomically creates a starter file whose title is the folder name
and whose metronome is enabled at level `0.12`, then parses that file normally. Creation uses an
exclusive temporary file and an atomic publish operation, so it never overwrites a file created by
another writer. An existing `song.json` is authoritative: if it is malformed or unsupported, the
song rejects and the existing file is not replaced by the starter.

## Root object

The root is a JSON object with the following complete field set.

| Field | JSON type | Status and default | Accepted value / interaction |
| --- | --- | --- | --- |
| `schema` | string or number | **required** | Exactly `"ff7rpianosongs.song.v2"`, `"v2"`, or numeric `2`. JSON schema remains v2. |
| `title` | string | **required** | Nonempty display title. |
| `bpm` | number | optional; no parser default | Finite 30–300. Required by final validation for explicit root `notes` and authored `profiles`; MIDI may derive it. |
| `difficulty` | integer | optional; default `1` | 0 through `INT_MAX`; forbidden when `profiles` is present. |
| `score_thresholds` | array of integers | optional; derived when absent | Nonempty; each item 0 through `INT_MAX`. Supplied values must be nondecreasing. The exact derived rule is below. |
| `mode_change_combo_counts` | array of integers | optional; derived when absent | Nonempty; each item 0 through `INT_MAX`. Supplied values must be nondecreasing. The exact derived rule is below. |
| `midi_audio_offset_seconds` | number | optional; default `0` | Finite `[-1,1]`; applies only to MIDI alignment. |
| `midi_audio_alignment_seconds` | number | optional; default `0` | Finite `[-1,1]`; when supplied it is the resolved alignment, otherwise MIDI/audio analysis estimates alignment. |
| `midi_minimum_lead_in_seconds` | number | optional; default `2` | Finite `[0,30]`; MIDI attacks before the resolved lead-in are rejected. |
| `loudness_normalization` | boolean | optional; default `true` | Enables or disables supported loudness normalization. |
| `loudness_target_lufs` | number | optional; default `-13` | Finite `[-30,-5]`. |
| `loudness_peak_ceiling_dbfs` | number | optional; default `-1` | Finite `[-6,0]`. |
| `gain_envelope` | array | optional; absent means unity/no envelope | 1–64 gain-point objects; see below. |
| `metronome` | object | optional; see defaults below | Closed metronome object; see below. |
| `notes` | array | optional | Nonempty explicit-note array. Mutually exclusive with `profiles`; when absent, exactly one MIDI source is required. |
| `profiles` | array | optional | 1–32 explicit profile objects. Mutually exclusive with root `notes` and root `difficulty`. |
| `diagnostic_extended_chart_fixture` | boolean | optional; default `false` | Legacy compatibility input only. It is inert: it neither grants extended authority nor changes validation. |

The parser owns JSON shape, types, local numeric ranges, nondecreasing supplied metadata arrays,
and closed field sets. Repository/compiler validation owns source-mode BPM requirements, native identities, groups,
IgnoreSound, row/event limits, exact source/compiled plans, and build capability.

For omitted metadata, let `A` be at least 1 and otherwise the profile's required-action count (a
group continuation contributes no action; each parentless monotone/chord side contributes one).
`score_thresholds` becomes `[0, round100(70*A), round100(85*A), 100*A]`, where
`round100(x) = ((x + 50) / 100) * 100` using integer division. For
`mode_change_combo_counts`, let `action = clamp(round(0.05*A),5,15)` and
`difficulty = clamp(5 + 2*(difficulty_label-1),5,15)`; the first value is the rounded-up mean,
clamped to `1..max(1,A/2)`, and the second is `min(A,max(first+1,2*first))`.

## Explicit notes

`notes` is a nonempty array of closed note objects. Entries must be nondecreasing by `beat`.

| Field | JSON type | Status and default | Accepted value / interaction |
| --- | --- | --- | --- |
| `beat` | number | **required** | Finite and at least 0. |
| `duration_beats` | number | **required** | Finite and greater than 0. Controls timing, envelope, and completion; notation overrides do not alter it. |
| `pitch` | string | optional | Exact pitch grammar below. At least one of `pitch` or `chord_id` is required. |
| `chord_id` | string | optional | Syntactically: `pca_` followed by one or more ASCII letters, digits, or underscores. Exact verified IDs are listed below. |
| `group_index` | integer | optional; default `0` | 0–255. Nonzero values define authored follower runs. |
| `monotone_variant` | string | optional; default `"default"` | `default` or `alternate`; requires `pitch`. Availability is identity-specific. |
| `ignore_sound` | array of strings | optional | 1–3 unique exact constituent names; requires `chord_id`. |
| `monotone_note_value` | string | optional | Exact note-value token below; requires `pitch`. |
| `chord_note_value` | string | optional | Exact note-value token below; requires `chord_id`. |

At least one side must be present. Supplying both `pitch` and `chord_id` creates one dual row and
two native events. Side-specific fields on an absent side reject.

### Pitch spelling

The exact grammar is **`[A-G](#|b)?[0-9]`**, it is case-sensitive, and the resolved semitone must
be C1 through C7 inclusive. Naturals are authored as `C4`, not `Cn4`. This rule is exhaustive:

| Written octave | Accepted spellings |
| --- | --- |
| 0 | only `B#0` (enharmonic C1) |
| 1 | all grammar spellings resolving into range; specifically `Cb1` is invalid |
| 2–5 | every spelling matching the grammar |
| 6 | every spelling matching the grammar, including boundary `B#6` (C7) |
| 7 | only `Cb7` (B6) and `C7` |
| 8–9 | none |

This semitone rule unambiguously covers every enharmonic boundary. Exact black-key pairs
`C#`/`Db`, `D#`/`Eb`, `F#`/`Gb`, `G#`/`Ab`, and `A#`/`Bb` preserve distinct verified native
spellings. Other accepted enharmonics resolve to the canonical native spelling for their semitone.

`monotone_variant: "alternate"` is separate from spelling and note value. It changes the native
input assignment while preserving the resolved pitch sound. It is available for values compiling
to natural C: authored `C1` through `C7` and the accepted enharmonic spellings `B#0` through
`B#6`. It is also available for exact C-sharp spellings `C#2` through `C#6`. C#1, C#7, every flat,
and every other resolved pitch reject when `alternate` is requested.

### Note values

Overrides select native notation independently for each present side. These are the complete
accepted values:

| JSON value | Native `(NoteType,DotType)` |
| --- | --- |
| `whole` | `(0,0)` |
| `dotted_whole` | `(0,1)` |
| `half` | `(1,0)` |
| `dotted_half` | `(1,1)` |
| `quarter` | `(2,0)` |
| `dotted_quarter` | `(2,1)` |
| `eighth` | `(3,0)` |
| `dotted_eighth` | `(3,1)` |
| `sixteenth` | `(4,0)` |
| `dotted_sixteenth` | `(4,1)` |

Native NoteTypes 5 and 6 are intentionally unsupported. Without an override, each present side
independently keeps the legacy mapping exactly: `duration_beats >= 2` uses `(2,0)`; otherwise it
uses `(3,0)`. Under the native scale those pairs are quarter and eighth. An absent side stores
`(0,0)` too, but side presence is determined by its nonempty pitch/chord identity, so a present
whole `(0,0)` is not ambiguous.

### Chords and `ignore_sound`

The following table is the complete compiler-verified chord/constituent inventory. A chord can be
authored without `ignore_sound` when its `pca_*` syntax is valid. When `ignore_sound` is present,
every requested value must be an exact, case-sensitive, exact-octave member of that row; aliases,
wrong case, wrong octave, duplicates, and nonmembers reject. One to three members may be requested.
Filtering suppresses only those selected constituent sounds when the chord is played; it does not
remove the authored row, chord event, group identity, or required/scoring action.

| Chord ID | Exact allowed `ignore_sound` constituents |
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

Exact 1.004 and 1.005 catalogs share verified `pca_Db` constituent authority. Unknown builds
remain fail-closed for its `ignore_sound`; a bare syntactically valid `pca_Db` remains parseable.

### Groups and dual rows

`group_index: 0` means a parentless required action. For each contiguous nonzero authored run, the
first row is the required root and every later row is an automated follower that retains its own
scheduled `beat` time. Every run must contain at least two rows. The same ID may be reused only
after a run ends; separated uses are independent runs. Group topology changes native links and
required-action count, not physical row/event identity. Grouping exists only in explicit JSON;
generated MIDI is always ungrouped. Author it deliberately because followers are not independent
player actions. A dual row emits monotone first and chord second; each side keeps its own notation
and identity. Explicit groups may cross the 512-row prefix only when the complete extended chart
passes generalized validation.

## Difficulty profiles

`profiles` is an array of 1–32 closed objects. Each object has exactly two **required** fields:

| Field | JSON type | Rule |
| --- | --- | --- |
| `difficulty` | integer | 0 through `INT_MAX`; labels must be unique and strictly increasing. |
| `notes` | array | Nonempty explicit-note array using all rules above. |

Root `bpm` is required. Root `notes` and root `difficulty` are forbidden. Each profile compiles and
validates independently; the first is the root/default chart. Sparse labels are allowed. A failed
profile/song is rejected rather than silently repaired or clipped.

## MIDI-backed songs

When root `notes` and `profiles` are absent, exactly one `song.mid` or `song.midi` is required.
The generator evaluates difficulty labels 1 through 6; root `difficulty` does not select one of
those levels. MIDI profiles are independently, deterministically reduced from normalized source events and every
generated row has `group_index: 0`. Exact/safe verified chord inference and exact IgnoreSound may
be generated. MIDI note spelling uses key-signature context but never pretends the file stores an
original sharp/flat spelling. Exact source tick lengths can derive side note values; otherwise the
same side-specific legacy fallback applies. The generated selector remains source-backed,
deterministic, route-validated, and fail-closed.

Root-field precedence is exact:

- Supplied `bpm` overrides source tempo for chart timing. If absent, chart BPM comes from the MIDI
  source. The resolved BPM is stored in each visible profile.
- Supplied `score_thresholds` are preserved for every generated profile. If absent, each profile
  derives them from its own required-action count using the standard rule above.
- Supplied `mode_change_combo_counts` are preserved. If absent, a visible profile uses the
  validated route's authored mode values when that route provides them; otherwise it keeps the
  standard action/difficulty-derived values above.
- Supplied `midi_audio_alignment_seconds` wins over alignment estimation. Without it, alignment is
  estimated from MIDI/audio evidence.
- Supplied `midi_audio_offset_seconds` wins for scheduling. Without it, offset is the resolved
  alignment plus the fixed playback compensation. Alignment is still resolved independently even
  when offset is supplied. The resolved alignment and offset are stored in each visible profile's
  metadata and in the root metadata from the first visible generated profile.

`midi_audio_offset_seconds`, `midi_audio_alignment_seconds`, and
`midi_minimum_lead_in_seconds` affect MIDI only. Generated labels may be omitted deterministically
when row, workload, route, or other validated limits cannot be met. No generated grouping is used.

## Metronome

`metronome` is a closed object:

| Field | JSON type | Status/default | Accepted value / interaction |
| --- | --- | --- | --- |
| `enabled` | boolean | optional; default `false` | Enables Mode0-only click synthesis. |
| `level` | number | optional; default `0.12` | Finite `[0,1]`; must be greater than 0 when enabled. |
| `beat_zero_offset_seconds` | number | optional; default `0` | Finite `[-30,30]`; valid only with explicit JSON notes. Its presence rejects a MIDI-backed song, including an explicit `0` and even when `enabled` is `false`. |

MIDI-backed generation rejects any authored `beat_zero_offset_seconds`, including an explicit `0`
and even when the metronome is disabled; omit the field so MIDI can use its resolved audio offset and lead-in. Metronome processing is
applied to Mode0 only.

## Audio, modes, loudness, and gain

Audio role filenames and fallback are defined in “Folder and source selection.” Each authored
source is decoded and processed independently. Loudness controls use the root defaults/ranges
above. `gain_envelope` is an array of 1–64 closed objects:

| Field | JSON type | Status | Accepted value |
| --- | --- | --- | --- |
| `time_seconds` | number | **required** | Final validation requires finite, nonnegative, and strictly increasing values. |
| `gain_db` | number | **required** | Final validation requires finite `[-12,12]`. |

Gain is interpolated linearly in amplitude between points. Before the first point and after the
last point, the nearest endpoint gain is held. Omitted `gain_envelope` means unity gain.

## Extended charts and build policy

Native-512 or unsupported policy accepts at most 512 source rows. On an exact supported build,
playable extended capability is automatic only after every build-specific validation/install gate
succeeds. It then accepts 513–8192 source rows and at most 8192 native events, validates the full
chart, publishes exactly the first 512 descriptor rows, and retains every remaining row/event in
the validated tail. It never clips. Authored groups, dual rows, chords, IgnoreSound, and independent
side note values remain subject to complete-plan validation.

Build 1.004 has catalog/static/offline compatibility but is **runtime-untested** for the current
release-facing extended/split-articulation claim. Build 1.005 has bounded evidence described in
`CurrentStatus.md`; evidence does not transfer between builds or artifacts.

## Resolved song convenience output

After every successful cold or warm load, the pipeline best-effort writes
`.cache/resolved-song.json`. This deterministic file uses the public
`ff7rpianosongs.song.v2` schema and contains the final visible rows. A single explicit root chart is
written as complete root `notes`; authored or MIDI profile sets are written as complete visible
`profiles`, preserving sparse labels and any rows retained beyond the 512-row descriptor prefix.
Each present side has an explicit final `monotone_note_value` or `chord_note_value`. Internal IDs,
diagnostics, source witnesses, camera state, paths, hashes, and cache-policy data are omitted.

The file is convenience output only: it is never discovered as source, never part of a cache key or
manifest, and never runtime or gameplay authority. Do not hand-edit `.cache` expecting gameplay
changes. Copy a desired `notes` array or `profiles` array into the real `song.json` instead. Copying
the complete resolved file is also valid; its explicit notes/profiles then take precedence over a
remaining `song.mid` or `song.midi`. Generated levels omitted by validation remain absent. Derived
per-profile score or mode metadata that cannot be represented honestly at the root is omitted and
is recomputed when the copied explicit profiles load.

Publication is atomic and best-effort. A missing file is recreated on the next successful warm
load without rebuilding valid authoritative caches. A render or write failure does not reject a
playable song and cannot replace a prior complete file with partial bytes.

## Failure behavior

- Invalid JSON, unknown fields, wrong types, out-of-range values, missing source files, ambiguous
  files, invalid groups/native identities/constituents, unsupported alternates/note values, and
  incompatible audio reject the song fail-closed.
- Unsupported authored charts above 512 reject; oversized generated profiles omit. Nothing is
  clipped or partially published.
- Parser acceptance of syntax (for example a `pca_*` string) is not proof of compiler/native/build
  eligibility. Final validation is authoritative.
- Cache identities include pipeline, source, generated-MIDI, row-policy, audio-policy, and exact
  native-asset capability semantics; stale artifacts rebuild or reject rather than gaining authority.

## Examples

### Minimal explicit chart

```json
{
  "schema": "ff7rpianosongs.song.v2",
  "title": "Minimal",
  "bpm": 120,
  "notes": [
    { "beat": 0, "duration_beats": 1, "pitch": "C4" }
  ]
}
```

### Dual row, chord filtering, note values, and grouping

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

Place exactly one `song.mid` beside this JSON:

```json
{
  "schema": "ff7rpianosongs.song.v2",
  "title": "Generated MIDI",
  "midi_audio_offset_seconds": 0,
  "midi_audio_alignment_seconds": 0,
  "midi_minimum_lead_in_seconds": 2
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
