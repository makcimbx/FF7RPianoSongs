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
  not a requirement when the embedded decoder is admitted.
- Offline-only builds remain separate from game memory, hook and runtime-tool
  dependencies. Native cache generation and source parsing run on repository
  workers, never in game callbacks.

## Per-Build Qualification

| Build | Current disposition | What is not established |
| --- | --- | --- |
| 1.005 | Development ASI accepted/kept after native audio, distinct-song chart and extended-chart scenarios; the later shared-code regression exercised the embedded executable decoder successfully. The owned `D3DCOMPILER_43.dll` also has a positive user report with native startup and two distinct song plans recorded. | Other proxy basenames, exhaustive songs/profiles/notes, separately witnessed pause/retry/stock transitions on the latest artifact, and complete lifecycle or Release qualification. |
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
No full new Release/package qualification is claimed. A new release requires
its own clean identity and the canonical validation workflow.
