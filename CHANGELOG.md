# Changelog

Notable changes are recorded here. The v4 entries are reconstructed from the
verified release notes and development conversation; v4.17 itself is binary-only.

## [Unreleased] - v5.0.0 reconstruction

### Added

- Complete native C++17 source for the main application and recovery utility.
- CMake configuration, developer presets, unit tests, packaging, and Windows CI.
- Event-driven Raw Input with redundant 100 ms mouse-state polling.
- Windows session notifications so lock/unlock and secure-desktop transitions
  restore safely.
- Silent recovery watchdog for cursor restoration after forced termination.
- Self-watchdog fallback when `RestoreCursor.exe` is absent, preserving recovery
  for single-file deployments.
- Embedded icon, Windows compatibility/DPI manifest, and version resources.
- Architecture, testing, history, and release documentation.

### Security

- Enabled strict warnings, SDL checks, ASLR, high-entropy VA, NX/DEP, Control
  Flow Guard, and CET-compatible linking.
- Replaced hand-written platform declarations with official Windows SDK headers.
- Quoted and validated the per-user startup command.

### Status

- Debug and Release x64 builds compile with warnings treated as errors.
- Core unit tests pass in both configurations.
- MSVC static analysis completes without diagnostics.
- A controlled process-lifetime test confirmed the recovery watchdog responds to
  forced termination without leaving a process running.
- The maintainer confirmed interactive parity with the historical v4.17 behavior.
- The expanded Windows/DPI compatibility matrix remains recommended before a
  production release.

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
