# Current Status

## Supported Now

- The native project configures and builds through the repository wrapper.
- Strict song discovery, JSON v2 parsing, generated-WAV decoding, explicit charts, MIDI profile generation, loudness/gain/metronome processing, cache identity, native HCA encoding, and MABF assembly are covered by offline tests. Tiny audited synthetic MP3 and FLAC fixtures additionally qualify production decoding at 48 kHz stereo with pinned frame, timing, and gain witnesses; this is bounded fixture coverage, not a claim of general encoder or corpus parity.
- JSON v2 also accepts 1 through 32 ordered explicit `profiles`, each containing only a non-negative difficulty label and explicit notes. The offline repository compiles and validates every profile, derives omitted gameplay metadata per profile, preserves the first profile as the root/default chart, ignores incidental MIDI, and publishes the existing descriptor profile path. Pipeline cache identity is v48; the runtime-cache binary format is 16.
- Descriptor-driven runtime list, profile-selection, chart, progress, and audio integration is implemented behind executable identity, catalog, signature, object-identity, and fail-closed guards.
- The RVA catalog describes supported game builds explicitly, and one ASI plus one archive is produced per declared build. Runtime qualification is per game build; a newly cataloged build inherits none.
- The 0.1.1 runtime adds the evidence-backed single-site piano-page guard for native negative selected indices. Only the weak-resolver CALL at RVA `0x039b04f9` is patched: negative values bypass resolver and selected visibility mutation at the native continuation, while every nonnegative value preserves the original resolver/visibility flow. Exact signatures and decoded targets are required, installation is transactional and fail-closed, and the process-lifetime relay has no live teardown path. Historical and decisive evidence is retained in [Piano Page Negative-Selection Guard Evidence](../analysis/PianoPageSelectionGuardEvidence-20260803.md); focused in-game list/navigation behavior remains unqualified and is accepted as a release limitation below.
- Transactional package staging and source/staged release audits are implemented.
- Static 1.005 split-articulation evidence records four independent monotone/chord NoteType/DotType arrays and separate constructor inputs. The retained corpus contained 2,385 rows and 338 dual rows; 232 dual rows had unequal complete side pairs. Focused 1.005 Development runtime evidence now qualifies only the bounded five-row scenario recorded below. Exact build 1.004 remains runtime-untested for split articulation.
- Ordinary post-expand count cleanup now derives its upper bound from the canonical compiled event plan rather than the number of source rows. This preserves both events of a dual row; required-action counts and stale stored totals do not grant authority. Retained-tail charts keep the existing source-row fallback unless the exact synchronous extended transaction authorizes preservation. The dedicated 1.005 five-row/seven-event Development result below supplies bounded count and score evidence, not qualification of every chart shape.
- Explicit and MIDI alternate emission is restricted to natural C2–C6 and exact C#2–C#6. [Exact 1.005 monotone inventory](../analysis/PianoMonotoneAssignEvidence-20260905.md) proves ten alternate rows with assignment 7 and unchanged sound references, no `Cn1_2`/`Cn7_2`, and ordinary `Cn7` already assigned high C. It contains no C-flat alternate; that request remains unsupported. This narrows the existing shared emitter conservatively, not a new 1.004 capability claim; exact 1.004 monotone parity and physical-input/audio behavior remain unqualified. The pipeline identity change invalidates cached unsupported boundary assignments without changing runtime-cache layout.

## Integration: Offline Portability Boundaries (2026-09-24)

- Descriptor values now live separately from registry authority in
  `src/game/song_descriptor.h`; root/profile/retained-tail rows share one
  field-complete projection. Capabilities are supplied explicitly from
  CMake-selected target composition through parsing, compilation, repository
  identity and warm-cache semantic validation. The existing pure capability
  mapping, source schema and valid artifact formats are unchanged.
- `FF7RP_BUILD_RUNTIME=OFF` builds the pipeline, offline tool and focused tests
  without configuring MASM, MinHook, Python/PowerShell, generated hook-address
  dependencies or runtime release validation. Default `ON` retains those runtime
  obligations. Commands and prerequisites are in [Build And Release](BuildAndRelease.md).
- Release configuration built offline targets and the default ASI/affected
  runtime targets. Nine selected OFF CTests passed: configuration isolation,
  pipeline, descriptor builder, repository, runtime-cache codec, MIDI generator,
  MIDI compilation, resolved renderer and cache-manifest renderer. Four selected
  ON tests passed: pipeline, descriptor builder, repository loader and chord
  voicing. The offline configuration regression also verifies unknown-target
  rejection with Python discovery disabled and pwsh/MASM unavailable.
- Existing Release MSVC dependency records confirm no registry-header dependency
  in the descriptor-builder target's three source entries. The records for all
  21 active first-party OFF targets (46 source entries) contain no dependencies
  under `src/generated/`. The source-side generated tree remained present; this
  is compiler dependency evidence, not a claim it was deleted during the build.
- Authored and MIDI cold fixtures generated through both tool configurations
  at identical absolute paths matched cache keys and runtime-cache/MABF bytes.
  Both final CMake configurations selected `ff7rebirth-steam-win64-68fd6fde`;
  policy was native 512 rows, extended disabled, generation zero. Keys were
  `bca23d68765b10cb` (authored) and `6655babbdf475204` (MIDI). Independently
  reconstructing the pre-refactor key algorithm from baseline commit
  `4377739f28051c77c1e5bca9d39d9df759381e1e` with those source bytes and ordered
  identities produced the same keys. This is baseline-formula equivalence plus
  new OFF/ON byte parity, not execution of a pre-change binary or cross-target
  artifact compatibility. The reusable fixture comparison is
  `tools/check_offline_runtime_parity.ps1`; timing/log text is excluded.
- Independent review found no concrete descriptor/capability regression. Its
  proposed CMake timeout-list blocker was checked against the complete list and
  withdrawn as a false positive; no speculative fix was added.
- Audio orchestration is unchanged. The
  [bounded substitution investigation](../analysis/NativeSubstitutionBoundary-20260924.md)
  records a no-go gate: common first/repeated/different-song resource identity
  remains unproved; the conditional native lifetime boundary is described below.
  Independent offline completion does not complete the wider audio/portability change. These offline
  changes were not installed or gameplay-qualified; prior accepted sessions
  below apply only to their recorded artifacts.
- In a subsequent user-launched session on the identified accepted baseline ASI,
  ordinary read-only CE inspection and existing logs observed first custom start
  and pause. The user confirmed Alicia sounded correctly and was silent while
  paused. The installed mod then logged `native_route_not_proven` at retirement
  cleanup. The source-based explanation is retained cleanup without ordinary
  poll retry, not permanent route disablement. The controlled comparison stopped
  at pause. After the user reported exit, later log records showed native custom
  continuation and list return, followed by release on the separate
  `aggregate_closed` path, `custom_absent`, `exit_cleanup_complete` and an Idle
  route with no lease/ownership/cleanup. This sequence therefore did resolve the
  retained cleanup. It does not qualify audible resume, repeat/song-switch
  coverage, leak-free teardown or a replacement seam; the precise failed
  successor-proof conjunct remains unresolved. A final destructor reservation
  rejection is recorded but not independently diagnosed. See the same evidence
  document for identities, log spans and source/artifact limits.
  No debugger, memory writes, new installation or gameplay qualification of the
  offline-refactor build occurred.
- An offline follow-up distinguished route relinquishment from detached-bank
  release rather than treating the two cleanup paths as interchangeable.
  `runtime_lifecycle_selftest` and `bgm_playback_aggregate_policy_selftest` were
  rebuilt in Release and passed their focused CTest run. Existing counterexamples
  cover forwarded-Play recovery, route-restore authority and outstanding-request
  release refusal; no additional lifecycle state or speculative test fixture was
  added. This does not prove the native borrow contract or authorize deletion.
