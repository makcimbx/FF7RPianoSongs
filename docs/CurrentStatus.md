# Current Status

Updated 2026-10-20. This page describes the current implementation, release
limitations and tested artifacts. Public version history remains in
[CHANGELOG](../CHANGELOG.md); commands and acceptance procedures belong in
[Build And Release](BuildAndRelease.md) and [In-Game Validation](InGameValidation.md).

## 0.3.0 release disposition (2026-10-20)

- The release packages use the standalone proxy DLL, with separate archives for
  the game builds declared in `release.json`. The alternative ASI is not bundled.
  Package identity, checksums and build provenance belong to the generated
  release sidecars; no publication or new game installation is implied.
- The product owner reports having tested the current development version:
  "everything works." No exact artifact hash, per-build scope, input-device
  inventory or completed scenario matrix was supplied with that report. It
  supersedes pending user feedback, not the historical logs or their bounded scope.
- The owner explicitly accepts the absence of fresh exact-ZIP gameplay and full
  per-build manual qualification for **0.3.0**. This includes older-build startup
  and gameplay, preview alignment/audio and native-controller controls, autoplay
  record preservation, and the complete native audio/chart transition matrix.
  Automated checks and the general positive report do not establish those claims.
- The earlier same-song replay/profile-change rejection below has no independently
  attributed fix or exact-artifact reproduction result. Retain it as a historical
  unresolved report, not a newly demonstrated release failure or a verified fix.
  The positive user report does not establish a targeted regression pass.
- This version-scoped manual-coverage decision does not waive automated build,
  test, audit, provenance or transactional package failures. Canonical Release
  validation is still required before a new archive is claimed complete.

## Older-build preview anchor (2026-10-20)

- Independently recovered the preview geometry verifiers, root globals and getter
  RIP references from the imported 1.004 program. Imported PE identity matches the
  catalog; the consumed widget, controller, geometry, parent and root layouts and
  coordinate conversion agree with the current reader. Exact per-build values
  and static evidence remain in the canonical RVA catalog.
- All entries remain optional, without hooks or native calls. Missing or
  mismatching gates, failed reads and inadmissible layouts hide only the badge;
  geometry admission is not a preview-audio, menu or gameplay prerequisite.
- Catalog generation/check, proxy compilation and focused anchor, preview-runtime
  and startup-overlay tests passed for both declared builds; source-document
  audits passed too. Independent review found no concrete defects and confirmed
  unchanged default-build mappings and optional-failure isolation. The build was
  returned to the default catalog. No fresh full imported-file hash, older live
  run or visual qualification is claimed. The installed proxy is unchanged.

## Song preview development (2026-10-19)

- The preview badge was redesigned (navy panel, time or control hint, whole-song
  timeline with the excerpt window, a 16-band equalizer while playing, and a
  contrast-stretched waveform when idle). Row changes and the manual toggle now
  fade out briefly instead of cutting, with a 400 ms hard-stop fallback. An
  independent review found no blockers; full ctest passes. Installed through
  prepared session `native-proxy-20261019T183459Z-73f804efd5044c1b818b5f44c29fdab8`
  (prior session rolled back); in-game listening and visual checks are pending.
- Autoplay L3+R3 and preview R3 were XInput-only, so a Bluetooth DualSense without
  XInput emulation could not trigger them. Native PlayerInput stick clicks are now
  merged in as event-backed facts cleared on focus/application loss, device
  removal and teardown.
- The badge now appears only while a preview loads or plays, fades in and out over
  150 ms, and its clock shows the real song position and length instead of the
  excerpt clock. Idle waveform and control hint were removed. The excerpt
  `waveform` remains in the runtime cache codec but is no longer drawn. Reviewed
  (no blockers) and installed through prepared session
  `native-proxy-20261019T192442Z-9756db1d00154c76989403456cc107fc` (prior session
  rolled back); in-game DualSense L3/R3, fade and clock checks are pending.
- Integration work is in progress for custom-song previews: automatic after a
  selection dwell by default, with manual and disabled modes in the INI.
  Excerpts and waveform metadata are prepared offline; preview playback must
  remain independent of native gameplay audio, charts and scores.
