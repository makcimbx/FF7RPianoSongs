# Mode-Specific Audio Design

## Status

This is an approved implementation design, not the current song-authoring
contract. Until the pipeline implementation lands, [Song Format](SongFormat.md)
continues to require exactly one `song.*` audio source.

## Authoring Contract

The implemented format will accept these fixed, case-insensitive roles:

```text
song.wav | song.mp3 | song.flac                 required Mode0/base source
song.mode1.wav | .mp3 | .flac                  optional Mode1 override
song.mode2.wav | .mp3 | .flac                  optional Mode2 override
```

Exactly one supported file is allowed for each role. Multiple extensions for
one role are ambiguous and reject the song. There is no separate
`song.mode0.*`: the required `song.*` already owns Mode0.

Each mode resolves directly against the base source:

| MABF mode | Resolved source |
| --- | --- |
| Mode0 | required `song.*` |
| Mode1 | `song.mode1.*` when present, otherwise `song.*` |
| Mode2 | `song.mode2.*` when present, otherwise `song.*` |

Fallback never cascades through another override. Adding a Mode1 file cannot
silently change Mode2. Existing one-file songs therefore remain valid and keep
the same source audio in all modes.

## Processing Contract

- Decode every resolved source through the existing supported WAV, MP3, or
  FLAC path. Different roles may use different supported source formats.
- The required base source remains the one authoritative chart timeline and
  descriptor duration.
- Every override must resolve to the exact same 48 kHz stereo logical frame
  count as the base source. Reject mismatches; do not trim, stretch, loop, or
  pad them implicitly.
- Apply the authored gain envelope and loudness policy independently to every
  resolved mode source. Authors who need to preserve deliberate relative
  levels can disable loudness normalization and author those levels directly.
- When enabled, mix the metronome only into resolved Mode0 after source
  selection and before final HCA encoding. Mode1 and Mode2 never receive it.
- Encode three HCA payloads with equal timing geometry and assemble one MABF.
  Mode payload bytes may differ; equality is not a validity requirement once
  explicit overrides are supported.

## Cache And Validation

The cache key must include, in fixed mode order:

- every present source file and its role;
- explicit direct-fallback markers for absent overrides;
- the resolved metronome placement and audio-processing policy;
- the existing chart and semantic configuration identity.

The implementation must advance pipeline cache identity so older one-source
artifacts cannot masquerade as mode-aware artifacts. The manifest must record
the resolved filename and fallback state for Mode0, Mode1, and Mode2, plus the
resulting mode geometry. Aggregate MABF structural and digest validation stays
authoritative.

The runtime-cache binary shape does not need to change if source-role facts
remain offline-only. If they are serialized for diagnostics, its writer,
reader, version, and round-trip tests must advance together.

MABF validation must use the resolved mode policy, not infer payload equality
from `metronome.enabled`. It must continue to reject malformed slots, unequal
logical frame counts, unsupported HCA geometry, bad offsets, and inconsistent
container metadata.

## Runtime Boundary

No game hook, RVA, registry, selection, profile, or lifecycle change is
required. Runtime continues to load one validated `song.mabf.bin`, publish one
descriptor duration, and hand the complete MABF allocation to SQEXSEAD. The
game keeps selecting Mode0, Mode1, or Mode2 through its native adaptive tier.

## Implementation Ownership

The future implementation is one offline-pipeline batch:

- strict role discovery and fallback resolution in `song_repository`;
- offline role/source facts in pipeline song types;
- explicit resolved Mode0/Mode1/Mode2 inputs in `audio_artifact_builder`;
- policy-aware MABF and runtime-artifact validation;
- cache identity and manifest updates;
- the current user contract in `SongFormat.md` only after support exists.

Focused tests must cover one-file compatibility, each override independently,
all three distinct sources, mixed source formats, ambiguous role rejection,
duration mismatch rejection, direct fallback, Mode0-only metronome placement,
cache invalidation, deterministic MABF output, and unchanged runtime descriptor
shape. In-game qualification should then traverse Mode0 to Mode1 to Mode2 and
confirm the expected audible source for every tier.
