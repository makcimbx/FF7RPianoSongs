# Current Status

Updated 2026-10-18. This page describes the current development worktree and
tested artifacts, not a new public release. Public version history remains in
[CHANGELOG](../CHANGELOG.md); commands and acceptance procedures belong in
[Build And Release](BuildAndRelease.md) and [In-Game Validation](InGameValidation.md).

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
  not a requirement of the experimental single-ASI builds.
- Offline-only builds remain separate from game memory, hook and runtime-tool
  dependencies. Native cache generation and source parsing run on repository
  workers, never in game callbacks.

## Per-Build Qualification

| Build | Current disposition | What is not established |
| --- | --- | --- |
| 1.005 | Development ASI accepted/kept after native audio, distinct-song chart and extended-chart scenarios; the later shared-code regression exercised the embedded executable decoder successfully. | Exhaustive songs/profiles/notes, separately witnessed pause/retry/stock transitions on the latest artifact, and complete lifecycle or Release qualification. |
| 1.004 | Separate bootstrap/runtime/asset profiles implemented; experimental ASI and debug-INI ZIP built with affected offline/mapped tests passing. | Authentic older installed-asset validation, real startup, decoder interoperability and gameplay. Friend testing is pending. |

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
  This tested ASI remains installed; the older build did not replace it.
- The experimental `FF7RPianoSongs-1.004-test.zip` contains only its selected-build
  ASI and mod INI with debug logging. Binary/provenance and ZIP contents were
  hash-verified. This is a friend-test artifact, not a qualified public release.

Immutable collection, manual receipts, tested binaries, provenance and rollback
copies remain in the corresponding runtime-gate sessions. Readiness collection
alone is not audible playback or note-correctness evidence. The bounded
[native integration record](../analysis/NativeAudioIntegration-20261017.md)
summarizes earlier failed runs and their corrections; those failures must not be
mistaken for the current artifact's disposition.

## Remaining Limits

- Startup requires an actually witnessed, authenticated early loader/executable
  continuation and the later normal native initializer. Unsupported loader,
  Windows/CRT or executable profiles reject; late pointers cannot reconstruct
  missed authority. No global ASI Loader INI change is required or performed.
- Native containers, acquired references and registered backing are retained
  until process exit. Release, unmount, live ASI unload/reload and same-process
  teardown are not qualified; active original-entry forwarding retains its hooks
  and module. Replace the ASI only with the game stopped.
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
No full new Release/package qualification is claimed. A new release requires
its own clean identity and the canonical validation workflow.
