# Architecture

## Data Flow

```text
song folder
  -> pipeline discovery and strict parsing
  -> audio decode/normalization/HCA/MABF cache + explicit or MIDI chart profiles
  -> immutable SongDescriptor registry
  -> game hooks and descriptor-backed list/selection/chart/audio integration
```

The offline pipeline completes validation before a descriptor is visible to runtime code. Runtime consumers do not parse user files or infer missing cache state.

## Module Ownership

- `src/core/` owns generic logging, configuration, memory/PE helpers, and hook infrastructure. It does not own song semantics.
- `src/pipeline/` owns user-file discovery, strict schema validation, audio/chart generation, cache identity, and publication of validated descriptors. It does not read or write game memory.
- `src/game/` owns executable-version gating, addresses, hook installation, native object identity, UI/list/selection/chart integration, and audio lifecycle interaction.
- `src/generated/<build-id>/` holds one derived source tree per supported game build, and a build selects exactly one of them. Generated files are outputs, not a second hand-edited source of truth.
- `src/tests/` owns offline validation and release-audit enforcement; tests do not establish in-game proof.

## Address Ownership

[rva_catalog.json](../src/game/rva_catalog.json) is the machine-readable source of supported executable identity, RVAs, signatures, hook declarations, and required/optional classification. Generated hook/address source is derived from that catalog. Documentation must not duplicate its current counts.

Any required timestamp, signature, or prologue mismatch fails closed. Optional behavior may be omitted only when its owning feature explicitly supports safe degradation.

### Catalog Schema

The catalog is `schema_version` 3. It declares the supported game builds once and records every address against those builds:

```jsonc
{
  "schema_version": 3,
  "catalog_id": "ff7rpianosongs-rva-catalog",
  "cataloged_on": "<date>",
  "default_build": "<build-id>",
  "builds": [
    { "id": "ff7rebirth-steam-win64-<pe-timestamp>", "game_version": "1.005",
      "pe_timestamp": "0x...", "size_of_image": "0x...", "pe_checksum": "0x...",
      "file_size": 0, "sha256": "..." }
  ],
  "addresses": [
    { "id": "<snake_case_id>", "cpp_symbol": "<CppSymbol>", "kind": "function",
      "subsystem": "...", "requirement": "release | optional | research",
      "validation": { "policy": "signature" }, "install_policy": "...",
      "status": "...", "hook_owner": "...", "consumers": ["src/game/..."],
      "hook_spec": { "name": "...", "order": 0, "required_for_release_startup": true },
      "builds": {
        "<build-id>": {
          "rva": "0x...",
          "signature": { "bytes": "...", "mask": "..." },
          "evidence": [ { "type": "...", "path": "analysis/...", "detail": "..." } ]
        }
      } }
  ],
  "locators": [
    { "id": "...", "cpp_symbol": "...", "match_policy": "first", "consumers": [ "..." ],
      "builds": {
        "<build-id>": {
          "pattern": { "bytes": "...", "mask": "..." },
          "decode": { "kind": "rel32", "displacement_offset": "0x...", "instruction_size": "0x..." },
          "candidate_adjustments": [ "0x00", "..." ],
          "evidence": [ { "type": "...", "path": "analysis/...", "detail": "..." } ]
        }
      } }
  ]
}
```

Shared metadata is written once per address. Only `rva`, `signature`, and `evidence` are per build, because a signature and the analysis that derived it are facts about one binary while a validation policy is not. Build identities carry the `ff7rebirth-` prefix; the release audit compares that set against the targets declared in `release.json`.

Locators split the same way. `id`, `cpp_symbol`, `match_policy`, and `consumers` are shared; `pattern`, `decode`, `candidate_adjustments`, and `evidence` are per build, because the decode offsets index into that build's own pattern and the adjustments depend on which field its chosen instruction touches. A masked pattern no more survives recompilation than a fixed signature does, so a locator is derived from each binary and never translated between builds.

