# Releasing

Releases are built by GitHub Actions from a version tag, so the published ZIP
always comes from the public source at that tag. Nothing is published
automatically: the workflow only creates a draft.

## 1. Prepare the version

1. Set the version in `project()` in `CMakeLists.txt`. The file properties of
   both executables and the ZIP name are generated from it.
2. Add a `## [x.y.z] - YYYY-MM-DD` section at the top of `CHANGELOG.md`. The
   release workflow uses that section as the release notes and stops if it is
   missing.
3. Run the manual checklist in [TESTING.md](TESTING.md) on a Release build and
   update its results.
4. Build and test locally:

   ```powershell
   cmake --preset windows-x64
   cmake --build --preset release
   ctest --preset release
   cmake --build --preset package
   ```

5. Commit, push to `main`, and wait for the **Build and test** workflow to pass.

## 2. Tag and draft

```powershell
git tag -a v5.0.0 -m "Abyss Cursor Hide 5.0.0"
git push origin v5.0.0
```

The tag runs the normal build and test jobs, then the `draft-release` job,
which:

- configures, builds, tests and packages Release from the tagged commit;
- writes `SHA256SUMS.txt` for the ZIP; and
- creates a **draft** GitHub Release with the ZIP, `SHA256SUMS.txt` and the
  matching `CHANGELOG.md` section.

## 3. Check and publish

1. Download the ZIP and `SHA256SUMS.txt` from the draft and compare the hash:

   ```powershell
   Get-FileHash .\AbyssCursorHide-5.0.0-windows-x64.zip -Algorithm SHA256
   ```

2. Unzip it, run the programs once, and confirm the file properties show the
   new version.
3. Check that the release notes say the executables are unsigned and link to
   [ANTIVIRUS.md](ANTIVIRUS.md).
4. Publish the draft.

## Code signing

The executables are not signed yet. When signing is added, sign both
executables in the workflow before packaging, then hash the signed ZIP. Keep
certificates, keys and passwords out of the repository and out of ordinary CI
variables; [ANTIVIRUS.md](ANTIVIRUS.md#what-actually-fixes-it) lists the
options being considered.

Never commit executables, ZIP files or signing material to the repository.
