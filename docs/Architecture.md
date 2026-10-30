# Architecture

## Data Flow

```text
song folder
  -> pipeline discovery and strict parsing
  -> audio decode/normalization/HCA/MABF cache + explicit or MIDI chart profiles
  -> authenticated native templates + immutable per-song IoStore cohort
  -> owned SongDescriptor/native token prefix
  -> qualified mount/acquisition/registration and Ready state
  -> game-thread catalog adoption
  -> game hooks and descriptor-backed list/selection/chart/audio integration
```

The offline pipeline validates source files and artifacts before offering a
descriptor. Native readiness is a separate game-thread state: prepared files or
an owning descriptor do not authorize selection. Runtime callbacks never parse
user files, rebuild caches or infer missing admission state.

## Module Ownership

- `src/core/` owns generic logging, configuration, memory/PE helpers, and hook infrastructure. It does not own song semantics.
- `src/pipeline/` owns user-file discovery, strict schema validation, audio/chart generation, cache identity, and publication of validated descriptors. It does not read or write game memory.
- `src/game/song_descriptor.h` owns value-only descriptor, profile, and chart-note shapes. `src/song_descriptor_builder.cpp` projects all three chart-note locations through one field-complete mapping; it does not include registry authority. `src/game/song_registry.h` owns synchronized catalog storage, selection, and adoption, not offline projection.
- `src/game/` owns executable-version gating, addresses, hook installation, native object identity, UI/list/selection/chart integration, and audio lifecycle interaction.
- `src/generated/<build-id>/` holds one derived source tree per supported game build, and a build selects exactly one of them. Generated files are outputs, not a second hand-edited source of truth.
- `src/tests/` owns offline validation and release-audit enforcement; tests do not establish in-game proof.

## Native Cached Audio

### Offline Artifacts

The pipeline authenticates the selected source-template/profile, projects the
generated audio and logical cue duration into Music, and emits a genuine
PackageStore container with separate Music, BGMList, PianoScoreInfo and PianoScore
packages. Generated names preserve the native BGM acquisition path and the
Music export/internal-cue/resource relationship. The score name is shared by
ScoreInfo and chart lookup; changing only the BGM cue does not provide a chart.

Artifacts are content-addressed and immutable. Validation rederives their bytes,
package identities and metadata; an existing corrupt or partial cohort is rejected,
not overwritten under a potentially retained native reader. A descriptor retains
the immutable artifact set, authenticated source-row name and prepared runtime
token. The pipeline owns no game-memory address or native reference.

One repository session reuses its successfully authenticated stock-template bytes
for the same source path/build. Each song still validates those bytes and rebuilds
the expected cohort; warm verification compares every existing file byte using
bounded read storage. No cache-authored digest replaces validation authority.

On Windows, SHA-256 may use the system CNG provider; provider failure falls back
to the portable implementation. Digest values and complete byte verification
remain identical. This accelerates computation, not trust in cached metadata.

Native binding capture uses one operation-local read journal. Immediate record
verification and final cohort verification both reread the underlying source;
verification reads are not journaled again. No proof survives a native boundary.

Repository trace stages are buffered and replayed at progressive settlement.
Adjacent `[song_load]` log timestamps therefore do not measure original stage
duration; native updater `*_us` fields measure their actual named operations.

The repository worker qualifies the executable's decoder/default callbacks, or
loads the explicitly authenticated companion. Decoder availability is not engine
ownership, a callback-replacement lease or native audio readiness. Native cache
paths and dependency handling are documented in [Build And Release](BuildAndRelease.md).

### Startup And Admission

The owned proxy and the legacy ASI are alternative product images, not two modules
that must be installed together. The proxy's bounded process-attach bridge requires
an actual normal import and the selected executable's exact entry profile. At the
real entry, outside DllMain, it restores the temporary patch/protection/cache before
resolving System32 forwarding and invoking internal startup. The private active
bridge scope, executable authentication and cold-state rereads gate protective
hook publication; only subsequent genuine AEP execution establishes runtime origin.
Export forwarding remains separate from optional product-startup acceptance.