- Prepared excerpts are bounded PCM artifacts with sound-derived waveform data,
  validated on cold and warm paths. Cache identity v49/runtime format 17 requires
  rebuilding earlier song caches; gameplay audio generation is unchanged.
- Authenticated read-only live observation and foreground scrolling correlated
  the selected row with the game's root-overlay coordinate conversion. The
  catalog-gated badge follows that row on the default build; absent or mismatching
  gates and unqualified layouts hide it rather than using guessed coordinates.
- Focused pipeline, descriptor, configuration, policy, audio, renderer, anchor,
  input/startup and native compatibility tests pass, as do the source-document
  audit and diff check. The production proxy built successfully without WinMM
  imports. Independent reviews found no remaining concrete blockers in preview
  lifetime, borrowed geometry reads or the combined worktree's native-cache
  changes. A documented WinMM position-unit fallback was corrected and tested.
- After explicit human authorization and verified game/bootstrap closure, the
  prior owned proxy session was rolled back and the reviewed preview proxy was
  installed through prepared session
  `native-proxy-20261019T175733Z-37d18ef68309478d892d03facf1d12ce`.
  Both DLLs, receipts, production-input hashes, checks and prior-version recovery
  plan remain under
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/song-preview-integration-20261019T175536Z-857aba5f`.
  Installed bytes match the reviewed candidate; settings, songs and scores were
  not modified. Actual-device listening, drawn scrolling/close/reopen/focus
  checks and post-exit collection remain pending. Preparation is not a gameplay
  or Release pass; the external-fixture native warm-cache benchmark was not run.

## Autoplay development (2026-10-19)

- The Integration candidate implements a seated-only toggle with configurable
  Ctrl+F-key input, same-XInput-controller stick clicks, distinct on/off sounds,
  and no native or custom personal-record replacement after any assisted play.
  Native note timing and actual score/rank calculation are retained for stock
  and custom songs. Its first focused run failed: custom songs were absent
  from the list. That candidate was rolled back; the corrected candidate later
  passed the bounded human check below, not full gameplay or release qualification.
- Independent scoped reviews found no blockers in the marker lifetime, copied
  input authority, nested/concurrent result shadows, optional-hook degradation,
  sound ownership or older-build catalog additions. The sound helper resolves
  the genuine System32 WinMM API without importing the supported proxy basename.
  Focused tests cover these policies and failure paths; actual device audibility,
  automatic notes and personal-record preservation still need human validation.
- Both catalog builds have statically recovered optional autoplay contracts;
  [1.004 evidence](../analysis/AutoplayRuntimeEvidence-1.004.md) is independently
  recovered, PE/signature-correlated import evidence, not an authenticated older
  disk or gameplay run. ASI/proxy builds and the nine selected tests pass for
  both catalog builds; the default build is restored in the build directory.
  Final DLL import inspection confirms no WinMM/waveOut imports in either
  default-build product. Complete
  optional-hook rollback leaves the rest of the product functional; retained
  hook ownership still fails closed. This is focused Integration evidence,
  not release qualification.
- Static contracts are retained in [runtime evidence](../analysis/AutoplayRuntimeEvidence.md)
  and [record-suppression evidence](../analysis/AutoplayRecordSuppressionEvidence.md).
  A human-authorized, non-mutating [Frida observation](../analysis/AutoplayRuntimeObservation-20261019.md)
  covered stock-list entry, pause/continue, replay through the list and standing
  up on the default build. It confirms that a retained chart is not gameplay
  authority; it does not qualify automatic notes, scores or another build.
- That observational run exited normally without changing the installed proxy.
  Subsequently, the human authorized installing the reviewed autoplay candidate
  while the game was closed. Prepared session
  `native-proxy-20261019T155834Z-622c330c08694682a501ed11565325c1`
  owned that candidate proxy; the product ASI remained disabled. The prior session was
  collected and rolled back before preparation, not overwritten. Both proxy
  binaries, reviewed receipts and the prior-version recovery plan are retained
  under `%LOCALAPPDATA%/FF7RPianoSongs/captures/autoplay-integration-20261019T155755Z-541c7300`.
  The human reported missing custom songs. The log shows completed song loading
  and repeated catalog-adoption rejection at `callback_gate_not_exclusive`.
  After human exit, the session was collected and rolled back; the exact previous
  proxy was restored through prepared session
  `native-proxy-20261019T163007Z-e5344e5c7ab447ecadd1eb90c09e401f`.
  The product ASI remains disabled. The failed candidate/log and prior bytes remain
  retained. Root cause: the new enclosing controller tick held the shared callback
  lease across native forwarding, so nested list adoption was never its sole
  owner. The correction isolates tick lifetime accounting and drains it before
  dependent service teardown, without weakening adoption's concurrency check.
  Independent concurrency review found no blockers. Corrected ASI/proxy builds
  pass for both catalog builds; the three correction tests plus catalog audit
  pass for the older build, and ten selected tests pass with the default restored.
  Final default DLL inspection again confirms no WinMM/waveOut imports.
  The human authorized a corrected-candidate installation after these checks;
  no automatic-note, audible-feedback or record-preservation success is claimed.
  No diagnostic product binary or release package was installed.
- The corrected autoplay candidate was accepted and retained in session
  `native-proxy-20261019T164143Z-1eeab49b65e746e791bba976376246e7`.
  The temporary previous-version restoration session above was rolled back
  without a gameplay run before this installation. Both artifacts, receipts,
  score baseline and recovery plan are retained in
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/autoplay-adoption-fix-20261019T164115Z-57055649`.
  The human reported that autoplay works very well and the game was closed.
  Post-exit collection verifies successful custom catalog adoption with no
  `callback_gate_not_exclusive` recurrence. The log records custom Amadeus chart
  activation, three logical play generations, on/off transitions and an assisted
  final play reaching results then returning to the list. No error/fatal or
  feedback-failure marker appears in this log. The custom scores INI is byte-identical
  to the pre-run snapshot; this does not establish completed-custom-play or stock
  personal-best preservation. Per-song timing, audible signal distinction, exact
  input-device coverage and native record behavior were not separately confirmed.
  The session was collected and finalized `Keep` with its bounded manual report;
  the reviewed development proxy remains installed and rollback evidence retained.
  This is a successful focused development check, not release qualification or
  gameplay qualification for the older build.

