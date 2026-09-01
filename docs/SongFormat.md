# Song Format

This document is the canonical user-authored song contract.

## Folder Layout

Create one directory per song:

```text
End/Binaries/Win64/Music/<Song Name>/
  song.json                           (authored or generated on first discovery)
  song.wav | song.mp3 | song.flac    (exactly one required base/Mode0 source)
  song.mode1.wav | .mp3 | .flac      (optional Mode1 override)
  song.mode2.wav | .mp3 | .flac      (optional Mode2 override)
  song.mid | song.midi              (optional)
```

File names and roles are fixed and case-insensitive. Provide exactly one base
source and at most one supported file for each optional override role. Different
roles may use different WAV, MP3, or FLAC formats. Multiple extensions for one
role are ambiguous and reject the folder. `song.mode0.*` is not supported because
the required base already owns Mode0. `.cache/` is generated beside the inputs;
do not author or distribute it as source material.

Every published song has a `song.json`. After discovery confirms one unambiguous
required base and validates every optional override role, a missing `song.json`
is atomically generated with schema v2, the folder name as the title, and an
explicitly enabled metronome at level `0.12`. Invalid optional roles prevent
generation. The generated file becomes normal authored input and may be edited;
discovery never overwrites an existing file. A starter file still needs
MIDI-backed timing/chart data or later authored timing and notes before the song
can pass normal validation. Manually authored JSON that omits
`metronome.enabled` retains the ordinary default of `false`.

Mode0 always resolves to the base. Mode1 and Mode2 independently resolve to their
own override when present and otherwise directly to the base; fallback never
cascades through another override. The base owns chart timing and descriptor
duration. Every override is decoded/resampled through the same 48 kHz stereo
path and must have exactly the base logical frame count. A mismatch rejects the
song; the pipeline never trims, pads, stretches, loops, or retimes an override.

A song may provide one explicit root `notes` chart, multiple explicit `profiles`, a
MIDI file, or an explicit chart plus an incidental MIDI file. Explicit charts are
authoritative and are not reduced or replaced by the MIDI generator. Malformed
existing JSON, conflicting audio inputs, and other invalid or ambiguous folders
fail closed and are omitted from the game list.

## JSON Root

Unknown fields are rejected. The root accepts these fields:

| Field | Requirement and range |
| --- | --- |
| `schema` | Required. `ff7rpianosongs.song.v2`, `v2`, or numeric `2`. |
| `title` | Required non-empty string. |
| `bpm` | Finite `30..300`. Required when timing cannot be obtained from MIDI. |
| `difficulty` | Non-negative integer used by an explicit chart. |
| `profiles` | Optional non-empty array of explicit difficulty profiles described below. Cannot coexist with root `difficulty` or `notes`. |
| `score_thresholds` | Optional non-empty, nondecreasing array of non-negative integers for an explicit chart; defaults to `[0,1200,2400,3600]`. |
| `mode_change_combo_counts` | Optional non-empty, nondecreasing array of non-negative integer combo boundaries; defaults to `[8,16]`. |
| `midi_audio_offset_seconds` | Optional finite `-1..1` second correction. |
| `midi_audio_alignment_seconds` | Optional finite `-1..1` second alignment correction. |
| `midi_minimum_lead_in_seconds` | Optional finite `0..30`; default `2`. |
| `loudness_normalization` | Optional boolean; default `true`. |
| `loudness_target_lufs` | Optional finite `-30..-5`; default `-13`. |
| `loudness_peak_ceiling_dbfs` | Optional finite `-6..0`; default `-1`. |
| `gain_envelope` | Optional ordered array described below. |
| `metronome` | Optional object described below. |
| `notes` | Optional non-empty explicit chart described below. |
| `diagnostic_extended_chart_fixture` | Reserved read-only diagnostic flag. Do not use for publishable songs. |

Minimal MIDI-backed example:

```json
{
  "schema": "ff7rpianosongs.song.v2",
  "title": "Example Song",
  "bpm": 120
}
```

## Explicit Notes

Each note permits only `beat`, `duration_beats`, `pitch`, and `chord_id`:

```json
{
  "beat": 4.0,
  "duration_beats": 1.0,
  "pitch": "C4"
}
```