Early-install diagnostics capture bounded hook/API outcomes, monotonic boundaries
and the first already-evaluated failed predicate without repeating native reads.
The snapshot is projected once after private startup authority closes; a handoff
marker does not itself prove that the original executable entry has executed.
These observations confer no readiness or recovery authority.

The selected early native hooks prepare their owned trampolines before activation,
then queue owned targets for one serialized MinHook apply. Queue acceptance is not
activation, and apply is not atomic: it may activate a prefix before failing.
Aborted batches cancel pending owned enables before releasing serialization, then
use the existing native-lifetime teardown rules. An unsuccessful actual apply must
retain every prepared trampoline through process exit, even after successful queue
cancellation: MinHook may already have relocated a suspended thread into a disabled
suffix trampoline before patching failed. Pre-apply failures do not create that
exposure. Uncertain cancellation or cleanup likewise retains ownership and forbids
retry/uninitialization; unrelated targets are never included. Successful apply still
requires the original final cold/publication checks.

The alternative synchronous ASI export authenticates a supported early
loader/executable continuation and its source-specific state before publishing
protective hooks. DllMain does not start asynchronous product initialization.
Unsupported or missed origins remain unavailable; later pointer snapshots cannot
reconstruct continuity. Exact per-build code/data contracts belong in the catalog
and indexed bootstrap evidence, not duplicated RVAs here.

Early publication performs no game constructors or worker startup. It retains the
direct piano-page call-site guard in the authenticated entry-held phase: unlike
MinHook installation, its transactional byte writer is not a live execution barrier.
The guard's installer owns its signature/relative-target validation. Its original
signature is excluded from late preflight because PME has already patched it; the
guard remains a prerequisite for successful early startup, not an optional feature.
Successful early protection and the genuine normal native-initializer notification meet in
a once-only latch. Their conjunction queues background preparation; it is not full
feature readiness or a durable native-owner lease. GU discovery, configuration
consumers, theme preparation and feature hooks belong to that late phase. Feature
targets are revalidated before installation; custom work stays unqueued until GU,
ScoreInfo and the complete feature set are available. Overlay, title input and
repository services follow successful feature installation. A missing window is
Pending, not permission to repeat constructors or create another poller.

Late feature-startup failure closes feature admission and disables owned feature
hooks, retaining mandatory native lifecycle protection and borrowed resources.
Once live feature installation begins, an irreversible process-lifetime boundary
forbids trampoline removal and MinHook uninitialization, including installer-local
rollback. Disabled hooks retain their original pointers and module state: admission
drains cannot account for every stock forwarder already holding an original pointer.
Early native-install rollback before that boundary remains transactional.
Late failure does not retry or tear down the early native transaction. An already-open stock
menu does not acquire synthetic custom authority when preparation finishes; a fresh
observed session boundary remains necessary. This split reduces work while holding
game entry; it does not certify arbitrary third-party pre-CRT engine calls as safe.

The native runtime captures the initializer's persistent owner, subscription,
dispatcher, PackageStore and generation. Replacement/destruction/shutdown invalidates
that authority before native storage disappears. On the qualified original updater
return, bounded stock-provider readiness precedes mount/acquisition/registration.
Valid empty startup state waits without side effects; malformed state fails.

All selected native manager operations participate in exclusion. Queue/repository/
audio locks are not held across mount or loader waits. Before each native mutation,
the whole cohort's owners, identities, registry/map/backing/code and expected
state are revalidated. Metadata serial-zero observations are not weak handles;
only a candidate's own returned bridge may establish its positive registry serial.
Unknown names, partial native mutation or uncertain completion are terminal and
retained: no retry, guessed repair, registration withdrawal or automatic unmount.

Admission transition diagnostics include steady-clock microseconds for mount,
request submission, discovery, binding capture and registration. An unattempted
stage is `-1`; retried authentication reports its latest attempt. These are native
call durations, not streaming-completion latency or total frame time.

