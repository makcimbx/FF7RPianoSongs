# Stock HUD font fields — 2026-10-23

Integration, static/read-only only. Default program `ff7rebirth_.exe-9457d4`,
project path `/ff7rebirth_.exe-1.005`, base `0x140000000`. Installed executable
SHA256 `752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc`,
124317952 bytes. No live reads, loads, attachment, font mutation or new hooks.

## Independent typed layout and stock load path

- Class parameters at `0x145ea0860` name Engine and reference property-pointer
  array `0x1462d1270`; class thunk `0x1427e9518 -> 0x1422ae504` registers
  `/Script/Engine.Engine`, size `0xe48`.
- MediumFont property record `0x1462d3af0`: name pointer `0x1462d0540`,
  object-property kind `0x12`, array dimension 1, field offset `0x70`,
  type thunk `0x144aa35f8`.
- LargeFont record `0x1462d35f8`: name pointer `0x1462d0520`, same kind,
  dimension and type thunk, field offset `0x90`.
- Type thunk `0x144aa35f8 -> 0x1450a22b4` registers `/Script/Engine.Font`,
  size `0x1d0`. These are typed UFont object fields, not guessed neighboring slots.
- Stock initializer `0x144ffd978` calls helper `0x1422a9970` at
  `0x144ffead7` with destination engine+`0x50` and configured name engine+`0x58`;
  at `0x144ffeb09` with destination+`0x70`, name+`0x78`; at `0x144ffeb41`
  with destination+`0x90`, name+`0x98`. Name conversion is `0x140cedcf8`.
- Helper `0x1422a9970` returns if destination already populated; otherwise uses
  `/Script/Engine.Font` class global `0x149176bf0` and native loader
  `0x14134feec`, assigning its returned font to the destination. Missing/failed
  configured names invoke the supplied fallback. This implementation invokes
  none of these functions: it only borrows already-populated typed fields.

`song_hud_engine_font_fields` authenticates the entire instruction-complete
89-byte medium/large call-site window `0x144ffeaed..0x144ffeb45`. Its exact bytes
are canonical in the catalog. One match each in the named Ghidra import, raw
disk (offset `0x04ffe0ed`) and PE-mapped disk (RVA `0x04ffeaed`); raw and mapped
checks used the SHA above. No older-build equivalents were investigated here.

## Runtime policy and residual appearance uncertainty

Compare admitted Small/Medium/Large unit `Mg` heights; select the largest real
measurement (Small wins ties). Do not assume Large is larger, load assets, alter
UFont/Canvas, or retain font pointers beyond the synchronous render frame.
Recheck engine field, object identity and complete frame authority around each
measurement and through the authority passed to every native item operation.
Retain actual selected-font unit widths and metadata/fallback-glyph height.

The runtime scale predicate rejects magnification of improved stock sources;
the owning native adapter must call it with the final uniform draw scale.
If no measured improvement is available, preserve admitted SmallFont behavior
and emit the bounded `small_font_fallback_blur_unresolved` diagnostic.

Exact configured asset names and atlas ink resolution are not established by
these static records. Larger unit metrics are a defensible candidate, not proof
of sharper rasterization. Manual metadata sharpness, Unicode, opacity, icon
spacing, UI scaling and immediate pause/resume remain required.
