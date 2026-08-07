# Controlled Live Teardown Invalidation — 2026-07-31

## Decision

Controlled live same-process FF7RPianoSongs teardown is **unsupported / NO-GO**.
It is not a release requirement. The product remains process-bound and
attach-only: normal song and list transitions perform their existing scoped
cleanup, while final ASI, hook, callback, UObject, and SQEXSEAD lifetime ends
with the game process. Same-process unload or reload is not supported.

The experiment-only production, build, runtime-gate, report-schema, package
guard, and test seams were removed back to the accepted pre-experiment boundary
`578542b92584d5af88f8fbc000cc0d544d15aaa8`. The accepted Phase 5
same-translation-unit activation, borrower, and retirement structural
extractions at that boundary remain product code.

## Invalidated Attempts

### `20260731T120530Z-351a81e41d56`

- Development artifact SHA-256:
  `6f0f516cb75a65f3f098f2c304a2767276d3821164a6cc07ee37160ac476476e`.
- The schema-2 shutdown report declared `state=succeeded`,
  `audio_succeeded=1`, every reported module successful, and
  `identity_cleared=1`.
- Report SHA-256:
  `9738b819c15ec016b49a261efa17e2057d3959434dfc59d193187a9e89f82245`.
- A user-visible Fatal Error followed the formal success. The runtime log does
  not contain that dialog text, so the report cannot be treated as successful
  in-game teardown evidence.
- Session disposition: `RolledBack`.
- Collected post-run log SHA-256:
  `8dd9507d26662f2ea18fab1d0a6c60c599ee6f33650a0160200b3e3d4c981658`.

### `20260731T190357Z-3e4ebbfe054d`

- Development artifact SHA-256:
  `1bcb27f8dc40358793a50bdb174f66722863a468b6ca6ae9d96e4d915c011b6c`.
- The schema-3 shutdown report declared `state=succeeded`, closed admission,
  disabled hooks, drained callbacks, completed its post-disable boundary,
  restored native state, cleared module and UObject identity state, and retained
  original forwarding authority.
- Report SHA-256:
  `2d8e7c5284be621d954e814ec136b7b6d0fa5cc5c5a5bafd6cf0994bf99db50d`.
- Custom audio stopped, but a user-visible Fatal Error followed. The runtime log
  does not contain that dialog text. Formal schema success and audio cessation
  therefore do not establish safe same-process teardown.
- Session disposition: `RolledBack`.
- Collected post-run log SHA-256:
  `3b7b3fdd53cf262a48e5b4310e2f451682b1041dc4f59db494107cf4110640e0`.

The immutable session directories remain outside Git under
`%LOCALAPPDATA%/FF7RPianoSongs/runtime-gates/` with their reports, manifests,
logs, prior-artifact backups, and tested-or-failed artifacts.

## Evidence Boundary

Both attempts prove only that their experiment-defined report predicates were
satisfied before the later user-visible failure. Neither proves callback
quiescence, safe hook retirement, safe UObject/SQEXSEAD release, unloadability,
or same-process reuse. No root cause is established. Delayed callback or
retained-trampoline execution is a plausible investigation hypothesis only and
must not be stated as the cause.

No in-game success is claimed by this record. The two Fatal Errors invalidate
the controlled-teardown product direction regardless of the formal reports.
