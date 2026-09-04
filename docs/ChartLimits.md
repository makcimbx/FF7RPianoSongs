# Chart Limits

## Playable Limit

On an exact build with the complete validated extended capability, authored and generated charts may contain up to 8192 source rows and 8192 compact native events. Startup attempts that capability automatically; there is no user setting. If any executable, helper, hook, or policy gate is unavailable, the pipeline and runtime remain at the native 512-row boundary and reject or omit larger charts without clipping.

## Retained Diagnostic Input

Legacy diagnostic fixtures remain parse-compatible for retained engineering evidence, but their marker grants no input or publication authority. A chart above 512 rows is playable only when it satisfies the same complete shape, event-plan, and verified-policy contract as every other count in the 513–8192 range.

On exact verified game builds 1.004 and 1.005, startup automatically attempts the reviewed extended transport capability for charts with at most 8192 source rows and 8192 compact native events. Playable authority remains unavailable until every build-specific helper, caller, signature, layout, and callback-vtable check passes and the reserve hook installs successfully. Unknown, mismatched, or partially validated builds remain native-512. Rows may contain a monotone, a chord, or both; strength, dot, and camera state remain zero. Automatically generated MIDI rows are ungrouped, while chord IgnoreSound slots and explicitly authored groups use the exact native ownership/linking path described below.

The runtime derives one canonical immutable plan. `R` is source rows, `P` is compact events from the first 512 rows, `E` is all compact events, and `A` is parentless required actions. Reserve and final native count use `E`; parser return and rollback use `P`; UI and result expectation use `A`. Before reserve mutation it validates `R/P/E/A`, digest, checked ordinals and compact order, and resolves every event and IgnoreSound ID with strict `FNAME_Find`. Raw event bytes through `0x20000` use the recovered `0x1000` allocation quantum; larger requests use `0x10000`. Capacity must exactly match that calculation, cover `E`, and remain at or below 8192; `E=8192` therefore requires capacity 8192 (1,179,648 bytes). After the parser returns with exactly `P`, the runtime reads parser-published FPS and decodes all times before constructing the first tail event.

During PlaySetup, expansion authority combines the guarded immutable selection/admission with a synchronous native relation: the chart captures a non-UObject controller at `+0x118`, and that controller must reciprocally publish the same chart at `+0xF48` with a stable shared-control-block capture at `+0xF50`. These native pointers remain transaction-local and are revalidated before reserve substitution, after expansion, and before every cleanup or ownership change. Retained completion-owner observations are downstream diagnostics only.

For an eligible profile, the first 512 source GroupIndex bytes are temporarily suppressed so the parser creates an unlinked `P`-event prefix in the final allocation. Source-array cleanup preserves that parser-produced count only when the immediately following generalized finish still proves the exact admitted transaction, controller/chart relation, allocation, capacity, caller, and exact `P` header; otherwise the legacy copied-count cap remains in force. The runtime does not recompute or rewrite `P`. It validates that compact prefix, constructs events `P..E-1` in row order (monotone then chord), populates ordered-unique chord IgnoreSound ownership, and applies the canonical full-chart group links only after all events exist. Link and group changes are journaled. Failure rollback restores count to `P`, max time, links/group state, then reverse-destructs freshly proved tail ownership. Max time is published after tails and links, and `E` is committed last.

The pending/active publication token retains only immutable `R/P/E/A`, digest, selection, policy, route, lease, song, lifecycle, activation, and preparation facts. Public note count is `A`, never `E`, and remains on the native fallback until exact playback identity and an immutable successful PlaySetup handoff prove the lifecycle transition. Success and pending may arrive in either order; no live polling or waits are used.

The path is restricted by executable identity, exact helper prologues, per-song policy identity, cache identity, and fail-closed fallback. A playable-helper mismatch keeps both accepted input and publication at 512. Builds 1.004 and 1.005 use separate immutable helper specifications and catalog records; neither may reuse the other's addresses, call bytes, FName signature, or callback-vtable slots.

Existing Development sessions provide build-1.005 evidence for the bounded extended transport and generalized transaction. Build 1.004 currently has direct static evidence and offline coverage but still requires a protected human-controlled runtime qualification before a release claim.

## Authoring Rule

- On a supported exact build, keep each explicit root chart or `profiles[].notes` array at or below 8192 rows and its compact native event plan at or below 8192 events.
- No INI option or diagnostic JSON marker is required to use rows 513..8192.
- On a build without playable extended authority, expect explicit charts above 512 to be rejected and generated MIDI levels above 512 to be omitted without clipping.
- Treat only profiles passing the complete 513..8192 eligibility contract as playable extended charts; legacy diagnostic markers do not bypass it.

## Why The Boundary Remains

Native chart events own subarrays and referenced state, and links point into the stable event allocation. Generalized admission therefore requires the exact build-specific 1.004 or 1.005 helper/signature specification, canonical compact/link plan, parser GroupIndex suppression, transaction-local ownership journal, and bounded `E<=8192`. Camera/dot/strength state and builds without a complete reviewed specification remain unauthorized.

Detailed reverse-engineering, ABI, ownership, synthetic-model, and runtime qualification records are repository-only evidence and are intentionally excluded from the binary package.