An address that a build does not catalog omits that build's key. The literal `"0x0"` remains forbidden in the catalog: zero is the generator's absence sentinel and must never be authored by hand.

| generated file | entry present | entry absent |
| --- | --- | --- |
| `rvas.generated.h` | `inline constexpr uintptr_t X = 0x...;` | `= 0x0;` with a comment |
| `hook_specs.generated.inc` | one spec line | no line at all |
| `rva_signatures.generated.inc` | one signature line | no line at all |

The omitted `.inc` lines carry the behavior: `find_hook_spec` and `find_rva_signature` return `nullptr`, and consumers already fail closed on a null spec. The zero in the header exists only so the tree compiles. A consumer that forms `exe_base + rva` without a spec lookup must test the RVA for zero, because `exe_base + 0` is a non-null pointer that would pass an ordinary pointer check.

The generator enforces the invariants that make the sentinel safe: a `release` address must be present in every declared build; every address must be present in at least one build; RVAs are unique within a build and satisfy `0 < rva < size_of_image` of that same build; and hook `order` values stay globally contiguous, with each build installing the sorted subset it declares.

Locators admit no absence at all. A locator carries no `requirement` field and has no null-spec degradation path, so a build that omitted one would still install its hooks and then silently publish nothing. The generator therefore requires every declared build to name exactly one derivation of every locator, and rejects a `builds` map that omits a declared build or names an undeclared one.

### Per-Build Generated Trees

Each build owns its generated tree:

```text
src/generated/<build-id>/game/generated/{rvas.generated.h, *.generated.inc}
src/generated/<build-id>/core/generated/build_identity.generated.h
```

The CMake variable `FF7RP_GAME_BUILD` places the selected tree ahead of `src` on the include path, so existing `#include "game/generated/rvas.generated.h"` directives resolve to the chosen build without source changes. The runtime stays single-build and its RVAs stay `constexpr`; one ASI is produced per game build. The generator is total over builds — it writes and checks every tree — so catalog validation does not depend on which build was selected for compilation. Adding a game build is a data change plus regeneration; the procedure is in [Build And Release](BuildAndRelease.md).

## Publication Invariants

- Invalid folders, schemas, audio, charts, caches, and generated profiles are rejected before registry publication.
- Runtime behavior is descriptor-driven; parallel ad-hoc song state is not authoritative.
- Selected-index detail rendering owns one thread-local descriptor/profile scope around the exact native callback, including explicit profile refresh. Title, duration, note count, and the cataloged menu-detail ScoreInfo caller consume this scope first; playback is consulted only when no menu scope exists. Temporary ScoreInfo rows are retained by the outer scope and released when that callback returns.
- Cache reuse requires matching source identity, semantic configuration, generated profile data, format metadata, and structural validation.
- Enabled-metronome cache keys include the procedural synthesis identity
  `metronome_synthesis=shared_woodblock_envelope:v2`. The identity changes the
  baked Mode0 artifact boundary without changing JSON, runtime-cache, MABF, or
  descriptor formats; metronome-disabled cache keys are unaffected.
- Generic repository discovery retains decoded PCM for callers that request the full
  offline result. Production startup explicitly opts into releasing only the resident
  PCM sample capacity after a song's validated audio artifacts and runtime cache have
  been atomically published. Logical source-frame metadata, chart/profile semantics,
  descriptor inputs, cache identity, and sidecar artifact paths remain available for
  lexical settlement and runtime admission.
- Pipeline cache identity v46 and runtime-cache format 15 include authored group,
  monotone-variant, IgnoreSound, retained source-voicing, compiled IgnoreSound,
  profile-witness, and diagnostic semantics. Older artifacts are invalidated
  rather than interpreted under the new layout. Generated MIDI adds its own semantic
  identity to every MIDI cache key. Version 46 preserves exact authored black-key
  sharp/flat identity and independent monotone/chord note/dot values; generated
  MIDI's v11 identity additionally covers normalized key-signature orientation and
  exact rational source-length notation. Every authored-JSON and
  MIDI repository cache key also includes an immutable native-asset capability
  identity selected from the exact generated catalog build ID; descriptive game
  version strings never grant asset authority. Runtime-cache format 15 serializes
  both source-side overrides and the separate compiled monotone/chord pairs.
  Source rows, prefix events, native events,
  required actions, physical digest, and any explicitly authored group topology are
  deterministically rederived from the already-counted prefix/tail rows.
