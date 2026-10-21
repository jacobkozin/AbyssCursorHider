# Project history

## Why v5 is a reconstruction

The historical application evolved through several generated prototypes and
native binary packages. The final v4.17 package is intact, but its main source
file was deleted before this repository was prepared. Static symbols identify
that source file as `y_v417.c`.

The newest surviving main source is packaged with v4.5 but is byte-identical to
the v4.1 source and implements only v4.1 behavior. It cannot reproduce v4.17.
The surviving recovery source remained stable and its compiled executable is
identical from v2 through v4.17.

Rather than mislabel the v4.1 source as v4.17, the repository treats v4.17 as a
verified historical binary and develops a clean successor from documented
behavior. The v5 source uses official Windows SDK declarations, ordinary MSVC
linking, modular code, tests, metadata, and CI.

## Documented final-version progression

| Version | Documented change |
| --- | --- |
| 4.8 | DPI-sized notification-area icon loading |
| 4.9 | Per-monitor V2 DPI work; introduced a startup import failure |
| 4.10 | Corrected imports and made DPI APIs defensive |
| 4.11 | Persisted delay/enabled state and added Start with Windows; archive is empty |
| 4.12 | Mouse clicks restore the cursor; settings menu reopens after startup changes |
| 4.13 | Reuses the original menu coordinates to prevent position drift |
| 4.14 | All settings reopen consistently; removed Restore from the normal menu |
| 4.15 | Reduced the menu to the final production choices |
| 4.16 | Anchored the popup to open upward from the notification area |
| 4.17 | Raised that popup anchor by eight pixels |

## v5

| Version | Change |
| --- | --- |
| 5.0.0 (2026-10-21) | First release built from source: a clean rebuild with unit tests, CI and documentation. Final testing moved the watchdog into the main program and fixed Hide Now and Start with Windows bugs. See [`../CHANGELOG.md`](../CHANGELOG.md). |

## Verified v4.17 identity

The package recovered directly from the historical conversation is byte-for-byte
identical to the v4.17 package inside the development archive:

```text
SHA-256  1E8FBA087BFB4B21D2BDCFAC26D07566AFE9CAD79CED52995DB964DFC2F02732
File     Abyss_Cursor_Hide_Tray_v4_17.zip
```

The same four payload files also appeared in an alternate outer ZIP named
`AbyssCursorHider.zip`; its different archive hash reflects packaging metadata,
not different executable contents. Both identities are recorded in
[`../checksums/SHA256SUMS-v4.17.txt`](../checksums/SHA256SUMS-v4.17.txt).

Neither historical ZIP is committed or published; only their hashes are
recorded here. The v4.17 binaries are unsigned and cannot be rebuilt from
source.
