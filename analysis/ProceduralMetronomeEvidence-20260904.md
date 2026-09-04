# Procedural Metronome Timbre Evidence — 2026-09-04

## Provenance

The product owner supplied two original renders for measurement from the parent
workspace's `Sources/Metronome` directory. They are reference evidence only;
their PCM bytes, transformed waveforms, and sample tables are not copied into
the product, tests, or packages.

| Reference | SHA-256 | Format and extent |
| --- | --- | --- |
| `bgm_piano_02_000 render 001.wav` | `A6662AFF1716A2E46D158A0060A7C57A8BD8B43957EFDCB098C403994BDE5D9E` | PCM24 stereo, 48 kHz, 33,798 frames / 0.704125 s |
| `bgm_piano_04_000 render 001.wav` | `A7789C95681E5E5136F624A5267B68F735EDC814E56853894303967C40C66C7C` | PCM24 stereo, 48 kHz, 35,769 frames / 0.7451875 s |

The `_02` render measured approximately -6.4 dBFS peak, -32.07 dBFS RMS,
-24.2 LUFS, and -6.2 dBTP. The `_04` render measured approximately -6.0 dBFS
peak, -32.20 dBFS RMS, -24.1 LUFS, and -5.8 dBTP. Their central 90% energy
spans were approximately 39.4 ms and 40.0 ms; central 99% spans were about
163 ms and 158 ms. Dominant shared structures appeared near 2.09, 2.04,
1.95, 1.03, 0.50, 0.10, and 0.05 kHz.

After a 17-frame alignment, the two mono renders had correlation 0.9726,
first-100-ms correlation 0.9745, 1-ms-envelope correlation 0.9979, and a
best-fit gain difference near -0.002 dB. This supports one shared source/timbre
family. It does not establish that the files are an ordinary/downbeat pair.

## Implemented Procedural Boundary

The deterministic synthesis uses damped analytic resonances at the measured
shared structures, a short band-limited procedural strike, and a small
mid/side procedural component. It contains no reference samples. The voice is
bounded to 180 ms: long enough to retain the measured 99% energy region and
short enough to leave a gap between beats at the supported 300 BPM maximum.
The established 1.25 downbeat gain remains; downbeats and ordinary beats use
the same timbre because no separate native pair was established.

Focused 48 kHz output measurements for the retained ordinary voice are:

- spectral centroid approximately 1.601 kHz;
- 1.75–2.25 kHz sampled power ratio approximately 0.688;
- below-800-Hz ratio approximately 0.252 and above-4-kHz ratio approximately 0.0073;
- central 90% mono-energy span approximately 43.4 ms;
- central 99% mono-energy span approximately 151.5 ms;
- stereo correlation approximately 0.970 and mid-energy ratio approximately 0.985.

These are source-derived guard ranges, not waveform-equivalence claims.

## Scope And Nonclaims

The stems map to different vanilla songs and `_000` is non-player backing.
No independently triggerable native click sample, native beat/downbeat pair,
or runtime sample-reuse path is established. This work qualifies deterministic
offline Mode0 synthesis and cache invalidation only. It does not claim exact
waveform recreation, perceptual identity, in-game listening qualification, or
permission to redistribute the supplied renders.