- Static tracing subsequently established external MABF borrowing and native
  bank reference retention by successfully initialized music requests. The
  bank worker drains those references before registry removal. For the mapped
  Sound/Music callback and externally owned on-memory payload, the examined
  post-unlink tail does not read/free the payload; exact successful-token absence
  is a conditional tracked-borrow closure, not arbitrary destructor completion.
  Release timing remains separate because release can stop playback. This is a
  native authority to rely on, not a reason to duplicate decoder-reader state;
  it does not yet select a replacement preparation/reuse route. See the evidence
  document for the callback, imported-image and source/artifact applicability.
- The final bounded entry-path investigation ruled out owner-request-only
  replacement: native same-key/same-sound reuse can bypass preparation, while
  custom rows currently share a base alias. The native fallback resolves keys
  through managed packaged-asset streaming; owner+`0x454` is request context,
  not an owning sound map. No arbitrary custom-object binding was established.
  A dedicated asset/lookup prototype was subsequently approved, but its offline
  contract task remains blocked: the contextual branch reads a shared keyed
  registry, not a supplied-object binding, and construction parameter recovery
  exposes qualified-name recycling without establishing the new object's owning
  reference or partial-failure restoration. No supported genuine key → initialized
  Music/source binding was selected. See the evidence document's task 1.5 section.
  No opt-in candidate, new package, UObject construction call, hook or production
  audio behavior was introduced. Another unchanged pause test cannot resolve this
  prerequisite. The user subsequently authorized one temporary content fixture.
- That content assessment found an existing-toolchain blocker: the local
  ENDEditor Music/Sound XML factories ignore their input buffers and create
  default objects, while the SQEXSEAD class shells have no native payload
  serializer. Editor reflection confirms the factories/classes exist, not a
  functioning importer. The normal plugin mount also differs from the native
  `/Game/Sound/BGM/...` lookup; Alpakit's staged directory remap does not change
  logical package identity. Read-only commandlet scripts completed, but the
  processes exited 1 on an unrelated existing `bgm_piano_09.uasset` malformed-tag
  error; that asset was preserved. The parent workspace's
  `FF7R2UProj/Mods/FF7RPianoLookupFixture/` initially held inspection only.
  The user then authorized assessing preservation of genuine cooked Music,
  rather than developing the missing XML importer. That follow-up produced
  task-owned offline controls, not a loadable mod or native candidate.
- The preserved-package control now narrows the tooling blocker: pinned retoc,
  given the full game Paks directory for global metadata, converted the stock
  Music to legacy header/export and back while preserving its entire opaque
  serialized Music export. A header-only distinct-name variant also preserved
  those exact bytes, with independently checked regenerated name/public-export
  hashes and unchanged class/template references. Both diagnostic containers
  passed retoc's integrity check; ten focused preservation/identity tests passed.
  Whole Zen chunk identity is not claimed, and the variants still contain stock
  audio. No importer repair, Editor resave, guessed package-store entry or native
  audio implementation was needed for these controls. A later retained historic
  new-Music load success was also found, but exact-carrier reproduction failed and
  successful preload did not establish playback; it is not current qualification.
  The fixture README owns the source/output hashes, reproduction commands and
  limits. The remaining task 1.5 conditions are intended custom-payload binding,
  native key lookup/retention and stock restoration, not blanket inability to
  preserve Music serialization. Its serialized source record is the next bounded
  static question. No installation or new game session occurred; 1.5 stays open.

## Integration: Complete Native Chords And Fractional Note Values (2026-09-24)

- The offline chord inventory now includes all decoded assignment/config joins,
  preserving exact ordered stock sounds for authored voicings, IgnoreSound and
  partial MIDI carriers. MIDI root/quality mappings now cover every root for the
  existing quality templates, without changing prior non-null mappings.
- Named note values include native one-third/sixth and their dotted variants;
  per-side runtime transport accepts the recovered types. Exact MIDI duration
  inference preserves older named values when durations coincide.
- Generated MIDI policy changes invalidate generated caches; authored cache and
  binary layout are unchanged. See [Song Format](SongFormat.md) and
  [MIDI Generation](MidiGeneration.md) for the current supported values and policy.
- This is asset-level and offline Integration support, not individual in-game
  qualification of every added chord or fractional value. Camera and strength
  authoring are unchanged.
- Exact-1.005 Development session `20260924T092820Z-912e59aedcec` installed the
  matching ASI, collected a `Passed` loader/sidecar assessment after normal game
  exit and was finalized `Accepted/Keep`. The user reported that the changes work
  well. This feedback belongs to that development artifact; the collected startup
  assessment alone does not prove individual chord, notation, input or scoring
  behavior, and it does not transfer to 1.004 or either packaged archive.

## Integration: Simultaneous MIDI Groups And Preparation (2026-09-09)

- Generated MIDI policy v16 preserves the selected RH root and adds unique supported
  pitches at the exact same source tick, track and channel as automatic same-time
  group followers. Adjacent groups remain independent; existing LH sounds are not
  duplicated. Followers consume physical rows/events but not required inputs or
  root-route strain. Complete enriched limits and compiler validation remain mandatory.
  Authored charts, pipeline v48 and binary format 16 are unchanged; generated caches
  invalidate once for the group policy.
- Selection paths now borrow immutable selection-local rows, copying only final owning
  output. This removes repeated allocation without changing traversal, beam breadth or
  scoring. Isolated Sorairo cold preparation fell from 124.38 to 41.66 seconds and
  Zanarkand from 27.66 to 11.64 seconds. Full manifest, resolved-chart, runtime-cache and
  MABF hashes matched before/after and on warm reuse for both songs. External evidence
  is retained at `Temp/opencode/preparation-ab`. These are bounded single-song runs,
  not worst-case or concurrent startup guarantees.
- The experimental four-worker pool was rejected by the user for excessive load;
  the shipping pool and ETA cap remain two, with the allocation optimization retained.
  Focused grouping, compilation, cache, loader and ETA checks passed during development;
  independent immutable-row lifetime/concurrency review found no blocking defect.
- Exact-1.005 Development session `20260909T135525Z-f305839d6259` used ASI SHA-256
  `9f2d64f48e1d1c3cc1e88174c52b32df8abbd3ab36f359e36f12d999274fe2e0`.
  The user reported excellent sound and requested release. Immutable collection passed
  progressive startup, full repository settlement and Sorairo sidecar readiness. The log
  records Sorairo Lv.4 expansion to 1062 events with 545 group links and 517 required
  actions, natural completion with native counters summing to 517, exact-profile score
  persistence, custom-bank absence, complete cleanup and return to the list. The retained
  handoff warning resolves before cleanup; subsequent controller destruction rejects an
  unconfirmed reservation rather than authorizing playback. Log SHA-256 is
  `c8a71c2fc868445a05a05197fd3b6e81457a8983fe9784b8284f795b513c6763`.
  This is bounded Development evidence, not every follower's acoustic timing, every
  grouped-tail/input combination, process-detach, other-build or packaged qualification.

## Integration: Partial MIDI LH Candidates (2026-09-08)

- Generated MIDI policy v15 adds exact-octave filtered bass/dyad carriers only
  below a tracked melody at the same onset cluster or still sounding at the
  fresh source tick, after complete inference declines. An ordered half-open
  source timeline excludes ended/future notes; sustained melody supplies context
  only, never a reattack or chord constituent. It uses verified native constituents and existing IgnoreSound,
  without new runtime capability, quotas, scores, route limits or held-tone
  inference. Authored JSON v2, pipeline v48 and binary format 16 are unchanged;
  generated caches deliberately invalidate. See [MIDI Generation](MidiGeneration.md).
