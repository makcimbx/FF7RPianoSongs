# Build And Release
<!-- current-release-version: 0.1.1 -->

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

One ASI supports one game build. `-CatalogBuildId` selects which cataloged build identity it is
compiled against; without it, the catalog's `default_build` is used:

```powershell
./build.ps1 -Configuration Release -CatalogBuildId <catalog-build-id>
```

The selection chooses the generated tree under `src/generated/<catalog-build-id>/` and is
compiled into the artifact, so producing artifacts for every supported build means running this
command once per build. Switching builds always reconfigures CMake; a cache configured for
another build is never reused silently.

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

This validates and byte-compares the generated tree of every build the catalog declares, not
only the build selected for compilation. Regenerate all trees after any catalog edit:

```powershell
python tools/generate_rva_catalog.py --write
```

Never hand-edit a file under `src/generated/`; the check fails when a tree is stale.

List the tests registered by the current CMake configuration:

```powershell
ctest --test-dir build -C Release -N
```

Run the complete Release suite:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Test names and totals are derived from CMake/CTest and release-audit output. They are deliberately not copied into status prose.

## Add A Game Build

Signatures cataloged for one build are not portable across builds: rescanning an adjacent
release with the existing signature set locates only a small minority of them. A new build's
addresses are therefore derived from that binary, never translated from another build. The
catalog layout and generator invariants are described in
[Architecture](Architecture.md). Work in this order.

1. Record the executable identity. Take `pe_timestamp`, `size_of_image`, `pe_checksum`,
   `file_size`, and `sha256` from the target executable and append one `builds` entry with the
   id `ff7rebirth-<store>-<platform>-<pe-timestamp>` and its two-part `game_version`.
2. Create the evidence document first, for example
   `analysis/RebirthBuild<game-version>RvaEvidence-<date>.md`. The generator requires every
   `evidence.path` to exist, so catalog edits fail until that file is present.
3. Derive only the addresses the build actually needs. A `release` address must be present in
   every declared build; `optional` and `research` addresses may be omitted, and an omitted
   address degrades through the null-spec path rather than through a guessed value. Derive each
   address from the new binary — matched call sites, string or vtable references, or an
   equivalent static route — and record that route in the evidence document. Do not copy an RVA
   between builds and do not treat a signature hit as provenance.
4. Cut a fresh signature per address from the new binary and confirm it matches its own address
   exactly once in that binary. A signature matching zero or several sites does not identify it.
5. Re-derive every `locators[].builds.<catalog-build-id>` entry from the new binary. A masked
   pattern is no more portable than a fixed signature. A locator has no optional form: the
   generator rejects a build that omits one, because a build without its locator still installs
   every hook and then publishes nothing. Confirm the anchor instruction decodes to a cataloged
   field of the target structure, and that the first image-wide match is the intended one —
   `match_policy` is `first`, so a later match cannot rescue a wrong one.
6. Add each `builds.<catalog-build-id>` block, regenerate, and validate. The generator rejects a
   missing `release` entry, an address present in no build, a duplicate or out-of-image RVA, a
   locator missing from a declared build, and a hand-authored `"0x0"`.
7. Add the matching `release.json` target and one matrix entry in
   `.github/workflows/windows-ci.yml`. The release audit fails while the catalog builds and the
   release targets disagree.
8. Build, test, and package that build, then qualify it against the real executable. Startup
   must stop reporting `status=disabled_game_updated` and must reach hook installation before
   any behavior is claimed. Offline checks cannot qualify a new build's addresses; run the
   focused scenarios in [In-Game Validation](InGameValidation.md).

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

Production inputs include the generated tree of every declared build, so editing one build's
catalog data invalidates the provenance of an already built ASI for a different build. Rebuild
each supported build after any catalog change; do not package a DLL whose provenance predates
it. The provenance record also names the build identity the ASI was compiled for, which is what
the runtime gate compares against the installed executable.

`package_dist.ps1` is the full package authority. It reads and validates
`release.json`, requires a clean Git checkpoint, and records that exact commit.
It reads the closed schema-v2 `package-docs.json` registry, requires every
canonical role and source, derives the slim packaged subset from `distribution`,
and verifies source/staged documentation, binary/document byte parity, exact
inventory, provenance, hashes, adverse paths, and rollback behavior before
replacing `package/`. No music examples, caches, PDBs, build products, analysis,
or developer tools are copied. Do not add a second document list.

`release.json` declares one target per supported game build. Each target names its game
version, the executable catalog identity that build is compiled against, and an archive
basename derived as `<product>-<version>-<platform>-ff7r<game-version>`, so the 1.005 artifact
publishes as `FF7RPianoSongs-0.1.1-win64-ff7r1.005`. Stage one archive per game build,
selecting the same build the ASI was compiled for. `-GameBuild` here takes the target's game
version and is required once more than one target exists:

```powershell
./package_dist.ps1 -Configuration Release -SkipBuild -GameBuild <game-version>
```

The same command then creates `release/<archive-basename>/<archive-basename>.zip` with a
sorted inventory and fixed ZIP timestamps, plus its `.zip.sha256`, `.release.json`, and
`.provenance.json` sidecars. The identity sidecar binds the archive, ASI, published
build-provenance record, supported executable catalog identity, version, and clean source
commit. Sidecars remain outside the ZIP to avoid a self-referential archive hash. Repeated
generation from identical package bytes is byte-for-byte deterministic.

Run the command sequentially for every target declared by `release.json`. `package/` remains
the exact selected-target staging tree, while `release/` is one aggregate publication surface:
each successful run replaces the selected target and retains every previously produced sibling
target. Before retention, each sibling is validated fail closed against the current release
authority and clean commit, its declared directory and sidecar names, exact inventory, archive
and checksum, packaged files, ASI hash, build-provenance catalog and production-input identity,
and release identity. An undeclared entry or a stale, malformed, foreign, or mismatched sibling
aborts staging without replacing either prior publication surface. After all declared targets
have been staged, `release/` must contain exactly one validated directory per declared target.

Archives for different game builds carry visually identical `FF7RPianoSongs.asi` files that
differ only in the build they were compiled against. The startup gate fails closed on a
mismatch and reports the executable identity it expected against the one it observed, so a
wrong install is self-diagnosing rather than silent. Publish the game version in the archive
name and in the release notes.

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
`ff7rebirth_` is running. It verifies the mode-appropriate artifact set, then requires the
installed game executable to match the exact build recorded in the artifact's provenance
rather than any supported build, before
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
- required focused scenarios in [In-Game Validation](InGameValidation.md) pass on each supported game build, using the archive built for that build;
- package contents match `package-docs.json` and contain no repository-only evidence or developer tooling;
- release identity, checksum, and published build-provenance sidecars match each deterministic archive and clean source commit;
- the first-party MIT license and separate third-party notices are present.

If staging fails before publication, the prior package remains in place. If a
post-publication validation or retirement step fails, use only the exact
`.package.previous-*`, `.package.failed-*`, or validated
`.package.recovery-*.zip` path reported by the script; do not merge trees by
hand. Release artifact publication covers the complete aggregate `release/`
inventory in the same two-surface directory transaction and reports corresponding
`.release.*` recovery paths.
