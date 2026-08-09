# Changelog

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
