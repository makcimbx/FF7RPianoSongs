# Piano Menu Metadata Evidence (2026-08-02)

This record preserves the matching-build static and runtime evidence for custom
duration, note-count, and ScoreInfo threshold rendering in the selected-index
piano detail view. Durable ownership rules belong in
`../docs/Architecture.md`; current qualification belongs in
`../docs/CurrentStatus.md`.

## Build Identity

Static analysis used the installed `ff7rebirth_.exe` at image base
`0x140000000`. Its PE timestamp `0x6a16ced2`, image size `0x099d9000`, and
checksum `0x0769ea6e` match catalog build
`ff7rebirth-steam-win64-6a16ced2`. The catalog file identity is length
`124317952` and SHA-256
`752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc`.
No binary or Ghidra metadata was mutated.

## Selected-Index Call Chain

Matching-build disassembly of the selected-index detail updater establishes the
following synchronous chain:

| Role | RVA / return RVA | Effective ABI and result |
|---|---:|---|
| Selected-index body | `0x039a84e4` | `void __fastcall(context*, int32_t selected_index)` |
| Duration helper | `0x00d934e8` / `0x039a85b5` | `float __fastcall(object*, uint64_t key)`, result in `XMM0`, then formatted as time |
| Note-count helper | `0x039a0744` / `0x039a8619` | Four-argument native helper, count returned in `EAX` |
| ScoreInfo resolver | `0x03951d5c` / `0x039a8659` | Four-argument native helper; returned wrapper supplies the menu-detail score row |

The duration, note-count, and menu-detail ScoreInfo calls execute inside the
selected-index body. This permits one owning thread-local descriptor/profile
scope around the exact outer callback. The scope must be established only after
selection and UObject target revalidation and must also cover the explicit
profile-refresh call of the same native body.

The return at `0x039a8659` is menu-detail work, not part of the playback/result
ScoreInfo authority sequence. Its temporary row and internal arrays therefore
belong to the outer selected-index scope and are released when that callback
returns. Result-screen authority, current-run score calculation, and progress
or high-score fields remain separate.

## Runtime Correlation

Development session `20260802T195940Z-4c30cb555f39` used ASI SHA-256
`efcd4fa3672c341b68a6fe19b068dc750c21b0d52edcf1b4b71009b1be3ee9ee`;
its retained full log has SHA-256
`ecf292f9aa1d3bfcc99ddf9fe522b2e3697b28c4b0030be2cfbc309b34eb1c10`.
The session was rolled back.

During list browsing, title callbacks had custom scoped render identity while
duration callbacks repeatedly reported `active_song=<none>` and retained native
values. Profile coordination independently observed correct custom note counts,
but the note-count helper also consulted playback only. Custom ScoreInfo
thresholds were published later in the playback/result sequence, while the
pre-play menu-detail caller remained native. This excludes initial empty-catalog
hook installation as the cause and establishes inconsistent UI authority.

## Required Authority Model

- The selected-index body owns one scoped, owning descriptor/profile snapshot.
- Duration and note count consume that scope first and use playback only when no
  menu scope exists. They never use an unscoped global selection fallback.
- Only the exact cataloged menu-detail ScoreInfo return may publish a temporary
  custom row from this scope. Source wrapper/row and scoped identity must be
  revalidated before pointer publication.
- The row owner is retained by the outer scope; no page widget or UI pointer is
  retained.
- Vanilla rows and any rejected scope use the original native values.
- Playback/result ScoreInfo, score calculation, and progress/high-score paths
  remain unchanged.

## Remaining Uncertainty

Offline tests establish scoped profile isolation, fallback behavior, and owner
release. In-game evidence is still required for profile cycling, duration,
note-count, and threshold presentation. The user-visible “points” field may
refer to the threshold array or to a distinct progress/high-score widget; this
record does not qualify the latter.
