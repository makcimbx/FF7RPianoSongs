# Chart Limits

## Playable Limit

Publishable explicit and generated charts contain at most 512 rows. The pipeline rejects or omits anything that cannot produce a valid chart within that boundary. Runtime list, selection, UI, scoring, duration, completion, and audio behavior never consume diagnostic tail rows.

## Diagnostic Input

The codebase contains a guarded read-only experiment that can accept one exact diagnostic fixture above the playable boundary. It publishes only the native prefix and records bounded evidence about the retained source tail. Enabling `Experimental.ExtendedCharts` does not raise the playable limit.

The diagnostic path is restricted by executable identity, exact helper prologues, per-song policy identity, cache identity, and fail-closed fallback. A mismatch restores ordinary 512-row acceptance.

## Authoring Rule

- Keep explicit `notes` arrays at or below 512 rows.
- Expect an independently generated MIDI level to be omitted when its valid minimum exceeds 512 rows.
- Do not split, stream, or append chart rows at runtime.
- Do not publish diagnostic fixtures as songs.

## Why The Boundary Remains

Native chart event objects own subarrays and referenced state, and their links point into event allocations. Although native arrays can grow, safe construction beyond generated source rows, link rebasing, rollback, unwind behavior, and all UI/scoring consumers have not been proven as one publishable lifecycle. The parser's source-row access is also capped independently from downstream dynamic arrays.

A row can generate both a monotone event and a chord event. The verified native parser reserves stable storage for 512 events; growth after that point does not rebase existing group links. Runtime planning therefore rejects a chart when grouping is present and its exact generated event count exceeds 512, before native chart mutation or name resolution. This is a grouped-link safety guard, not a broader event-count limit: an ungrouped chart remains governed by the existing row-publication boundary.

Detailed reverse-engineering, ABI, ownership, synthetic-model, and runtime qualification records are repository-only evidence and are intentionally excluded from the binary package.
