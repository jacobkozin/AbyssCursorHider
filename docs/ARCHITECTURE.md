# Architecture

## Overview

Abyss Cursor Hide is a native x64 Win32 application. The tray app owns a hidden
top-level window, a notification-area icon, a Raw Input registration for the
mouse, and a 100 ms timer. A second copy of the same executable runs as its
watchdog, and a third, short-lived copy (`--restore`) is started only when a
restore inside the tray app leaves a cursor transparent. There is no
installer, service, driver, network code or administrator component.

```text
Mouse input ──> WM_INPUT ──────────┐
                                   ├──> activity: restore cursor, reset idle timer
100 ms poll (position, buttons) ───┘
        └──> no activity for the delay ──> install transparent cursors
             (not while the menu is open, a button is held, a more privileged
              window is in front, the watchdog is missing, or a restore is
              still unconfirmed)

Lock, UAC, Remote Desktop, sleep ──> restore and reset the idle timer
Tray menu ──> settings, Start with Windows, Hide Now, Exit

AbyssCursorHide.exe ──starts and supervises──> AbyssCursorHide.exe --watch <pid>
                                                 └── waits for the app to exit,
                                                     however it exits, then
                                                     reloads the cursor scheme

AbyssCursorHide.exe ──if a restore left a cursor transparent──>
        AbyssCursorHide.exe --restore <roles>
            └── reloads the scheme in a fresh process, checks it and exits
```

## Components

### `src/main.cpp`

Runs the application and its UI:

- limits DLL loading to System32 before anything else, so a DLL dropped next to
  the exe cannot run inside it (`wtsapi32.dll`, the only import that is not a
  KnownDLL, is delay-loaded for this reason);
- handles `--watch <pid>` and `--restore <roles>` next. With `--watch` the
  process only waits for the given process to exit and then reloads the cursor
  scheme. With `--restore` it reloads the scheme once and exits with 0 if the
  given cursor roles are visible again, or 1 if not;
- reloads the cursor scheme on every launch, before the single-instance check,
  so starting the app again fixes a pointer that a killed, hung or broken copy
  left invisible;
- enforces one instance per Windows session with a named mutex; a second launch
  asks the running copy to open its menu at the tray icon and passes it the
  right to take the foreground, so the menu closes normally;
- creates the hidden window and the notification-area icon at the taskbar's
  DPI. If Explorer is slow to take the icon (common at sign-in), it retries
  every 2 seconds for 2 minutes instead of giving up. On `TaskbarCreated`,
  which Explorer sends after a restart and also after a DPI or monitor change,
  it reloads the icon and adds it again, or updates it if it still exists;
- registers for background mouse Raw Input and runs the 100 ms timer;
- builds the tray menu, saves changes, and reopens the menu at the same anchor
  after a setting changes; and
- starts the watchdog and the `--restore` helper and keeps an eye on both (see
  Failure handling).

Raw Input gives the immediate restore. Absolute pointing devices (pens,
tablets, Remote Desktop, virtual machines) report positions rather than
movement, and some repeat the same position continuously, so for them only a
change of more than a few pixels counts. The timer also polls the cursor
position and the five mouse buttons. If the poll cannot read the cursor
position, the input desktop is a secure desktop (lock screen or UAC), so the
app restores the cursor and does not hide it again until access returns.
Session notifications also restore immediately on lock, unlock and Remote
Desktop changes, and power notifications on sleep and wake.

Windows hides clicks and wheel input from normal programs while a more
privileged program's window is in front, so the app checks the integrity level
of the foreground process before hiding and skips hiding over Task Manager, an
admin terminal and similar windows. It also waits while the tray menu is open
or a mouse button is held.

Pausing auto-hide only stops automatic hiding. **Hide Now** hides the pointer
until the mouse is used, whether auto-hide is on or paused. Before hiding, it
takes a fresh polling baseline so the pointer's trip to the menu item is not
counted as new movement.

### `src/cursor_controller.cpp`

Hiding creates a monochrome transparent cursor at the current cursor size for
each standard cursor role (arrow, text, busy, hand, resize and so on) and passes
it to `SetSystemCursor`. Windows takes ownership of a cursor only when it
accepts the role, so a cursor it rejects is destroyed at once. The obsolete
`OCR_SIZE` and `OCR_ICON` roles are left alone: Windows rejects `OCR_SIZE`, and
5.0.0 never destroyed the rejected cursor, so every hide leaked one. If any
required role cannot be replaced, the controller restores the cursors at once
rather than leaving a half-hidden state.

