# Changelog

Notable changes are recorded here. The v4 entries are reconstructed from the
verified release notes and development conversation; v4.17 itself is binary-only.

## [5.0.1] - 2026-11-13

### Fixed

- After a few days of use, links and other places could show no pointer at
  all. Every hide leaked a cursor through the obsolete `OCR_SIZE` role, which
  current Windows versions reject; the rejected cursor was never destroyed.
  Once the app reached the Windows limit of 10,000 USER objects per process,
  reloading the cursor scheme quietly left most cursors, including the link
  hand, transparent. The app no longer replaces the obsolete roles and
  destroys any cursor Windows does not accept.
- If the cursor scheme named a file that no longer exists, such as one from a
  deleted cursor pack, that pointer stayed invisible after the first hide
  until you signed out. The app now keeps a copy of each visible cursor while
  the pointer is hidden and puts it back when reloading the scheme cannot.
- Starting the app again did not bring the pointer back while another copy was
  running. Every launch now reloads the cursor scheme first.
- With Start with Windows on, a busy notification area at sign-in made the app
  show an error and quit. It now keeps running and retries the tray icon.
- After a DPI or monitor change, the tray icon could stop updating and stay
  blurry until sign-out.
- Pens, tablets, Remote Desktop and virtual machines that keep reporting the
  same position could stop the pointer from ever hiding.
- A menu opened from the keyboard, or by a second launch, appeared at the
  hidden pointer instead of at the tray icon.

### Changed

- Every restore is checked: the app reads each cursor back and puts back its
  saved copy of any that is still transparent. If that fails too, it starts
  `AbyssCursorHide.exe --restore`, which reloads the scheme in a fresh
  process. It retries with a growing delay and does not hide the pointer again
  until a restore is confirmed.
- The app only hides the pointer while its watchdog is running, and restarts
  the watchdog if it exits.
- It refuses to hide when it is using an unusual number of handles, so a
  future leak cannot end the way 5.0.0 did.
- It no longer hides the pointer over the windows of more privileged programs
  (Task Manager, an admin terminal), where Windows hides clicks and the wheel
  from it, or while the tray menu is open or a mouse button is held.
- Only sleep and wake count as power events, not battery level changes.
- DLLs load from System32 only, so a DLL dropped next to the exe cannot run
  inside it.

### Build and release

- New tests: `abyss_cursor_tests` checks the cursor code with private cursors,
  and `abyss_system_cursor_tests`, which runs only on CI, hides and restores
  the runner's real cursors 2,000 times, checks that the handle counts stay
  flat, and exercises a full handle quota, a missing cursor file, `--restore`
  and the watchdog.
- `abyss_binary_tests` checks the security settings of both programs on every
  build: ASLR, DEP, Control Flow Guard, CET, DLLs from System32 only, no
  networking imports, the manifest and the version information.
- Debug builds run MSVC `/analyze`, and its warnings fail the build. CodeQL
  scans the code on every change.
- Release files come with a build-provenance attestation, so
  `gh attestation verify` can confirm they were built from this repository.

### Known issues

- The executables are still not code-signed. See
  [docs/ANTIVIRUS.md](https://github.com/jacobkozin/AbyssCursorHider/blob/main/docs/ANTIVIRUS.md).

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