- Isolated Everlasting Summer copies compared with clean `6d28ebb`: LH counts
  changed from zero to 7/7/9/11/24/25 for labels 1–6 (first same-onset-only
  iteration: 0/0/1/1/9/7). Source inspection found 67 exact-carrier fresh-onset
  opportunities during tracked melody sustain. Every surviving LH sound
  matched an exact normalized source pitch and source-onset native frame.
  Final prompt stayed 149.183333s; labels 1–2's first prompt moved from 3.033333s
  to 3.683333s and label 4's to 3.466667s. Tracked-melody RH retention changed
  from 138/149/189/192/170/165 to 140/147/183/193/168/171 (264 eligible tracked attacks). This is an explicit
  selection tradeoff, not a claim of improved musical quality. Same-frame hand
  competition remains; no salience inflation or dual-row rewrite was added.
- Cold preparation measured 36.28s before and 42.83s after on this host; warm
  reuse took 0.20s with identical artifact hashes. All six profiles remained
  complete and within existing physical routes/512-row bounds. Evidence is
  retained outside Git at `Temp/opencode/lh-integration-6d28ebb`; installed
  sources/cache were never modified. This is offline Integration evidence,
  not release certification or human play/listening qualification.
- Focused compilation/generator suites, repository `partial_lh_cache`,
  `offline_goldens`, and `normal_policy` passed. Controls cover exact bass/dyad
  sounds, reduced octave doubling, unmappable dyads/octaves, unknown capability,
  monophonic/high leads, sustained versus exactly-ended/expired/future melody,
  deterministic reductions and unchanged warm artifacts. Renderer/artifact-validator
  goldens passed without changes in the first iteration. LH remains sparse;
  easy labels' last LH is 39.25s in a 153.496s song. This does not solve the
  overall accompaniment UX; simultaneous RH/LH selection remains outside this
  batch. Stock-name research is not used to recalibrate physical routes.
- The user-authorized 1.005 Development installation built this uncommitted
  candidate and prepared session `20260908T092138Z-236bd71a0b1e` for Everlasting
  Summer. Installed/dist SHA-256 matched
  `c8ab000c97ec8f3c7de5d02866e64bb270c3e49091beef66d713e5954b1fbf0a`;
  normal executable/provenance/process guards passed. The prior packaged session
  `20260907T213156Z-eabae8976248` was collected and finalized Keep after loader/
  settlement evidence and bounded review found completed cleanup for all five
  observed leases, with no unresolved stop condition. It remains the new session's
   release-binary rollback baseline. The candidate was initially **AwaitingUserRun**;
   the subsequent bounded result and disposition follow below.
  No game launch, INI/song/score/cache edit, package or publication was performed.

The tester subsequently reported that both the sound and increased left-hand participation
were enjoyable and requested no further algorithm changes. Session
`20260908T092138Z-236bd71a0b1e` was collected and finalized **Keep** on 2026-09-08.
Its immutable machine assessment remains **NotObserved**: the selected sidecar was ready,
but accepted startup ordering and full repository settlement were not observed. Bounded
log review found all five observed cleanup leases reaching custom-bank absence and complete
cleanup; deferred handoffs and an uncommitted retirement refusal resolved, without an
outstanding stop condition. Log SHA-256 is
`708e3840f84074314a775480db7cb5e7558a6888bb1c8049ad243c4a8300d3c1`;
the external session retains `manual-result.md`, immutable collection and rollback evidence.
This is positive auditory/play feedback on the exact installed Development artifact, not
complete musical, input, lifecycle, process-exit, other-build or packaged-artifact qualification.

## Known Limitations / 0.2.3 Release Scope

On 2026-09-24 the product owner explicitly authorized publication of 0.2.3 with
separate 1.004 and 1.005 archives after independent review and complete automated
Release validation. The owner accepts incomplete broader manual scenario coverage,
no new 1.004 gameplay checks, no exhaustive check of individual added chords or
fractional note-value behavior, and no in-game test of the exact packaged archives
as version-scoped unqualified limitations. The 1.005 Development feedback above
does not transfer to another build or artifact. This decision waives no demonstrated
defect, failed automated check or blocking review finding.

On 2026-09-09 the product owner explicitly authorized publication of 0.2.2 and
accepted incomplete focused manual coverage (including broader long-group scenarios),
no new 1.004 gameplay checks and no repeated test of the exact packaged artifacts on
either build as version-scoped unqualified limitations. The Development evidence above
does not transfer to either release artifact. Independent release review and complete
automated build/test/package checks remain mandatory; this decision waives no demonstrated
defect, failed automated check or blocking review finding. Historical manual gates below
are superseded only for this version's explicitly accepted unqualified scope.

On 2026-09-08 the product owner explicitly authorized publication of 0.2.1, including
commits, tag and per-build GitHub packages, after independent review and complete automated
Release validation. Incomplete manual scenario coverage, no new 1.004 gameplay qualification
and no repeat test of the exact packaged artifacts on either build are accepted as
version-scoped unqualified limitations. The successful Development feedback above remains
specific to that artifact. This decision supersedes historical manual-qualification gates
for 0.2.1 only; it does not waive demonstrated defects, failed automated checks or blocking
review findings. It does not transfer evidence across builds or artifacts.

On 2026-09-07 the product owner explicitly authorized publication of 0.2.0, including the required commits, tag and per-build GitHub archives, after independent review and complete automated Release validation. The owner explicitly accepted incomplete focused manual coverage, no new gameplay qualification on 1.004, and no repeated qualification of the exact packaged artifacts on either build as version-scoped limitations. This decision supersedes historical manual-qualification prerequisites below for this release only; it does not waive a demonstrated safety defect, failed automated check, or blocking review finding, and transfers no evidence across artifacts or builds.

- Song-local `chord_voicings` has bounded 1.005 Development auditory/count/completion evidence below, not complete gameplay qualification. The source accepts ordered replacements for verified existing chord inputs on exact 1.004 and 1.005, bounded by their original velocity-slot width; `ignore_sound` validates against the effective composition. Definitions are shared across explicit profiles, preserved in cache/export/descriptors, and forbidden for automatic MIDI generation. Unknown builds or unavailable native capability reject this feature. Broader projection, readiness/failure withdrawal and immediate-input claims require their focused scenarios in [In-Game Validation](InGameValidation.md); free-play/wrong-key input remains stock. Exact 1.004 remains gameplay-unqualified.

- Complete native audio lifecycle behavior is not universally qualified across retry, abort, active-process exit, and broader teardown. The bounded natural-completion path described below is qualified for the retained development artifact. These unqualified combinations are accepted as known limitations for 0.1.3 rather than claimed as validated behavior.
- Matching-build evidence closes Native-HCA parity for a bounded representative structural/metadata/decode scope: all eight piano MABFs were inspected, three standard-profile durations passed pinned independent VGAudio decode and declared PCM tolerances, and all inspected header/frame checksums passed. The work corrected dynamic slot padding and a 128-frame encoder timing defect. One native extended-header profile was inspected but is not generated. No looped piano HCA was observed, so loops remain unsupported; compressed-byte equivalence and universal CRI parity are not claimed. In-game acceptance is a separate axis and was explicitly waived for this parity-release claim; that waiver does not qualify the lifecycle or manual scenarios listed separately here.
- List/UI/chart/audio behavior remains bounded by the retained manual evidence in [In-Game Validation](InGameValidation.md); a launch-only smoke test cannot establish broader behavior. The differing-native-row fix passed a focused external 1.004 list/navigation check on its reviewed development artifact, and focused 1.005 Development evidence covers the mode-audio, progressive-reactivation, authored-profile, and repeated list-return changes below. Evidence does not transfer between builds or artifact types. At the product owner's explicit decision, exact packaged-artifact qualification is not repeated for either 1.004 or 1.005 and is accepted as a known 0.1.3 release limitation rather than claimed as validated behavior.
- The first-party project license is MIT. Third-party notices remain separate and are required in every package.

## Latest Verification Summary

