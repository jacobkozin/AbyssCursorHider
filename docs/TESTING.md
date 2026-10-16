# Testing

## Automated checks

Configure, build, and run the tests from a Visual Studio developer shell:

```powershell
cmake --preset windows-x64
cmake --build --preset debug --parallel
ctest --preset debug

cmake --build --preset release --parallel
ctest --preset release
```

`abyss_core_tests` currently verifies:

- every supported delay value is accepted;
- unsupported and unsafe delay values fall back to three seconds;
- settings normalization preserves the enabled state;
- the notification-area menu anchor moves exactly eight pixels upward;
- startup commands quote paths containing spaces; and
- empty paths or paths containing quotes are rejected.

The Windows CI workflow treats compiler warnings as errors, builds x64 Debug and
Release-compatible source, runs the Release tests, and creates an unsigned ZIP
artifact.

During reconstruction, both configurations were built locally, both test runs
passed, and MSVC `/analyze` completed without diagnostics. A half-second runtime
smoke test also verified that the main process starts and that the recovery
watchdog responds to forced termination. That smoke test intentionally ends
before the minimum one-second hide delay and does not replace the manual cursor
checks below.

The maintainer subsequently confirmed that the reconstructed application behaves
the same as the historical v4.17 build during interactive use. The broader
compatibility and release checklist remains below for future production releases.

## Manual behavior checklist

Run these checks on a disposable Windows user profile or virtual machine before
publishing a release. Keep `RestoreCursor.exe` beside the main executable.

### Cursor behavior

- [ ] Cursor hides after every selectable delay.
- [ ] Movement restores the cursor immediately.
- [ ] Left, right, middle, X1, and X2 button presses restore it.
- [ ] Wheel and horizontal-wheel input restore it.
- [ ] Movement and clicks over an elevated Task Manager restore it.
- [ ] **Hide Now** hides without waiting for the delay.
- [ ] Pausing restores the cursor and prevents subsequent hiding.
- [ ] Resuming restarts a complete delay interval.

### Menu and persistence

- [ ] Tray icon and tooltip reflect enabled/paused state and selected delay.
- [ ] Enabled, delay, and startup changes reopen at the identical anchor.
- [ ] Menu opens upward and eight pixels above the notification-area anchor.
- [ ] Enabled state and delay survive an ordinary restart.
- [ ] Startup checkmark matches the actual current-user `Run` value.
- [ ] Moving the executable and re-enabling startup updates the path.

### Recovery

- [ ] Normal exit restores the cursor scheme.
- [ ] Ending the main process in Task Manager triggers watchdog restoration.
- [ ] Manual `RestoreCursor.exe` restores the scheme and reports success.
- [ ] Windows sign-out/shutdown does not leave an altered cursor scheme.
- [ ] Lock/unlock and a UAC secure-desktop transition restore the cursor and
      restart a complete delay interval.
- [ ] Running without `RestoreCursor.exe`, then ending the interactive main
      process, triggers restoration through the main executable's self-watchdog.

### Compatibility

- [ ] Windows 10 x64 at 100%, 150%, and 200% display scaling.
- [ ] Windows 11 x64 at 100%, 150%, and 200% display scaling.
- [ ] Mixed-DPI multi-monitor movement and notification-area placement.
- [ ] Explorer restart recreates the notification-area icon.
- [ ] A second launch surfaces the existing instance instead of duplicating it.
- [ ] Full-screen games and borderless-window applications receive normal input.

## Static release inspection

Before release, verify that both executables:

- are PE32+ x64 GUI applications;
- contain product, file, and original-filename metadata;
- embed the requested-execution-level and per-monitor V2 manifest;
- enable ASLR, NX/DEP, high-entropy VA, Control Flow Guard, and CET compatibility;
- import no networking libraries;
- contain no source-machine paths or credentials; and
- are signed with the intended certificate, if signing is available.

See [`RELEASING.md`](RELEASING.md) for packaging and checksum steps.
