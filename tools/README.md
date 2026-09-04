# Dev Tools

These tools are for building the release ASI source tree. They are not required by end users.

Some optional authoring and analysis scripts use Python packages such as
`numpy`, `librosa`, `soundfile`, or `pydub`. Install only the dependencies named
by the script you choose to run, preferably in a local virtual environment.
They are authoring-tool dependencies, are not linked into the ASI, and are not
package dependencies.

## gen_mabf_scaffold.py

Generates `src/pipeline/mabf_scaffold.h` from the proven MABF template:

```powershell
python tools/gen_mabf_scaffold.py `
  "<reference.mabf.bin>" `
  "src/pipeline/mabf_scaffold.h"
```

The generated header embeds only the non-HCA scaffold: the 0x460-byte MABF header and three 62-byte slot trailers. Runtime HCA payloads are still generated from user WAV files.

## song_cache_tool

Loads one song directory through the production repository pipeline and prints the current pipeline/runtime magic and format, chart policy, cache source, profile/tail diagnostics, loudness/gain envelope, metronome, and MABF metadata. This is a mutating developer tool: running it creates or refreshes that song's local `.cache` files and may atomically create a missing `song.json` before cache generation. It never mutates source audio or MIDI. An optional difficulty argument must be an exact integer from 1 through 6 with no sign, whitespace, or trailing characters; missing or omitted sparse labels fail explicitly.

## extended_chart_fixture_tool

Creates an ordinary exactly-520-row compatibility fixture in a new or empty destination. The generated JSON contains no legacy diagnostic authority field: a proven playable exact-build runtime publishes the complete 512-prefix-plus-eight-tail chart, while native-512 or unsupported policy rejects it. Generation is staged in a unique sibling directory and validates the exact two-file protocol (`song.json` plus a 70-second, 48 kHz mono PCM16 `song.wav`). An existing empty destination is first renamed to a unique sibling backup; staging is then published by same-parent rename and validated again before that backup is retired. Publish or post-publish failures remove the generated fixture and restore the original empty directory. Existing nonempty data is never overwritten. If generated-output cleanup or empty-directory restoration fails, the error reports the exact `.ff7rp-previous-*` backup, failed or obstructing output, and staging paths needed for recovery.

## runtime_gate.ps1

Owns explicitly authorized developer installation and focused in-game evidence
sessions. It verifies Release provenance and the cataloged executable identity,
fails closed on process-query uncertainty, transactionally installs only the
ASI with atomic replace-and-backup, archives and clears the pre-launch log,
collects the complete post-run log, emits
a bounded sanitized marker excerpt, and finalizes by verified keep, rollback,
or clean-install uninstall. Interrupted sessions are recovered by the atomic
manifest and known hashes rather than timestamps; unknown installed content is
quarantined without deletion before restoration. It never launches the game or
changes INI files, songs, scores, or caches. The complete command workflow is in
`docs/BuildAndRelease.md`; `runtime_gate_selftest.ps1` exercises temporary fake
trees and adverse publication paths without touching the installed game. The
default `Packaged` Prepare validates and installs `package/`; `Development`
validates the exact Release DLL, `dist` ASI, provenance, toolchain, CMake cache,
and production inputs without requiring package or publication-document gates.
All process, backup, transaction, evidence, Keep, Rollback, and Recover behavior
is shared by both modes.