### Selection, Charts And Retention

Only Ready tokens may enter catalog adoption and activation. The existing selected
entry hook forwards the original once and supplies the genuine registered name to
its qualified caller; it does not rewrite owner requests, stock rows or Sound sources.
Profile/chart authority remains generation-bound to the same registry storage.

Chart projection resolves the exact retained PianoScore row for the selected full
FName. Provider/name/generation and bounded headers are recaptured before the
reversible journal is applied. The parser therefore consumes the projected song,
not a cached first-basename carrier. Extended validation accepts either the exact
precommit selection guard or its genuine current playback successor; consumed,
revoked and cleanup-only authority cannot be fabricated into a new guard.

Files, native acquisitions and registered backing are retained to process exit.
The small `audio_sead` adapter owns readiness and chart/selection handoff—not
Sound-source patches, PlaySetup replay or Set/Play/Stop reconstruction. The
historical filename `audio_sidecar_runtime` now owns immutable prepared native
token prefixes, not a synthetic MABF allocation handed to SQEXSEAD.

## Song-List Previews

Preview artifacts are bounded PCM excerpts and waveform metadata prepared by the
pipeline, independently of gameplay HCA/MABF artifacts. Descriptors own immutable
artifact metadata, not whole-song decoded audio. `game/song_preview` loads only
these validated excerpts on its background worker; game callbacks do not decode
source files or read preview files.

The pipeline/runtime cache identities include the preview format and immutable
path, content hash, frame count, source offset and waveform metadata. Warm loads
validate the bounded excerpt; missing or corrupt artifacts trigger ordinary
offline rebuilding rather than retaining full-song PCM.

`core/preview_audio` owns a separate OS audio stream and its queued buffers.
Preview never obtains chart/playback authority or calls SQEXSEAD. Menu/selection
generations bind pending loads; navigation, confirmation, menu retirement and
focus loss cancel preview authority. Cancellation failure retains device-owned
storage instead of freeing it or reporting successful retirement. Row changes and
the manual toggle fade out by rewriting the not-yet-played tail of the queued
buffer (about 100 ms ahead of the device position, then a 150 ms ramp and
silence), and the device is stopped outright if that fade has not completed after
400 ms; confirmation, focus loss, menu exit and teardown still stop the device
immediately.

Before queueing preview PCM, the runtime copies the game's BGM Config settings
gain through a bounded, build-qualified read-only projection. This gain already
includes master volume. The retained native asset and Music-record gains are
decoded from the export/bank templates and included once, rather than guessing a
preview-volume coefficient. The combined gain is applied to owned PCM before the OS driver
borrows it. Zero is valid mute, whereas unavailable state denies that preview
start without poisoning the audio driver. No native audio accessor is called on
the input thread, and no category pointer is retained between attempts. The
projection follows the configured target, not every transient mixer envelope or
final native voice effect; source artifacts and cache identity remain unchanged.

The equalizer is a per-excerpt spectrogram (16 log-spaced bands, 30 frames per
second) computed by the preview worker from the loaded PCM, never in a game
callback. It is not cached; the render snapshot interpolates it by the device
position.

The badge is visible only while the preview is loading or playing, so scrolling
past rows never shows it. The render callback owns a 150 ms opacity ramp and keeps
the last shown snapshot (with its storage reference) to fade out after stop, row
change or menu transitions. Its clock is the excerpt start plus device position,
over the song duration.

## Address Ownership

[rva_catalog.json](../src/game/rva_catalog.json) is the machine-readable source of supported executable identity, RVAs, signatures, hook declarations, and required/optional classification. Generated hook/address source is derived from that catalog. Documentation must not duplicate its current counts.

Any required timestamp, signature, or prologue mismatch fails closed. Optional behavior may be omitted only when its owning feature explicitly supports safe degradation.

### Catalog Schema

