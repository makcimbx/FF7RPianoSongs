# Startup Cache Overlay Native Canvas Evidence (v1, 2026-08-01)

This record preserves the bounded matching-build provenance for the optional
native UE4 startup-cache overlay. It does not establish release readiness or
general-purpose Canvas ABI compatibility.

## Executable And Analysis Identity

- Catalog build: `ff7rebirth-steam-win64-6a16ced2`.
- Executable SHA-256:
  `752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc`.
- PE timestamp: `0x6a16ced2`; image size: `0x099d9000`; checksum:
  `0x0769ea6e`; file size: `124317952`.
- Ghidra project directory: `D:/GhidraProjects/FF7R`.
- Ghidra program: `/ff7rebirth_.exe-9457d4`, image base `0x140000000`.

The recovery was static and read-only. No executable, Ghidra program, live
process, or installed-game state was mutated.

## Recovery Chain And Calling Contracts

The primary `UGameViewportClient` vtable slot at `+0x320` resolves to
`PostRender` RVA `0x016ca7e0`. Its independently recovered viewport draw call
at RVA `0x009ef331` (`ff 90 20 03 00 00`) passes the live debug `UCanvas`.

```cpp
using PostRenderFn = void(__fastcall*)(void* viewport, void* canvas);
```

The matching-build text helper is RVA `0x05015114`. Caller and callee review
established this effective contract:

```cpp
using DrawShadowedStringFn = int32_t(__fastcall*)(void* fcanvas, float x,
    float y, const wchar_t* text, void* font, const FLinearColor* color);
```

The return value is a truncated text-layout metric, not a success flag. The
callee initializes the corresponding floating-point item field to zero before
submission, and all recovered matching-build callers ignore the returned
integer. A zero return therefore cannot be used to reject or disable a draw.

The first viewport rectangle call site is RVA `0x009eec80`, bytes
`e8 57 70 62 04`. Following that direct relative call resolves
`FCanvas::DrawTile` RVA `0x05015cdc`. Caller register/stack setup and callee
review establish the following effective ABI:

```cpp
using DrawTileFn = void(__fastcall*)(void* fcanvas, float x, float y,
    float size_x, float size_y, float u, float v, float size_u, float size_v,
    const FLinearColor* color, const void* texture, bool alpha_blend);
```

The recovered prologue is:

```text
40 55 48 8d 6c 24 e9 b8 e0 00 00 00 e8 93 5e 15 fd 48 2b e0
```

Static caller/callee analysis proves that `texture == nullptr` selects the
native white texture and that a true final argument enables alpha blending.
The viewport call site is provenance evidence only; runtime gating uses the
cataloged callable signature rather than adding a second call-site gate.

## Runtime Evidence And Limits

Native PostRender text rendering was observed with ASI SHA-256
`9ebef9fa69019f3b9154a8d72837e22fedfd2963b1bf23e8f138e4e84f1051b4` in
runtime-gate session `20260801T175644Z-5c02242ad475`, including a retained user
screenshot. That run ended at `discovery_started candidates=21`; it proved the
PostRender/text path but did not prove Ready rendering, cosmetic hiding, the
DrawTile path, or ETA presentation. Rectangle rendering therefore remains an
optional independently fail-closed enhancement over the proven text path.

Runtime-gate session `20260802T223322Z-681dc8d9be97` isolated the later invisible
overlay regression. PostRender, the callback gate, native Canvas/font/FCanvas
reads, the progress snapshot, all three tile submissions, and the first three
text submissions succeeded. The Loading-only ETA submission returned layout
metric zero, after which the old renderer permanently disabled itself. This
matches the recovered return-value contract above and justifies ignoring text
layout metrics while retaining exception containment and setup-time signature
validation.

## Future Matching-Build Recovery

For another executable, first establish the complete executable identity and
recover the primary viewport `+0x320` PostRender target. Reconfirm the live
debug-Canvas virtual call, then follow the first viewport rectangle call to the
DrawTile target. Re-derive every argument from caller register/stack setup,
verify null-texture and alpha semantics in the callee, capture a complete fixed
prologue, and update the canonical RVA catalog. Regenerate catalog artifacts
and retain text-only degradation until a focused runtime run proves the new
rectangle path. Do not carry these RVAs or ABIs across builds by assumption.

The bounded renderer submits at most three DrawTile calls (panel, track, and a
positive-width fill) followed by at most four text calls (headline, detail,
status, and the Loading-only ETA/estimates line). Only candidates directly observed
in a typed rebuild stage contribute duration samples; warm-cache completions do
not establish or alter the rebuild EMA. One completed representative rebuild is
required before estimates are exposed. Later samples smooth the estimate.
The Total estimate is the successful cold-rebuild EMA multiplied by the exact
unresolved candidate count and divided by at most two bounded workers. It is
coarsely rounded up and capped at one day. Stage units remain only a visual
progress measure; they do not discount ETA because stages have no equal-duration
contract and chart generation can dominate a rebuild. Thus a two-minute EMA
with seven unresolved candidates reports about seven minutes, while one
unresolved candidate reports about two minutes. Typed progress and completion
events refresh the cached estimate; snapshots and rendering do not advance it.
Snapshots are cached state copies and do not scan candidate storage from
PostRender.
