# Changelog

Notable changes are recorded here. The v4 entries are reconstructed from the
verified release notes and development conversation; v4.17 itself is binary-only.

## [5.0.0] - 2026-10-21

The first release built from source. v5 is a clean rebuild of the v4.17 tray
app, whose source was lost.

### What it does

- Hides the mouse pointer after 1 to 60 seconds without mouse input, and
  brings it back on any movement, click or scroll.
- Tray menu with **Auto-hide enabled**, **Hide delay**, **Start with Windows**,
  **Hide Now** and **Exit**. Settings are saved per Windows user.
- A watchdog (a second copy of the app started with `--watch <pid>`) restores
  the pointer if the app is killed, and starting the app again also restores
  it. `RestoreCursor.exe` is a one-click manual fix.
- Restores the pointer on lock, UAC prompts, Remote Desktop changes, waking
  from sleep, sign-out and shutdown, and re-adds its tray icon when Explorer
  restarts.
- Native C++17 and Win32, per-monitor DPI aware, no administrator rights, no
  network access and nothing extra to install.

### Fixed during final testing

- Hide Now did nothing while auto-hide was paused, and could undo itself right
  after the click.
- Start with Windows ignored the on/off switch in Task Manager's Startup apps.
- After a second launch, the running copy's menu might not close when clicking
  elsewhere.

### Build and release

- CMake presets for Visual Studio 2022 or newer. GitHub Actions builds and tests
  Debug and Release on every change, and attaches the ZIP and its SHA-256 to
  each published release.
- Warnings as errors, SDL checks, ASLR, DEP, Control Flow Guard and CET
  compatibility. MSVC `/analyze` reports no warnings.

### Known issues

- The executables are not code-signed. Windows SmartScreen may warn about them,
  and some antivirus products flag new unsigned programs. See
  [docs/ANTIVIRUS.md](https://github.com/jacobkozin/AbyssCursorHider/blob/main/docs/ANTIVIRUS.md).

## [4.17]

- Raised the notification-area popup anchor by eight pixels.
- Preserved upward opening and exact anchored reopening after settings changes.

## [4.16]

- Changed the notification-area popup to open upward.

## [4.15]

- Reduced the menu to enabled state, delay, startup, Hide Now, and Exit.
- Removed diagnostic and redundant status text from the menu.

## [4.14]

- Reopened the menu consistently after enabled, delay, or startup changes.
- Removed Restore Cursor from the normal menu while retaining the recovery tool.

## [4.13]

- Reused the original popup coordinates after settings changes to stop menu drift.

## [4.12]

- Restored the cursor on left, right, middle, X1, or X2 mouse clicks.
- Reset the inactivity timer on clicks.
- Reopened the menu after changing Start with Windows.

See [`docs/HISTORY.md`](docs/HISTORY.md) for provenance and earlier-version notes.
