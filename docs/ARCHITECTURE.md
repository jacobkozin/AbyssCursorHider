# Architecture

## Overview

Abyss Cursor Hide is a native x64 Win32 application. Its main process owns a
hidden message-only-style window, a notification-area icon, a raw mouse-input
registration, and an inactivity timer. It has no network, service, driver,
installer, or administrator component.

```text
Mouse input ──> WM_INPUT ──> reset timer ──> restore cursor if hidden
                    ^            |
                    |            v
100 ms state poll ──┴──> delay elapsed ───> install transparent cursors

Session/input-desktop transition ─────────> restore and reset timer

Tray menu ──> settings/startup registry or immediate hide/exit

Main process lifetime ──> watchdog process ──> reload cursor scheme
```

## Components

### `src/main.cpp`

Coordinates the application lifetime and UI:

- enforces one instance with a per-user-session named mutex;
- creates the hidden window and notification-area icon;
- registers for background mouse Raw Input;
- runs the inactivity timer;
- builds and anchors the popup menu;
- persists menu changes;
- re-adds the icon after Explorer restarts; and
- starts the recovery watchdog, falling back to a noninteractive second instance
  of itself when `RestoreCursor.exe` is unavailable.

Raw Input provides the immediate event-driven path. The application also polls
cursor position and button transitions on the existing 100 ms timer, even when
Raw Input registration succeeds. This redundant path covers higher-integrity
foreground windows such as Task Manager. If the process cannot query the active
input desktop during a lock or secure-desktop transition, it restores the cursor
and suppresses hiding until access returns. Windows session notifications also
restore immediately on lock, unlock, and console or remote-session transitions.

### `src/cursor_controller.cpp`

Creates monochrome transparent cursors at the active Windows cursor dimensions
and passes an independent cursor handle to `SetSystemCursor` for each standard
system cursor role. If any required role cannot be replaced, the controller
immediately reloads the Windows cursor scheme rather than leaving a partial
state.

Restoration uses `SystemParametersInfo(SPI_SETCURSORS)` and resets the active
thread cursor to the standard arrow. The controller restores automatically when
destroyed while hidden.

### `src/windows_settings.cpp`

Reads and writes two `REG_DWORD` values under the current user's
`Software\AbyssCursorHide` key:

| Value | Meaning |
| --- | --- |
| `Enabled` | `1` for automatic hiding, `0` for paused |
| `DelaySeconds` | One of the eight documented delay values |

Invalid delay values are replaced with the three-second default. Startup is
managed separately through the current user's `Run` key. The executable path is
quoted and compared case-insensitively before the menu displays a checkmark.

### `src/restore_cursor.cpp`

Has two modes:

- A normal launch reloads the Windows cursor scheme and shows the result.
- `--watch <process-id>` waits silently for the main process to end, then
  reloads the scheme.

The main executable implements the same internal `--watch <process-id>` mode as
a deployment fallback. The watchdog therefore makes ordinary forced termination
recoverable even if only `AbyssCursorHide.exe` was copied. The separate utility
remains valuable for manual recovery.

### `src/core.cpp`

Contains platform-light validation and command construction shared by the app
and tests. It has no I/O or mutable global state.

## Menu behavior

The menu contains only:

```text
Auto-hide enabled
Hide delay >
Start with Windows
------------------
Hide Now
Exit
```

Changing the enabled state, delay, or startup registration rebuilds the menu at
the exact original anchor. **Hide Now** and **Exit** close it. The popup uses
right and bottom alignment at a point eight physical pixels above the click
position, causing it to extend upward from the notification area.

## DPI and resources

The executable embeds the multi-resolution project icon, a Windows 10/11
compatibility manifest, per-monitor V2 DPI awareness, and version information.
The shell receives an icon loaded at the current small-icon system metrics.

## Failure handling

Cursor restoration occurs through several independent paths:

1. ordinary mouse activity;
2. the redundant cursor-position and button-state poll;
3. session lock, unlock, disconnect, and input-desktop isolation;
4. disabling auto-hide or changing the delay;
5. normal application exit;
6. Windows session shutdown;
7. the main process unhandled-exception filter;
8. the external watchdog after process termination; and
9. a manual launch of `RestoreCursor.exe`.

The watchdog is intentionally a separate process because in-process cleanup
cannot run after every possible forced termination.

## Security boundaries

- All persisted state is scoped to `HKEY_CURRENT_USER`.
- No elevation is requested.
- The application opens no sockets and loads no third-party runtime code.
- Startup registration specifies the executable explicitly with a quoted path.
- Release builds use compiler and linker hardening configured in CMake.
- The current build is unsigned; code signing remains a release-process task.
