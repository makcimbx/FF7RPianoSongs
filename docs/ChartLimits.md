# Chart Limits

## Playable Limit

Ordinary publishable explicit and generated charts contain at most 512 rows. The only exception is the restricted exact-build path below. The pipeline rejects or omits anything else that cannot produce a valid chart within the ordinary boundary.

## Diagnostic Input

A noneligible exactly-520-row diagnostic fixture remains read-only: it publishes only the native 512-row prefix and retains eight diagnostic rows. An exact-520 fixture receives extended publication authority only when it satisfies the same complete restricted-shape and verified-policy contract as every other count in the 513–8192 range.

On verified game build 1.005 only, `Experimental.ExtendedCharts=1` may publish one restricted count-driven chart from 513 through 8192 rows. Every row must be monotone-only, ungrouped, strength-zero, dot-free, and contain no chord, camera, or IgnoreSound state. The native parser builds the first 512 events in the final allocation; the exact parser reserve hook requests the immutable target count once, and the outer expansion detour constructs the retained contiguous tail before committing the target count last.

Before reserve mutation, the runtime validates every retained tail shape, performs checked count/offset/ordinal arithmetic, resolves every monotone with strict `FNAME_Find`, and computes the matching-build reserve capacity. Raw event bytes through `0x20000` are rounded to a `0x1000` allocation quantum; larger requests use `0x10000`. Quantized bytes are floored to whole `0x90` events. Capacity must exactly match that calculation, cover the target, and remain at or below 8192; target 8192 therefore requires capacity 8192 (1,179,648 bytes). The parser does not publish the current chart frame rate at `chart+0x48` until after reserve, so pre-expansion FPS is never used as authority. After the parser returns and exact 512-prefix/allocation authority is re-proved, the runtime reads the finite positive parser-published FPS and decodes every tail time before constructing the first tail. Any FPS, time-parse, or ordering failure leaves the native count at 512 and constructs no tail.

During PlaySetup, expansion authority combines the guarded immutable selection/admission with a synchronous native relation: the chart captures a non-UObject controller at `+0x118`, and that controller must reciprocally publish the same chart at `+0xF48` with a stable shared-control-block capture at `+0xF50`. These native pointers remain transaction-local and are revalidated before reserve substitution, after expansion, and before every cleanup or ownership change. Retained completion-owner observations are downstream diagnostics only.

Tail construction advances only a private completed-tail count while the native header remains at 512. Ordinary failure cleanup is reverse-order and only touches tails whose ownership is freshly proved. Uncertain identity or memory state blocks the custom route and preserves native evidence without further mutation. After all tails validate, max time is published and the exact target count is committed last.

The pending/active publication token retains only immutable target, selection, policy, route, lease, song, lifecycle, activation, and preparation facts. Public count remains 512 until exact playback identity and an immutable successful PlaySetup handoff prove the lifecycle epoch advanced once from the admission epoch. Success and pending may arrive in either order; no live lifecycle polling or waits are used. Replacement expansion, rejecting terminal, identity drift, lifecycle reset, and shutdown invalidate the token without retaining or freeing native storage.

The path is restricted by executable identity, exact helper prologues, per-song policy identity, cache identity, and fail-closed fallback. A playable-extended research-helper mismatch keeps publication at 512; the pre-existing exact diagnostic fixture input may remain accepted when its separate shipping diagnostic helpers verify. Default configuration remains `0`, and build 1.004 omits the required research helpers and fails closed.

Focused Development session `20260902T175102Z-a761305b6d59` qualified the 513-row member of this restricted path on verified game build 1.005 at checkpoint `88f1f99`: reserve changed once, prefix/tail/callback validation and count-last commit succeeded, natural completion succeeded, and a subsequent ordinary song restored normal behavior. That evidence does not qualify larger target counts, mixed event shapes, another executable build, or a packaged release artifact. The next runtime qualification target is one exact 8192-row scenario after checkpoint and review.

## Authoring Rule

- Keep ordinary explicit `notes` arrays at or below 512 rows.
- Expect an independently generated MIDI level to be omitted when its valid minimum exceeds 512 rows.
- Treat only profiles passing the complete restricted 513..8192 eligibility contract as playable extended charts.
- Do not publish diagnostic-only fixtures as songs.

## Why The Boundary Remains

Native chart event objects own subarrays and referenced state, and their links point into event allocations. The count-driven exception is limited to the proved final-allocation, ungrouped monotone shape; it does not authorize chords, groups, camera state, IgnoreSound, mixed rows, or counts above 8192. The parser's source-row access remains independently capped at 512.

A row can generate both a monotone event and a chord event. Runtime planning rejects any grouped chart whose exact generated event count exceeds 512 before native mutation or descriptor row-name resolution. This is a grouped-link safety guard, not permission for other extended event shapes.

Detailed reverse-engineering, ABI, ownership, synthetic-model, and runtime qualification records are repository-only evidence and are intentionally excluded from the binary package.