## Second-play chart rejection investigation (2026-10-19)

- A reported same-song replay after a difficulty change rejected the final selected-row capture. The old `selected_provider_changed` marker also covered capture failure; its zero output does not prove provider drift or a catalog-republication defect. Earlier captures and audio admission succeeded.
- Added bounded first-failure capture facts and separated failed capture from observed identity drift, without changing validation, native reads, or fallback behavior. Independent scoped review found no actionable issues; the runtime, adapter, selected-row and extended-composition selftests pass.
- The diagnostic proxy session `native-proxy-20261019T130912Z-1ce0c5f8fea440aaa8502a33396912df` was collected and rolled back after a subsequent startup failure. The rollback restored its prior product ASI. This supersedes the installed-artifact disposition below, not its historical acceptance evidence.
- Original replay log/DLL, source snapshots, reviewed receipt and focused test output are retained under `%LOCALAPPDATA%/FF7RPianoSongs/captures/replay-chart-20261019T155547`. A new startup log is retained under the sibling `cache-setup-failure-20261019T164851` directory: GU object-count growth during a binding uniqueness scan produced `gu_header_drift`, terminal native bindings failure, and repository publication rejection.
- The startup recovery candidate treats only readable, unchanged-storage count growth with unique scanned candidates as `IncompleteSnapshot`, not success. Initial authentication remains pending for the next existing updater; acquisitions and registration are not repeated. Ambiguity, storage drift and post-bridge failures still fail closed. Independent review passed; bindings, runtime, adapter, selected-row and extended-composition selftests pass, including repeated deferral, recovery and an ambiguous appended tail.
- The reviewed recovery proxy was installed through session `native-proxy-20261019T135529Z-20c6d22425cf49119954c19e8ad602db`; its source/binary/receipt/test evidence is in the startup-failure capture directory above. That session was later collected and rolled back for the authorized autoplay installation above; its proxy was preserved for recovery. The original replay failure remains unresolved, and no Release qualification is claimed. Forced reproduction is not required.

## Offline MIDI generation investigation (2026-10-18)

