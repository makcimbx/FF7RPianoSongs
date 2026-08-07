# Security Policy

## Supported Version

Security fixes are applied to the latest release and the default branch.

## Reporting A Vulnerability

Prefer GitHub's private vulnerability-reporting feature when it is available
for this repository. If it is unavailable, contact the maintainer through the
email address published in the repository's Git commit metadata. Do not include
private game paths, personal information, copyrighted inputs, access tokens, or
weaponized proof-of-concept material in a public issue.

Include the FF7RPianoSongs version, the game build the installed artifact was
built for, the executable catalog identity, affected safety boundary, and the
smallest reproducible description. Reports involving native
hooks, memory mutation, object lifetime, cache parsing, installation, or
rollback are treated as security-sensitive until reviewed.

One artifact supports one game build. An artifact installed against a different
build is refused, not adapted; a report that such an artifact ran anyway is a
boundary bypass. The project intentionally fails closed on unsupported executable identity,
signature mismatch, unsafe memory, invalid artifacts, and incomplete lifecycle
evidence. A bypass of those boundaries is particularly important to report.
