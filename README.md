<p align="center">
  <img src="abyss_cursor_hide.ico" alt="Abyss Cursor Hide icon" width="96" height="96">
</p>

# Abyss Cursor Hide

[![Build and test](https://github.com/jacobkozin/AbyssCursorHider/actions/workflows/build.yml/badge.svg)](https://github.com/jacobkozin/AbyssCursorHider/actions/workflows/build.yml)

Abyss Cursor Hide is a focused Windows notification-area utility that hides the
mouse pointer after a configurable period of inactivity and restores it as soon
as the mouse moves or any mouse button is pressed. It is a native Win32
application with no network access, telemetry, external runtime, or
administrator requirement.

> **Development status:** The source on `main` is a clean reconstruction of the
> behavior of the historical v4.17 binary. It is being developed as v5.0.0 and
> has not been published as a release. The deleted original v4.17 source was not
> recovered, so this project does not claim source-level identity with that
> binary.

## Features

- Hides Windows system cursors after 1, 2, 3, 5, 10, 15, 30, or 60 seconds
- Restores the cursor on movement, wheel input, or any of five mouse buttons
- Uses redundant mouse-state polling and session notifications for elevated
  windows, workstation lock, and secure-desktop transitions
- Offers **Auto-hide enabled**, **Hide delay**, **Start with Windows**,
  **Hide Now**, and **Exit** from the notification-area menu
- Reopens the menu at the same anchored position after a setting changes
- Opens the menu upward, eight pixels above its notification-area anchor
- Saves the enabled state and delay for the current Windows user
- Prevents duplicate instances and surfaces the existing instance when reopened
- Embeds a multi-resolution icon and declares per-monitor V2 DPI awareness
- Launches a silent recovery watchdog and includes a manual recovery utility
- Builds entirely from reviewed repository source with CMake and MSVC

## Safety model

Windows does not provide a process-independent `ShowCursor` switch. To hide the
pointer across applications, Abyss Cursor Hide temporarily replaces the current
user's system cursor shapes with transparent cursors through `SetSystemCursor`.
It reloads the configured Windows cursor scheme when activity resumes, when the
application exits, when Windows shuts down, or when an unhandled failure occurs.
If Windows switches to an input desktop the application cannot access, it
restores the scheme and will not hide it again until normal desktop access
returns.

The main process launches a lightweight watchdog. It uses `RestoreCursor.exe`
when that packaged companion is available and otherwise launches a silent
watchdog mode from the main executable itself. If the interactive process is
terminated unexpectedly while the cursor is hidden, the watchdog reloads the
Windows cursor scheme. `RestoreCursor.exe` can also be launched manually at any
time.

## Requirements

### Running

- Windows 10 or Windows 11, x64
- No administrator access
- No external runtime or internet connection

### Building

- Visual Studio 2022 or newer with **Desktop development with C++**
- Windows 10/11 SDK
- CMake 3.25 or newer

## Build from source

From a Developer PowerShell or Developer Command Prompt:

```powershell
cmake --preset windows-x64
cmake --build --preset release --parallel
ctest --preset release
```

The executables are produced in:

```text
build/windows-x64/Release/AbyssCursorHide.exe
build/windows-x64/Release/RestoreCursor.exe
```

To create the unsigned ZIP package used by continuous integration:

```powershell
cmake --build build/windows-x64 --config Release --target package
```

Build products remain ignored by Git. The GitHub Actions workflow performs the
same configure, build, test, and packaging sequence on Windows.

## Use

1. Keep `AbyssCursorHide.exe` and `RestoreCursor.exe` in the same folder so the
   manual recovery option remains available.
2. Launch `AbyssCursorHide.exe`.
3. Use the notification-area icon to select the delay or pause automatic hiding.
4. Move or click the mouse to restore a hidden cursor.
5. Choose **Exit** before replacing or removing the program.

The default is enabled with a three-second delay. **Start with Windows** is off
until explicitly enabled.

## Local configuration

No `.env` file or environment variables are used. Per-user settings are stored
under:

```text
HKEY_CURRENT_USER\Software\AbyssCursorHide
```

The optional startup entry is stored under:

```text
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
```

The application does not create a database or log file and contains no
networking functionality.

## Project structure

```text
.
|-- .github/                 Issue templates and Windows CI
|-- checksums/               Historical release integrity hashes
|-- docs/                    Architecture, testing, history, and release notes
|-- include/abyss/           Public internal interfaces
|-- resources/               Manifest, icon resources, and version metadata
|-- src/                     Main app, settings, cursor, and recovery sources
|-- tests/                   Dependency-free unit tests
|-- CMakeLists.txt           Build, hardening, test, install, and package rules
|-- CMakePresets.json        Repeatable Windows x64 developer commands
|-- LICENSE                  MIT License
`-- README.md                Project overview
```

Implementation details and design decisions are documented in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). Validation instructions are in
[`docs/TESTING.md`](docs/TESTING.md).

## Security and releases

Release builds enable ASLR, high-entropy address space, DEP/NX, Control Flow
Guard, CET compatibility, SDL checks, and strict compiler warnings. Generated
development binaries remain unsigned; a checksum identifies a file but does not
establish publisher identity. See [`SECURITY.md`](SECURITY.md) and
[`docs/RELEASING.md`](docs/RELEASING.md) before distributing a build.

## Historical v4.17 package

The verified legacy binary package is retained as historical evidence, not as a
reproducible build. Its original main compilation unit was named `y_v417.c`, but
that file was deleted before this repository was prepared. See
[`docs/HISTORY.md`](docs/HISTORY.md) and
[`checksums/SHA256SUMS-v4.17.txt`](checksums/SHA256SUMS-v4.17.txt).

## License

Released under the [MIT License](LICENSE).
