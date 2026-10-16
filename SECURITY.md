# Security policy

## Supported versions

No production source-based release is supported yet. The v5 reconstruction on
`main` is prerelease development code. Historical v4.17 binaries are unsigned,
binary-only, and retained solely as verified project history.

## Reporting a vulnerability

Use GitHub's private vulnerability reporting flow from the repository's
**Security** tab when available. If private reporting has not been enabled, open
a minimal issue asking the maintainer to establish a private contact channel;
do not include vulnerability details in that issue.

Include the affected commit or version, Windows version, reproduction steps,
impact, and suggested mitigation. Remove credentials, personal data, registry
exports, dumps, and unrelated machine information.

## Security design

- No network or telemetry code is present.
- No administrator privileges are requested.
- Settings and startup registration are scoped to the current user.
- Startup paths are validated and quoted.
- Raw Input plus redundant mouse-state polling observes activity without a
  global hook DLL.
- Cursor changes are reversed on activity, normal exit, shutdown, unhandled
  failure, session/input-desktop transitions, watchdog detection, or manual
  recovery.
- Release builds enable modern Windows exploit mitigations.

## Important platform risk

The program temporarily changes global system cursor resources. A failure that
terminates both the main process and watchdog could leave transparent cursors
active until the cursor scheme is reloaded, the recovery utility runs, or the
user signs out. Keep `RestoreCursor.exe` with the main program.

## Release verification

Checksums establish file identity only. They do not prove publisher identity or
that a binary is safe. Production releases should be built from a reviewed tag,
scanned, Authenticode-signed, timestamped, and published with SHA-256 checksums.
Signing keys and passwords must never be committed or stored as ordinary CI
variables without an approved secret-management design.

See [`docs/RELEASING.md`](docs/RELEASING.md) for the complete release checklist.
