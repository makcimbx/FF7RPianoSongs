# Song Authoring Starter

No music is bundled with FF7RPianoSongs. Create one folder per song from audio
and MIDI that you have permission to use. Follow
[`docs/SongFormat.md`](../docs/SongFormat.md) for the exact file names and
descriptor schema. Generated `.cache/` files are derived data and must not be
committed or distributed as song source.

Tests create synthetic audio, MIDI, and descriptors in temporary directories;
this folder intentionally contains no music bytes.