Historical full-repository and focused results below belong to their recorded checkpoints. At this 0.2.3 source-preparation boundary, independent exact-checkpoint canonical Release validation has not yet run; its subsequent report must identify the actual clean commit and artifact identities. Test, hook, format, and package-document facts are emitted from current CMake and source inputs by the audit rather than copied into this document.

Focused development runtime qualification has passed custom-to-custom and custom-to-native-to-custom transitions, including canonical request rebasing, the native list-return route reset, immediate custom admission, a further custom switch, and verified custom cleanup after each song. This closes that transition-specific lifecycle defect, but does not replace the remaining retry, completion, abort, and broader teardown scenarios above.

The current committed runtime has also passed repeated custom-song pause/resume checks across multiple songs without audible vanilla fallback. Retained evidence shows exact canonical/custom transition ownership, custom play confirmation, owner rebound, and canonical restoration without the old retained-cleanup native recovery path.

The retained callback-retirement artifact has now passed one combined focused scenario covering pause/resume, early list return, difficulty changes, repeated same-song admission, natural completion, results return, and immediate admission of a different custom song with custom audio, notes, and progress. Immutable evidence records exact terminal aggregate cleanup and natural Stop; the following native Set/Play refresh remained outside retired custom lineage instead of poisoning result or menu readiness. Exact custom-only release retained the canonical substrate, and the next custom admission and publication completed. A later unrequested activation ended at the process-exit evidence boundary before another list-return drain, so active-process exit remains a separate unqualified teardown claim.

Focused runtime qualification has passed active-performance custom title replacement at the exact cataloged converter call site for multiple custom selections. The retained log records stable pre-play selection identity and successful title application before playback publication; unrelated and vanilla converter paths remain fail-closed by caller and selection policy.

Phase 0 architecture hardening is complete. Both startup discovery paths now contain setup, partial worker construction, worker-body, and native startup-boundary failures under shared C++17 join-on-unwind ownership; deterministic tests cover failure and ordering semantics, and a focused launch-only smoke reached registry publication and hook initialization with all configured songs ready. The exact production aggregate mutation-lease protocol is also covered by deterministic drain/reopen characterization. No ordinary leaked lease was found, and no unsafe production timeout was introduced; the known reopen-during-drain interleaving remains an explicit ownership/decomposition constraint rather than an uncharacterized wait.

The bounded Phase 1 pre-decomposition characterization is complete. Production-used seams now cover exact native Set/Play/Stop/hooked-PlaySetup ordering, ordered controller audio-memory reads and guarded PlaySetup observations, owned descriptor mapping, immutable registry generations, and deterministic MABF/chart/cache/manifest/starter-template golden boundaries. These checks preserve native call order, partial-read and first-failure semantics, cache identity, and serialized output; they do not claim coverage of every manual memory reader or remaining retry/completion/abort/teardown branch.

Phase 2 is complete. First-party compiler policy is centralized and all owned targets build under C++23; external dependencies retain their own policy. Fresh all-target compilation, the complete CTest graph, catalog/provenance checks, deterministic artifact goldens, and release audit passed. Focused Development runtime qualification then covered startup, registry/list rendering, custom admission and guarded PlaySetup audio-chain reads, pause/resume exact forwarding, and complete list-return cleanup on the C++23 artifact, which remains installed through `Keep`.

Phase 3 is complete. Sidecar ownership, pointer-patch rollback, descriptor composition, runtime cache codec, manifest renderer, runtime-artifact validator, atomic cache-artifact writer, and in-memory audio-artifact builder are extracted behind compatibility boundaries. Runtime policy is split by setup/publication and cleanup/projection ownership behind its retained compatibility facade. Lifecycle self-tests are split into substantial foundation, OnMemory, cleanup/retirement, and cross-domain integration units while retaining one ordered executable and exact original cases. The repository extraction preserves the public facade, exact parent-derived cache, manifest, HCA, and MABF bytes, canonical renderer and validator authority, mode and trace order, failure diagnostics, and filesystem publication order. Cache decisions, validation, hashing, and publication remain in the repository coordinator. Immutable MIDI decomposition covers source normalization, a consolidated analysis core for timing/onset, melody/shared scoring, and audio-alignment measurement, and a chart-compilation package for audio prominence, configuration, candidates, assignment, difficulty, frame filtering, and diagnostics. The generator facade retains preflight, normalization, invocation, and result publication. Focused component/repository/pipeline checks and the complete CTest graph pass at the accepted checkpoints.

Phase 4 is complete. Production startup uses the tested discovery/composition coordinator while preserving native paths, lexical projection, accepted-only visible indices, and one final registry publication. Profile/list coordination uses owning views and generation-bound menu authority; chart planning, admission, restoration, playback, note-count, title, and sidecar selection retain coherent registry ownership across complete transactions. Legacy raw APIs and append-only retired storage are removed with deterministic snapshot-reclamation coverage. Production PlaySetup/Set/Play/Stop invocation and fail-closed install/shutdown sequencing share one production-used wiring authority. Accepted Development evidence qualifies shared-close activation transfer, one-shot chart claim, profile freeze/consume/admission, custom chart/audio, pause/resume, early-return cleanup, cross-song reuse, native-song isolation, uninterrupted natural completion, exact terminal Stop, cleanup quiescence, custom-bank absence, and zero route/lease/song/ownership state after returning to the list. Persistent score semantics, explicit process-detach/hook-disable/worker-join completion, final controller teardown, and leak-free broader shutdown remain unqualified and belong to later lifecycle/release work.

The custom selected-index detail behavior now scopes duration, note count, and menu-detail ScoreInfo thresholds to the exact selected descriptor/profile, including profile refresh before playback. Deterministic tests cover profile isolation, playback/vanilla fallback, bounded row ownership, and continued exclusion from result authority. Broader in-game list/profile behavior remains unqualified and is accepted as a 0.1.3 release limitation.

The current Integration candidate changes startup to install release hooks against an explicit empty custom catalog, accumulate validated lexical settlement deltas once, append one persistent immutable audio-sidecar node per admitted song, and offer every increased valid prefix as a private pending catalog. Prior sidecars are neither reread nor rebuilt; a safe reopen can therefore adopt all songs currently ready at the lexical frontier. An unchanged final prefix gets one publication-only retry only from that exact prefix's retained prepared candidate after a recoverable rejection; starting any newer offer first clears older retry ownership. Exact menu-open admission is evaluated before catalog adoption: unrelated calls and recoverable callback-admission failure forward the native original exactly once without product bookkeeping or mutation, while authoritative terminal failure takes precedence and remains suppressed. For admitted callbacks, `Adopted`, `NoPending`, and recoverable `Blocked` all forward exactly once. `Blocked` retains pending work and opens the unchanged active catalog, including vanilla/empty custom state, without wait, polling, replay, deferred input, or catalog mutation. Adoption still occurs only at the existing quiescent pre-open boundary, and open lists/playback remain frozen. List ownership follows immutable storage plus a catalog-only revision, so ordinary selection/profile state-generation changes cannot invalidate unchanged list rows; storage replacement still invalidates the prior owner. The startup Canvas wording from this earlier candidate is superseded below; progressive-prefix adoption remains internal and runtime-unqualified. The prior progressive-prefix candidate was runtime-qualified for custom playback and later adoption, while session `20260802T193604Z-9ac25886276e` exposed indefinite menu lockout from the former recoverable-`Blocked` suppression policy. This UX correction remains runtime-unqualified until the focused progressive-open/reopen scenario in [In-Game Validation](InGameValidation.md) passes.

