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
  USER object, and reports what Windows does with the two obsolete roles (it
  rejects `OCR_SIZE` and accepts `OCR_ICON`);
- hides and restores 2,000 times, checking that every role is transparent
  while hidden and drawn again afterwards (also by rendering it), and that the
  USER and GDI object counts stay flat;
- fills the process's USER object quota, as 5.0.0's leak did. A plain reload of
  the scheme, which is all the watchdog and `--restore` can do, must report
  that it left cursors transparent, and a fresh `AbyssCursorHide.exe --restore`
  must then bring every cursor back. The app's own restore, which also puts
  back the copies it kept, must bring every cursor back even at the quota.
  Hiding must be refused either way;
- starts `AbyssCursorHide.exe --watch` on a stand-in process and checks that it
  waits, then restores the cursors as soon as that process is killed; and
- points the scheme's hand cursor at a missing file and checks that a hide and
  restore still leave it visible.

`abyss_binary_tests` reads `AbyssCursorHide.exe` and `RestoreCursor.exe`
without running them and checks the settings listed under
[Static release checks](#static-release-checks).

GitHub Actions configures, builds and tests Debug and Release with the same
presets plus the option above, treats compiler warnings as errors, runs MSVC
`/analyze` on Debug builds (its warnings fail the build too) and uploads the
unsigned Release ZIP. When a test fails, its failed checks and the end of the
test output also appear on the run's summary page. A separate CodeQL workflow
scans the C++ code.

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

`abyss_binary_tests` checks both executables on every build, locally and on
CI:

- they are 64-bit Windows GUI programs with ASLR (keeping their relocations),
  high-entropy addresses, DEP, Control Flow Guard and CET compatibility, and
  no section that is both writable and executable;
- their dependent load flag is 0x800, so their imports load from System32
  only. Every DLL they import directly is one Windows always loads from
  System32 (a KnownDLL), `AbyssCursorHide.exe` delay-loads `WTSAPI32.dll`, and
  neither imports a networking library;
- the embedded manifest runs them as the invoking user with per-monitor V2 DPI
  awareness, and its version matches `CMakeLists.txt`;
- their file properties show the right product name, version, original file
  name, description, company and copyright; and
- Release builds name no debug database and contain no path from the build
  machine.

Before a release, also look at the file properties and icon of both
executables. The v5.0.0 Release build was checked by hand on 2026-10-21. See
[RELEASING.md](RELEASING.md) for the release steps.
