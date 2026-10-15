# Security policy

## Reporting a vulnerability

Use GitHub's private vulnerability reporting flow from the repository's **Security** tab when it is available. If private reporting has not yet been enabled, open a minimal issue asking the maintainer to establish a private contact channel; do not include vulnerability details in that issue.

Please include the affected version, Windows version, reproduction steps, impact, and any suggested mitigation. Remove credentials, personal data, and unrelated machine information from all submissions.

## Release verification

Release checksums are stored in [`checksums/`](checksums/). The current v4.17 executables are not Authenticode-signed and do not contain publisher or version metadata. A matching checksum establishes file identity only; it does not prove publisher identity or that a binary is safe.

## Audit limitations

The supplied v4.17 project is a binary distribution without source code or a reproducible build definition. Static inspection found no embedded credentials, private keys, personal paths, email addresses, URLs, or network-library imports. This cannot provide the assurance of source review, reproducible builds, dynamic testing, code signing, or malware analysis.

Before a future release, the project should add authoritative source, build in CI from a tagged revision, publish checksums, strip unintended symbols, add complete Windows version resources, scan the release artifacts, and sign them with an identifiable publisher certificate.