The 0.1.1 UX behavior supersedes every startup-overlay description in the preceding paragraph. Difficulty input merges held facts from keyboard, PlayerInput, WindowProc/RawInput/HID, async gamepad, and XInput, emitting one edge per aggregate physical hold with explicit release/rearm rather than timer debounce. During Loading the overlay shows only processed/total and ready counts plus concise ordinary-validation or typed active-rebuild state. It never renders active/current-menu counts, count transitions, adoption/deferred state, or reopen guidance; internal native menu lifecycle cannot alter its vocabulary. Repository Ready shows one bounded `Piano songs ready` notice and hides regardless of adoption state. Deterministic overlap, release, repeat, provider-loss, gate, internal-lifecycle, delayed-Canvas, and pending/aligned bounded-hide tests pass; broader in-game profile and overlay behavior remain runtime-unqualified and are accepted as 0.1.3 release limitations.

For the 0.1.3 release scope, historical candidate statements above that describe further focused qualification as required are superseded by the explicit limitation decision in this section. They identify behavior that remains unqualified; they are not open publication gates. No evidence is transferred between game builds or from a development artifact to a packaged artifact.

The 0.1.2 pipeline implements fixed case-insensitive optional Mode1/Mode2 audio-source roles. Each mode resolves directly to its own override or the required base; overrides must match the base 48 kHz stereo logical frame count, processing is independent per distinct authored source, and metronome placement remains Mode0-only. Pipeline identity is advanced to v40 with role, fallback, policy, and source-content identity; exact manifests record per-mode resolution and geometry. Focused offline writer, repository, cache-manifest, and artifact-validation tests cover the deterministic contract. Focused 1.005 Development session `20260809T162747Z-7ea5a283304a` accepted checkpoint `753ac37` with the user-authored `3 Audio Test` fixture: the log recorded three separate decode/process/encode paths, expected stale-cache rejection and successful v40 rebuild/publication, four successful activations, and two native Mode0-to-Mode1-to-Mode2 traversals; the user confirmed distinct A-to-B-to-C audio, Mode0-only clicks, and preserved synchronization. No frame/geometry, cache-publication, selection, ownership, assertion, crash, or corruption failure appeared. No runtime descriptor, hook, RVA, runtime-cache binary, or MABF binary layout changed. Relaunch-based warm reuse, independent override-removal fallback, and broader real-world mixed-codec corpus behavior remain unqualified.

The 0.1.2 runtime also preserves qualified activation readiness across redundant list-return callbacks instead of advancing the canonical reset observation for an exact zero/Idle route. Focused 1.005 Development session `20260809T153039Z-d50c7b969728` accepted checkpoint `921c254` after two progressive catalog adoptions and two successful custom playback starts, including immediate activation of a newly available song after returning to the expanded list. The retained log contained no `substrate_route_predecessor_drift`, activation-reservation rejection, crash, assertion, ownership violation, or list corruption. It observed one real nonzero-to-zero reset and one earlier authority-less rejected callback; the exact `already_ready` marker path was not produced by that run, so the idempotent branch remains covered deterministically rather than directly observed in-game.

The 0.1.3 pipeline accepts one through 32 ordered manually authored `profiles`, compiles and derives gameplay metadata for each profile independently, preserves the first profile as the default chart, and leaves the existing single-chart authoring form compatible. Focused 1.005 Development session `20260901T114331Z-8bed30437bb3` exercised the three-profile `Multi Difficulty Manual Test` fixture with sparse labels 0, 2, and 5, observed 4, 8, and 16 note rows, and successfully started two different profiles. The final accepted checkpoint was then smoke-qualified in session `20260901T121524Z-b028b0880572`; its immutable evidence passed startup, repository-settlement, selected-sidecar, profile-selection, chart-plan, admission, and playback boundaries with no terminal disable marker.

The 0.1.3 runtime also closes the forward revocation-epoch variant of redundant zero/Idle list-return handling that the earlier policy did not admit. Failed session `20260901T103533Z-a9e516472f6c` identified permanent activation rejection after reset observation drift and was rolled back. The corrected Development artifact preserved immediate custom activation and multi-profile playback without the prior `substrate_route_predecessor_drift`; final synchronization holds borrower ownership stable through classification while every native identity and structural predicate remains fail-closed. Exact packaged-artifact in-game qualification for both declared builds is waived for 0.1.3 as stated above.

The earlier restricted count-driven extended-chart artifact received focused 1.005 Development qualification at both bounds. Session `20260902T175102Z-a761305b6d59` qualified 513 rows at checkpoint `88f1f99`. Session `20260902T202554Z-ba1ee1e9374f` qualified 8192 rows at checkpoint `827a7c8`: the retained log records exact synchronous native authority, one reserve substitution from 512 to 8192 with capacity 8192, validation of the 512-event prefix and all 7680 tail events/callbacks, count-last commit 8192, exact lifecycle/playback publication, native score counters totaling 8192, natural completion, replacement invalidation, and successful chart/audio playback of a subsequent ordinary 481-event song. The reviewed ASI SHA-256 was `c1cfeb1f16d207ea1dd6268822dee4c4ba76584469493abdd7d9b7d3ee4aba6c`. That evidence qualifies the shared 513–8192 mechanism only for that artifact's restricted monotone-only, ungrouped, dot-free shape without chord, camera, IgnoreSound, or strength state; its former 512-only ordinary-authoring, diagnostic-fixture, and 1.004-fail-closed product policy is historical and does not describe the current source.

The current Integration source automatically enables 513–8192 authored and generated charts only after complete exact-build capability validation and successful reserve-hook installation, with no user option or diagnostic-fixture authority. Failure at any gate retains native-512 rejection without clipping. Builds 1.004 and 1.005 use separate immutable helper specifications while sharing the reviewed transaction algorithm. Offline tests cover ordinary authored profiles, generated MIDI, complete prefix/tail cache transport, generalized groups, dual rows, chords, and IgnoreSound. Focused 1.005 Development session `20260904T083654Z-1502682d5087` qualified an ordinary authored, ungrouped 520-row chart at checkpoint `261806d`: the exact plan was `R=520`, `P=512`, `E=520`, and `A=520`, all eight tail events were constructed, native completion and ScoreInfo/list-return cleanup succeeded, and the user confirmed all 520 notes were displayed without the earlier post-completion freeze. The accepted ASI SHA-256 is `5088704d2f0720f9f09624b484c8ec711586d668db40522b35b06a92ded930fe`. The canonical aggregate machine assessment remains `NotObserved` because the captured window lacked complete startup ordering, repository-settled state, and process-detach evidence; those limitations are retained rather than promoted to a broader claim. This result does not qualify generalized grouped, dual, chord, or IgnoreSound shapes on the current artifact. Build 1.004 currently has static/offline evidence only. Counts above 8192, unknown builds, and packaged-artifact or Release behavior remain unqualified.

Focused exact-1.005 Development session `20260904T133418Z-8f0569f41f00` was canonically finalized `Accepted/Keep` at checkpoint `193285be6155b8c8b5c056e8a3266f8f1f398831` with installed ASI SHA-256 `e8ab3e86fefc6ea88397639848c2c24cbd8438091d2af74e0bb89f1b69909268`. Its bounded split-articulation fixture contained five ungrouped rows with no retained tail and exact plan `R=5`, `E=7`, `A=7`. It exercised independent monotone/chord values including whole `(0,0)` versus dotted-sixteenth `(4,1)`, dotted whole, half `pca_Db`, a dotted-quarter/eighth dual row, and dotted-sixteenth. The user confirmed the expected notation was visible and audible; natural completion, custom cleanup, and return to the list succeeded. The aggregate machine assessment remains `NotObserved`, so this does not qualify complete startup/process-detach evidence, other note-value combinations, grouped or extended-tail split articulation, scoring/judgement equivalence, animation universality, packaged artifacts, unknown builds, or exact 1.004 runtime behavior. Exact 1.004 remains runtime-untested.

