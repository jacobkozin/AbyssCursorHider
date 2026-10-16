# Releasing

This checklist separates ordinary continuous-integration artifacts from a
public release. CI artifacts are unsigned and should not be presented as an
authenticated production build.

## Prepare

1. Complete the manual checklist in [`TESTING.md`](TESTING.md).
2. Update `CHANGELOG.md` and remove the prerelease flag/version suffix from the
   resource files.
3. Confirm `LICENSE` is present in both the source archive and packaged build.
4. Build from a clean tagged commit on a controlled Windows build environment.

## Build and test

```powershell
cmake --preset windows-x64
cmake --build --preset release --parallel
ctest --preset release
cmake --build build/windows-x64 --config Release --target package
```

Do not package locally ignored historical executables. The package must contain
only products built from the release commit.

## Inspect

- Review compiler output and confirm there were no warnings.
- Inspect imports, PE security flags, embedded manifests, and version resources.
- Scan the executables and ZIP with the available endpoint-security tools.
- Check the package for credentials, local paths, debug databases, logs, dumps,
  and intermediate build files.
- Verify `AbyssCursorHide.exe` and `RestoreCursor.exe` stay together.

## Sign and hash

If an Authenticode certificate is available, sign both executables before
packaging and timestamp the signatures through the certificate provider. Never
store the certificate or its password in the repository.

Generate checksums only after signing and final packaging:

```powershell
Get-FileHash .\AbyssCursorHide.exe -Algorithm SHA256
Get-FileHash .\RestoreCursor.exe -Algorithm SHA256
Get-FileHash .\AbyssCursorHide-5.0.0-windows-x64.zip -Algorithm SHA256
```

## Publish

1. Create a version tag that points to the reviewed commit.
2. Create a GitHub Release from that tag.
3. Attach the final ZIP and a plain-text SHA-256 file.
4. State clearly whether the files are signed.
5. Download the published assets and verify their hashes one final time.

Never commit release executables, ZIP files, certificates, or signing secrets to
the normal Git history.