- Corrected the historical reference corpus provenance: retained “VanillaPiano” assets are modded, not stock; historical route settings are not newly qualified stock/controller limits.
- Added the [stock export/comparison benchmark](VanillaBenchmark.md) and repaired the optional parent decoder, retaining required-input/group/control identities separately from musical attacks.
- Generated-policy v22 retains simultaneous tracked RH + representable LH pairs and adds the [source-exact LH/local-selection corrections](../analysis/MidiSelectionCorrections-20261018.md); policy-keyed generated caches rebuild, authored formats and binary layout are unchanged.
- The [eight-chart roundtrip](../analysis/MidiGenerationRoundtrip-20261018.md) improves attack retention but also increases some uninterrupted input series. It does not prove reduced fatigue or solve all LH underuse.
- The [installed-song review](../analysis/InstalledSongGenerationReview-20261018.md) exposed wrong-register full/superset LH admission. The follow-up now requires literal source pitches and truthful witnesses; existing exact reduced bass/two-tone carriers retain compatible accompaniment without invented octaves. Across the final isolated corpus, independently detected LH mismatch rows fall from 1,038 to zero. Unavailable or spacing-conflicting harmony can still be reduced or omitted.
- Generated-policy v23 ranks equally exact partial/reduced LH carriers by an emitted carrier root, fewest ignores, accompaniment pitch-class agreement, plain major/minor triads, then whole-onset agreement before lexical ID. The lexical rule had named lone C3 `pca_A_dim` and favored dim/sus4 names over the heard harmony; emitted sound is unchanged.
- Generated-policy v24 raises RH floors toward stock minima (0.35..0.15 s), lowers LH floors (3.0..0.45 s) so accompaniment and chord repeats fit, and limits RH root changes under 0.25 s to repeats or steps of at most 4 semitones. v25 widens that fast step to 7 (Lv5) and 12 (Lv6) semitones, because one fixed step made dense leaping sources such as Everlasting Summer select identical Lv4..Lv6 actions. Calibrated from stock statistics; game feel is unverified.
- Existing spacing floors now apply to native frames as well as source seconds, and selection uses a soft categorical-assignment switch preference. Independent review finds mixed local outcomes and longer series in some profiles; this is not proof of uniformly reduced fatigue.
- A bounded representative-root exchange improves source coverage without increasing required inputs or combined switch cost. All 30 isolated songs / 178 profiles prepare successfully; the Gate/Megalovania Lv6 gaps gain source-backed anchors, with no missing labels/endings and unchanged authored Rufus. All eight focused CTests pass. Remaining lower-level gaps and perceptual importance/controller comfort are not claimed solved.
- These are offline development changes, not changes to the installed artifact qualified below. Generated dual sound/scoring/tail checks, authenticated-audio listening and actual stick/retrigger/window semantics remain unqualified. No game launch, install or release package was performed for this work.

## Implemented Now

- Strict song discovery, JSON v2 explicit charts/profiles, MIDI generation,
  WAV/MP3/FLAC decoding, mode-specific audio processing, HCA/MABF generation and
  validated cache reuse. Authoring inputs are unchanged; see [Song Format](SongFormat.md).
- Automatic immutable native Music/BGMList/PianoScoreInfo/PianoScore containers
  built from authenticated source templates and generated audio. They contain
  genuine PackageStore metadata; users do not install generated pak files.
- Native mounting, independently retained DataObject acquisitions and additive
  row registration behind exact-build startup, identity, signature, ownership
  and exclusion guards. Artifact preparation is not runtime readiness.
- Genuine per-song selected names and retained chart providers, preserving
  existing list/profile/progress/score integration. Chart projection uses the
  exact selected PianoScore row and a reversible journal, not a first-basename
  stock-row match.
- The audio adapter no longer patches Sound/source fields, replays PlaySetup or
  orchestrates Set/Play/Stop. Native playback owns those transitions; the adapter
  retains chart/selection authority and readiness checks.
- Native decoding first uses the selected executable's authenticated decoder
  and default callbacks. An explicitly hash-qualified companion is a fallback,
  not a requirement when the embedded decoder is admitted.
