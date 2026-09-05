# Exact 1.005 Monotone Assignment Inventory

Bounded offline extraction on 2026-09-05; Integration support against requested
baseline `c1058c4fc98addf852bce5de180c4c4fc0a31ccd`. This is independently
recovered file-data evidence, not runtime qualification or a catalog migration
claim. No game/debugger operations, production changes, or shared builds occurred.

## Measured Provenance

Installed game root: `D:/SteamLibrary/steamapps/common/FINAL FANTASY VII REBIRTH`.
SHA-256 values below were freshly measured, not copied from the catalog.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `End/Binaries/Win64/ff7rebirth_.exe` | 124317952 | `752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc` |
| `End/Content/Paks/pakchunk3-WindowsNoEditor.utoc` | 3094682 | `c7efec9da756b390004debc4524b9a4bdba81e90c8241fd2217a98d826443a12` |
| Extracted `PianoMonotoneAssign.uasset` | 13120 | `c13cc1b83eb0e7cd17c50e92d89c9e5db6fb8360fc2c1c4e878f18361724e8f4` |

The executable also has PE timestamp `0x6a16ced2`, SizeOfImage `0x099d9000`,
checksum `0x0769ea6e`: exact catalog `ff7rebirth-steam-win64-6a16ced2` (1.005).
Executable and UTOC identities were rechecked after successful analysis.
This identifies the archive index and extracted payload; the entire UCAS file
was not hashed, and installed mod/overlay precedence was not investigated.

Fresh `retoc manifest` returned package
`/Game/DataObject/Resident/PianoMonotoneAssign`, chunk
`249d0bdce1e6011600000002`, filename
`../../../End/Content/DataObject/Resident/PianoMonotoneAssign.uasset`, no bulkdata.

## Recovery And Comparison

Used only existing parent-workspace tools (paths relative to `rebirth-mods`):

- `Tools/ThirdParty/retoc-ff7r/retoc.exe`, SHA-256
  `ebcfab4c29f96413d74a23c17b38851c0607ced1adb72991969d1eec37cd3a61`;
  pinned README identifies FF7R branch commit `234cd0d86e5d2db21d187b8133efea456737802e`.
- Its existing `oo2core_9_win64.dll`, SHA-256
  `6f5d41a7892ea6b2db420f2458dad2f84a63901c9a93ce9497337b16c195f457`.
- Unmodified `Tools/decode_ff7r_piano_dataobject.py`, SHA-256
  `92da72d0e46d62885595ee6d889af99649796a099ea1a0b1f53312be39d339db`.

From a verified temporary output directory, run `retoc manifest <UTOC>`, then
`retoc get <UTOC> 249d0bdce1e6011600000002 <temp>/PianoMonotoneAssign.uasset`.
Decode with the existing script's `decode_rows` function (Python `-B`), or its
`--out <temp>/decoded.json <temp>/PianoMonotoneAssign.uasset` CLI.
**Preserve the basename:** schema selection uses the filename stem. An initial
`PianoMonotoneAssign-1.005.uasset` invocation produced unsupported-property
placeholders and failed comparison; it is not evidence. Correct-basename decoding
consumed row headers 2720 through 7240, with all eight properties decoded.

Compared against `FF7R2UProj/Saved/AssetDumps/piano_dataobject_decoded.json`,
historical `PianoMonotoneAssign` table (lines 15477-17001). That JSON's measured
SHA-256 is `9925494af21ef30db0c9534151cb1d302e306e03d4ded7813f11ce9e26f7e571`.
Normalize each row by removing only decoder `__*` offsets, preserving RowName and
all eight property fields, then sort by RowName. **All 113 rows compare equal.**
Canonical JSON (`sort_keys=True`, separators comma/colon, `ensure_ascii=False`,
UTF-8, no newline) of either normalized row list has SHA-256
`7a0e0bb5369b5c3fae9a3944ba30e76628b0c91f22bb1255e8176cc69a5d7214`.
The extracted payload hash also equals the historical asset hash recorded in
`FF7R2UProj/Mods/FF7RPianoSong/Docs/FullTableOverrideStatus.md:174-188`;
the historical binary itself was unavailable for a direct byte comparison.

Temporary extraction, manifest, decode, comparison script and summary are retained
under `C:/Users/makci/AppData/Local/Temp/opencode/ff7rp-monotone-20260905-audit/`.
No binary or full decoded table is copied into the native product repository.

## Exhaustive Inventory And Boundary Findings

The table has 113 unique rows, 113 keys, zero free indices. Its exact row-name set
is 17 ordinary pitch prefixes (`Cn Cs Db Dn Ds Eb En Fn Fs Gb Gn Gs Ab An As Bb Bn`)
at each octave 1-6, ten alternate rows, and the terminal ordinary `Cn7` row.

- **Alternates are exactly `Cn2_2` through `Cn6_2` and `Cs2_2` through `Cs6_2`.**
  Each has KeyAssign **7**, Semitone **0** for C / **1** for C-sharp, and SoundName
  equal to its ordinary counterpart (`CnN` / `CsN`), whose KeyAssign is **0**.
  TextID uses `$minigame_piano_C2` / `$minigame_piano_C2_sharp` rather than the
  ordinary C text. Alternate OctaveInt/socket refer to the preceding register;
  these are distinct native assignments preserving the sound reference.
- **`Cn1_2` and `Cn7_2` do not exist.** Ordinary `Cn1` has KeyAssign 0,
  Semitone 0, SoundName `Cn1`, OctaveInt -2, SocketName `Attach_KeyB`.
- **Ordinary `Cn7` already has KeyAssign 7**, Semitone 0, SoundName `Cn7`,
  TextID `$minigame_piano_C2`, OctaveInt 3, SocketName `Attach_KeyG`, HandType 2.
  There is no separate default/alternate row pair at octave 7.
- **No `Cb*`, `Cb*_2`, or `Bn*_2` row exists.** Ordinary `Bn1`-`Bn6` have
  KeyAssign 6, Semitone 0, and matching B sound names. They are not native
  C-flat alternate identities. Nor is there a KeyAssign 7 / Semitone 2 row.
- `Cs1_2`, `Cs7`, and `Cs7_2` are also absent.

These are full-table membership findings, not merely failed text searches.

## Implementation Boundary And Nonclaims

For exact 1.005, a distinct alternate monotone identity is supported by this
inventory only for resolved natural C2-C6 and exact sharp C#2-C#6. An emitter
producing `Cn1_2` or `Cn7_2` is not supported by this native assignment table.
Keep ordinary C7 mapped to `Cn7`; do not invent its alternate, silently treat an
alternate request as default, or substitute ordinary B for C-flat alternate.
No accepted C-flat alternate range/native ID can be justified from these assets.

This supports a bounded pipeline identity-range correction for exact 1.005;
it does not authorize native memory mutation or new game addresses. Affected
catalog identity is `ff7rebirth-steam-win64-6a16ced2`; no RVA entries changed.
Exact 1.004 monotone assets were not available or independently verified.
Historical equality does **not** establish 1.004 provenance, and the separate
chord parity evidence cannot grant monotone parity for `...68fd6fde`.

KeyAssign 7 and the high-C TextID establish the asset-level high-C assignment;
physical controller direction, native lookup success in a running scene, sample
playback, judgment, grouped followers, and extended-tail behavior were not tested.
Absence is scoped to this stock assignment table, not every possible FName or
asset elsewhere in the game. Follow-up is a focused lead-owned compiler boundary
regression and, separately, exact 1.004 monotone extraction if parity is needed.
