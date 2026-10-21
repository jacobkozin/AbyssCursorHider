# Antivirus false positives

Abyss Cursor Hide is new and its executables are not code-signed. Some antivirus
products judge programs like that by educated guesses: machine-learning scores
of the file and rules about what a program does. This page records what was
flagged during testing, what was learned from it, and how to check a download
yourself.

Nothing in the source opens a network connection, downloads anything, or writes
anywhere except the two registry locations listed in the [README](../README.md#settings).

## What was flagged

All of this happened on one Windows 11 PC running Trend Micro Maximum Security,
on October 20 and 21, 2026, while v5.0.0 was being finished.

| File | Detection | How it was caught |
| --- | --- | --- |
| `RestoreCursor.exe` while it doubled as the watchdog | `TROJ.Win32.TRX.XXPE50FFF110` | Real-time scan, the moment the linker wrote the file |
| Rebuilds of that same `RestoreCursor.exe` | `TROJ.Win32.TRX.XXPE50FFF110` | Same, every time |
| The v1 executable from September 2026, copied to a new folder | `TROJ.Win32.TRX.XXPE50FFF110` | Same |
| The unit-test program `abyss_core_tests.exe` | `TROJ.Win32.TRX.XXPE50FFF110`, `TSPY.Win32.TRX.XXPE50FFF110` | Same |
| The new manual-only `RestoreCursor.exe`, built with Visual Studio 2026 | `TROJ.Win32.TRX.XXPE50FFF110` | Same |
| `AbyssCursorHide.exe`, right after it added itself to Start with Windows | `HEU_AEGISCS984` | Behavior monitoring |

`TRX` detections come from Trend Micro's machine-learning engine. It scores a
file's static features; it is not a signature for any known malware. The same
Trend Micro installation has also removed developer tools such as Qt's `moc.exe`
and Go's `go.exe`. `HEU_AEGIS` detections come from its behavior monitor, which
reacts to what a program does, here an unknown program adding itself to Windows
startup.

## What was learned

To find out what the machine-learning engine reacted to, three copies of
`RestoreCursor.exe` were built, each changing one thing:

| Test build | Result |
| --- | --- |
| Unchanged | Removed |
| Without its icon, which is over half of the file | Removed |
| Without the `--watch <pid>` watchdog mode | Not flagged |

At the time `RestoreCursor.exe` was a small, windowless program that was handed
a process ID, waited for that process to exit, and then changed a system
setting. That is also how many malware helper processes behave.

v5.0.0 moved the watchdog into the main program (`AbyssCursorHide.exe --watch
<pid>`) and made `RestoreCursor.exe` a plain manual tool. That change was worth
making on its own, because it left one watchdog implementation instead of two.
It did not make the detections go away for good, though: the same manual-only
`RestoreCursor.exe` built with a newer compiler was flagged again, and so was
the unit-test program. Small unsigned programs can be flagged almost at random,
so rewriting code until one scanner is happy is not a real fix.

## What actually fixes it

- **Code signing.** A valid Authenticode signature with a verified publisher is
  the main signal these engines trust. Options for this project:
  - [SignPath Foundation](https://signpath.org/) signs open-source projects for
    free. It requires an OSI license, an automated CI build from the public
    repository, a published code-signing policy and an existing release. The
    publisher shown is SignPath Foundation.
  - [Microsoft Artifact Signing](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options)
    (formerly Trusted Signing) costs about $9.99 a month and is open to
    individual developers in the US and Canada after identity validation. The
    publisher shown is the developer.
- **Reporting false positives** to the vendor, with the file and the detection
  name. Trend Micro takes these through its
  [detection re-evaluation form](https://www.trendmicro.com/en_us/about/legal/detection-reevaluation.html)
  (or through support for Trend Micro customers).
- **Time.** Reputation systems such as Windows SmartScreen trust a file more as
  more people download it without problems.

v5.0.0 is released unsigned. Signing is the next step once the project has a
release to point to.

## Checking a download yourself

- Compare the ZIP's SHA-256 with `SHA256SUMS.txt` on the release page:

  ```powershell
  Get-FileHash .\AbyssCursorHide-5.0.0-windows-x64.zip -Algorithm SHA256
  ```

- The release ZIP is built by the `draft-release` job in
  [GitHub Actions](https://github.com/jacobkozin/AbyssCursorHider/actions/workflows/build.yml)
  from the tagged commit, so its build log shows exactly what went into it.
- You can upload the ZIP to [VirusTotal](https://www.virustotal.com/) for a
  second opinion from many scanners.
- You can build it yourself; see [Build from source](../README.md#build-from-source).

## If your antivirus blocks it

- If a scanner quarantines `AbyssCursorHide.exe` or `RestoreCursor.exe`, restore
  it from the quarantine and mark it as trusted, or build it from source.
- If a scanner blocks **Start with Windows**, either allow the app in the
  scanner or leave Start with Windows off and start the app yourself.
- If a scanner ends the app while the pointer is hidden, the watchdog normally
  brings the pointer back. If the scanner ended the watchdog too, use the
  keyboard to start the app again (it restores the pointer as it starts) or run
  `RestoreCursor.exe`. Signing out also always works.
