# Native cached-audio control (2026-10-16)

## Scope and source identity

This is evidence for implementing a replacement for source substitution, not an
implemented ASI backend. The controlled run used the executable identified by
catalog build `ff7rebirth-steam-win64-6a16ced2`; no applicability to another
catalog build follows.

The retained parent-workspace capture is
`Tools/Frida/captures/piano-pair-session-7yc1prw7/startup-events.json`, SHA-256
`5e9ee9f5c0bb1778567a2c87ff110154ee8d26490bade6963b29432f98383ba9`.
Its assertion-backed projections are `native-start-pause-resume-summary.json`
and `process-exit-disposition.json` in that directory. These external research
inputs are provenance, not dependencies of this standalone repository.

## Observed route

1. A fresh clean process mounted a privately cached, metadata-bearing IoStore
   cohort: a genuine Music export and independently packaged BGMList,
   PianoScoreInfo and PianoScore DataObjects. No file was installed in game Paks.
2. Three independent completed streaming acquisitions retained their owning
   references. The exact objects/classes/export names, stock full-FName to
   manager bindings, supported backing interfaces and weak registry entries were
   authenticated before the class-specific additive group-2 bridges ran.
3. Stock queries before and after registration matched. The new BGMList key
   resolved the intended Music name, the new ScoreInfo name resolved that key,
   and the same selected name resolved a nonnull PianoScore row. Native weak
   construction assigned positive serials to initially zero-serial candidates;
   subsequent observations required those exact positive identities.
4. All hooks were published and checked before native preparation. Following
   complete registration, one qualified selected-entry return changed only its
   caller-local eight-byte name from Journey to Journey_97. The game itself
   populated the selected BGM key and constructed its playback model. No owner,
   Sound/source, request argument, shared stock row or native chart was patched.
5. The passive observer recorded one ordinary selected request and three later
   correlated resolver/PlaySetup pairs using the same new key, owner, Music,
   source and token. Two intermediate PlaySetup observations lacked a resolver
   tuple and remain unattributed. The user reported “Да всё отлично” after the
   requested changed-sound, pause and continue scenario.

The observer performed no request-key substitution or native playback replay.
This distinguishes the result from the earlier once-only request substitution
that reverted to stock after pause.

## Native contracts to preserve

- BGMList, ScoreInfo and PianoScore use different managers and class bridges.
  Their export basenames must already resolve through the correct manager's
  full packed-FName map. An unknown name can reset stock groups; no speculative
  name insertion or generic-append bypass is qualified.
- ScoreInfo and PianoScore accessor constructors take two semantic arguments:
  writable zeroed 32-byte output storage and a packed FName by value. They return
  the output address, even on a miss. Their forwarders are not interchangeable
  call targets. A nonnull row, not the opaque backing byte, establishes a hit.
- Only the ScoreInfo row's eight-byte BGM field and the BGM row's eight-byte
  resource field were copied. The PianoScore row was not decoded in callbacks.
- The selected name is also the chart lookup key. Providing only a ScoreInfo
  row is insufficient; the corresponding usable PianoScore row must exist.
- Registered rows are borrowed from their DataObject backing. Owners, mounted
  files and registration were retained until process exit. No live release,
  removal, unmount, retry or partial-registration rollback is qualified.
- Native preparation used a continuously monitored original-updater-return
  gate. Frida's exclusive/default-traps dispatch is not a C++ synchronization
  primitive: migrating addresses does not migrate this invocation authority.

Exact build-specific addresses and signature guards belong in
`src/game/rva_catalog.json`, not a second handwritten address table. The
research profiles and complete instruction/disk checks provide the migration
provenance; new catalog entries must not install additional production hooks.

## Limits and integration boundary

The Music carrier preserved a known export's opaque wrapper and replaced only
an equal-geometry validated bank. Arbitrary bank sizes, a general Music
serializer, arbitrary generated charts, repeated selection, restart, song
switching and the complete gameplay/teardown lifecycle were not established by
this run. The stock chart was a valid controlled carrier, not a replacement for
the product's existing MIDI/profile compiler.

Manual stop restored all checked original windows. Outer Frida cancellation
warnings remain in the evidence; normal outer game detach and native quiescence
are not claimed. The later process-exit disposition verified game/bootstrap
absence and restoration of the original proxy DLLs.

The existing product still publishes MABF sidecars, not this native cohort.
Integration must first produce validated descriptor-backed native packages,
then retain/admit them through a qualified C++ game-thread boundary and connect
their real selected names to the existing list/chart ownership. Only after that
path replaces working behavior can source patching, replay and their obsolete
audio state be deleted. Copying the research controller into another mega-file
or deleting the current backend before its replacement works is not the change.

The initial product port deliberately stops below invocation authority:
`native_music_export` implements the bounded equal-geometry export operation,
and the catalog retains exact-build resource contracts without new hook owners.
Its test fixtures are synthetic; optional local-control verification reads
copyrighted assets externally and does not check them into this repository.
Other export geometries remain rejected, not silently routed through a guessed
serializer. Neither catalog availability nor exporter success admits an asset
into the game or enables the replacement backend.