- `beat` is finite, non-negative, and nondecreasing across the array.
- `duration_beats` is finite and greater than zero.
- Provide at least one of `pitch` or `chord_id`.
- `pitch` must be a non-empty supported pitch name.
- `chord_id` must be a valid native `pca_*` identifier.

Playable charts are limited to 512 rows. Inputs above that boundary are not publishable; see [Compatibility and Limitations](../README.md#compatibility-and-limitations).

## Explicit Difficulty Profiles

Use `profiles` when one song should expose multiple manually authored charts
without duplicating its folder or audio. Each profile permits exactly
`difficulty` and `notes`:

```json
{
  "schema": "ff7rpianosongs.song.v2",
  "title": "Example Song",
  "bpm": 120,
  "profiles": [
    {
      "difficulty": 0,
      "notes": [
        { "beat": 0, "duration_beats": 1, "pitch": "C4" }
      ]
    },
    {
      "difficulty": 3,
      "notes": [
        { "beat": 0, "duration_beats": 1, "pitch": "C4" },
        { "beat": 1, "duration_beats": 1, "chord_id": "pca_C" }
      ]
    }
  ]
}
```

- Provide from 1 through 32 profiles.
- Difficulty labels must be unique, non-negative integers in strictly increasing
  order. Labels may be sparse.
- Every `notes` array follows the same explicit-note contract and 512-row
  publishable limit above.
- Root `bpm` is required and shared by all profiles. Scoring thresholds, mode
  change combo counts, audio processing, gain, metronome, and MIDI timing policy
  also remain root-owned shared settings; profile objects cannot override them.
- When shared `score_thresholds` or `mode_change_combo_counts` are omitted, the
  pipeline derives each profile's values independently from that profile's action
  count and difficulty.
- The first authored profile is the default chart. Its config and compiled chart
  also remain the song's root/default descriptor values.
- `profiles` cannot coexist with root `difficulty` or root `notes`. An incidental
  `song.mid` or `song.midi` is ignored when explicit profiles are present.

## Pitch Contract

MIDI and named pitches are restricted to the playable piano range C1 through C7. MIDI drum-channel events are ignored. The generator preserves source pitches and never invents or transposes notes to force a profile.

## MIDI Contract

MIDI format 0 and format 1 inputs are supported. Format 2 is rejected. Tempo and meter events are linked into one deterministic timeline. For compatibility with common non-standard exports, a preceding channel running status may resume after a Meta or SysEx event; this exception does not extend to other system status bytes. Notes outside the pitch range, before the established audio lead-in, or after known source-audio duration do not become prompts.

Generated levels are independent profiles, can have sparse labels, and may be omitted when no valid source-backed chart satisfies the calibrated constraints.

## Metronome

`metronome` permits only:

```json
{
  "enabled": true,
  "level": 0.12,
  "beat_zero_offset_seconds": 0.0
}
```

- `enabled` is boolean and defaults to `false`.
- `level` is finite in `0..1` and must be greater than zero when enabled.
- `beat_zero_offset_seconds` is finite in `-30..30`.

When enabled, the click is mixed only into resolved MABF `Mode0`; Mode1 and Mode2
never receive it. When disabled, all modes contain their clean resolved sources.
Metronome processing does not alter chart timestamps or source-audio duration.
Cache manifests record the effective mapping and placement, so older manifests
are rejected and rebuilt.

## Gain And Loudness

`gain_envelope` contains from 1 through 64 ordered points:

```json
"gain_envelope": [
  { "time_seconds": 0.0, "gain_db": -3.0 },
  { "time_seconds": 5.0, "gain_db": 0.0 }
]
```

Each point permits only `time_seconds` and `gain_db`. Times must be finite, non-negative, and strictly increasing; gain must be finite in `-12..12` dB. Gain, loudness normalization, and limiting are applied independently to each distinct authored source. A fallback reuses processed base semantics rather than normalizing the base again. With `loudness_normalization=false`, authored relative levels are preserved subject to the configured gain envelope and existing limiter policy. Changing any related field invalidates derived cache data.

## Input Limits

- Supported source duration is at most 10 minutes after decode at 48 kHz.
- Each source audio file may be at most 512 MiB.
- A generated MABF payload may be at most 64 MiB.
- A playable chart may contain at most 512 rows.

Limits are enforced before publication. Rejected inputs do not partially enter the runtime registry.
