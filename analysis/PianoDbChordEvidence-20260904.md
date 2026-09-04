# Exact `pca_Db` Chord Evidence (Build 1.005)

This is a bounded offline asset-evidence record, not runtime qualification or a
claim for another executable or asset revision.

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

## Recovered Rows

The exact assignment row `pca_Db` has ChordID `Db`, KeyAssign `1`, Semitone
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
MIDI selection of `pca_Db` only when the compile-selected generated catalog is
the exact build identity above and the source has one unambiguous flat
key-signature context. Build 1.004 and unknown catalogs retain `pca_Cs` for
automatic pitch-class-1 major harmony and cannot use this row to validate
IgnoreSound. The selected asset-capability identity participates in every JSON
and MIDI repository cache key. Runtime FName lookup remains fail closed.
Asset-byte equality for build 1.004 has not been established, so this record
does not qualify `pca_Db` publication on 1.004 and does not claim protected
runtime validation on 1.005.
