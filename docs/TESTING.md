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
- process IDs are parsed strictly (no empty, zero, non-numeric or oversized
  values);
- Start with Windows and watchdog command lines quote the executable path and
  reject paths that contain a quote; and
- Task Manager's Startup apps flag is read correctly (odd first byte means off).

GitHub Actions builds Debug and Release with the same presets, treats compiler
warnings as errors, runs the tests in both configurations and uploads the
unsigned Release ZIP. MSVC `/analyze` also reports no warnings for the sources
(last run 2026-10-21).

## Manual checklist

Last full run: 2026-10-21, v5.0.0 Release build, Windows 11 Home (build 26300),
three monitors at 150%, 125% and 100% scaling. A small helper drew the system
arrow cursor into a bitmap to tell "hidden" from "visible" without moving the
mouse. Unchecked items were not tested in that run.

### Hiding and restoring

- [x] Hides after the selected delay (checked with 1 and 3 seconds).
- [x] Movement restores the pointer (within 150 ms).
- [x] Left, right, middle, back and forward buttons restore it.
- [x] Wheel and horizontal-wheel input restore it.
- [x] **Hide Now** hides at once and stays hidden until the mouse is used, with
      auto-hide on and with auto-hide paused.
- [x] Pausing auto-hide stops automatic hiding.
- [ ] Movement and clicks over an elevated Task Manager restore it.
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
- [ ] The app really starts at the next sign-in.

### Recovery

- [x] **Exit** restores the pointer and ends both processes.
- [x] `taskkill /F` on the app while the pointer is hidden: the watchdog
      restores it within half a second and exits.
- [x] Killing the app and its watchdog together, then starting the app again,
      restores the pointer.
- [x] `RestoreCursor.exe` restores a hidden pointer without the mouse moving.
- [ ] Lock and unlock, and a UAC prompt, restore the pointer.
- [ ] Sign-out and shutdown leave the normal cursor scheme.

### Shell and display

- [x] Restarting Explorer brings the tray icon back.
- [x] A second launch opens the running copy's menu and exits; the menu closes
      when you click elsewhere.
- [x] The menu is sharp on a 150% monitor.
- [ ] Windows 10.
- [ ] Changing display scaling while the app runs.
- [ ] Full-screen games and borderless windows.
- [ ] Touch and pen input.

## Static release checks

Before a release, check that both executables:

- are 64-bit Windows GUI programs with the right product name, version,
  description and copyright in their file properties;
- embed the manifest (run as the invoking user, per-monitor V2 DPI);
- have ASLR, high-entropy addresses, DEP, Control Flow Guard and CET
  compatibility turned on;
- import no networking libraries; and
- contain no paths from the build machine.

The v5.0.0 Release build was checked this way on 2026-10-21. See
[RELEASING.md](RELEASING.md) for the release steps.
