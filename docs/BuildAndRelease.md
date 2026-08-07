# Build And Release
<!-- current-release-version: 0.1.0 -->

This is the canonical repository workflow. Commands are run from the repository
root in PowerShell; no parent repository is required.

## Prerequisites

- Windows with MSVC and CMake available.
- PowerShell 7 (`pwsh`) for the repository scripts.
- Git and network access for the first CMake population of MinHook, miniaudio,
  and midifile through `FetchContent`, unless those exact pinned commits are
  already available in the local CMake dependency cache. These dependencies
  are not checked into this repository.

Build and packaging commands do not install the mod or launch the game. The
runtime-gate workflow below installs only after explicit human authorization;
it never launches the game.

## Configure And Build

Run the supported wrapper rather than duplicating generator or host parallelism choices:

```powershell
./build.ps1 -Configuration Release
```

### Refresh The clangd Compilation Database

The repository `.clangd` configuration reads compile commands from `build-lsp`.
Ordinary edits to existing `.cpp` and header files do not require regenerating
that database; clangd observes those file changes directly.

Run the supported refresh script when `CMakeLists.txt`, a `cmake/*.cmake` file,
or the set of `.cpp` files under `src/` or `tools/` changes and LSP information
must be current before the next build:

```powershell
./configure_lsp.ps1
```

`build.ps1` runs this refresh automatically after a successful build. Invoke
`configure_lsp.ps1`, not the generated `build-lsp/configure_lsp.cmd` helper.

To discover the build targets defined by the current CMake graph, ask CMake instead of maintaining a hand-written list:

```powershell
cmake --build build --config Release --target help
```

## Catalog And Tests

Validate the machine-readable RVA catalog:

```powershell
./tools/check_rva_catalog.ps1
```

List the tests registered by the current CMake configuration:

```powershell
ctest --test-dir build -C Release -N
```

Run the complete Release suite:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Test names and totals are derived from CMake/CTest and release-audit output. They are deliberately not copied into status prose.

## Release Audit

Run the built audit against the source tree before staging. The audit derives pipeline/cache metadata, CTest registration, hook inventory, package-document ownership, INI coverage, and Markdown policy from source inputs. Documentation checks are limited to the canonical files registered in `package-docs.json`; unrelated Markdown and build trees are not scanned:

```powershell
./build/Release/release_audit_tool.exe .
```

Treat any audit failure as a release blocker. Do not edit documentation to mirror a count or mutable format identifier; fix the source, registry, link, or policy mismatch that the audit reports.

## Stage The Package

After a successful Release build and CTest run, stage transactionally with:

```powershell
./package_dist.ps1 -Configuration Release -SkipBuild
```

Omit `-SkipBuild` when the script should build the production ASI and audit tool itself. A successful production build writes `FF7RPianoSongs.provenance.json` beside the DLL. The record binds the DLL hash to the configuration, compiler and CMake executables, CMake cache identity, and deterministic production input hashes.

`-SkipBuild` still refreshes the audit tool, but it does not rebuild the production ASI. It accepts the existing DLL only when that provenance record exactly matches the current DLL, toolchain, CMake configuration, and production inputs. Timestamps are not evidence of build currency. Changes limited to package documentation do not invalidate the DLL because those files are not compilation inputs.

`package_dist.ps1` is the full package authority. It reads and validates
`release.json`, requires a clean Git checkpoint, and records that exact commit.
It reads the closed schema-v2 `package-docs.json` registry, requires every
canonical role and source, derives the slim packaged subset from `distribution`,
and verifies source/staged documentation, binary/document byte parity, exact
inventory, provenance, hashes, adverse paths, and rollback behavior before
replacing `package/`. No music examples, caches, PDBs, build products, analysis,
or developer tools are copied. Do not add a second document list.

The same command then creates `release/FF7RPianoSongs-0.1.0-win64.zip` with a
sorted inventory and fixed ZIP timestamps, plus
`FF7RPianoSongs-0.1.0-win64.zip.sha256` and
`FF7RPianoSongs-0.1.0-win64.release.json`. The identity sidecar binds the
archive, ASI, build-provenance record, supported executable catalog, version,
and clean source commit. It remains outside the ZIP to avoid a self-referential
archive hash. Repeated generation from identical package bytes is byte-for-byte
deterministic.

Inspect the staged inventory and run its audit before publication:

```powershell
./build/Release/release_audit_tool.exe . --staged-docs ./package
```

`--staged-docs` audits only the registered documentation subset: byte parity,
exact staged text inventory, internal links, and absence of repository-only
registered documents. It does not validate binaries or the complete package
inventory; `package_dist.ps1` remains authoritative for those checks.

## Manual Qualification

