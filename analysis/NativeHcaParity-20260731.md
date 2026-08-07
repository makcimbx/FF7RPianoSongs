# Native HCA/MABF Parity Evidence — 2026-07-31

## Disposition

This run closes the release blocker for **representative matching-build native structural, metadata, integrity, and independently decoded-content parity**. It covers all eight installed piano MABFs structurally and three materially different durations through matched project generation and independent decode. It does not claim compressed-byte identity, complete CRI encoder equivalence, loop support, or in-game acceptance.

Two production defects were found and corrected: dynamic MABF slots used the wrong metadata/padding split, and the encoder prepended 128 silent frames in addition to declaring the decoder-trimmed 128-sample analysis delay. Pipeline cache identity advances from v38 to v39. No game audio or decoder binary is retained in Git or a package; temporary inputs and outputs remained under a private temporary workspace outside the repository.

## Reproducible tool provenance

- Installed source: `<game-directory>/End/Content/Paks/pakchunk8-WindowsNoEditor.utoc` and matching `.ucas`.
- Inventory/extraction: repository-pinned [retoc](https://github.com/trumank/retoc) FF7R branch, `retoc 0.1.0`, executable SHA-256 `ebcfab4c29f96413d74a23c17b38851c0607ced1adb72991969d1eec37cd3a61`; its repository README/LICENSE provide provenance and license.
- Auxiliary extraction: AudioMog v2025.02.16.01, SHA-256 `7865a294d1a90a0417d4960a22a78964c119e54d20f33a578df11a2d4e6ec85c`. Its missing local upstream/license record prevents treating it as the sole reference path.
- Independent licensed decoder: [VGAudio](https://github.com/Thealexbarney/VGAudio) 2.2.1, tag `v2.2.1`, commit `9c40e30baf4b5ab66dac07522adef4192d9a2e19`, MIT, as pinned in `THIRD_PARTY_NOTICES.md`. NuGet package SHA-256: `5ba8acb67eb546f4700fcc2e9ac205673284598c1e47e8dbeefb2a656bf9e2f0`; loaded `VGAudio.dll`: `e32c17ed422f5e6419050fe4bb43f752ed11c8a7796c45f693865237d8a200a3`.
- Temporary decoder wrapper: net8.0 source using `HcaReader.Read` and `WaveWriter.WriteToStream`; `Program.cs` SHA-256 `fa39b9d64805823c0171a94d70a65a87ecec8859a9e310e79de9fb70224312c2`, project SHA-256 `b3a98e6f06f4f644e05f1cd41ff30b8bb28d43ff79aec336b5d58875ba049b74`, built DLL SHA-256 `ca592aaf319303750a3ffc7b38215c7f3f1e3aa8ca5e9752c614101192d26fa4`. Built with .NET SDK 10.0.302 targeting net8.0 and the exact NuGet package above.

Representative commands, with paths abbreviated only here:

```text
retoc.exe list --path --package pakchunk8-WindowsNoEditor.utoc
retoc.exe get pakchunk8-WindowsNoEditor.utoc <chunk-id> <temp>/bgm_piano_0N.uasset
AudioMog.exe <temp>/bgm_piano_0N.uasset
dotnet build VgaudioDecode.csproj -c Release
dotnet VgaudioDecode.dll <mode.hca> <decoded.wav>
hca_parity_fixture.exe <clean.wav> <output.mabf.bin> [<mode0.wav> <mode1.wav>]
python tools/hca_parity_report.py inspect <asset> --offset 0x430 --extract-dir <temp>
python tools/hca_parity_report.py compare <source.wav> <decoded.wav>
```

## Native inventory and bounded loop search

`retoc list --path --package` inventoried all `bgm_piano_01..08` ExportBundleData entries in the installed matching-build container. All eight were extracted; all 24 HCA headers and all eight AudioMog rebuild records were inspected. Every HCA was HCA v2, 96-byte header, 48 kHz stereo, 682-byte frames, 128 inserted samples, and contained `fmt`, `comp`, unencrypted `ciph=0`, and `pad` chunks. No `loop` chunk was present; all rebuild records had `LoopStartSample=0` and `LoopEndSample=0`. Loop parity is therefore **unsupported/not observed in the exhaustive eight-piano-asset scope**, not proven for arbitrary game audio.

Seven assets use the production-targeted standard MABF profile with Mode0 at `0x460`; song 04 uses an observed extended native header profile with Mode0 at `0x4f0`. Its mode-offset fields are correspondingly shifted by `0x90`. The report tool recognizes both profiles explicitly; the builder continues to emit only its proven standard scaffold and does not claim the extended header as an output profile.

| Song | IoStore chunk | Uasset SHA-256 | Embedded MABF SHA-256 | MABF bytes | Frames | Logical samples | Appended | Mode prefix bytes |
|---|---|---|---|---:|---:|---:|---:|---:|
| 01 | `80bd4180ba7f81e100000002` | `707fcad468b95724d09031fdfe83c2103c1e7a7aa1330822e8bc973992f53559` | `ed02b367fb26eae0898716ba78f50e790ee9281b8a1484a7ffc3e5630bdb2f2d` | 11,227,984 | 5,487 | 5,617,547 | 1,013 | 12 |
| 02 | `1b48c4dcb35d8fe500000002` | `736febe9ef3c1b135e717fe5261d30a208a6eee8b91385dde4c6c22e6c1d3479` | `a1e5301fbe88dafb18fa0700bc3e929c61f7124f10ffefce6883eef133f3a977` | 12,948,640 | 6,328 | 6,478,726 | 1,018 | 2 |
| 03 | `7506f95475f7b74200000002` | `d5a35eec85d6699f3fc0966b87c2758a47c25048f8f3c45e7dc5aaa11c3ccfbd` | `e14daf149b5dcb880d8b274dde98bf6c498ebfd31900030392b73f47010a48eb` | 10,747,168 | 5,252 | 5,377,780 | 140 | 10 |
| 04 | `0aeb573405afb2ff00000002` | `9aec07f38a8892932d147b219a796bf052097e6559eef7d8df88d4d9e87f5b07` | `c213b6f2d7f24462d04cf4f53dd0079c02e39e281e2c3236049dca54ec5d574b` | 10,286,944 | 5,027 | 5,147,402 | 118 | 4 |
| 05 | `4cced788f18e994700000002` | `e2378a05b3396591289176ef66cb21d43c6e08a359579e254bee154b7d9c8b7f` | `7100a9a016b32097f2ddaf872692050d3d6e13e9cf8d41232a4ff2f49137daad` | 11,643,328 | 5,690 | 5,826,024 | 408 | 14 |
| 06 | `4c2b3b9a63b6386900000002` | `f406762b9e814c5ed39ab619f9b2b05bdd41d389ee170f39f458de053a71bf7b` | `a8ab4ff82065eafdae14619f2915fa91893e3f315917c31fac83a85a7395588b` | 12,281,680 | 6,002 | 6,145,536 | 384 | 14 |
| 07 | `45ee59e918098c5800000002` | `1b24dc147c95676292b5b5b16dd2f59b888c618068501765899db2031be57ac3` | `bd0eea9c2111978c32b5fdcc5f9daa5258fd0c6197196c527e810a2612fc4a4d` | 9,789,616 | 4,784 | 4,897,781 | 907 | 2 |
| 08 | `2a8b50e0a6cc5ad800000002` | `12ba42d152629de9e71d8a1b3df1716696c05a8e9b1f09228792b9a060d57689` | `db67c62835eac45e7d55d93a7901b857c5fe18bb4a7f3661b885aff8cb14cb1b` | 12,469,888 | 6,094 | 6,240,105 | 23 | 6 |

The three modes in each asset have equal HCA geometry except native song 02, where Modes1/2 decode to one more sample than Mode0. The production adaptive builder correctly rejects unequal guide durations; the matched song-02 project fixture therefore used clean Mode0 for all three generated modes.

## Integrity and MABF semantics

`hca_parity_report.py` independently applies the HCA CRC-16 polynomial `0x8005` to each complete header/frame. All 24 native headers passed and all 133,992 native frames passed; no invalid frame was observed. For the three generated representative MABFs, all nine headers and all 53,727 frames passed. Unsupported encrypted or looped chunks were absent.

Native slots end with a mode-specific **1–16 byte alignment prefix** followed by a 46-byte metadata suffix. Mode0 and Mode1 prefixes are zero except for an exact trailing `01 00`; Mode2 prefixes are entirely zero. All 24 native slots across the eight assets obeyed this uniform contract. Mode0/1 therefore require a fail-closed minimum two-byte prefix. The old report incorrectly called the complete prefix zero padding. Observed prefix lengths were 2, 4, 6, 10, 12, and 14 bytes. The historical fixed 663,682-byte profile has a 16-byte prefix (`00` repeated 14 times followed by `01 00` for Mode0/1, 16 zero bytes for Mode2) plus the same 46-byte suffix, preserving its exact 62-byte trailers and container size.

Representative native Mode0/1/2 MABF-relative offsets, slot sizes, prefix sizes, and suffix sizes were: song 01 `0x460/0x391eb0/0x723900`, 3,742,288-byte slots, 12-byte prefixes, 46-byte suffix; song 02 `0x460/0x41df20/0x83b9e0`, 4,315,840-byte slots, 2-byte prefixes, 46-byte suffix; and song 08 `0x460/0x3f6fc0/0x7edb20`, 4,156,256-byte slots, 6-byte prefixes, 46-byte suffix. The alternate song-04 profile uses `0x4f0/0x3455c0/0x68a690`, 3,428,560-byte slots, 4-byte prefixes, and the same 46-byte suffix. All slot ends were 16-byte aligned. Correcting scaffold extraction also restored the dynamic next-HCA payload field to native metadata-relative offset `0x16` (absolute fixed-trailer offset `0x26`).

Mode0/1/2 HCA SHA-256 values for the three decoded representatives:

- Native 01: `1dc122e91520851855ec22b7acba1c5b02ba40c29712a579314eb91d3e4aec30`, `ebc1a4bd41214c69f47e39bed45500c60ea37bbb71e237702a19a8b60246cc7d`, `db7bf0e04aef8c61bf55447b02fe485e81c79937b545fce3e6b989c213a0a196`.
- Native 02: `7e1c3f7d4c210bcbec80c805478e9d88549f7a845c8ecae8c2cc1e1c3f844519`, `7e5f97b57d16332784e173c6d8e17b0127beaa288b16b966985e1e1c68807d57`, `11942d2fb09d4d07ffaec7566c9e495f1fa9b61ae13722d3111b2996f87bfdc1`.
- Native 08: `d042edad4a6f9e50c1d6350a239418ccc5b6f76c6e16d5342e97e8f8f53bfc4a`, `9ad63fa2c1e85643e2264f1ac132446cd05fea4e2a52e27af01cfbd489199c95`, `3f1498fe1f067c248950314fe77f7667271dd29de4df8ab3c83ce669adf63d28`.
- Generated 01: `ca0b3689a85b8e201b49a0ae838656aac62a60d1c5e6e1ea693437ea870fefa7`, `6acc5e8fdfc8a81ca64f50427a139cb661db6eb377a1def39c6d56f53b7b7686`, `7e314f93268a9876146255b89fd435329275a4a96a9ca67bfd6e59da2397c736`.
- Generated 02 clean: all modes `c0d1242d679f7da4b94b05f8bdec805442802efa9542dd298b84bb7a5c2c029d`.
- Generated 08: `fc7e0d045ce96b4104c68f20155212494e5125b173c479168cb0d10d5fde6e59`, `8baaff71c0ecdf404d101c813dbfdc73e0de52bfc6bdc93d7417ac2f59537fab`, `ebc570312c58ee838abb780374334ef1f2bcf49562d267d94bb3222089456cf3`.

Final generated MABF SHA-256 values after the mode-prefix correction were song 01 `eb2417ab0f7a58062250b3a0467228e80e317959c8684caac5f2d7cd9fdcea76`, song 02 `09e7f468e74b8c33ed4a20cbde791b665f936aa472e2b7d44d557d232855f49a`, and song 08 `ea7816ba1d51f41fd7d01497a01e697a7c1eb1141edcad7795653b7de69b7d3f`. Their sizes exactly matched the corresponding native MABFs. Song 01 was generated twice and was byte-identical across runs. HCA payload hashes were unchanged, so the pinned decoder PCM metrics remain applicable; the final reports revalidated all nine HCA headers and all 53,727 frame CRCs. Compressed-byte equality with native is neither expected nor claimed.

## Independent decode and declared tolerances

Before judging the corrected output, the bounded lossy-transcode acceptance thresholds were set to: exact channel/rate/logical frame count; correlation at least 0.9999; peak error at most 0.03 full scale; RMS error at most 0.001 full scale; SNR at least 40 dB; and a bounded spectral probe mean absolute difference at most 1.5 dB and maximum at most 20 dB. The spectral probe uses eight evenly spaced 2,048-frame mono windows and direct DFT measurements at ten fixed frequencies from 125 Hz through 20 kHz. The wide maximum bound accounts for ratios near low-energy spectral nulls; the mean, RMS, and SNR are the primary aggregate quality measures. A lossy HCA re-encode is not expected to be PCM- or compressed-byte-identical.

VGAudio independently decoded every native and generated representative mode. For native 01, 02, and 08, VGAudio output was sample-for-sample identical to AudioMog output in all nine comparisons, replacing AudioMog-only trust.

| Fixture/mode | Output frames | Peak FS | RMS FS | SNR dB | Correlation | Spectral mean/max dB |
|---|---:|---:|---:|---:|---:|---:|
| 01 Mode0 | 5,617,547 | 0.020355 | 0.000550 | 43.793 | 0.999979 | 0.955 / 18.648 |
| 01 Mode1 | 5,617,547 | 0.004639 | 0.000232 | 51.828 | 0.999997 | 0.558 / 11.542 |
| 01 Mode2 | 5,617,547 | 0.008606 | 0.000358 | 51.884 | 0.999997 | 0.801 / 12.004 |
| 02 clean Mode0/1/2 | 6,478,726 | 0.018402 | 0.000577 | 42.934 | 0.999975 | 0.883 / 14.417 |
| 08 Mode0 | 6,240,105 | 0.023285 | 0.000713 | 43.739 | 0.999979 | 0.916 / 7.581 |
| 08 Mode1 | 6,240,105 | 0.007813 | 0.000318 | 52.870 | 0.999997 | 0.631 / 10.182 |
| 08 Mode2 | 6,240,105 | 0.009277 | 0.000481 | 51.622 | 0.999997 | 0.305 / 6.993 |

Every declared threshold passed. The initial generated decode was exactly 128 stereo frames late; inspection found that the encoder both wrote `inserted_samples=128` and prepended 128 silent PCM frames. VGAudio correctly trims the declared MDCT delay, so the extra silence double-counted it. Removing the explicit input shift restored exact frame timing and produced the metrics above.

## Bounded conclusion

- **Passed for the representative scope:** inspection of all eight matching-build piano MABFs, including one alternate native header profile; three distinct standard-profile durations through independent native/generated decode; exact logical frame accounting and trim; metadata/chunks; all header/frame CRCs; native mode semantics; deterministic generation; fixed-profile compatibility; and variable-cache invalidation through v39. Project generation remains intentionally limited to the proven standard scaffold.
- **Unsupported/not observed:** loop chunks or nonzero loop metadata in the complete eight-piano-asset scan. The encoder does not claim looped-HCA support.
- **Separate from this closure:** in-game acceptance/lifecycle and any broader publication/license decision.

The Native-HCA parity blocker is closed only under this representative structural/metadata/decode claim. This evidence does not establish CRI compressed-byte equivalence or universal parity across arbitrary game HCA profiles.