The catalog is `schema_version` 4. It declares the supported game builds once and records every address against those builds:

```jsonc
{
  "schema_version": 4,
  "catalog_id": "ff7rpianosongs-rva-catalog",
  "cataloged_on": "<date>",
  "default_build": "<build-id>",
  "builds": [
    { "id": "ff7rebirth-steam-win64-<pe-timestamp>", "game_version": "1.005",
      "pe_timestamp": "0x...", "size_of_image": "0x...", "pe_checksum": "0x...",
      "file_size": 0, "sha256": "...",
      "runtime_profile": {
        "native_audio": { "bgm_resource": "0x...", "scoreinfo_bgm": "0x...",
          "append_slot": "0x...", "backing_slot": "0x...", "serialized_interface_slot": "0x..." },
        "extended_chart": { "persistent_expand_call": "...", "fname_signature": "...",
          "callback_vtable_slots": [ "0x...", "0x...", "0x...", "0x..." ] },
        "evidence": [ { "type": "...", "path": "analysis/...", "detail": "..." } ] } }
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

Each build also requires a complete `runtime_profile`: the native-audio field/virtual-slot
layout and extended-chart exact caller bytes, FName prefix and callback-target RVAs.
These previously separate C++ tables are generated into
`runtime_build_profiles.generated.inc`; `src/game/runtime_build_profile.h` owns their
value-only shapes and inventory lookup. The inventory retains all declared profiles
to preserve existing lookup behavior, but is not runtime multi-build dispatch:
native audio still rejects any ID other than the compiled build, and chart admission
still validates the compiled executable's bytes and layouts. Missing fields, invalid
byte extents, unaligned layout offsets, duplicate/out-of-image callback targets or
missing evidence fail catalog generation rather than selecting a default profile.

### Per-Build Generated Trees

Each build owns its generated tree:

```text
src/generated/<build-id>/game/generated/{rvas.generated.h, *.generated.inc}
src/generated/<build-id>/core/generated/build_identity.generated.h
```

The CMake variable `FF7RP_GAME_BUILD` places the selected tree ahead of `src` on the include path, so existing `#include "game/generated/rvas.generated.h"` directives resolve to the chosen build without source changes. The runtime stays single-build and its RVAs stay `constexpr`; each product DLL or ASI targets one game build. The generator is total over builds — it writes and checks every tree — so catalog validation does not depend on which build was selected for compilation. Adding a build is a data change plus regeneration only while the remaining structural, bootstrap and asset contracts hold; the procedure is in [Build And Release](BuildAndRelease.md).

### Portability Boundary

Offline isolation is not game neutrality. Musical source processing is separate from
game-memory access, but pitch filtering, native note/chord names, chart time/row
encoding, mode roles and stock-template/container serialization still describe Rebirth.
`runtime_profile` covers the migrated build-specific tables, not every shared layout
in `runtime_layouts.h`, native lifecycle, loader handoff or asset schema. Another game
requires independent evidence for those contracts. Preserve audio/MIDI sources;
recompile target-specific charts and caches instead of assuming binary compatibility.

## Publication Invariants

- Invalid folders, schemas, audio, charts, caches, and generated profiles are rejected before registry publication.
- Runtime behavior is descriptor-driven; parallel ad-hoc song state is not authoritative.
- Explicit song-level chord definitions replace only the ordered sound slots of
  verified existing chord inputs. They belong to the immutable song descriptor,
  shared by all profiles; configuration/cache semantic binding protects the
  mapping separately from the unchanged physical row/event plan. IgnoreSound
  resolves exact members of the effective definition. Automatic MIDI generation
  cannot carry authored replacements or silently change its pitch witnesses.
