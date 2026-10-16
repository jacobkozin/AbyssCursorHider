# Technical notes

## Source status

The repository contains a new implementation of the documented v4.17 behavior,
developed as v5.0.0. It is not recovered original source. The historical v4.17
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
| Build | CMake, MSVC, Windows SDK, warnings as errors |
| Recovery | In-process cleanup, external/self-watchdog, and manual utility |
| CI | Windows build, tests, package artifact |

No environment variables, networking libraries, third-party packages, database,
log, service, driver, or elevated process are required.

## Release-build observations

Local x64 Release builds have been verified to include:

- PE32+ Windows GUI subsystem;
- no COFF symbol table in the final image;
- ASLR and high-entropy virtual addresses;
- NX/DEP compatibility;
- Control Flow Guard instrumentation and function table;
- CET compatibility metadata;
- embedded per-monitor V2 DPI and as-invoker manifest;
- embedded multi-resolution icon; and
- populated company, product, file-version, and original-filename resources.

Development builds are not Authenticode-signed. Signing is intentionally kept
outside the repository because private keys and certificate passwords must never
enter Git history.

## Cursor API constraints

`ShowCursor` maintains a display count associated with the calling thread and is
not sufficient to hide the pointer consistently across other applications.
Consequently, the app uses `SetSystemCursor`, matching the behavior of the
historical implementation. This changes the current user's global system cursor
resources until `SPI_SETCURSORS` reloads the configured scheme.

That API choice creates the project's most important failure mode: abrupt process
termination while hidden can otherwise leave transparent cursor resources
active. The separate watchdog and manual restoration utility specifically
mitigate that risk.

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