- A profile descriptor keeps the first 512 source rows in `chart_notes` and owns
  the remainder in `extended_chart_tail_notes`. `source_row_count`,
  `native_prefix_event_count`, `native_event_count`, and `required_action_count`
  are distinct authoritative facts; compatibility `note_count` means required
  actions. Native event order within a row is monotone then chord. Automatically
  generated MIDI rows are ungrouped parentless actions and profiles may have
  different physical digests. Runtime support for explicit mixed row-level groups,
  explicit dual rows, generated one-event rows, chord/IgnoreSound tails, and
  event-count allocation remains a separate qualification gate; offline authority
  does not authorize mutation on an older runtime.
- Extended authority is global runtime capability, not an author-controlled mode.
  On an exact build whose validated helper and reserve hook publish playable
  capability, authored root charts, authored profiles, and generated MIDI all use
  the same complete 512-prefix-plus-tail transport through 8192 rows/events.
  Without that capability, authored charts above 512 reject and generated profiles
  above 512 omit. The legacy JSON diagnostic flag is accepted only as inert input;
  actual tail presence determines the internal format-15 transport marker.
- `ChartEventRow` is the game-neutral compiled-row projection used by the canonical
  `derive_chart_event_plan` engine. The resulting immutable plan contains stable
  monotone-then-chord event entries and ordered root/child links in addition to
  R/P/E/A. Runtime reconstructs these rows from `chart_notes` plus
  `extended_chart_tail_notes`; format 15 does not serialize a redundant plan.
  `physical_chart_digest` hashes complete compiled/runtime physical semantics
  (`TimeStr`, monotone/chord IDs, each side's note/dot fields, camera fields, and
  IgnoreSound) while
  excluding only GroupIndex topology. R/P/E/A and links are separately rederived
  and checked for every profile. Source pitch spelling, authored IgnoreSound, and
  retained MIDI chord voicing remain protected by diagnostic/cache semantic hashes;
  they are deliberately outside the runtime-reconstructible physical digest.
- The offline pipeline owns an immutable exact-spelling evidence table for 64
  chord identities, including distinct `pca_Cs` and `pca_Db` rows. Build-scoped
  entries are available to IgnoreSound and MIDI inference only when the
  compile-selected exact generated catalog exposes the matching immutable asset
  capability. Exact builds 1.004 and 1.005 expose the verified-equal `pca_Db`
  row under one `verified1004+1005` capability identity; unknown catalogs do
  not. Resolution neither reads game memory nor
  constructs native FNames from source-note spelling.
- Playable chart publication never exceeds the shipping boundary in [Chart Limits](ChartLimits.md).
- Diagnostic helper availability without playable runtime capability does not
  authorize extended input, cache reuse, or descriptor publication.
