# Exact `pca_Db` Chord Evidence (Builds 1.004 And 1.005)

This is a bounded offline asset-evidence record, not human runtime qualification.

## Scope And Identity

- Exact executable/catalog: `ff7rebirth-steam-win64-6a16ced2` (Steam 1.005),
  executable SHA-256
  `752807180C4ED919667FF0EC163046410187E873888DD75248E78AAA72E363DC`.
- Archive: `pakchunk3-WindowsNoEditor.utoc`, SHA-256
  `C7EFEC9DA756B390004DEBC4524B9A4BDBA81E90C8241FD2217A98D826443A12`.
- Package `/Game/DataObject/Resident/PianoChordsConfig`, chunk
  `d76ff057ae10146600000002`, extracted `PianoChordsConfig.uasset`: SHA-256
  `B362E69D2E0E3A274EBC1B7B75142E0AE67AC420FF3E2BA0030A3DD84482D8EE`.
- Package `/Game/DataObject/Resident/PianoChordsAssign`, chunk
  `f100ac0e2fa93fe000000002`, extracted `PianoChordsAssign.uasset`: SHA-256
  `D2FBCD18D5357753258FB815B836B82FB3086FBBE90E2A711FFC0C77C7810AA8`.
- The user attests that `Sources/1.004/PianoChordsAssign.uasset` and
  `Sources/1.004/PianoChordsConfig.uasset` were extracted from exact Steam
  build 1.004 and that piano content did not change between 1.004 and 1.005.
- The exact 1.004 `PianoChordsAssign.uasset` is 19,280 bytes with SHA-256
  `D731249203881AD63F231F1C2D388B7EFB67C8FBAB98A8C9CDA6660D14B77F8A`.
  Its `pca_Db` frozen row header is 20 bytes at row index 46 and has SHA-256
  `5E0A8FD7201E9D0D45124AF4A6DF7223AEAA40CE6E3EF6D7AD2479A6664BF619`.
- The exact 1.004 `PianoChordsConfig.uasset` is 35,072 bytes with SHA-256
  `1107E2C32B6E111832208C2E2B4EEFA1766C244F67E3DFD5A7FC913B4363F66A`.
  Its `Db` frozen row header is 48 bytes at row index 12 and has SHA-256
  `4E347E5AD251D15A43026893FBDF5DCF9025E2F895168BADA853CB597907088E`.
- Completed decoding compared all 170 assignment rows and all 170 config rows:
  their semantics are equal between exact 1.004 and 1.005. The frozen content
  and patch regions used by the decoded rows are byte-identical as well.

## Recovered Rows

On both exact builds, the decoded assignment row `pca_Db` has ChordID `Db`, KeyAssign `1`, Semitone
`2`, Minor `0`, and PageIndex `2`. The authoritative `PianoChordsConfig` row
`Db` has ordered `SoundName_Array = ["Db2", "Fn2", "Ab2"]`, velocity `75`
for every sound, and TextID `$minigame_piano_code_Db`.

The enharmonic assignment is not interchangeable: `pca_Cs` uses the distinct
ordered row `["Cs2", "Fn2", "Gs2"]`. Offline compilation and IgnoreSound
validation must therefore retain the selected chord ID's exact names.

Tracked vanilla scores repeatedly use exact `pca_Db`. Six rows suppress
`Fn2`; fourteen rows suppress both `Fn2` and `Ab2`. These uses independently
cross-check that those exact case-sensitive constituents participate in native
IgnoreSound behavior.

## Disposition And Nonclaims

This evidence authorizes the offline verified-constituent row and deterministic
MIDI selection of `pca_Db` when the compile-selected generated catalog is exact
1.004 or 1.005 and the source has one unambiguous flat key-signature context.
Both builds use the shared cache identity
`native_assets=pca_Db_voicing:verified1004+1005`. Unknown catalogs retain
`pca_Cs` and cannot use this row to validate IgnoreSound. Runtime FName lookup
remains fail closed. This record does not claim protected human runtime
qualification of `pca_Db` on build 1.004.
