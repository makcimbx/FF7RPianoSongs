# Chart Limits

## Playable Limit

Ordinary publishable explicit and generated charts contain at most 512 rows. The only exception is the dormant exact-build experiment below. The pipeline rejects or omits anything else that cannot produce a valid chart within the ordinary boundary.

## Diagnostic Input

The existing exactly-520-row fixture remains read-only: it publishes only the native 512-row prefix and retains eight diagnostic rows.

On verified game build 1.005 only, `Experimental.ExtendedCharts=1` also requests one restricted exactly-513-row experiment. All 513 rows must be monotone-only, ungrouped, strength-zero, and contain no chord, camera, or IgnoreSound state. The native parser builds the first 512 events; an exact parser reserve-call hook pre-reserves 513 stable slots and the outer expansion detour constructs one tail event before committing count 513 last. During PlaySetup, expansion authority combines the already-guarded immutable selection/admission with a synchronous native relation: the chart captures a non-UObject controller at `+0x118`, and that controller must reciprocally publish the same chart at `+0xF48` with a stable shared-control-block capture at `+0xF50`. These native pointers remain transaction-local and are revalidated before reserve substitution and after expansion; retained completion-owner observations are downstream diagnostics only. A successful native count-last commit first creates a pending token containing only immutable selection, policy, route, lease, song, lifecycle, activation, and preparation identities, so public count remains 512 while playback is absent; the exact matching playback route/lease/song token activates publication at 513. Missing playback does not expire the pending token, while replacement expansion, terminal activation failure, identity drift, lifecycle reset, and shutdown invalidate it. Failed activation terminals and native publication are serialized by lifecycle-qualified activation generation, allowing synchronous rollback when failure wins before publication without letting stale terminals affect a newer activation. Note-count callbacks match immutable token facts without dereferencing retained native pointers. Capacity is bounded by the existing 1024-row diagnostic-input safety limit before byte-size arithmetic. The evidence distinguishes the two constructor lookup paths but does not prove a universal numeric path or assignment range for every monotone; the complete prefix must therefore share one exact constructor-resolved `+0x49` path, and the tail must match it and a playable assignment observed in that parser-built prefix. Missing helpers, signatures, callback-vtable identity, build identity, or transaction identity leave playable publication at 512. Build 1.004 has no research helper metadata and fails closed.

The diagnostic path is restricted by executable identity, exact helper prologues, per-song policy identity, cache identity, and fail-closed fallback. A row-513 research-helper mismatch keeps playable publication at 512; the pre-existing exact diagnostic fixture input may still be accepted when its separate shipping diagnostic helpers verify.

## Authoring Rule

- Keep ordinary explicit `notes` arrays at or below 512 rows.
- Expect an independently generated MIDI level to be omitted when its valid minimum exceeds 512 rows.
- Do not treat this restricted fixture as general extended-chart support.
- Do not publish diagnostic fixtures as songs.

## Why The Boundary Remains

Native chart event objects own subarrays and referenced state, and their links point into event allocations. Although native arrays can grow, safe construction beyond generated source rows, link rebasing, rollback, unwind behavior, and all UI/scoring consumers have not been proven as one publishable lifecycle. The parser's source-row access is also capped independently from downstream dynamic arrays.

A row can generate both a monotone event and a chord event. The verified native parser reserves stable storage for 512 events; growth after that point does not rebase existing group links. Runtime planning therefore rejects a chart when grouping is present and its exact generated event count exceeds 512, before native chart mutation or descriptor row-name resolution. This is a grouped-link safety guard, not a broader event-count limit: an ungrouped chart remains governed by the existing row-publication boundary.

Detailed reverse-engineering, ABI, ownership, synthetic-model, and runtime qualification records are repository-only evidence and are intentionally excluded from the binary package.