- Failure never publishes a partial descriptor or partially replaces the active package directory.
- Release hooks install against an explicit empty custom catalog. Discovery exposes stable non-owning views of each newly contiguous lexical settlement delta only for the synchronous callback lifetime. Startup admits descriptors once and offers every increased fully validated prefix. If the unchanged final prefix follows a recoverable publication rejection, it receives exactly one final publication retry from the retained immutable descriptor/sidecar candidate without source I/O, sidecar reconstruction, or allocation; invalid-only settlement does not republish an already accepted descriptor identity. Each admitted song owns one immutable shared sidecar node linked to the prior prefix, so snapshots are O(1), prior MABF data is never reread or reallocated, and aggregate sidecar preparation is O(N).
- Progressive composition centralizes the already-enforced piano-list bound of 128 total rows (five vanilla plus at most 123 custom rows). This is an existing runtime safety bound, not a new recovered layout or address claim; exceeding it fails composition while retaining any earlier accepted catalog.
- Pending catalogs are adopted only by the game thread at the exact pre-open boundary after callback, menu, selection, playback, list, audio, and registry quiescence are revalidated. List tuple, owner, sidecar, and registry publication is one failure-atomic transaction; an already open list or playback retains its previous catalog identity. Registry state generation guards the quiescent transaction and mutable selection/profile/playback state, while a separate monotonic catalog revision plus immutable storage identity owns list coherence and changes only when storage is replaced.
- Exact menu-open admission is evaluated before catalog adoption. Unrelated calls and recoverable callback-admission failure forward the native original exactly once without product bookkeeping or mutation; authoritative terminal failure takes precedence and remains suppressed because an unverified native tuple rollback makes forwarding unsafe. For admitted callbacks, the authoritative adoption result governs catalog behavior: `Adopted`, `NoPending`, and recoverable `Blocked` all forward exactly once. `Blocked` retains the pending catalog and opens the unchanged active catalog (including the initial vanilla/empty-custom state) without waiting, polling, replay, deferred input, or catalog mutation. A later quiescent open may adopt that pending catalog.

## Lifecycle Invariants