- Selected-index detail rendering owns one thread-local descriptor/profile scope around the exact native callback, including explicit profile refresh. Title, duration, note count, and the cataloged menu-detail ScoreInfo caller consume this scope first; playback is consulted only when no menu scope exists. Temporary ScoreInfo rows are retained by the outer scope and released when that callback returns.
- Cache reuse requires matching source identity, semantic configuration, generated profile data, format metadata, and structural validation.
- After either a successful cold build or accepted warm-cache load, the repository best-effort
  renders `.cache/resolved-song.json` through a deterministic public-schema projection and publishes
  it with the atomic artifact writer. This convenience file is excluded from source discovery,
  cache keys, manifest/runtime-cache validity, descriptors, gameplay publication, and package
  inventory. Failure is a nonterminal trace event and never invalidates the playable song or
  replaces a prior complete file with partial bytes.
- Generic repository discovery retains decoded PCM for callers that request the full
  offline result. Production startup explicitly opts into releasing only the resident
  PCM sample capacity after a song's validated audio artifacts and runtime cache have
  been atomically published. Logical source-frame metadata, chart/profile semantics,
  descriptor inputs, cache identity, and sidecar artifact paths remain available for
  lexical settlement and runtime admission.
- Pipeline cache identity v49 and runtime-cache format 17 include authored group,
  monotone-variant, IgnoreSound, retained source-voicing, compiled IgnoreSound,
  profile-witness, and diagnostic semantics. Older artifacts are invalidated
  rather than interpreted under the new layout. Generated MIDI adds its own semantic
  identity to every MIDI cache key. The source model preserves exact authored black-key
  sharp/flat identity and independent monotone/chord note/dot values; generated
  MIDI's policy identity additionally covers normalized key-signature orientation,
  exact rational source-length notation, policy-independent exclusion of
  out-of-range source attacks, soft density/growth goals and bounded feasible-beam
  selection with feasible salience priority, source-timed fallback competition,
  exact partial LH carriers, and final same-tick/track/channel RH group enrichment.
  Cold normalization emits count/range warnings through
  existing song-scoped trace stages; valid warm caches do not reparse MIDI merely
  to repeat those warnings. Every authored-JSON and
  MIDI repository cache key also includes an immutable native-asset capability
   identity supplied explicitly by composition from the exact catalog build ID; descriptive game
   version strings never grant asset authority. Runtime-cache format 17 serializes
  both source-side overrides and the separate compiled monotone/chord pairs, plus
  canonical song-level chord definitions in each configuration. Ordered sound slots
  are semantic; dictionary insertion order is not. Root and profile definitions must
  agree, and cached definitions must match current source configuration. The schema
  stays v2; older binary caches rebuild rather than acquiring implicit empty fields.
  Source rows, prefix events, native events,
  required actions, physical digest, and any explicitly authored group topology are
  deterministically rederived from the already-counted prefix/tail rows.
- MIDI profile publication requires a complete feasible chart, not compliance with
  a preferred density, retained-percentage or adjacent-growth quota. The independent
  full-route validator and source/timing/physical limits remain authoritative. Search
  breadth adapts deterministically to projected additional analysis cost while the
  complete source timeline is traversed; the allowance is not an absolute total-work
  or wall-clock cap. Cold selection telemetry is observational and is not serialized
  as cache authority. Generated-MIDI identity invalidates former policy results;
  authored-chart identities and the binary layout do not change.
- A profile descriptor keeps the first 512 source rows in `chart_notes` and owns
  the remainder in `extended_chart_tail_notes`. `source_row_count`,
  `native_prefix_event_count`, `native_event_count`, and `required_action_count`
  are distinct authoritative facts; compatibility `note_count` means required
  actions. Native event order within a row is monotone then chord. Automatically
  generated MIDI roots remain independent actions; exact simultaneous same-stream
  source pitches become same-time grouped followers only after route selection.
  Followers add rows/events but not required actions or strain, and complete enriched
  material must pass the existing compiler/event-plan limits before publication.
  Profiles may have different physical digests. Runtime support for mixed row-level groups,
  explicit dual rows, generated one-event rows, chord/IgnoreSound tails, and
  event-count allocation remains a separate qualification gate; offline authority
  does not authorize mutation on an older runtime.
