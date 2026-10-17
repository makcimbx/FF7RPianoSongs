# Native Music export: supported serialized profile

This is the bounded shipping 1.005 profile recovered during native cached-audio
integration, not a general Unreal property serializer. Executable identity is
owned by `src/game/rva_catalog.json`. The original control is the genuine
single-cue, three-mode export used by `native_music_export`; the source bytes
must still pass that profile's complete structural validation.

## Resource identity projection

The supported projection keeps the export and inner `SoundNameIndexTable` key
coherent with the generated Music resource. The cohort writer retargets the authenticated
package/name slots, preserving serialized FName indexes and unrelated master
entries. It places the recognized `bgm_` resource at
`/Game/Sound/BGM/<name>.<name>`; native acquisition constructs this path rather
than discovering arbitrary package basenames. The BGM lookup key is separate.
This is not a hard package reference or an AssetRegistry fallback.

## Reflected prefix

The top-level unversioned fragments are `0400 020d 0601 0213 0310`, selecting
shipping schema indexes `0, 1, 15, 17, 18, 19, 39, 56`. Their zero-mask flags
are clear; values begin at export offset `0x0a`. Shipping order is
derived-before-superclass PropertyLink order with editor-only properties
excluded, not reconstructed editor-header order.

| Export bytes | Property |
| --- | --- |
| `0x0a..0x1d` | Music SectionNameIndexTable: Name-to-int map, zero removals, one entry |
| `0x1e..0x49` | Music ModeNameIndexTable: Name-to-int map, zero removals, three entries |
| `0x4a..0x60` | Sound SoundNameIndexTable: one Name-to-SoundIndexInfo entry |
| `0x61`, `0x62`, `0x63` | PS5, XSX and SW2 streaming bools |
| `0x64..0x67` | SoundWave Volume, float32 |
| `0x68..0x6b` | SoundBase Duration, float32 seconds |
| `0x6c..0x6f` | UObject GUID-presence DWORD, zero in the control |

The nested SoundIndexInfo header at `0x5a` is `0780`; its zero mask at `0x5c`
is `05`. SoundIndex (nested index zero) and VoicedSectionAdjustmentTime
(index two) are zero-masked. Nested index one is Duration: float32 seconds
at `0x5d..0x60`. No loop or sample-count property is present. Inherited
MaxDistance and TotalSamples are absent; do not invent serialized values.

Both durations may be replaced with the finite positive logical cue duration
in seconds after validating this exact shape. They are not required to equal
padded HCA duration: the genuine control stores float32 `117.032`, while its
HCA sample arithmetic differs slightly. Use the source logical frame count
and sample rate, not reverse-fitted rounding of the old value. Preserve all
other prefix values and property masks.

## Variable bank wrapper

Let `N` be the mapped MABF bank bytes, excluding the preserved 48-byte suffix.

| Export offset | Encoding |
| --- | --- |
| `0x70` | signed image byte count `N + 16` |
| `0x74` | preserved format/version `0x10000` |
| `0x78` | preserved alignment `0x10` |
| `0x7a` | preserved padding length `4` |
| `0x80` | preserved frozen-pointer word `0x21` |
| `0x88`, `0x8c` | signed array count and capacity, both `N` |
| `0x90` | first mapped-bank byte |

The resulting export extent is `144 + N + 48`. Relocate the validated suffix
after the image and update package export-size metadata. Its first three
DWORDs are zero fixup counts, not audio samples; later fields contain cooked
serialization metadata. Preserve the entire qualified suffix rather than
claiming a generic suffix serializer. Existing MABF validation owns internal
slot offsets, bounds, HCA geometry and checksums.

## Evidence and scope

Read-only shipping Ghidra inspection recovered Music, Sound, SoundIndexInfo,
SoundWave and SoundBase native property tables, property construction/schema
iteration and unversioned fragment decoding. Runtime offsets corroborate
Music maps, Sound's map, inherited Volume/Duration and streaming flags.
Independent parsing reproduced the genuine control through `0x70`.
The inherited mapped-asset image/fixup readers establish the variable extent
and assignment of the mapped image to Sound's source field. Supported-disk
comparisons matched the inspected tables and serializer routines.

This qualifies implementing the exact source profile, not arbitrary Music
schemas, multi-cue timing, native invocation, package admission or playback.
The successful fixed-control scenario and remaining lifecycle limitations are
in [Native Cached Audio Evidence](NativeCachedAudioEvidence-20261016.md).