Phase 5 retains the accepted same-translation-unit activation, borrower, and retirement structural extractions from the pre-experiment boundary. The later controlled live-teardown work is removed and is a **NO-GO**: live same-process ASI teardown, unload, and reload are unsupported and are not release requirements. Session `20260731T120530Z-351a81e41d56` produced a formally successful schema-2 report and was followed by a user-visible Fatal Error; session `20260731T190357Z-3e4ebbfe054d` produced a formally successful schema-3 report, stopped custom audio, and was followed by a user-visible Fatal Error. Both sessions were `RolledBack`. The reports therefore do not establish safe teardown. The logs do not contain the user-visible Fatal Error, and no root cause is claimed; delayed callback or retained-trampoline execution remains only a hypothesis. Normal product lifetime remains process-bound and attach-only, with cleanup during ordinary song/list transitions and final lifetime termination at process exit. See the retained evidence disposition in [Controlled Teardown Invalidation](../analysis/ControlledTeardownInvalidation-20260731.md).

The ordinary dual-event audit found that the historical five-row split-articulation scenario's planned `E=7` did not itself prove a post-expand native count of seven: the old cleanup could cap it to five source rows. Its observed notation/completion evidence remains retained, but cannot qualify preservation of every event. The later dedicated 1.005 fixture result below supplies bounded native-count and score evidence for the corrected event-derived bound; it does not retroactively strengthen the historical artifact's evidence.

The 2026-09-05 Integration checks built the production ASI and passed `pipeline_selftest`, `extended_chart_selftest`, `midi_chart_compilation_selftest`, and `runtime_cache_codec_selftest`, including the new count/alternate regressions and parsing/compilation of the bundled JSON examples. `song_repository_selftest` passed its preceding cases but failed at `synthetic_profiles`; an isolated clean build of baseline `c1058c4fc98addf852bce5de180c4c4fc0a31ccd` reproduced that failure. Independent integer-frame measurement confirms the first published half-second stream metric, not the fixture's pinned expectation; further profile labels/windows also disagree with the old expectations. Selector behavior versus the intended reviewed profile contract needs separate investigation. Assertions were not relaxed, later warm-cache assertions remain unreached, and a fully green repository suite is not claimed. The manifest golden now verifies and normalizes its process-local policy generation, so both isolated and full-suite execution pass that boundary. No package or new in-game qualification was performed.

The 2026-09-06 chord-voicing Integration checks built the production target for both declared catalogs. Exact-1.005 focused checks passed the parser/compiler and bundled examples, cache codec, descriptor/startup composition, resolved export, registry, chart limits, hook ordering, and native adapter harness. The harness exercised real MASM register/stack/unwind behavior plus pending input, sealing, token evolution, nested scopes, follower identity, invalid native identity withdrawal, and retained failure; it does not execute the game's emission loop. Its initial invalid-time fixture and subsequent missing-binding dereference were corrected without weakening production sealing. A separately identified known-override stock-fallback path now withdraws authority instead; unrelated callbacks still forward once. Focused repository cases `chord_voicings`, `authored_profiles`, `resolved_song`, `growth_cache_manifest`, `extended_diagnostic`, `gain_envelope_hca`, and `offline_goldens` passed. Format-16 byte/hash goldens were refreshed only for deliberate serialization/version changes, retaining semantic, corruption, source-binding and round-trip checks. Exact-1.004 production compilation and focused rejection/codec/descriptor/harness checks passed; this does not enable or qualify authored voicing there. The baseline `synthetic_profiles` discrepancy above remains out of scope. No package, installation, or new gameplay qualification occurred.

The chord-voicing review follow-up retains failed-chart membership for denial-only classification of further root/follower callbacks from an already-entered update. Playback is observed before cleanup so withdrawal between snapshots cannot erase both identities; cleanup never grants projection authority. The production build and callback/ABI harness passed after this correction. The build writer and Development/package validator now share one production-input inventory containing the MASM adapter but excluding its test-only harness. Provenance adversarial checks passed, including ASM-only source drift rejection and harmless harness-only changes; the rebuilt real artifact's provenance also validated. These checks do not replace gameplay qualification.

Independent [1.004 chord-voicing evidence](../analysis/ChordVoicing1004Evidence.md) now supplies its six existing catalog roles, unique instruction windows, matching RSI handoff, native ownership and pre-judgement readiness path. This supersedes the earlier source policy that rejected 1.004, not the historical test or gameplay evidence. The shared implementation and serialization layout are unchanged; only mapping-bearing cache identity changes to record the shared verified capability. Ordinary song caches are unaffected. Static evidence does not newly qualify the full 1.004 monotone inventory or audible resolution of every sound name; native name resolution remains fail-closed.

The 1.004 enablement batch built both production catalogs and passed the focused pipeline, native voicing/ABI/catalog-byte, cache codec, descriptor, resolved-export and hook-order tests on each build, plus the repository's `chord_voicings` case. The final build selection is 1.005 for the user's authorized manual run. Five separate silent-source fixtures passed production parsing, compilation and audio/cache preparation for dual-event preservation, groups/alternate input, shared-profile effective voicing/filtering, different-song voicing and stock comparison. The developer cache tool's stale format-15 header expectation was corrected to the current format 16; production serialization did not change. These are offline results only, not installed-hook or gameplay evidence on either build.

The 2026-09-06 focused 1.005 Development session `20260906T161624Z-49e651c903d4` was collected and finalized **Keep** for reviewed checkpoint `44bf6055ed501bb7b3f56946cd465d685fee3351`, ASI SHA-256 `c01f338bcb8e241a0c72b3eaee53b688273cf73b065697b6e893a94e82962427`. The user reported that everything seemed good by ear. All five isolated fixtures ran, including both C3 profiles: native event counts/result-counter sums were respectively 7/7, 11/9, 8/7, 10/9, 4/4 and 4/4, with exact profile score persistence, natural completion and completed custom cleanup. The ordinary five-row fixture retained seven events and scored 700. Loader/sidecar collection passed; separate source-correlated review found no unresolved rollback condition in the error-level guard/retained-cleanup records, whose subsequent recovery is preserved in the session. Three unrelated MIDI songs were skipped for pitches outside C1–C7. Full logs, fixture/provenance/rollback records and bounded `manual-result.md` remain outside Git with the session. Debug logging was restored to its prior level. This is a limited auditory/count/completion result, not an individual constituent or alternate-direction checklist, immediate-input/pause/retry qualification, extended-tail or process-detach proof, Release acceptance, or any 1.004 gameplay qualification.

The MIDI-range follow-up removes the policy-dependent whole-source rejection: valid linked non-drum attacks outside MIDI 24–96 are excluded for both row policies, with cold-analysis count/range warnings and no transposition. An empty usable source still fails; profile feasibility and native bounds remain enforced. Generated-MIDI identity advances to v12; authored-chart identity, pipeline v48 and cache format 16 remain unchanged. On isolated copies of the three previously skipped songs, exclusions were respectively 2/1237, 3/1307 and 2/2467 relevant attacks. Production cold generation and warm-cache checks passed without source changes. Ordinary policy published Beneath The Mask levels 1–3 (340/392/453 rows), Somnus 1–5 (214/247/281/319/375), and Weight of the World level 1 (474). Playable-extended policy published the same first two sets and Weight of the World levels 1–3 (474/512/512); higher levels still fail documented route, target/growth or deterministic analysis-budget limits. All extended warm runs retained byte-identical runtime-cache and sidecar hashes and did not pretend to repeat source analysis. Raw source inventories and loader reports remain outside Git under the `midi-range-de275bb-audit` temporary evidence directory; installed songs and caches were not used as scratch space.

