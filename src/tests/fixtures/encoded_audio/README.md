# Encoded Audio Decoder Fixtures

These files are generated test signals, not music or game audio. The generator
is first-party MIT-licensed with this repository. Its generated waveform and
encoded forms are dedicated to the public domain under CC0-1.0.

Reproduction on 2026-08-01 used FFmpeg `7.1-full_build-www.gyan.dev`:

```powershell
python ./generate_fixture.py
ffmpeg -y -i source.wav -map_metadata -1 -c:a libmp3lame -b:a 128k fixture.mp3
ffmpeg -y -i source.wav -map_metadata -1 -c:a flac -compression_level 8 fixture.flac
```

SHA-256 provenance:

- `source.wav`: `ee81afb67e3bb0290ed862a3d744ab89811809f85d6d5586ec5731f8831c5bd7`
- `fixture.mp3`: `1c148d618841e1548b6f3b196d11e7ce70f4e57ed72ffe63a8c2102eac1c46f0`
- `fixture.flac`: `2bc57379745761fc488332a39501b42b26e25729d9f969cb781dedd9e7c22ba3`

The self-test decodes all three files through production miniaudio. Package
inventory rules forbid this test-fixture directory and all audio fixture files.
