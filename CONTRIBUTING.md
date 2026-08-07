# Contributing

Thank you for helping improve FF7RPianoSongs.

## Before Opening A Change

- Read the repository guidance in [`AGENTS.md`](AGENTS.md) and the documentation
  index in [`docs/README.md`](docs/README.md).
- Use only source material that you have permission to contribute. Do not commit
  game files, copyrighted music, generated song caches, logs, or local paths.
- Keep game-specific addresses and signatures in
  [`src/game/rva_catalog.json`](src/game/rva_catalog.json), with evidence for
  executable-specific changes.
- Preserve fail-closed executable, signature, memory, object-identity, and
  lifecycle checks.

## Build And Test

The supported development environment is Windows x64 with MSVC, CMake,
PowerShell 7, Python, Git, and network access for the pinned CMake dependencies.
Run commands from the repository root:

```powershell
./build.ps1 -Configuration Release
./tools/check_rva_catalog.ps1
ctest --test-dir build -C Release --output-on-failure
./build/Release/release_audit_tool.exe .
```

The complete packaging and validation contract is documented in
[`docs/BuildAndRelease.md`](docs/BuildAndRelease.md). Automated tests do not
replace the focused in-game scenarios in
[`docs/InGameValidation.md`](docs/InGameValidation.md).

## Pull Requests

- Explain the user-visible behavior or safety contract being changed.
- Include the focused automated checks that were run and state what was not
  tested in game.
- Keep generated build, package, release, cache, and log files out of commits.
- Sanitize logs and paths before attaching evidence.

Opening a pull request does not grant permission to redistribute third-party
music or game assets.