- Offline-only builds remain separate from game memory, hook and runtime-tool
  dependencies. Native cache generation and source parsing run on repository
  workers, never in game callbacks.

## Per-Build Qualification

| Build | Current disposition | What is not established |
| --- | --- | --- |
| 1.005 | Development ASI accepted/kept after native audio, distinct-song chart and extended-chart scenarios; the later shared-code regression exercised the embedded executable decoder successfully. The owned `D3DCOMPILER_43.dll` also has a positive user report with native startup and two distinct song plans recorded. | Other proxy basenames, exhaustive songs/profiles/notes, separately witnessed pause/retry/stock transitions on the latest artifact, and complete lifecycle or Release qualification. |
| 1.004 | Separate bootstrap/runtime/asset profiles implemented; experimental ASI and full-product proxy archives built with affected offline/mapped tests passing. | Authentic older installed-asset validation, real startup, decoder interoperability and gameplay. Friend testing is pending. |

Exact executable identities, addresses and signatures remain in the
[RVA catalog](../src/game/rva_catalog.json). Older profiles were recovered from
the explicitly selected imported program; they are not authenticated older-disk
or gameplay evidence. Different virtual slots and inherited Music properties
are selected per build, not translated by RVA deltas.

## Current Evidence And Artifact Disposition

- `20261017T171411Z-4ef691a66203`: accepted 1.005 multi-song run. Amadeus,
  Everlasting Summer and Gate of Steiner used distinct projections; the latter
  committed 695 native events, including its extended tail. The user reported
  successful play and exited normally. The reviewed Development artifact was kept.
- `20261017T185024Z-b89afffc9fc4`: accepted newer-first shared-code regression.
  The log records `decoder_ready source=verified_executable`, all native cohorts
  Ready, separate Amadeus/Beneath The Mask preparations and an 845-event extended
  commit without rejection. The user reported successful play and normal exit.
  This tested ASI remains the archived rollback artifact; the older build did not
  replace it. It remains archived and disabled while the kept proxy below is installed.
- The experimental `FF7RPianoSongs-1.004-test.zip` contains only its selected-build
  ASI and mod INI with debug logging. Binary/provenance and ZIP contents were
  hash-verified. This is a friend-test artifact, not a qualified public release.

Immutable collection, manual receipts, tested binaries, provenance and rollback
copies remain in the corresponding runtime-gate sessions. Readiness collection
alone is not audible playback or note-correctness evidence. The bounded
[native integration record](../analysis/NativeAudioIntegration-20261017.md)
summarizes earlier failed runs and their corrections; those failures must not be
mistaken for the current artifact's disposition.

## Owned Proxy Development Distribution

A single-DLL proxy build contains the full mod as an alternative to the ASI
distribution. Its owned entry route does not depend on foreign-loader/CRT startup
profiles and does not load a sibling ASI. The default filename is
`D3DCOMPILER_43.dll`; optional profiles are `d3d9.dll`, `d3d11.dll`, `dsound.dll`,
`winmm.dll`, `dxgi.dll` and `xinput1_3.dll`. These names were taken from the
authenticated 1.005 executable's normal imports, not from a generic DLL-name list.
The selected filename must actually be a normal import of the target executable;
older import-name parity has not been independently established. Arbitrary
renaming or late `LoadLibrary` does not provide the required early origin.
The refreshed `FF7RPianoSongs-1.004-proxy-test.zip` contains one
`D3DCOMPILER_43.dll`, the mod INI with debug logging, and the user documentation.
Its selected-build artifact, generated entry profile, callback/export contracts
and archive contents were checked independently. It was not installed locally;
the kept 1.005 proxy is unchanged. This remains a test build pending older-game
startup and gameplay evidence.
Existing occupied proxy files must not be overwritten or chained implicitly.
The proxy and `FF7RPianoSongs.asi` are alternative distributions of the same mod;
do not load both together.

The entry transport restores the original executable entry before initializing
forwarding or the internal mod startup. Genuine System32 exports are resolved
separately. Executable authentication, the module-private active entry scope,
cold-state checks and later native-initializer/ownership gates still apply; a
caller-supplied receipt grants no native readiness. See the
[static design evidence](../analysis/NativeProxyBootstrap-20261018.json).

