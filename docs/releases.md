# Releases and GitHub automation

Versions use **vX.X.X** tags, with X.X.X in package metadata. Prerelease status
is a GitHub release property rather than a different version spelling.
`python tools/check_release_version.py --tag v0.3.1` checks the JavaScript,
Tauri, Rust package and lockfile versions together.

## Downloads

Open a release's **Assets** list and choose the file for your task:

| Download | Use it for |
| --- | --- |
| Firmware `.333` | Install on the TheoryBoard through the browser or desktop updater. |
| Windows setup `.exe` or portable `.zip` | Run the desktop Client on Windows x64. Extract the complete portable ZIP before opening it. |
| macOS `.dmg` | Install the desktop Client on Apple Silicon. |
| Matching firmware source/input archive | Inspect or reproduce the exact firmware build, including its reviewed musical input. |
| Checksums and build manifests | Verify the selected download and identify its build. |

The browser Client needs no desktop download. GitHub also adds **Source code**
ZIP/TAR archives automatically; these contain project source, not built apps or
firmware, and do not include the separate musical input needed to build firmware.

The historical v0.1.0 release provides source only. Release downloads include
standalone firmware, matching source/input, Windows and macOS Clients, and
checksums. Read the notes attached to the release for platform and device-test
limits.

## Release history

- **v0.3.1:** All seven Play views share the approved dotted, notched status-icon
  frame. Detailed retains its six information rows, with the last below the
  lower rule. The site gains the Client-style mobile navigation control, and
  the README and overview explain resource telemetry and performance captures.
  C-rendered previews, native regressions and phone/desktop browser checks pass;
  the new frame still awaits a physical OLED check. Musical behavior, saved
  settings format and existing platform/recovery qualification are unchanged.

- **v0.3.0:** Performance telemetry and timed Diagnostics captures, guarded
  configuration-restoration progress, and behavior-preserving rendering and
  musical-snapshot optimizations. The accepted firmware is
  **db4aaa0ea0e62fe0**. Focused board comparisons measured 10.5% less render work
  at idle, 35.3% with three held notes and 41.4% during MPE pressure, with unchanged
  frame cadence. Static RAM/CCM did not grow; the firmware uses 848 additional
  flash bytes. The browser update and running application were verified. Guarded
  restore failure/disconnect paths, maximum-load timing and broader platform
  qualification remain separate; no backup was restored in these checks.

- **v0.2.2:** Split physical-half expression settings, selectable MPE pressure
  output and the shared HUD component system. The approved site and Client design
  includes a divider-integrated sidebar tab, continuous hover/focus trace,
  closer icon accents and a linked component reference in the guide. macOS
  window controls remain available in the bundled reference. V19 expression
  checks have focused board evidence; later routing indicators/menu cleanup
  remain host-tested. macOS/Linux USB and recovery limits remain unchanged.

- **v0.2.1:** Client navigation, backups, Harmony controls and
  live diagnostics are refined; CPU, RAM, stack and flash resources are exposed
  in the Client and device menu. Shared OLED styling and a three-part Quick start
  guide complete this milestone. Focused display, resources and Arp timing
  checks passed on a board; broader platform and recovery limits remain.
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
before publication. Keep testing releases marked prerelease until the required board tests
are recorded. Preserve original third-party terms and project GPL notices.

## Prepare each release

1. Merge the intended changes through develop into reviewed main, including
   matching X.X.X package versions, release notes and qualification status.
2. Check the version with `python tools/check_release_version.py --tag vX.X.X`,
   then create the release tag on that reviewed main commit.
3. The tag starts **Release Client installers**. After the Windows and macOS
   builds pass, it uploads their packages, checksums and manifests to a draft
   prerelease. A manual run can build an existing compatible tag.
4. From the exact tag and reviewed musical input, prepare and inspect the
   firmware and its matching source/input archive as described above. Attach
   these, their checksums, the firmware manifest and qualification notes to the
   same draft. This is a reviewed maintainer step; public Client CI does not
   build the private-input firmware.
5. Verify that the notes and manifests identify the tag, full firmware build ID
   and exact asset hashes. Check the complete download set, retained notices
   and declared platform limits before publishing. Keep testing releases marked
   prerelease until their qualification checklist is complete.

Merging to main deploys the site; it does not create a release, move an existing
tag or replace its downloads. Publish a new version for new binaries rather
than relabeling them as an earlier release.

## Pages and pull requests

The Project site workflow builds the bounded public bundle and checks shared
installer/guide behavior for pull requests. Only the default branch deploys to
the `github-pages` environment, with scoped Pages/OIDC permissions. License-only
changes also trigger a rebuild. No firmware, private inputs or device bindings
are included in Pages.

Core checks, native desktop source checks and updater checks exercise public
source independently of instrument builds. Main protection requires `core` and `PR routing`, an up-to-date branch and
resolved review conversations. Normal PRs target develop; only develop from this
repository can promote to main. Force-pushes and deletion are blocked, including
for administrators. A green build does not substitute for board qualification.