Automated tests cannot prove list/UI/chart/audio integration. Use [In-Game Validation](InGameValidation.md) only when a focused build needs manual qualification. A launch-only smoke test does not validate those mechanics and must not be reported as doing so.

### Transactional runtime gate

After the checks required by the selected work mode pass, prepare an explicitly
authorized manual session with the product-owned runtime-gate tool. The default
`Packaged` mode validates and installs the staged package artifact:

```powershell
$session = ./tools/runtime_gate.ps1 `
  -Action Prepare `
  -Mode Packaged `
  -GameRoot "<FINAL FANTASY VII REBIRTH>" `
  -SongId "<existing-song-id>"
```

For focused Debug and Integration runs, `Development` mode installs the exact
Release `dist/FF7RPianoSongs.asi` without requiring `package/` or rerunning
publication/package-document audits:

```powershell
$session = ./tools/runtime_gate.ps1 `
  -Action Prepare `
  -Mode Development `
  -GameRoot "<FINAL FANTASY VII REBIRTH>" `
  -SongId "<existing-song-id>"
```

Both modes fail closed unless Release build provenance still matches the DLL,
`dist` ASI, toolchain, CMake cache, and complete production-input identity.
`Packaged` additionally requires the packaged ASI to have the same hash and
installs that path. `Development` does not read `PackageRoot`; it changes only
the selected artifact source, not the process, executable, backup, publication,
collection, Keep, Rollback, or Recover safety contracts. Omitting `-Mode`
preserves the historical `Packaged` behavior.

`Prepare` fails closed if process state cannot be established or
`ff7rebirth_` is running. It verifies the mode-appropriate artifact set and
cataloged executable identity before
transactionally replacing only `End/Binaries/Win64/FF7RPianoSongs.asi`. The
existing ASI and log are archived into a session outside Git, the stale live log
is removed before the launch boundary, and the previous ASI is retained beside
the target until finalization. INI files, songs, scores, and caches are not
changed.

The human tester launches and exits the game normally. After process exit,
collect the complete post-run log and a bounded sanitized excerpt:

```powershell
./tools/runtime_gate.ps1 -Action Collect -Session $session
./tools/runtime_gate.ps1 -Action Status -Session $session
```

The product and runtime gate do not support live same-process ASI teardown,
unload, or reload. Do not signal or script an in-process shutdown attempt.
This is not a release requirement: keep the ASI attached until normal process
exit, then collect evidence and perform any replacement while the game is
stopped. Normal Release builds produce only the production DLL/ASI and its
provenance record. Experimental teardown variants and reports are not release
artifacts.

Finalize only after reviewing the evidence. `Keep` requires one immutable
successful collection and accepts the reviewed ASI;
`Rollback` restores the exact prior ASI; `Uninstall` is accepted only when the
session began without an ASI. During active Debug and Integration work, keep a
successfully reviewed current development ASI by default. Roll back after
failed or compromised evidence, a stop condition, or an explicit rollback
request. Choosing `Keep` does not promote the task to Release mode or require
the full Release pipeline:

```powershell
# Successful current development candidate
./tools/runtime_gate.ps1 -Action Finalize -Session $session -Disposition Keep

# Failed, compromised, or explicitly rejected candidate
./tools/runtime_gate.ps1 -Action Finalize -Session $session -Disposition Rollback
```

Do not leave a reviewed runtime-gate session unfinalized.

If an action is interrupted, do not copy files manually. Recover by the hashes
and journal recorded in the session. Unknown installed content is copied into
the session as a hash-named quarantine artifact before the prior ASI is restored:

```powershell
./tools/runtime_gate.ps1 -Action Recover -Session $session
```

The default session root is
`$env:LOCALAPPDATA/FF7RPianoSongs/runtime-gates`. Pass `-StateDirectory` to use
another non-reparse location outside the repository and game trees. A fixed
per-executable ownership receipt and an OS-owned mutex coordinate sessions even
when evidence roots differ. The offline adverse-path test is registered in
CTest; it uses only temporary fake game and repository trees.

## Release Gate

A publishable artifact requires all of the following:

- catalog validation, Release build, full CTest, source audit, package staging, and staged audit pass;
- the current blockers in [Current Status](CurrentStatus.md) are resolved;
- required focused scenarios in [In-Game Validation](InGameValidation.md) pass on the supported executable;
- package contents match `package-docs.json` and contain no repository-only evidence or developer tooling;
- release identity and checksum sidecars match the deterministic archive and clean source commit;
- the first-party MIT license and separate third-party notices are present.

If staging fails before publication, the prior package remains in place. If a
post-publication validation or retirement step fails, use only the exact
`.package.previous-*`, `.package.failed-*`, or validated
`.package.recovery-*.zip` path reported by the script; do not merge trees by
hand. Release artifact publication follows the same directory transaction and
reports corresponding `.release.*` recovery paths.