Before hiding, the controller refuses if the process is using more than 1,000
USER or GDI objects. The app normally uses a few dozen, and a process near the
Windows limit of 10,000 cannot load the cursor files when it restores, which is
how 5.0.0 left cursors invisible.

Just before hiding it also records which roles are visible and keeps a copy of
each of those cursors. Restoring calls `SystemParametersInfo(SPI_SETCURSORS)`,
which reloads the user's configured cursor scheme, and then reads each of
those roles back. Windows loads the scheme's cursor files inside the calling
process and keeps the current cursor of any role whose file it cannot load,
because the file is missing or the process is short of handles, so the
read-back is what decides whether a restore worked. A role that the user's own
scheme leaves blank was not visible before the hide, so it does not count as a
failure.

For a role that is still transparent after the reload, the controller puts its
copy back with `SetSystemCursor`. Windows takes ownership of the copy, so this
needs no new handles and works even in a process short of them. An animated
cursor may come back as a still image until the next successful reload.

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
only when that value is exactly that quoted command and Task Manager's Startup
apps switch has not turned it off. Task Manager keeps that switch in a separate
`Explorer\StartupApproved\Run` value whose first byte is odd when the entry is
off. Turning Start with Windows on from the menu clears that flag, so Windows
really starts the app again.

### `src/restore_cursor.cpp`

`RestoreCursor.exe` reloads the cursor scheme and shows whether every cursor is
visible again. It is only a manual tool and never runs in the background.

### `src/core.cpp`

Small pure functions shared by the app and the unit tests: delay validation,
the menu anchor offset, strict number parsing, building the quoted Run-key,
watchdog and `--restore` command lines, and reading Task Manager's startup
flag. It has no I/O or global state.

## Menu

```text
Auto-hide enabled
Hide delay        >   1, 2, 3, 5, 10, 15, 30 or 60 seconds
Start with Windows
------------------
Hide Now
Exit
```

The shell reports one event per click or key press (`NIN_SELECT`,
`NIN_KEYSELECT` or `WM_CONTEXTMENU`) together with an anchor point, and the
menu opens above that point, so a menu opened from the keyboard appears at the
icon. Changing auto-hide, the delay or Start with Windows reopens the menu at
the same anchor, so several settings can be changed in a row. **Hide Now** and
**Exit** close it.

## DPI and resources

The executables embed the multi-size icon, a manifest (Windows 10/11
compatibility, per-monitor V2 DPI awareness, run as the invoking user) and
version information read from the `project()` version in `CMakeLists.txt`. The
manifest's own assembly version is updated by hand.

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

Every restore from paths 1 to 5 is checked, and a cursor that is still
transparent after reloading the scheme gets back the copy saved before hiding.
When even that leaves a cursor transparent, the tray app starts
`AbyssCursorHide.exe --restore` with the roles to check, waits for its exit
code, and tries again after 1, 2, 4 seconds and so on up to a minute until one
succeeds. Until then it does not hide the pointer again.

The watchdog, `--restore`, a new launch and `RestoreCursor.exe` run in other
processes and can only reload the scheme. A cursor whose file is missing
therefore comes back from them only after signing out.

The watchdog is a separate process because nothing inside a process can run
after that process is killed. The tray app keeps its process handle, starts a
new watchdog if it exits (after 5 seconds, then less often if it keeps dying),
and only hides the pointer while one is running. Even if every path fails, the
transparent cursors are never saved to the user's profile, so signing out
restores them.

## Security boundaries

- All saved state is under `HKEY_CURRENT_USER`.
- No elevation is requested.
- The app opens no sockets and loads no third-party code; DLLs load from
  System32 only.
- The Run-key command is the quoted path of the executable; paths containing a
  quote are rejected.
- Only the window's own creation can set the pointer its window procedure uses;
  a `WM_NCCREATE` sent later by another program is ignored.
- Release builds use the compiler and linker hardening set in `CMakeLists.txt`.
- The executables are not code-signed yet; see [ANTIVIRUS.md](ANTIVIRUS.md).
