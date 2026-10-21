# Architecture

## Overview

Abyss Cursor Hide is a native x64 Win32 application. The tray app owns a hidden
top-level window, a notification-area icon, a Raw Input registration for the
mouse, and a 100 ms timer. A second copy of the same executable runs as its
watchdog. There is no installer, service, driver, network code or
administrator component.

```text
Mouse input ──> WM_INPUT ──────────┐
                                   ├──> activity: restore cursor, reset idle timer
100 ms poll (position, buttons) ───┘
        └──> no activity for the delay ──> install transparent cursors

Lock, UAC, Remote Desktop, sleep ──> restore and reset the idle timer
Tray menu ──> settings, Start with Windows, Hide Now, Exit

AbyssCursorHide.exe ──starts──> AbyssCursorHide.exe --watch <pid>
                                   └── waits for the app to exit, however it
                                       exits, then reloads the cursor scheme
```

## Components

### `src/main.cpp`

Runs the application and its UI:

- handles `--watch <pid>` first: in that mode the process only waits for the
  given process to exit and then reloads the cursor scheme;
- enforces one instance per Windows session with a named mutex; a second launch
  asks the running copy to open its menu and passes it the right to take the
  foreground, so the menu closes normally;
- reloads the cursor scheme at startup, in case an earlier copy was killed
  together with its watchdog while the pointer was hidden;
- creates the hidden window and the notification-area icon, and re-adds the
  icon when Explorer restarts (`TaskbarCreated`);
- registers for background mouse Raw Input and runs the 100 ms timer;
- builds the tray menu, saves changes, and reopens the menu at the same anchor
  after a setting changes; and
- starts the watchdog.

Raw Input gives the immediate restore. The timer also polls the cursor position
and the five mouse buttons, even when Raw Input works, because a
normal-privilege background process does not receive every input event while an
elevated window such as Task Manager is in front. If the poll cannot read the
cursor position, the input desktop is a secure desktop (lock screen or UAC), so
the app restores the cursor and does not hide it again until access returns.
Session notifications also restore immediately on lock, unlock and Remote
Desktop changes.

Pausing auto-hide only stops automatic hiding. **Hide Now** hides the pointer
until the mouse is used, whether auto-hide is on or paused. Before hiding, it
takes a fresh polling baseline so the click that chose the menu item is not
counted as new activity.

### `src/cursor_controller.cpp`

Creates monochrome transparent cursors at the current cursor size and passes a
separate handle to `SetSystemCursor` for each standard cursor role (arrow, text,
busy, hand, resize and so on). If any required role cannot be replaced, it
reloads the cursor scheme at once rather than leaving a half-hidden state.

Restoring calls `SystemParametersInfo(SPI_SETCURSORS)`, which reloads the
user's configured cursor scheme, and resets the current thread's cursor to the
arrow. The controller restores automatically if it is destroyed while hidden.

### `src/windows_settings.cpp`

Reads and writes two `REG_DWORD` values under
`HKEY_CURRENT_USER\Software\AbyssCursorHide`:

| Value | Meaning |
| --- | --- |
| `Enabled` | `1` for automatic hiding, `0` for paused |
| `DelaySeconds` | One of the eight delays in the menu |

An unknown delay falls back to the 3-second default.

Start with Windows is the `AbyssCursorHide` value under the current user's
`Run` key, holding the quoted path of the executable. The menu shows it as on
only when that value points at the running executable and Task Manager's
Startup apps switch has not turned it off. Task Manager keeps that switch in a
separate `Explorer\StartupApproved\Run` value whose first byte is odd when the
entry is off. Turning Start with Windows on from the menu clears that flag, so
Windows really starts the app again.

### `src/restore_cursor.cpp`

`RestoreCursor.exe` reloads the cursor scheme and shows whether it worked. It is
only a manual tool and never runs in the background.

### `src/core.cpp`

Small pure functions shared by the app and the unit tests: delay validation,
the menu anchor offset, process-ID parsing, building the quoted Run-key and
watchdog command lines, and reading Task Manager's startup flag. It has no I/O
or global state.

## Menu

```text
Auto-hide enabled
Hide delay        >   1, 2, 3, 5, 10, 15, 30 or 60 seconds
Start with Windows
------------------
Hide Now
Exit
```

Changing auto-hide, the delay or Start with Windows reopens the menu at the
same anchor, so several settings can be changed in a row. **Hide Now** and
**Exit** close it. The menu is right- and bottom-aligned to a point eight pixels
above the click, so it opens upward from the notification area.

## DPI and resources

The executables embed the multi-size icon, a manifest (Windows 10/11
compatibility, per-monitor V2 DPI awareness, run as the invoking user) and
version information read from the `project()` version in `CMakeLists.txt`. The
tray icon is loaded at the system small-icon size when the app starts.

## Failure handling

The pointer is restored through several independent paths:

1. mouse movement, clicks and scrolling (Raw Input);
2. the 100 ms position and button poll;
3. lock, unlock, Remote Desktop changes, and the secure desktop;
4. waking from sleep;
5. pausing auto-hide or changing the delay;
6. Exit, sign-out and shutdown;
7. the unhandled-exception handler;
8. the watchdog process after the app is killed;
9. starting the app again; and
10. running `RestoreCursor.exe`.

The watchdog is a separate process because nothing inside a process can run
after that process is killed. Even if every path fails, the transparent cursors
are never saved to the user's profile, so signing out restores them.

## Security boundaries

- All saved state is under `HKEY_CURRENT_USER`.
- No elevation is requested.
- The app opens no sockets and loads no third-party code.
- The Run-key command is the quoted path of the executable; paths containing a
  quote are rejected.
- Release builds use the compiler and linker hardening set in `CMakeLists.txt`.
- The executables are not code-signed yet; see [ANTIVIRUS.md](ANTIVIRUS.md).
