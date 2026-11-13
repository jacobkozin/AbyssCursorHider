# Testing

## Automated checks

From a Visual Studio developer shell:

```powershell
cmake --preset windows-x64
cmake --build --preset debug
ctest --preset debug

cmake --build --preset release
ctest --preset release
```

`abyss_core_tests` covers the platform-independent logic in `src/core.cpp`:

- every delay in the menu is accepted, and unknown delays fall back to three
  seconds without changing the enabled state;
- the menu anchor moves exactly eight pixels up, including on monitors with
  negative coordinates;
- numbers and process IDs are parsed strictly (no empty, signed, spaced,
  hexadecimal or oversized values, and no zero process ID);
- Start with Windows, watchdog and `--restore` command lines quote the
  executable path and reject paths that contain a quote; and
- Task Manager's Startup apps flag is read correctly (odd first byte means off).

`abyss_cursor_tests` checks the cursor code with private cursors only; it
never changes the system cursors of the machine running it:

- at 24, 32, 48 and 72 pixels, a completely transparent cursor is recognized,
  padding bits are ignored, and a black cursor, one with only its last pixel
  visible and one that inverts a single pixel are not taken for transparent;
- a cursor that Windows rejects for a cursor role is destroyed: 200 rejected
  replacements leave the process's USER and GDI object counts unchanged (role
  1 is first shown to be rejected with a harmless visible cursor); and
- a zero-width or larger-than-512-pixel cursor is refused.

`abyss_system_cursor_tests` changes the real system cursors, so it is only
built with `-DABYSS_SYSTEM_CURSOR_TESTS=ON` and only runs when
`ABYSS_SYSTEM_CURSOR_TESTS=1` is set on GitHub Actions, whose runners are
thrown away after each job; anywhere else it reports itself as skipped. It:

- replaces every cursor role three times and checks that none of them leaks a
  USER object, and reports what Windows does with the two obsolete roles;
- hides and restores 2,000 times, checking that every role is transparent
  while hidden and drawn again afterwards (also by rendering it), and that the
  USER and GDI object counts stay flat;
- fills the process's USER object quota, as 5.0.0's leak did, and checks that
  a restore then reports the failure, that hiding is refused, and that a fresh
  `AbyssCursorHide.exe --restore` brings every cursor back;
- starts `AbyssCursorHide.exe --watch` on a stand-in process and checks that it
  waits, then restores the cursors as soon as that process is killed; and
- points the scheme's hand cursor at a missing file and checks that a hide and
  restore still leave it visible.

GitHub Actions configures, builds and tests Debug and Release with the same
presets plus the option above, treats compiler warnings as errors, runs MSVC
`/analyze` on Debug builds (its warnings fail the build too) and uploads the
unsigned Release ZIP. A separate CodeQL workflow scans the C++ code.

## Manual checklist

Last full run: 2026-10-21, v5.0.0 Release build, Windows 11 Home (build 26300),
three monitors at 150%, 125% and 100% scaling. A small helper drew the system
arrow cursor into a bitmap to tell "hidden" from "visible" without moving the
mouse. Unchecked items were not tested in that run. 5.0.1 was checked on
GitHub Actions only; the items below still describe the 5.0.0 run.

### Hiding and restoring

- [x] Hides after the selected delay (checked with 1 and 3 seconds).
- [x] Movement restores the pointer (within 150 ms).
- [x] Left, right, middle, back and forward buttons restore it.
- [x] Wheel and horizontal-wheel input restore it.
- [x] **Hide Now** hides at once and stays hidden until the mouse is used, with
      auto-hide on and with auto-hide paused.
- [x] Pausing auto-hide stops automatic hiding.
- [ ] The pointer does not hide while an elevated window such as Task Manager
      is in front (5.0.1).
- [ ] The pointer does not hide while the tray menu is open or a button is
      held (5.0.1).
- [ ] After a long session, links still show the hand pointer, and Task
      Manager's "USER objects" column for the app stays flat (5.0.1).
- [ ] A pen, a tablet or a Remote Desktop session lets the pointer hide (5.0.1).
- [ ] Resuming auto-hide waits a full delay before hiding.

### Menu and settings

- [x] Menu items and checkmarks match the saved settings.
- [x] Changing the delay, auto-hide or Start with Windows reopens the menu at
      the same spot.
- [x] The menu opens upward from the click.
- [x] The tooltip shows the delay (`Abyss Cursor Hide: hides after 3 s`).
- [x] The delay and enabled state survive a restart of the app.
- [x] Start with Windows writes the quoted path of the running executable, and
      moving the executable then turning it on again updates the path.
- [x] With Task Manager's Startup apps switch off, the menu shows Start with
      Windows as off, and turning it on clears Task Manager's flag.
- [ ] The app really starts at the next sign-in, and its tray icon appears
      even on a busy sign-in.
- [ ] Opening the menu from the keyboard (Win+B, arrow keys, Enter) puts it at
      the tray icon (5.0.1).

### Recovery

- [x] **Exit** restores the pointer and ends both processes.
- [x] `taskkill /F` on the app while the pointer is hidden: the watchdog
      restores it within half a second and exits.
- [x] Killing the app and its watchdog together, then starting the app again,
      restores the pointer.
- [ ] Starting the app again while it is still running restores the pointer
      (5.0.1).
- [ ] Ending only the watchdog makes the app start a new one within seconds,
      and the app doesn't hide the pointer meanwhile (5.0.1).
- [x] `RestoreCursor.exe` restores a hidden pointer without the mouse moving.
- [ ] Lock and unlock, and a UAC prompt, restore the pointer.
- [ ] Sign-out and shutdown leave the normal cursor scheme.

### Shell and display

- [x] Restarting Explorer brings the tray icon back.
- [x] A second launch opens the running copy's menu and exits; the menu closes
      when you click elsewhere.
- [x] The menu is sharp on a 150% monitor.
- [ ] Windows 10.
- [ ] Changing display scaling while the app runs keeps the tray icon sharp
      and working (5.0.1).
- [ ] Full-screen games and borderless windows.
- [ ] Touch and pen input.

## Static release checks

Before a release, check that both executables:

- are 64-bit Windows GUI programs with the right product name, version,
  description and copyright in their file properties;
- embed the manifest (run as the invoking user, per-monitor V2 DPI);
- have ASLR, high-entropy addresses, DEP, Control Flow Guard and CET
  compatibility turned on, and load their DLLs from System32 only
  (`dumpbin /loadconfig` shows a dependent load flag of 0x800, and
  `dumpbin /imports AbyssCursorHide.exe` lists `WTSAPI32.dll` as delay-loaded);
- import no networking libraries; and
- contain no paths from the build machine.

The v5.0.0 Release build was checked this way on 2026-10-21. See
[RELEASING.md](RELEASING.md) for the release steps.
