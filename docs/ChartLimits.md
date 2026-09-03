# Chart Limits

## Playable Limit

Ordinary publishable explicit and generated charts contain at most 512 rows. The only exception is the restricted exact-build path below. The pipeline rejects or omits anything else that cannot produce a valid chart within the ordinary boundary.

## Diagnostic Input

A noneligible exactly-520-row diagnostic fixture remains read-only: it publishes only the native 512-row prefix and retains eight diagnostic rows. An exact-520 fixture receives extended publication authority only when it satisfies the same complete restricted-shape and verified-policy contract as every other count in the 513–8192 range.

On verified game build 1.005 only, `Experimental.ExtendedCharts=1` may publish one restricted chart with at most 8192 source rows and 8192 compact native events. Rows may contain a monotone, a chord, or both; strength, dot, and camera state remain zero. Chord IgnoreSound slots and run-local groups use the exact native ownership/linking path described below.

The runtime derives one canonical immutable plan. `R` is source rows, `P` is compact events from the first 512 rows, `E` is all compact events, and `A` is parentless required actions. Reserve and final native count use `E`; parser return and rollback use `P`; UI and result expectation use `A`. Before reserve mutation it validates `R/P/E/A`, digest, checked ordinals and compact order, and resolves every event and IgnoreSound ID with strict `FNAME_Find`. Raw event bytes through `0x20000` use the recovered `0x1000` allocation quantum; larger requests use `0x10000`. Capacity must exactly match that calculation, cover `E`, and remain at or below 8192; `E=8192` therefore requires capacity 8192 (1,179,648 bytes). After the parser returns with exactly `P`, the runtime reads parser-published FPS and decodes all times before constructing the first tail event.

During PlaySetup, expansion authority combines the guarded immutable selection/admission with a synchronous native relation: the chart captures a non-UObject controller at `+0x118`, and that controller must reciprocally publish the same chart at `+0xF48` with a stable shared-control-block capture at `+0xF50`. These native pointers remain transaction-local and are revalidated before reserve substitution, after expansion, and before every cleanup or ownership change. Retained completion-owner observations are downstream diagnostics only.

For an eligible profile, the first 512 source GroupIndex bytes are temporarily suppressed so the parser creates an unlinked `P`-event prefix in the final allocation. The runtime validates that compact prefix, constructs events `P..E-1` in row order (monotone then chord), populates ordered-unique chord IgnoreSound ownership, and applies the canonical full-chart group links only after all events exist. Link and group changes are journaled. Failure rollback restores count to `P`, max time, links/group state, then reverse-destructs freshly proved tail ownership. Max time is published after tails and links, and `E` is committed last.

The pending/active publication token retains only immutable `R/P/E/A`, digest, selection, policy, route, lease, song, lifecycle, activation, and preparation facts. Public note count is `A`, never `E`, and remains on the native fallback until exact playback identity and an immutable successful PlaySetup handoff prove the lifecycle transition. Success and pending may arrive in either order; no live polling or waits are used.

The path is restricted by executable identity, exact helper prologues, per-song policy identity, cache identity, and fail-closed fallback. A playable-extended research-helper mismatch keeps publication at 512; the pre-existing exact diagnostic fixture input may remain accepted when its separate shipping diagnostic helpers verify. Default configuration remains `0`, and build 1.004 omits the required research helpers and fails closed.

Existing Development sessions qualify the earlier ungrouped monotone endpoints on build 1.005. They do not qualify the generalized chord, dual-hand, IgnoreSound, or group-link transaction; that shape still requires focused in-game evidence.

## Authoring Rule

- Keep ordinary explicit `notes` arrays at or below 512 rows.
- Expect an independently generated MIDI level to be omitted when its valid minimum exceeds 512 rows.
- Treat only profiles passing the complete restricted 513..8192 eligibility contract as playable extended charts.
- Do not publish diagnostic-only fixtures as songs.

## Why The Boundary Remains

Native chart events own subarrays and referenced state, and links point into the stable event allocation. Generalized admission therefore requires the exact 1.005 helper/signature set, canonical compact/link plan, parser GroupIndex suppression, transaction-local ownership journal, and bounded `E<=8192`. Camera/dot/strength state and other builds remain unauthorized.

Detailed reverse-engineering, ABI, ownership, synthetic-model, and runtime qualification records are repository-only evidence and are intentionally excluded from the binary package.
