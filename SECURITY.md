# Security policy

## Supported versions

| Version | Supported |
| --- | --- |
| 5.0.x (latest release) | Yes |
| 4.17 and earlier | No. These were binary-only builds and are kept only as history. |

## Reporting a vulnerability

Please use GitHub's private vulnerability reporting ("Report a vulnerability"
on the repository's **Security** tab). If that button is not there, open a
short issue asking for a private contact, without any vulnerability details.

Include the affected version or commit, your Windows version, steps to
reproduce, and the impact. Leave out credentials, personal data, registry
exports, crash dumps and unrelated machine information.

## Security design

- No network or telemetry code.
- No administrator rights are requested.
- Settings and the Start with Windows entry are stored for the current user
  only.
- The Start with Windows command is the quoted path of the executable; paths
  containing a quote are rejected, and only that exact quoted command counts
  as on.
- Both programs load DLLs from System32 only, so a DLL placed next to the exe
  cannot run inside it. Keep the app in a folder only you can change, such as
  `%LOCALAPPDATA%\AbyssCursorHide`.
- Mouse activity is read with Raw Input and a 100 ms poll. There is no hook DLL
  and no code is injected into other processes.
- Besides the watchdog (`--watch <pid>`), the only other copy the app starts is
  `AbyssCursorHide.exe --restore <roles>`, which reloads the cursor scheme once
  and exits. Both are restarted with a growing delay, never in a tight loop.
- The window procedure ignores a `WM_NCCREATE` sent by another program after
  the window exists.
- The hidden cursors are reversed on mouse activity, on lock and session
  changes, on sleep and wake, on exit, sign-out and shutdown, from the crash
  handler, by the watchdog process, when the app starts, and by
  `RestoreCursor.exe`. Every restore is checked, and a failed one is retried
  from a fresh process.
- Release builds turn on ASLR, DEP, Control Flow Guard and CET compatibility.

## Known risk

Hiding the pointer changes system-wide cursor resources for the current user.
If the app and its watchdog were both killed while the pointer was hidden, the
pointer would stay transparent until it is restored by starting the app again,
running `RestoreCursor.exe`, or signing out. Crashes that end the process
without running its crash handler (for example a failed security check) are
covered by the watchdog alone. The change is never saved to the user's profile,
so signing out always restores it.

## Release verification

The executables are not code-signed yet, and some antivirus products flag new
unsigned programs. Each release lists SHA-256 checksums for the ZIP, and the
ZIP is built by GitHub Actions from the tagged commit. From 5.0.1 on, the
release files also carry a build-provenance attestation:

```powershell
gh attestation verify .\AbyssCursorHide-5.0.1-windows-x64.zip --repo jacobkozin/AbyssCursorHider
```

A checksum or attestation shows where a file came from; it does not prove the
file is safe. See [docs/ANTIVIRUS.md](docs/ANTIVIRUS.md) and
[docs/RELEASING.md](docs/RELEASING.md).
