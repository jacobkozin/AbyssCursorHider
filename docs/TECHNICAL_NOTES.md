# Technical notes

## Source status

The repository contains a new implementation of the documented v4.17 behavior,
first released as v5.0.0 on 2026-10-21. It is not recovered original source. The historical v4.17
main executable embeds the compilation-unit name `y_v417.c`; that file is not
present in any supplied archive or searched backup location.

The distinction matters:

- v4.17 can be identified and described by its verified hashes and behavior;
- v4.17 cannot be rebuilt, audited at source level, or reproduced exactly; and
- v5 can be reviewed and built entirely from this repository but must be tested
  as a new implementation.

## Reconstructed implementation

| Area | v5 implementation |
| --- | --- |
| Platform | Native Windows x64 GUI applications |
| Language | C++17 with official Windows SDK headers |
| UI | Notification-area icon and native popup menu |
| Input | Background Raw Input with redundant 100 ms mouse-state polling |
| Persistence | Current-user registry settings and optional Run entry |
| Build | CMake presets, MSVC (Visual Studio 2022 or newer), warnings as errors, MSVC code analysis on Debug builds |
| Recovery | In-process cleanup, a check after every restore with a retry from a fresh process (`--restore <roles>`), a supervised watchdog copy of the app (`--watch <pid>`), a restore at every launch, and a manual utility |
| CI | Debug and Release builds and tests, tests that hide and restore the runner's real cursors, CodeQL, the package artifact, and the release ZIP, its SHA-256 and a build-provenance attestation for each published release |

The app needs no environment variables, networking libraries, third-party
packages, database, log, service, driver, or elevated process.

## Release-build observations

Local x64 Release builds have been verified to include:

- PE32+ Windows GUI subsystem;
- no COFF symbol table in the final image;
- ASLR and high-entropy virtual addresses;
- NX/DEP compatibility;
- Control Flow Guard instrumentation and function table;
- CET compatibility metadata;
- embedded per-monitor V2 DPI and as-invoker manifest;
- embedded multi-resolution icon;
- populated company, product, copyright, file-version, and original-filename
  resources; and
- no debug-database path or other build-machine path in the image.

Builds are not Authenticode-signed yet. Signing would happen in CI with keys
kept outside the repository; see [`ANTIVIRUS.md`](ANTIVIRUS.md) for why signing
matters and the options being considered.

## Cursor API constraints

`ShowCursor` maintains a display count associated with the calling thread and is
not sufficient to hide the pointer consistently across other applications.
Consequently, the app uses `SetSystemCursor`, matching the behavior of the
historical implementation. This changes the current user's global system cursor
resources until `SPI_SETCURSORS` reloads the configured scheme.

`SetSystemCursor` takes ownership of the cursor it is given only when it accepts
the role. When it rejects one, the caller still owns that cursor and has to
destroy it. 5.0.0 also replaced two obsolete roles, `OCR_SIZE` and `OCR_ICON`.
Current Windows versions accept `OCR_ICON` but reject `OCR_SIZE`, and 5.0.0
never destroyed the rejected cursor, so every hide leaked one until the app
reached the Windows limit of 10,000 USER objects per process.

`SPI_SETCURSORS` loads the scheme's cursor files inside the calling process.
When that process cannot create more cursors, or the scheme names a file that
cannot be loaded, Windows keeps whatever cursor those roles had, which after a
hide is the transparent one. The app therefore reads every role back after a
restore instead of trusting the call. For a role that is still transparent it
puts back a copy of the cursor saved before hiding, and if that fails it
reloads the scheme from a fresh process (`AbyssCursorHide.exe --restore`).

That API choice creates the project's most important failure mode: abrupt process
termination while hidden can otherwise leave transparent cursor resources
active. The watchdog process, the restore at every launch and the manual
restoration utility specifically mitigate that risk.

## Historical binary observations

The supplied v4.17 main and recovery executables are unsigned x64 native
applications. Their static content contains no detected username, user-profile
path, credential, private key, email, URL, IP address, environment file,
database, or networking import. Those bounded observations establish neither
source provenance nor malware safety.

The historical main executable has ASLR, NX, and high-entropy VA enabled but
retains a full COFF symbol table and lacks populated version information. The v5
build corrects those packaging deficiencies while preserving the intended user
experience.

See [`HISTORY.md`](HISTORY.md) for the version timeline and
[`ARCHITECTURE.md`](ARCHITECTURE.md) for the new design.
