# Releases and GitHub automation

Versions use **vX.X.X** tags, with X.X.X in package metadata. Prerelease status
is a GitHub release property rather than a different version spelling.
`python tools/check_release_version.py --tag v0.2.0` checks the JavaScript,
Tauri, Rust package and lockfile versions together.

## Release history

- **v0.1.0:** cleaned historical snapshot of the first published implementation.
  The public history was rewritten to remove internal comparisons and apply GPL
  consistently. It does not retroactively withdraw permissions from earlier copies.
- **v0.2.0:** Musical Expansion, initially a prerelease. Harmony suggestions,
  modulation guidance, scale discovery and voicing modes have host coverage;
  physical qualification follows the [board checklist](v0.2-board-checklist.md).

## Client installers

The Release Client installers workflow builds the exact tag on macOS ARM64 and
Windows x64. It runs native core and Rust tests, builds the real public Client,
and omits local firmware resources. It produces a DMG, an EXE installer,
Windows portable ZIP, checksums and build manifests. Windows needs the WebView2
runtime. The installers are unsigned and the macOS app is not notarized.
Installer packaging success does not qualify USB operation on that platform.

The workflow stages a **draft prerelease** after both platforms succeed. A
maintainer reviews the complete asset set and notes before publication. It
never overwrites assets or a published release. On a partial upload, compare
existing asset hashes before manually removing an incomplete upload and retrying.
Historical tags without this workflow need a separately reviewed build; do not
label a newer binary as an older release.

## Standalone firmware and matching source

Firmware remains a separate `.333` download; it is not hidden in a Client
installer. Build from a fresh directory with the authorized musical archive,
run the native tests and build audit, then package the observed plaintext
profile and run the Rust updater's read-only `inspect` and synthetic `simulate`.
Neither operation authorizes flashing.

Retain the `THY666-instrument-alpha-<8 hex build prefix>.333` basename: existing
Clients use that prefix to bind the package to its embedded full build identity.
The release tag, package checksum and full firmware build ID must all be stated
in the release notes/manifest. A Git tag is not the firmware's build identity.

Publish matching source alongside firmware: the exact tagged project source,
reviewed lossless musical input, its checksum, dependency source/version pins,
notices and reproduction instructions. The maintainer has authorized this musical
input for release source archives; it remains outside the default Git checkout
and Pages bundle. Exclude vendor firmware, captures, backups, credentials,
device identifiers and local paths. Do not publish a binary if its corresponding
source/input bundle is missing or differs from the audited build.

Attach the firmware, matching source, checksums and board checklist to the draft
before publication. Keep v0.2.0 marked prerelease until the required board tests
are recorded. Preserve original third-party terms and project GPL notices.

## Pages and pull requests

The Project site workflow builds the bounded public bundle and checks shared
installer/guide behavior for pull requests. Only the default branch deploys to
the `github-pages` environment, with scoped Pages/OIDC permissions. License-only
changes also trigger a rebuild. No firmware, private inputs or device bindings
are included in Pages.

Core checks, native desktop source checks and updater checks exercise public
source independently of instrument builds. Required branch checks should use
these job names; a green build does not substitute for board qualification.
