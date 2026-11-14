# Releasing

A release is created by hand on GitHub, and GitHub Actions then builds the
tagged source and attaches the ZIP, so the published ZIP always comes from the
public source at that tag.

The commands below use a `$version` variable; set it first:

```powershell
$version = '5.0.1'
```

## 1. Prepare the version

1. Set the version in `project()` in `CMakeLists.txt`. The file properties of
   both executables and the ZIP name are generated from it.
2. Set the same version, with a fourth `.0`, in the `assemblyIdentity` of
   `resources/app.manifest`. That one is updated by hand.
3. Add a `## [x.y.z] - YYYY-MM-DD` section at the top of `CHANGELOG.md`. It is
   also the text for the release notes, so use full `https://` links in it.
4. Run the manual checklist in [TESTING.md](TESTING.md) on a Release build and
   update its results.
5. Build and test locally:

   ```powershell
   cmake --preset windows-x64
   cmake --build --preset release
   ctest --preset release
   cmake --build --preset package
   ```

6. Commit, push to `main`, and wait for the **Build and test** and **CodeQL**
   workflows to pass. Only CI runs the tests that hide and restore the real
   system cursors.

## 2. Tag and publish

```powershell
git tag -a "v$version" -m "Abyss Cursor Hide $version"
git push origin "v$version"
```

On GitHub, open **Releases**, choose **Draft a new release**, pick the tag,
use `Abyss Cursor Hide <version>` as the title and paste the version's
`CHANGELOG.md` section as the notes, then publish it.

Publishing runs the **Build and test** workflow for the tag, and its
`release-assets` job:

- builds, tests and packages Release from the tagged commit;
- stops if the ZIP's version does not match the tag;
- records a build-provenance attestation for the ZIP and both executables; and
- attaches the ZIP and `SHA256SUMS.txt` to the release, usually within a few
  minutes.

If the job fails for a reason that has nothing to do with the source, such as
a runner problem, open the run and choose **Re-run failed jobs**; it builds the
same tagged commit again. If the source itself is wrong, fix it on `main` and
release the next patch version. The **Protect version tags** ruleset blocks
moving or deleting `v*` tags, so a published tag always means the same code.

## 3. Check

1. Confirm both files are on the release and the job passed.
2. Compare the hash of the downloaded ZIP with `SHA256SUMS.txt`, and check the
   attestation:

   ```powershell
   Get-FileHash ".\AbyssCursorHide-$version-windows-x64.zip" -Algorithm SHA256
   gh attestation verify ".\AbyssCursorHide-$version-windows-x64.zip" --repo jacobkozin/AbyssCursorHider
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