Both build-specific full proxy DLLs compile. Component forwarding/entry/private
origin tests, an owned synthetic static-import process, linked export inspection
and scoped native-safety reviews have passed. In current-build Debug experiment
`native-proxy-20261018T120416Z-a05267627b514b1d87eafab230391edc`, Windows loaded the
app-local `D3DCOMPILER_43.dll`, but no product log or readiness appeared. The exact
built callback was a leaf tail jump without the unwind record required by its
authenticator; the actual runtime rejection predicate was not captured. Normal
process exit was followed by hash-verified rollback to the accepted ASI. Existing
DXGI, XInput and mod INI files were unchanged. The immutable session/manual receipt
and checkpoints under LocalAppData captures preserve this failed test. The callback now performs
post-call bookkeeping; a disk-only COFF/PE regression verifies the emitted DLL's
real call/return, post-call store and complete unwind coverage. Rejection also
preserves copied first-failure facts in a new, non-overwriting
`FF7RPianoSongs.proxy-startup-failure.log` beside the DLL. Focused tests and review
passed. In follow-up Debug session
`native-proxy-20261018T123852Z-191f5a3f259247d5875b84b52d3a41fa`, the corrected
full-product `D3DCOMPILER_43.dll` recorded `source=owned_proxy_entry`, normal native
initialization, repository settlement and separate Amadeus/Believe Me chart
preparations. The user reported that it worked well. The assertion-backed
`successful-proxy-checkpoint.json` and exact log snapshot preserve that bounded
observation; other basenames, older gameplay and exhaustive lifecycle scenarios
were not tested. After confirmed process exit, immutable collection and a separate
reviewed manual receipt were saved, and the session was finalized with `keep`.
The tested proxy remains installed; the accepted ASI stays archived and disabled.
DXGI, XInput and mod settings were unchanged. No broader loader, OS or release
compatibility is claimed.

An owned-route portability audit found no active pin to this computer's UAL,
UCRT or ntdll build. Distribution still requires the chosen System32 backing
library, the dynamic MSVC runtime, writable song and LocalAppData caches, and
the supported stock archive/template layout. The export union is not an arbitrary
plugin-compatible system-DLL clone. Non-ASCII source paths and alternate proxy
names/plugin combinations remain unverified; the legacy ASI's OS pins must not
be confused with the reachable owned route.

## Remaining Limits

- Startup requires an actually witnessed early executable continuation and the
  later normal native initializer. The owned proxy validates its private restored
  entry scope; the alternative ASI route retains its qualified loader/Windows/CRT
  profiles. Unsupported sources reject; late pointers cannot reconstruct missed
  authority. No global ASI Loader INI change is required or performed.
- Native containers, acquired references and registered backing are retained
  until process exit. Release, unmount, live ASI unload/reload and same-process
  teardown are not qualified; active original-entry forwarding retains its hooks
  and module. Replace the product DLL or ASI only with the game stopped.
- Custom audio generation supports the recovered single-cue/three-mode Music
  profiles, not arbitrary Music serialization, multi-cue sources or author-supplied
  HCA/MABF/containers. Sources remain WAV/MP3/FLAC plus explicit notes or MIDI.
- Ordinary and extended chart bounds, grouping, input/chord capabilities and
  exclusions remain as documented in [Chart Limits](ChartLimits.md) and
  [Song Format](SongFormat.md). Older static parity does not establish older
  input, sound or chart behavior in-game.
- Matching-build HCA structural/metadata/decode evidence is bounded, not universal
  encoder parity. See [HCA Validation](HcaValidation.md).
- Historical release-specific manual-coverage decisions do not waive failures
  or qualify this development backend. See the
  [earlier development milestones](../analysis/DevelopmentMilestones-202609.md).

## Verification Boundary

The affected producer, decoder, startup, native runtime, adapter, chart/profile,
catalog and publication tests pass for the built targets. Installed-source
short/long generation also passed with immutable cache reuse and distinct
per-build output. Mapped native-call doubles do not replace real engine execution.
The historical development checks above are not qualification of a new package.
The 0.3.0 manual-coverage decision is recorded separately above; a new archive
still requires its own clean identity and the canonical validation workflow.