- Hook callbacks validate executable identity, native object identity, ownership generations, and pointer readability before use.
- Runtime registry snapshots outlive callbacks that can observe them.
- Piano-list activity is owned by a monotonic menu-session generation. Open, shared close, state exit, and controller destruction form one mandatory hook transaction; stale generations cannot submit profile input, refresh UI, or claim activation ownership.
- Profile-direction input is sampled as explicit held facts per keyboard, PlayerInput, WindowProc, RawInput/HID, async-gamepad, and XInput provider. A merged direction emits only its aggregate released-to-held edge and rearms only after aggregate release, so overlapping reports for one physical hold cannot submit multiple profile changes. Authoritative focus loss, application deactivation, device removal, and teardown clear all event-backed PlayerInput, WindowProc-key, and RawInput/HID facts; sampled keyboard, async-gamepad, and XInput facts remain independent. Native forwarding, opposite-direction policy, callback admission, and missing-provider fallback remain unchanged.
- Cache-overlay `Ready` means discovery and artifact preparation are fully settled. It does not mean that the loader thread mutated game objects or the active registry. Rendering consumes one fixed-size O(1) startup snapshot and does not scan repository or catalog storage.
- Catalog adoption continues to emit already-computed prepared, deferred, and adopted count events through a startup-owned process-lifetime observer for internal readiness state. The startup overlay never renders active/current-menu counts, adoption/deferred state, count transitions, or reopen guidance. During Loading it shows processed/total and ready counts and uses typed stage plus active-worker facts to distinguish ordinary cache validation from actual rebuilding. Repository settlement produces one bounded `Piano songs ready` notice and then hides on the cosmetic deadline regardless of adoption or menu lifecycle.
- During Loading, `ready on reopen` is the count accepted into a pending immutable prefix. A menu that is already open, and any playback started from it, retain their prior catalog identity until the existing quiescent pre-open transaction adopts a newer prefix.
- A confirmed selection handoff crosses the shared activation/cancel close only when the menu session, list widget, registry selection, BGM controller, and optional canonical-substrate identities remain exact. Unowned closes revoke, while accepted activation closes preserve a single generation-bound transfer.
- Persistent chart expansion claims that preserved transfer once using the validated caller, controller-owned wrapper, and immutable selection identity. Duplicate or stale claims reject without revoking the valid owner; every pre-admission failure performs an exact generation-bound thaw.
- Chart planning, admission, the restoration journal, playback publication, title, and note-count consumers retain owning snapshots from the same registry storage instead of reopening raw registry views mid-transaction.
- The runtime sidecar catalog retains its registry snapshot for the catalog lifetime. Admission proves exact catalog storage, song, profile, and profile index before chart or audio mutation.
- Production PlaySetup, Set, Play, and Stop forwarding execute through one wiring authority that owns exact argument tuples, exact-once dispatch, callback/TLS restoration, lifecycle leasing, and the recursive operation boundary. Route and borrower decisions remain in the audio coordinator.
- Audio shutdown is an explicit fail-closed phase transaction. Cleanup readiness is the only failure that reopens admission; disable, drain, aggregate rollback, restore/release, or publication failure retains authority and cannot publish successful state clearing.
- Native audio state transitions are requested through verified native paths; arbitrary owner-memory replay is not a supported recovery strategy.
- A retained canonical substrate may qualify the one-use list-return reset lineage either through the authenticated changed-request rebase or through exact unchanged-request continuity. The unchanged path requires the same live controller, slot, BGM, sound pointer and UObject identity, state 4, clear aggregate/cleanup/quarantine ownership, and the exact lease-cleared canonical-relinquishment successor (`proof generation N` to idle unowned `reset generation N+1`); changed requests still require a valid authenticated reset lease and lifecycle lineage. Both paths also require lifecycle admission, canonical-only token ownership, and unchanged bridge/source/list-exit identity. Qualification commits only reset route/lease/lifecycle lineage and one revocation epoch; it does not release ownership, reset route state, clear failure latches, free tokens/backing, or invoke native audio operations.
- Install rollback is serialized against callbacks. Process detach does not perform blocking hook teardown under the loader lock.
- The piano-page negative-selection guard owns exactly the selected weak-resolver CALL declared by the RVA catalog. It installs while callback admission is closed through an exact-signature, target-decoded, protection-restoring rel32 transaction. Negative native no-index values bypass weak resolution and selected visibility mutation at the cataloged continuation; nonnegative values tail-call the original resolver unchanged. The process-lifetime relay is retained whenever a live or uncertain instruction may reference it and is not subject to live teardown.
- Disabled trampoline ownership can remain for process lifetime when delayed pre-entry callbacks may still reach originals.
- Runtime ownership is process-bound and attach-only. Live same-process ASI teardown, unload, or reload is unsupported; normal shutdown relies on process exit rather than attempting to retire hooks, callbacks, native objects, or SQEXSEAD state while the host remains alive.
- Audio resources are released and reacquired through validated native operations before replacement; same-pointer shortcuts cannot be assumed to rebuild state.

## Runtime Observability

- Production diagnostics are part of the runtime support contract. Retain bounded, event-driven markers that identify admission, route, bridge, cleanup, token ownership, release, readiness, and rollback failures without requiring a replacement diagnostic build.
- State-machine failures should report a stable stage/outcome and the first failed predicate. Include only the generation, identity, ownership, and provenance needed to correlate the event safely.
- Diagnostic capture must reuse already-computed facts, remain fixed and bounded, and never change control flow or repeat native reads. Formatting and logging occur only after native mutation authority and protected locks are released.
- Per-frame repetition, duplicate snapshots, and unbounded raw dumps are not durable observability. Remove or rate-limit those while preserving high-signal transition and failure markers.

## Durable Boundaries

Current reverse-engineering outcomes and unresolved lifecycle work belong in [Current Status](CurrentStatus.md) and the [analysis index](../analysis/README.md), not here. Defaults belong in source configuration and user contracts. Build targets and tests belong in CMake/CTest and release-audit output.

The descriptor catalog also owns the `GUObjectArray` locator contract. The
pattern, rel32 decode, and ordered candidate adjustments were migrated from
`runtime_layouts.h` on 2026-07-16 and became per build on 2026-08-08, once a
second game build showed that one build's pattern matches nothing in another.
Consumers in list, chart, and UObject-identity code use the generated descriptor
for the selected build; they do not carry independent locator policy.
