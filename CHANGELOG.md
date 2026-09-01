# Changelog

## 0.1.3 - 2026-09-01

- Added up to 32 ordered manually authored difficulty profiles per song, with
  independent explicit notes, gameplay metadata, compilation, and cache
  validation while preserving the existing single-chart format.
- Kept the first authored profile as the default chart and exposed every
  profile through the existing in-game difficulty selector with its actual
  sparse difficulty label.
- Fixed a fail-closed activation lockout after returning to and immediately
  reopening the piano list during progressive song preparation.
- Hardened redundant list-return handling so forward-only reservation epoch
  changes remain idempotent only when native identity and audio ownership are
  still exact.

## 0.1.2 - 2026-08-09

- Added optional `song.mode1.*` and `song.mode2.*` audio sources with direct
  fallback to the required Mode0/base `song.*`, strict equal-timeline
  validation, and deterministic role-aware cache invalidation.
- Corrected generated metronome audio so clicks are present only in Mode0;
  Mode1 and Mode2 now remain clean unless their own authored source differs.
- Preserved custom-song activation readiness across redundant list-return
  callbacks, fixing songs that appeared after progressive caching but could no
  longer be selected.
- Hardened three-mode MABF validation with exact per-mode HCA geometry,
  resolved-source policy checks, and expanded cache/manifest test coverage.

## 0.1.1 - 2026-08-09

- Added separate fail-closed downloads for Final Fantasy VII Rebirth 1.004 and
  1.005, backed by per-build executable identities, signatures, and RVA data.
- Placed custom songs after the player's actual native piano-song rows instead
  of assuming a fixed original-song count, including progressive catalog
  replacement without duplicate or missing rows.
- Hardened progressive startup and catalog adoption so recoverable contention
  keeps the native menu usable while retaining the pending custom-song prefix.
- Improved guarded selection, playback ownership, shared audio-bank lifetime,
  cleanup, and diagnostics across custom/native transitions.
- Extended deterministic build provenance, multi-build packaging, runtime-gate
  identity checks, and rollback coverage for per-game-version artifacts.

## 0.1.0 - 2026-07-31

- First standalone release.
- Added descriptor-driven custom-song discovery, strict schema validation,
  deterministic MIDI chart generation, audio processing, HCA/MABF caching,
  and guarded game integration for the supported executable catalog.
- Added deterministic release archives, checksums, release identity, immutable
  dependency pins, and transactional package staging.
- Declared the first-party MIT license and removed bundled music examples.
- Cataloged supported game builds explicitly and published one archive per
  supported game build, named after the game version it supports.

Compatibility and remaining limitations are documented in the
[player guide](README.md#compatibility-and-limitations).