- Extended authority is global runtime capability, not an author-controlled mode.
  On an exact build whose validated helper and reserve hook publish playable
  capability, authored root charts, authored profiles, and generated MIDI all use
  the same complete 512-prefix-plus-tail transport through 8192 rows/events.
  Without that capability, authored charts above 512 reject and generated profiles
  above 512 omit. The legacy JSON diagnostic flag is accepted only as inert input;
  actual tail presence determines the internal retained-tail transport marker.
- `ChartEventRow` is the game-neutral compiled-row projection used by the canonical
  `derive_chart_event_plan` engine. The resulting immutable plan contains stable
  monotone-then-chord event entries and ordered root/child links in addition to
  R/P/E/A. Runtime reconstructs these rows from `chart_notes` plus
  `extended_chart_tail_notes`; the cache does not serialize a redundant plan.
  `physical_chart_digest` hashes complete compiled/runtime physical semantics
  (`TimeStr`, monotone/chord IDs, each side's note/dot fields, camera fields, and
  IgnoreSound) while
  excluding only GroupIndex topology. R/P/E/A and links are separately rederived
  and checked for every profile. Source pitch spelling, authored IgnoreSound, and
  retained MIDI chord voicing remain protected by diagnostic/cache semantic hashes;
  they are deliberately outside the runtime-reconstructible physical digest.
- The offline pipeline owns an immutable exact-spelling evidence table for all
  170 decoded `PianoChordsAssign`/`PianoChordsConfig` chord identities, including
  distinct sharp and flat IDs. Each row preserves the asset's ordered stock
  sounds for `ignore_sound`, authored voicings, and MIDI inference. Authored
   voicings require the compile-selected exact catalog's asset
  capability; the additional `pca_Db` stock entry is also capability-gated.
  Exact builds 1.004 and 1.005 share the verified-equal chord rows. Resolution
   neither reads game memory nor constructs native FNames from source-note spelling.

The CMake-resolved `FF7RP_GAME_BUILD` (catalog default unless explicitly selected)
is passed as a private composition definition to the ASI, offline tool, and
target-aware tests. They map it to the pure native-asset capability value
and pass that value through parsing, MIDI generation, chart compilation,
rendering, repository cache identity, and warm-cache semantic recompilation.
The pipeline does not include generated hook addresses or infer a selected
build itself. An unknown catalog ID is rejected at configure time; the pure
capability mapping remains conservative for unknown inputs. The same source
schema v2 works in offline-only and runtime builds; cache reuse still requires
the exact capability identity and complete policy snapshot, and no cache
compatibility is claimed across different game targets.
- Playable chart publication never exceeds the shipping boundary in [Chart Limits](ChartLimits.md).
- Diagnostic helper availability without playable runtime capability does not
  authorize extended input, cache reuse, or descriptor publication.
- Failure never publishes a partial descriptor or partially replaces the active package directory.
- Discovery uses at most two joined song-load workers, admitted only after the
  complete pool starts successfully. Worker-owned slots and serialized contiguous
  lexical settlement remain unchanged. Startup ETA uses the same two-worker cap;
  this is song-level concurrency, not a limit on all process threads or a guarantee
  of proportional speedup. Decoding and chart preparation can increase simultaneous
  memory and CPU demand.
- Release hooks install against an explicit empty custom catalog. Discovery exposes
  non-owning settlement views only for the synchronous callback lifetime. Startup
  admits each descriptor once and enqueues its prepared native token before
  offering the immutable prefix. Prefix snapshots share prior nodes. A recoverable
  final publication rejection permits one retry of that exact retained candidate,
  not repeated source processing or native acquisition. Invalid-only settlement
  does not republish an unchanged identity.