This follow-up also closes the earlier `synthetic_profiles` test discrepancy, not a demonstrated selector regression. Historical positional labels and stream snapshots outlived independent-profile/source-timing changes; the earlier supposed 174-row Lv.2 measurement was an old expectation, not an observed baseline count. Tests now check reviewed labels/routes, calibrated target bands, exact source pitches/onsets, independent integer-frame windows/streams, complete deterministic regeneration and warm-cache equality under both explicit row policies. The full `song_repository_selftest` passed, as did focused MIDI compilation, pipeline/documentation, codec, extended-chart, selection/aggregate policy and production-audio-wiring tests; the 1.005 production target built. Only observed MIDI-identity goldens changed, retaining musical semantics and manifest length. Runtime logging now distinguishes pending/not-applicable/deferred/committed outcomes while preserving actual ownership, native identity, failed-clear and release errors; native decisions and cleanup guards are unchanged. These are offline Integration checks, not new gameplay or Release qualification. A subsequent authorized 1.005 session must verify song-list admission, playback and the revised diagnostic outcomes.

The subsequent soft-goal MIDI batch supersedes the v12 density/growth and projected-work omissions above. Preferred density, retained percentage and adjacent profile growth no longer veto a complete feasible chart. Search breadth adapts deterministically while retaining route-feasible alternatives and guarding the last available source bridge; real route margins, source/timing witnesses, nonempty output and physical limits remain enforced. The projection allowance bounds additional optimization breadth, not all mandatory work or wall-clock duration. MIDI identity advances to v13; pipeline v48, binary cache format 16 and authored-chart identity are unchanged. Cold telemetry records beam width, processed frames and measured skill-analysis row visits.

On fresh isolated ordinary/extended copies, all six labels now publish for all three songs with no omissions. Complete action counts (including tails) are Beneath The Mask ordinary 340/392/453/487/505/512 and extended 340/392/453/490/503/523; Somnus 214/247/281/319/375/399 under both policies; Weight of the World ordinary 474/489/512/512/512/512 and extended 474/549/658/770/888/1006. All pass the unchanged per-level route margins. Somnus Lv.6 is feasible at 399 actions despite its preferred minimum of 406. Cold runs took approximately 38–135 seconds per song/policy in this environment; warm runs retained identical runtime, sidecar and resolved-chart hashes and profile records, with source files unchanged. The report is retained outside Git at `midi-soft-61675a6-audit/candidate-02/verification.json`. This is not proof of globally optimal selection, six distinct levels for every source, musical quality or native gameplay on either build. The installed v12 Development session `20260906T210825Z-e17cd5427de9` remains separate and has not been overwritten by this batch.

Soft-goal focused verification passed the full MIDI compilation and generator tests, runtime cache codec, and all repository cases across the initial run and targeted continuation, including synthetic profiles under both policies, ordinary/extended bounds, physical caches, omission tampering and observed identity goldens. The independent 2,000-onset regression traversed all non-protected frames with repeatable output and measured visit counts; a one-action growth preference still produced a complete feasible chart rather than an incomplete one-action result. The final 1.005 production build and pipeline/bundled-document checks passed. No runtime hooks, installed files, game operations or release packaging changed in this batch.

The subsequent 1.005 Development session `20260907T072019Z-83d437e5dba0` tested checkpoint `0ce16f2208d86971201640643e033fc95dbf481f`, ASI SHA-256 `84d0da3161cec0d95697fac26936979d3740eaf98c5d1fc44da39e2201a335a8`. The user reported excellent gameplay alongside UI issues involving long-chart list counts, difficulty icons stuck at one, and list return losing the last played selection. These are not UI acceptance. The collected log confirms Beneath The Mask Lv.6's 523-event expansion, all 523 judged actions, exact-profile score persistence, natural completion and eventual cleanup. Weight of the World Lv.6 committed 1006 events, but left playback around 27 seconds; full-tail playback/completion is not qualified. Deferred cleanup records resolve to custom absence and idle/unowned state; bounded log inspection found no outstanding safety rollback condition. The artifact is retained as the development baseline with the UI defects tracked separately. Immutable log, detailed disposition, provenance and rollback copies remain in that external session; temporary debug logging is restored to info. No all-profile, exhaustive input/transition, 1.004 or Release claim follows.

The UI follow-up at `57ed12e` addressed those list issues. Scoped menu counts use the complete selected profile's required actions without requiring a live extended playback commit. The newly verified list-item ScoreInfo caller receives a private render-owned row: difficulty labels 0–6 produce the corresponding localized tokens, larger labels produce six tokens, and the exact title/profile label remains unchanged. The speed panel and RESULT difficulty policy are not repurposed. Accepted playback retains a stable song/difficulty bookmark; profile preferences resolve by label across catalog replacement. That candidate restored the bookmark after native open and before Ready/input draining, with exact widget/catalog/array checks, scoped saved-FName restoration, same-index synchronization and explicit rejection of input bound to a superseded target. Confirmed stock activation clears the custom bookmark. The post-Open invocation was subsequently found unsafe; the corrected native-call evidence supersedes that recommendation in `analysis/PianoUiPresentationEvidence.md`.

The count/focus/list-caller candidate passed production builds and registry, list-catalog transaction, menu-session authority, profile-list coordinator and ScoreInfo result-policy tests on both exact builds. The final six-icon-only policy passed the affected registry/result-policy checks and production build on 1.005; no numeric fallback setter is installed. Those offline checks did not cover native Open's existing focus-history registration followed by the candidate's second registration.

The authorized 1.005 session `20260907T085725Z-ef9b9fc8976c` tested `57ed12e95abe3e787d1078399345d97ccc4d56ec`, ASI SHA-256 `41464889ee938e703dc1e17a61dbef4e6a1f1c05e15b530265bcbea3ebcef54f`, and **failed gameplay**: the first custom song completed, then subsequent custom selections produced vanilla music and charts. Loader/sidecar `Passed` explicitly excludes this mechanic and does not override that failure. Immutable log SHA-256 `8a9cb4910b8961b9f0f249f1518da41c26d61280e8a719bf212bafb7b23ab365` records Melodies of Life custom publication, natural completion and enabled/idle cleanup, followed by confirmed Multi Difficulty Manual Test, Nexus Feature Integration Test and ZZ Check 01 reservations lost during native Close, then empty custom claims and native fallback. Independent recovery proves the additional post-Open restore call registers duplicate native focus-history entries without deduplication; Close restores their intermediate targets. The precise final callback revoking the reservation was not logged. Canonical Rollback restored the kept `0ce16f2` ASI `84d0da3161cec0d95697fac26936979d3740eaf98c5d1fc44da39e2201a335a8`; the failed ASI, provenance, log and manual report remain in the session, and logging is back to info. The correction must intercept the single native Open-owned restore call rather than add another registration, preserve admission guards, and demonstrate subsequent custom admission after reopen; no corrected runtime result is claimed yet.

The correction removes the post-Open helper invocation and intercepts the one native restore call inside original Open under the existing menu-session owner. Both exact builds have independently verified entry/caller windows. A nested-safe synchronous Opening context distinguishes original entry from projection outcome: ordinary/no-bookmark calls forward once, successful projection enters once, and failed mutation or an already-entered original cannot trigger fallback replay. Temporary saved-FName restoration occurs inside that call; final identity/index/selection/coordinator checks occur after Open without another focus registration. Menu counts, six-icon policy, reservation/claim guards and audio ownership are unchanged. The new hook participates in the owner's existing transactional install/disable/drain/rollback. Production builds and five focused menu/list/registry/coordinator/hook-inventory tests passed on both builds, ending on 1.005. The regression combines production interception/projection/finalization and registry/menu/activation policies with a native-history model, covering first play, same/different-index reopen, one balanced registration, preserved next reservation and persistent claim; it is not native gameplay evidence. Unrelated/nested/skipped calls, native exceptions, mutation failures without replay, and hook-install rollback are also covered. The corrected artifact has not been installed or gameplay-qualified; the previous working build remains installed, and the next focused run must demonstrate second and later custom chart/audio admission.

