# Technical notes

These notes describe what can be established from the supplied v4.17 release bundle without executing it. They are intended to make the current binary-only state explicit for maintainers and reviewers.

## Supplied components

| Component | Purpose | Format |
| --- | --- | --- |
| `AbyssCursorHide.exe` | Main notification-area application | Native Windows x64 GUI executable |
| `RestoreCursor.exe` | Reloads the Windows cursor scheme | Native Windows x64 GUI executable |
| `abyss_cursor_hide.ico` | Runtime/tray icon | 13-image Windows icon, 16x16 through 256x256 |
| `README.txt` | Original v4.17 delta note | Plain text; retained locally and migrated to `CHANGELOG.md` |

The two executables remain in the local prepared folder but are ignored by Git. They should be distributed as release assets rather than normal source-branch contents.

## Observed application behavior

Static imports and embedded labels indicate that the main application:

- creates a Windows notification-area icon and popup menu;
- offers delays of 1, 2, 3, 5, 10, 15, 30, and 60 seconds;
- exposes enabled/paused, **Hide Now**, **Start with Windows**, and **Exit** actions;
- hides and restores system cursor resources through User32 APIs;
- monitors pointer movement and mouse clicks to restore the cursor;
- uses a named mutex to prevent multiple running instances;
- persists `Enabled` and `DelaySeconds` values under `HKEY_CURRENT_USER\Software\AbyssCursorHide`;
- optionally writes an `AbyssCursorHide` value beneath the current user's Windows `Run` key; and
- loads `abyss_cursor_hide.ico` from beside the application, with a fallback icon path.

The recovery executable uses `LoadCursorW`, `SetCursor`, and `SystemParametersInfoW`; its messages identify its purpose as restoring the Windows cursor scheme.

## Platform and implementation evidence

- Both executables are PE32+ GUI programs for AMD64 (`0x8664`), not managed .NET assemblies.
- The main executable contains a compilation-unit reference named `y_v417.c`, supporting a native C implementation.
- The main executable imports Kernel32, User32, and Shell32 functions and dynamically resolves registry and DPI functions from Windows system libraries.
- The available binary does not identify an exact compiler or build command, so neither is documented as authoritative.
- Both files are unsigned and omit populated company, product, file-version, and original-filename fields.
- The main executable retains internal symbol names. A production rebuild should remove unintended debug/linker symbols after preserving them separately for maintainers.

## Security and privacy observations

The supplied files contain no detected usernames, user-profile paths, email addresses, URLs, IP addresses, API tokens, passwords, private-key markers, certificates, databases, environment files, or alternate data streams. No networking API imports or network endpoints were observed.

Those are bounded static observations, not guarantees about runtime behavior. Complete validation requires the source code, a documented build, and controlled runtime testing.

## Work required for a reproducible source repository

1. Recover the authoritative source for both executables.
2. Document the compiler, SDK, build flags, and supported Windows versions.
3. Add a deterministic build definition and dependency lock or provenance information.
4. Add unit and behavior tests, especially around timer behavior, startup registration, DPI positioning, and cursor restoration.
5. Add CI that builds and tests from a tagged commit.
6. Add Windows manifests and complete version resources.
7. Strip release binaries, preserve private debug symbols, scan artifacts, publish checksums, and Authenticode-sign releases.
8. Confirm ownership and redistribution rights for the source and icon, then add an appropriate license.