- Progressive composition centralizes the already-enforced piano-list bound of 128 total rows (five vanilla plus at most 123 custom rows). This is an existing runtime safety bound, not a new recovered layout or address claim; exceeding it fails composition while retaining any earlier accepted catalog.
- Pending catalogs are adopted only at the game-thread pre-open boundary after
  all native tokens are Ready and callback/menu/selection/playback/list/audio/
  registry quiescence is revalidated. Pending or Failed native audio cannot
  publish a new tuple. List tuple, owning native-token prefix and registry swap
  form one failure-atomic transaction; an open list/playback retains its prior
  catalog. Mutable state generation and immutable catalog revision/storage
  identity remain distinct authorities.
- Exact menu-open admission is evaluated before catalog adoption. Unrelated calls and recoverable callback-admission failure forward the native original exactly once without product bookkeeping or mutation; authoritative terminal failure takes precedence and remains suppressed because an unverified native tuple rollback makes forwarding unsafe. For admitted callbacks, the authoritative adoption result governs catalog behavior: `Adopted`, `NoPending`, and recoverable `Blocked` all forward exactly once. `Blocked` retains the pending catalog and opens the unchanged active catalog (including the initial vanilla/empty-custom state) without waiting, polling, replay, deferred input, or catalog mutation. A later quiescent open may adopt that pending catalog.

## Lifecycle Invariants

- Hook callbacks validate executable identity, native object identity, ownership generations, and pointer readability before use.
- Runtime registry snapshots outlive callbacks that can observe them.
- Piano-list activity is owned by a monotonic menu-session generation. Open, shared close, state exit, and controller destruction form one mandatory hook transaction; stale generations cannot submit profile input, refresh UI, or claim activation ownership.
- Profile-direction input is sampled as explicit held facts per keyboard, PlayerInput, WindowProc, RawInput/HID, async-gamepad, and XInput provider. PlayerInput stick clicks (L3/R3) are event-backed facts merged with XInput for the autoplay chord and preview toggle, so controllers the game reads without XInput (for example a Bluetooth DualSense) also work. A merged direction emits only its aggregate released-to-held edge and rearms only after aggregate release, so overlapping reports for one physical hold cannot submit multiple profile changes. Authoritative focus loss, application deactivation, device removal, and teardown clear all event-backed PlayerInput, WindowProc-key, and RawInput/HID facts; sampled keyboard, async-gamepad, and XInput facts remain independent. Native forwarding, opposite-direction policy, callback admission, and missing-provider fallback remain unchanged.
- Autoplay is optional: unavailable contracts or a fully rolled-back hook failure
  leave existing product hooks usable. Its coupled actuator hooks publish authority
  only after their complete transaction succeeds; uncertain rollback remains fatal
  to startup rather than advertising safe degradation.
- Autoplay input consumes copied seated generations, never retained native pointers.
  The enclosing controller tick has separate lifetime accounting and releases shared
  admission before native forwarding, preserving nested catalog-adoption exclusivity.
  Tick disable/drain must finish before dependent service teardown; failure retains
  those services for a later shutdown attempt.
  Chart admission is synchronous inside the owned controller/update path and honors
  custom chart readiness. Only pending parentless required actions receive a scoped
  native automatic marker. Introduced markers restore before terminal callbacks;
  discharged journals never access event storage afterward. Native timing, results,
  callbacks and follower propagation remain native-owned.
- Assistance is sticky for one logical play, including toggle-off and result phases.
  Assisted score calculations bypass custom persistence; the existing outer native
  result consumer owns an independent writable record shadow through its complete
  synchronous borrow. Nested and concurrent result consumers cannot share shadows.
  Actual calculated score and rank remain unchanged; quest/reward effects are not
  intercepted. New play generations reset assistance, not chart-address changes.
