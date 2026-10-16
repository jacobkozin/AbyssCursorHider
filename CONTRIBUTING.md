# Contributing

Thank you for improving Abyss Cursor Hide.

## Before contributing

The current source is a clean v5 reconstruction of documented v4.17 behavior,
not recovered original v4.17 source. Avoid changing history or documentation in
a way that implies the legacy binary is reproducible.

Contributions are accepted under the repository's [MIT License](LICENSE).
Discuss substantial changes with the maintainer before investing significant
work so the proposed direction can be agreed upon first.

## Development setup

Install Visual Studio 2022 or newer with the Desktop development with C++
workload, a Windows SDK, and CMake 3.25 or newer. Then run:

```powershell
cmake --preset windows-x64
cmake --build --preset debug --parallel
ctest --preset debug
```

## Change expectations

- Keep the utility native, offline, per-user, and usable without elevation.
- Preserve reliable cursor restoration on every exit and error path.
- Treat changes to `SetSystemCursor`, startup registration, or the watchdog as
  security-sensitive.
- Add focused tests for validation or platform-independent logic.
- Build with warnings as errors and run the appropriate manual checks in
  [`docs/TESTING.md`](docs/TESTING.md).
- Update user-facing documentation and `CHANGELOG.md` when behavior changes.
- Do not commit executables, ZIPs, build folders, registry exports, logs, dumps,
  credentials, certificates, signing material, or machine-specific settings.

## Pull requests

Describe the problem, the chosen approach, testing performed, Windows versions
used, and any behavior that could not be tested. Keep unrelated changes out of
the same pull request.

For cursor behavior, include explicit results for normal restoration and forced
termination recovery. For notification-area changes, include scaling and
Explorer-restart checks.

## Bug reports

Use the bug-report template and remove usernames, filesystem paths, registry
exports, screenshots with private information, credentials, and other personal
data before posting.

Do not report vulnerabilities in a public issue. Follow [`SECURITY.md`](SECURITY.md).
