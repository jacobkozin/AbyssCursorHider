<p align="center">
  <img src="abyss_cursor_hide.ico" alt="Abyss Cursor Hide icon" width="96" height="96">
</p>

# Abyss Cursor Hide

Abyss Cursor Hide is a focused Windows notification-area utility that hides the mouse pointer after a configurable period of inactivity. It provides quick tray controls, remembers its settings, can start with Windows, and includes a small recovery utility for restoring the Windows cursor scheme.

> **Repository status:** Version 4.17 is available as a prebuilt Windows x64 distribution. The supplied project does not contain source code, build scripts, or tests, so this repository is not yet a reproducible source project.

## Features

- Automatic cursor hiding after 1, 2, 3, 5, 10, 15, 30, or 60 seconds
- Immediate **Hide Now** action
- Enabled and paused states from the notification-area menu
- Cursor restoration on mouse activity
- Optional **Start with Windows** behavior
- Persistent enabled state and delay selection
- Single-instance protection
- Separate `RestoreCursor.exe` recovery utility

## Quick start

The distributed applications are native 64-bit Windows executables. They have not been runtime-tested as part of this repository preparation.

1. Download the v4.17 ZIP from the repository's Releases page once it is published.
2. Verify the archive before opening it:

   ```powershell
   Get-FileHash .\AbyssCursorHider.zip -Algorithm SHA256
   ```

   The expected v4.17 archive hash is `1385AE5117E8812887AB653FF7CD9A860138224BDEFE08C9A36DC69F05A6A3AC`.
3. Extract the complete archive. Keep `abyss_cursor_hide.ico` beside `AbyssCursorHide.exe`.
4. Exit v4.16 if it is running, then launch `AbyssCursorHide.exe`.
5. Use the notification-area icon to choose a delay, pause or resume hiding, hide the cursor immediately, or enable startup.
6. If the cursor scheme does not return normally, run `RestoreCursor.exe`.

Settings from v4.16 are expected to carry forward automatically, according to the supplied v4.17 release note.

## Configuration and system changes

No environment variables or `.env` file are required. The application stores per-user settings in the Windows Registry under:

```text
HKEY_CURRENT_USER\Software\AbyssCursorHide
```

When **Start with Windows** is enabled, the application manages an entry under:

```text
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
```

Static inspection found no bundled database, log, network endpoint, or external runtime dependency. Because source code is not present, those observations are not a substitute for a source-level security review.

## Technology

| Area | Implementation |
| --- | --- |
| Platform | Windows x64 (PE32+ GUI applications) |
| Main application | Native C, using the Win32 API |
| Interface | Windows notification area and native popup menu |
| Persistence | Per-user Windows Registry settings and optional startup entry |
| Recovery | Native companion utility that reloads the Windows cursor scheme |

The main executable uses Windows APIs from Kernel32, User32, Shell32, and Advapi32. See [Technical notes](docs/TECHNICAL_NOTES.md) for the evidence and limitations behind these details.

## Development status

The current repository is a documented binary-release snapshot of v4.17. The source files for both executables, exact compiler configuration, dependency information, automated tests, and reproducible build instructions were not included in the supplied archive.

Before presenting the project as open source or accepting code changes, the next development milestone is to recover and add the authoritative source tree and build configuration. Documentation and issue-report improvements are welcome in the meantime; see [CONTRIBUTING.md](CONTRIBUTING.md).

## Repository layout

```text
.
|-- .github/                 Issue-reporting templates
|-- checksums/               Release integrity hashes
|-- docs/                    Static technical notes
|-- abyss_cursor_hide.ico    Application icon required by the release bundle
|-- CHANGELOG.md             Available release history
|-- CONTRIBUTING.md          Contribution guidance
|-- SECURITY.md              Security and vulnerability reporting policy
`-- README.md                Project overview and usage
```

Executable and packaged release artifacts are intentionally excluded from Git and should be attached to a tagged GitHub Release instead.

## Security

The v4.17 executables are unsigned and do not contain publisher or version metadata. Verify release hashes before running them, obtain binaries only from the project's official release location, and read [SECURITY.md](SECURITY.md) before reporting a vulnerability.

## Release history

Version 4.17 raises the tray popup anchor by eight pixels relative to v4.16 while preserving its upward-opening behavior and saved position. See [CHANGELOG.md](CHANGELOG.md).

## License

No license has been selected for this project. Until a license is added, the repository does not grant permission to copy, modify, or redistribute its contents beyond rights provided by applicable law.