- Cache-overlay `Ready` means discovery and artifact preparation are fully settled. It does not mean that the loader thread mutated game objects or the active registry. Rendering consumes one fixed-size O(1) startup snapshot and does not scan repository or catalog storage.
- Catalog adoption continues to emit already-computed prepared, deferred, and adopted count events through a startup-owned process-lifetime observer for internal readiness state. The startup overlay never renders active/current-menu counts, adoption/deferred state, count transitions, or reopen guidance. During Loading it shows checked/total, ready, and skipped (preparation or native-admission failure) counts, a remaining-time estimate only while rebuilding, and uses typed stage plus active-worker facts to distinguish ordinary cache validation from actual rebuilding. A rectangle-drawn chocobo runs along the eased progress track; when DrawTile is unavailable or fails, the overlay degrades to its three text lines. Repository settlement produces one bounded `Piano Songs: Ready!` notice with ready/skipped counts, holds it, fades it out, and then hides regardless of adoption or menu lifecycle.
- During Loading, `ready on reopen` is the count accepted into a pending immutable prefix. A menu that is already open, and any playback started from it, retain their prior catalog identity until the existing quiescent pre-open transaction adopts a newer prefix.
- A confirmed selection handoff crosses the shared activation/cancel close only when the menu session, list widget, registry selection, BGM controller, and optional canonical-substrate identities remain exact. Unowned closes revoke, while accepted activation closes preserve a single generation-bound transfer.
- Persistent chart expansion claims that preserved transfer once using the validated caller, controller-owned wrapper, and immutable selection identity. Duplicate or stale claims reject without revoking the valid owner; every pre-admission failure performs an exact generation-bound thaw.
- Chart planning, admission, the restoration journal, playback publication, title, and note-count consumers retain owning snapshots from the same registry storage instead of reopening raw registry views mid-transaction.
- Authored chord voicing uses a preallocated chart-admission binding carried by
  that same guard/playback/cleanup lifecycle. Native expansion seals its final
  allocation and event plan before the existing chart-update hook permits
  judgement; pending input is rejected, not buffered or timing-gated. Reparse
  invalidates the binding, and failed ownership remains failed-retained rather
  than silently becoming stock playback. Withdrawal does not itself claim native
  Stop, resource release, score rollback, or list return.
- At the verified charted-chord callback, the original cached-row copier runs
  once with unchanged arguments. Only its exact caller may receive a synchronous
  sound-span projection through the reviewed register adapter. Native positional
  velocity, exact-name IgnoreSound, delay/duration, and voice ownership stay in
  the original emission loop. Persistent caches, chord inputs, scoring events,
  and free-play remain unchanged. Nested ineligible callbacks shadow outer
  projection authority; current binding identity is rechecked before projection.
- The native-token catalog retains its owning registry snapshot. Admission proves
  exact storage, song, profile and index before chart publication.
- Pause, resume and native playback transitions use the game's selected resource.
  The adapter supplies no source substitution, forced playback restart or
  owner-memory recovery path.
- Shutdown first closes new admission. Protective lifetime/manager hooks and
  borrowed GU/ScoreInfo services remain installed while native work, notification,
  selection validation or original-entry forwarding is active or uncertain.
  Incomplete shutdown is nonblocking and retains module/trampolines; disabling
  protection first or forcing a timed drain is not successful cleanup.
- Install rollback is serialized against callbacks. Process detach does not perform blocking hook teardown under the loader lock.
- The piano-page negative-selection guard owns exactly the selected weak-resolver CALL declared by the RVA catalog. It installs while callback admission is closed through an exact-signature, target-decoded, protection-restoring rel32 transaction. Negative native no-index values bypass weak resolution and selected visibility mutation at the cataloged continuation; nonnegative values tail-call the original resolver unchanged. The process-lifetime relay is retained whenever a live or uncertain instruction may reference it and is not subject to live teardown.
- Disabled trampoline ownership can remain for process lifetime when delayed pre-entry callbacks may still reach originals.
- Runtime ownership is process-bound and attach-only. Live same-process ASI teardown, unload, or reload is unsupported; normal shutdown relies on process exit rather than attempting to retire hooks, callbacks, native objects, or SQEXSEAD state while the host remains alive.
- Returning to the list relinquishes chart/profile authority through its exact
  generation-bound transaction; it does not claim native resource release,
  registration rollback or container unmount.

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
