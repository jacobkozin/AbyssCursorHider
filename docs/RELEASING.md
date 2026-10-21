# Releasing

A release is created by hand on GitHub, and GitHub Actions then builds the
tagged source and attaches the ZIP, so the published ZIP always comes from the
public source at that tag.

## 1. Prepare the version

1. Set the version in `project()` in `CMakeLists.txt`. The file properties of
   both executables and the ZIP name are generated from it.
2. Add a `## [x.y.z] - YYYY-MM-DD` section at the top of `CHANGELOG.md`. It is
   also the text for the release notes.
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

## 2. Tag and publish

```powershell
git tag -a v5.0.0 -m "Abyss Cursor Hide 5.0.0"
git push origin v5.0.0
```

On GitHub, open **Releases**, choose **Draft a new release**, pick the tag,
use `Abyss Cursor Hide 5.0.0` as the title and paste the version's
`CHANGELOG.md` section as the notes, then publish it.

Publishing runs the `release-assets` job, which:

- builds, tests and packages Release from the tagged commit;
- stops if the ZIP's version does not match the tag; and
- attaches the ZIP and `SHA256SUMS.txt` to the release, usually within a few
  minutes.

## 3. Check

1. Confirm both files are on the release and the job passed.
2. Compare the hash of the downloaded ZIP with `SHA256SUMS.txt`:

   ```powershell
   Get-FileHash .\AbyssCursorHide-5.0.0-windows-x64.zip -Algorithm SHA256
   ```

3. Unzip it, run the programs once, and confirm the file properties show the
   new version.

## Code signing

The executables are not signed yet. When signing is added, sign both
executables in the workflow before packaging, then hash the signed ZIP. Keep
certificates, keys and passwords out of the repository and out of ordinary CI
variables; [ANTIVIRUS.md](ANTIVIRUS.md#what-actually-fixes-it) lists the
options being considered.

Never commit executables, ZIP files or signing material to the repository.