The subsequent offline MIDI Integration change gives salience priority over spare route headroom at equal density-goal error, while retaining feasible-route beam survivors and all final feasibility gates. Bounded fallbacks now compete at actual selection rather than being deleted near preliminary melody attacks, and retain their own source timing. Level-1 regressions preserve the exact accented G#5 onset with and without dense simultaneous accompaniment, and a humanized alternate after its preliminary primary fails lead-in filtering; all three reject the prior ranking/proximity policy. Full generator and compilation behavior tests passed, including deterministic complete long-source traversal, as did runtime-cache codec and focused repository identity/publication, physical/dense cache, resolved-song, authored-profile and synthetic-profile cases. Compilation's full suite took 203.8 seconds outside CTest's former 60-second limit; its bounded timeout is now 360 seconds with serialized scheduling. Generated-MIDI cache identity advances to v14 only; authored identity, pipeline v48 and binary format 16 do not change. This is synthetic offline evidence, not a real-song musical-quality assessment, a tracker/phrase-model improvement, or runtime qualification. No ASI, installation or package was produced.

The user subsequently confirmed that the previously pending Development session `20260907T101402Z-8c7002ae6b9d` worked normally. Its available log was collected, the loader/sidecar boundary passed, and the session was finalized with Keep as a local development baseline; this does not establish every UI scenario. The authorized MIDI test installation then built the current worktree for 1.005 and prepared session `20260907T140643Z-ed03f8f87463`, ASI SHA-256 `bfceeefa569578f085d35b1a7e91a196681847c1f4b8382c50e22de9dbf4947d`. Exact executable/provenance and installed/dist hashes matched. The previous ASI `9cdfadcf64a83f72d1f79440e597c0e052e9671e09c1c8c74f26ccb7f5345186` and logs are retained for rollback.

On 2026-09-07 the user reported good music/notes and repeated custom playback without crashes or hangs on that MIDI candidate. Collection and independent source/log correlation supported **Accepted/Keep** through the normal stopped-process and artifact guards. The immutable aggregate assessment remains **NotObserved**, with sidecar readiness but incomplete accepted startup ordering and no full repository settlement. Alicia Lv.1 and Forest Maiden Lv.6 naturally completed with result-counter sums matching their 199 and 215 planned events and exact-profile persistence; Alicia Lv.3 and Lv.6 exercised early list return instead. Nine menu opens recorded one original call and readiness, followed by repeated custom publication. Deferred native handoff and retained-retirement attempts resolved to committed cleanup, custom-bank absence and idle/unowned state; no unresolved concrete stop condition was found. Full log SHA-256 `8cde004ad8c52b8d4ae49cf1bba5dba5e5245c05def3480965e29fad0c325048`, immutable collection, source-correlated line references and `manual-result.md` remain with the external session. Temporary debug logging was restored to info without changing other settings. This supersedes the pending installation disposition, but does not qualify every musical reduction, full settlement, extended-tail playback on this artifact, exhaustive input/transition scenarios, process-detach/leak-free shutdown, 1.004, or packaged artifacts.

The full release-range review identified two concrete failure paths beyond the MIDI-selection change: required extended admission/finish rejection could leave a custom prefix playable, and warm-cache decoding could reject a valid group rooted at the transport prefix's final row. The correction makes the expansion owner consume admission failure while it still owns cancellable preparation; original stock expansion requires restored source state and cancelled custom audio. After native expansion, a failed required extension withdraws exact publication authority and retains a cleanup-lease-owned update denial, including charts without chord voicings. Reparse does not clear that denial; exact cleanup retirement does. Existing synchronous native tail rollback is unchanged. The cache reader now applies complete topology checks to the assembled profile while retaining prefix-field/source, hash and policy validation, with no layout or identity bump. Focused behavioral regressions cover admission outcomes, before/after publication, stale leases, retained denial/retirement, and root/non-root boundary-group cold/warm reuse with malformed-chart rejection. These source corrections have not been installed or gameplay-qualified.

The coordinated correction build and registry, extended-chart, chord-voicing and codec checks passed. The repository boundary fixture initially failed: duplicate source rows were corrected, then an executed trace proved that a valid authored tail was still subjected to the MIDI-only minimum lead-in. That predicate now applies only to generated MIDI, matching full-profile timing validation. Root/non-root `extended_diagnostic` warm reuse passes with cold-rebuild fallback disabled, unchanged artifact/semantic checks and useful rejection-stage diagnostics; `physical_midi_cache` also passes, preserving MIDI timing and corruption checks. The material runtime/codec re-review found no blocking defect; the final authored-timing correction and complete clean-checkpoint Release workflow remain separately reviewable evidence. No native rejection/destructor execution, new in-game run or exact packaged-artifact qualification is claimed by these offline checks.

The first independent 0.2.0 Release run at `e8c280228c11e7475d9443890514ca475452af08` passed its 1.005 full build, both-build generated-catalog checks and actual-DLL provenance, but stopped on failed CTest checks before packaging or a 1.004 build. The retained report is outside Git at `FF7RPianoSongs-release-validator-e8c2802-20260907/validator-report.md`. Follow-up comparison reproduced the historical manifest goldens and proved that only pipeline identity and semantic hashes changed with the already-reviewed chord-voicing serialization. Test hashes and the obsolete first-line corruption identity were corrected without changing renderer/validator behavior. The installer fixture now includes the required production MASM input; the production installer and provenance checks are unchanged. Their complete focused tests pass.

Repository expectations also retained superseded selection assumptions: the root difficulty need not contain a chord, source-backed lower-register alternatives are valid, and every profile need not start on the first source attack. Tests now verify exact source pitches/onsets, native spelling, side-specific values, route feasibility and preserved final attack/audio tail rather than pinning those old selection choices. The full run passed through the corrected accidental/cache case; the later synthetic-envelope and every remaining internal case passed separately after the first-input correction. Detailed historical/current manifest comparison and remaining-case logs are retained outside Git under `manifest-oracle-e8c2802`. No production selection, serialization or cache identity changed in this test-only follow-up. Observed prefix and remaining-case runtimes total approximately five minutes, so the repository test's bounded timeout is increased from 240 to 420 seconds; assertions and scenario coverage are unchanged. A successful unfiltered run and both-build clean-checkpoint packaging remain for the independent validator, not inferred from these partial runs.

The first independent 0.2.1 run at `522ae8bf70c2206473fc78a6974754d7fde6dde1`
passed its 1.005 full build, actual-artifact provenance, both-build catalog checks and
47 registered tests, but `song_repository_selftest` timed out at its 420-second limit.
No package was produced. A separate read-only run of the same unchanged executable
completed all 23 internal cases in 376.46 seconds, exit zero. Buffered output arrived
only at completion, explaining the empty timeout capture but not identifying the
precise original progress or cause. Physical-MIDI caches, synthetic profiles and row
limits accounted for most execution time. High host load was observed in the diagnostic
run; no unrelated applications were stopped. The measured pass left little headroom,
so only the registered bound advances to 600 seconds, retaining all assertions,
processors and resource locks. Production code is unchanged. The original failed
Release report and separate `repository-timeout-diagnosis-522ae8b-20260908` evidence
remain outside Git. This diagnostic does not waive the failed gate: an independent
registered full-suite pass and complete clean-checkpoint packages are still required.

Maintainer evidence is indexed by `analysis/README.md` in the source repository and is intentionally excluded from the user package. Those records do not qualify the limitations above.

## Artifact Disposition

Repository builds and staged packages remain engineering artifacts until the exact 0.2.3 release-candidate commit passes independent Release/package validation. `package/` is generated transactionally from source, `release.json`, and `package-docs.json`; it must not contain repository-only architecture, build workflow, detailed analysis, developer tools, caches, or music examples. Do not install or launch the game as part of automated release verification. Exact packaged-artifact in-game qualification is explicitly accepted as an unqualified 0.2.3 release limitation above and is not a remaining publication gate. Validation results must name their actual clean commit and artifact identities; the version update itself is not successful qualification.
