# THY666 updater

The Rust updater installs supported `.333` packages through the existing held-pad
bootloader. Its native host or direct WebHID adapter drives the **Update** wizard:
**Backup → Update → Verification**. The executable also has command-line
tools. Same-build THY666 installations through native Windows and direct browser
WebHID have passed physical transfer and application verification. The browser
wizard also passed explicit configuration restoration with durable readback and
complete payload comparison. Host and simulation coverage remains separate.
Original v5 installation through the Rust updater and
macOS/Linux operation remain unqualified. The official updater remains an
exercised fallback.

The [desktop app](desktop.md) embeds this wizard and calls the same Rust updater
library directly. It also supplies native serial access for Client. Its
packaged interface needs no local web server, separate helper download or
session-token setup. The loopback helper remains an optional local route. Direct
browser installation uses WebHID with the same Rust validator/session compiled
to WebAssembly; see [Browser installation](browser-installation.md). That path
needs no helper download and has passed the Windows device workflow described below.

## Build

Use Python 3.10+, Rust 1.89+ and the platform build prerequisites in
[updater/README.md](../updater/README.md). Local validation uses Rust 1.98.1;
the declared minimum is not separately qualified.

```sh
python tools/build_updater.py --test
python -m unittest discover -s tests -p test_updater_helper.py -v
```

Dependencies are pinned in `updater/Cargo.lock`. The wrapper uses workspace
caches, remaps private build paths and checks the release artifact. Once the
dependencies are cached, add `--offline`. It never enumerates or opens a board.
The executable is `build/updater-target/release/thy666-updater`, with `.exe` on
Windows. Windows releases use a static C runtime; Linux still needs libudev.

## Direct browser wizard

Open the project site's Install page in desktop Chrome/Edge over HTTPS or
localhost. Nothing opens USB automatically. Detect board invokes the browser's
chooser for the update-mode interface; an existing THY666 application can first
connect over Web Serial to export its configuration. Custom files use the same
supported `.333` validation as native, including the full build identity.

After transfer and application health checks, a retained backup can be restored
through a separate explicit confirmation. Green completion waits for requested
restoration to pass durable save and complete payload re-export comparison, or
for an explicit decision to keep the current settings. This integrated flow
passed a Windows browser test through saved readback and payload comparison;
its cold-boot check is pending. It does not extend qualification to every backup type.

Build the portable policy with `python tools/build_browser_updater.py` after
installing Rust's `wasm32-unknown-unknown` target. It emits a verified, host-import-free
WASM/manifest pair under `web/generated/`; the public site stages only that pair
and the explicit installer modules. Firmware release choices require a reviewed
same-origin catalog; no vendor image or private musical corpus is bundled.

## Optional local helper wizard

Start the local app server at `http://127.0.0.1:3330/`, then double-click the
updater executable or launch it without arguments. This runs `serve --open` and
opens the served Update screen. An ephemeral token is consumed from the URL
fragment and removed. Hardware detection and installation still require explicit
actions. Advanced connection details retain manual helper address/token entry.

The app server must already be running; the executable is not yet a self-contained
public installer. For a different port, start `serve --open --origin` with the
actual loopback app origin. A `file://` page is unsupported.

The app normally opens **Client → Board**. Choose the first header tab,
**Update**, or the **install the firmware on your device** link beneath the
disconnected wireframe. Update is outside the Client sidebar. Its canonical
route is `#update`; older `#client/update` links remain supported.

## Three steps

| Step | Action and limits |
| --- | --- |
| Backup | Download applied THY666 configuration while that application is available. If already in the update mode or running unsupported firmware, explicitly continue with backup unavailable/skipped. |
| Update | Choose Latest THY666, Original v5 or Custom; inspect the package and review the target before Apply. Release the application connection and enter the update mode before transfer. |
| Verification | For THY666, explicitly reconnect the application, match its full build and target UID, then run the read-only status, I/O, settings and display checks. Only all passing checks produce white **Firmware verified** and a route to Client. |

To enter **update mode**:

1. **Power down.** Disconnect USB and the 9V supply, if used. Wait until the
   screen and all LEDs are off.
2. **Hold the pads.** Hold the top-left and bottom-left large pads of the
   **LEFT cluster**.
3. **Power up while holding.** Reconnect USB and the 9V supply, if used, while
   keeping both pads held. Only the TheoryBoard logo should appear, with no LEDs
   illuminated.
4. **Detect the board.** Release both pads, then click **Detect board**.

The animated 96-pad/43-control diagram marks the pair as instructions, not live
pad telemetry. Reduced motion uses a static illustration. Progress advances
from helper reports, never from the animation.

A detected THY666 application can advance through Backup before entering the
update mode to apply firmware. A board already in the update mode can advance through
Detect with the explicit unavailable-backup choice. There is no verified stock
backup path. **Do not install diagnostic firmware to obtain a backup.**

Configuration backup covers applied settings, calibration, custom library and
current song. It excludes firmware, bootloader and raw internal/external flash.
**Back up before installing:** the Windows same-build test reset configuration
to exact factory defaults, confirmed by comparing backup payloads before and
after installation. The cause and erase ranges are unproven. Restoring the
captured configuration passed readback and cold-boot checks: a fresh export
matched the original backup byte for byte. That sample had an empty library and
song, with default calibration overrides; nonempty content and nondefault
overrides still require restoration qualification.
Memory preparation may change external contents **before hardware/UID checks**;
an optional backup UID match protects payload selection, not that preparation.

For both Windows native and browser tests, all successfully paired application reports matched the
selected package exactly, both bootloader CRC replies matched, and activation
was followed by the expected application build and identity. Read-only status,
I/O, settings and display checks passed. The browser flow additionally restored
its retained configuration through durable readback and complete payload comparison.
This establishes completed same-build THY666 installations, not interrupted-update
recovery or raw-flash readback.

After a supported Original v5 transfer, the neutral **Update finished** result
means the transfer CRC checks completed. Original firmware has no THY666 telemetry
verification here; this result does not establish its running build or health.
SIMULATION remains visibly labeled and never produces physical verification or
a physical-success result. Test actual playing and persistence separately.

The helper owns an advisory per-user update lease while using HID. Other THY666
helper/CLI instances honor it; third-party updaters do not. Close them yourself.
The browser releases CDC before update work and prevents reopening it during an
active physical operation. No automatic backup restoration follows installation.

## Local firmware choices

| Source | Accepted material |
| --- | --- |
| Latest THY666 | The THY666 package in the locally prepared release catalog. This label does not fetch a hosted latest release. |
| Original v5 | Only the exact known original package SHA-256/profile supported by the Rust updater. Arbitrary encrypted packages are rejected. |
| Custom | Supported plaintext THY666 `.333` with its full build bound to the embedded literal, or the exact approved Original v5 package. A renamed THY666 file may need the expected 16-hex-digit build entered explicitly. Arbitrary encrypted vendor files are rejected. |

Prepare the optional catalog from authorized local files:

```sh
python tools/prepare_updater_releases.py --thy666 /path/to/THY666-instrument-alpha-12345678.333 --stock /path/to/Original-v5.333
```

For a renamed THY666 input, also pass `--expected-build` with its full 16-hex-digit
build. Keep the catalog and copied packages in ignored local output, outside `web/`.
The helper uses a sibling `releases.json` by default, or an explicit
`serve --releases PATH`. The project does not redistribute vendor firmware and
has no hosted latest-release service. A missing local choice is unavailable;
it does not silently download a replacement. Package integrity is not a publisher
signature.

## Package the Windows download

After a verified build, and optional local catalog preparation above, run:

```sh
python tools/package_updater.py
```

For a Windows x64 build this creates two identical ZIPs:

| Output | Purpose |
| --- | --- |
| `build/releases/THY666-updater-<version>-windows-x64.zip` | Versioned local release artifact. |
| `web/generated/updater/THY666-updater-windows-x64.zip` | Ignored download copy served by the local app. |

The ZIP contains the checked executable, project/dependency notices, quick start,
and the sibling release catalog/packages when present. The packager rechecks
their identities and ZIP contents without opening a board or browser. Without
a catalog, the wizard retains its Custom file option.

Extract the entire ZIP into one folder and keep its files together. The local app
server at port **3330** must still be running before opening the executable.
These are local distribution artifacts, not a self-contained public installer
or evidence of physical/cross-platform qualification. Vendor files included from
local inputs retain their existing redistribution rights; do not treat the local
download copy as permission to publish them.

## Standalone commands

These commands work offline and never open USB:

```sh
thy666-updater inspect THY666-instrument-alpha-12345678.333
thy666-updater inspect renamed.333 --expected-build 1234567890abcdef
thy666-updater backup-info saved.thyb
thy666-updater simulate THY666-instrument-alpha-12345678.333
thy666-updater serve --simulate
```

`simulate` exercises a synthetic transport, including activation disconnection.
The simulation helper and its board are visibly labeled; it cannot verify a
physical installation. `backup-info` checks supported container integrity and
redacts the UID, without claiming to validate all payload semantics.

The following commands access real hardware and belong only in an explicitly
planned device test:

```sh
thy666-updater devices
thy666-updater install THY666-instrument-alpha-12345678.333 --device SELECTED_ID --backup saved.thyb --confirm-install --acknowledge-preparation --confirm-physical-board
```

`devices` enumerates the exact update HID collection; it sends no protocol
commands. Installation re-enumerates that selection before opening it. If no
supported backup exists, replace `--backup saved.thyb` with `--without-backup`.
This acknowledges the limitation; it does not create recovery data.

## Supported package and result boundaries

| Item | Current policy |
| --- | --- |
| Package | Supported plaintext THY666 or the exact supported Original v5 SHA-256/profile. Other encrypted, scale and unknown profiles are rejected. |
| THY666 application | 64-byte aligned, valid SRAM/CCM stack and Thumb vectors, within `0x08040000..0x080c0000` (512 KiB). |
| Settings | Following 256 KiB belongs to settings; application payload cannot cross it. Preparation's external-memory effects remain a separate concern. |
| Identity | THY666's full build must be bound to an embedded literal. Original v5 is accepted only by its exact package SHA-256/profile. Optional backup UID is checked after preparation and before payload transfer; boot HID descriptors do not reveal the installed firmware. |
| Integrity | SHA-256 identifies the selected bytes. Both bootloader CRC replies must match the supported package's expected application CRC. These checks are not a cryptographic signature. |
| Completion | Activation can disconnect HID normally. THY666 needs explicit application and read-only telemetry checks; Original v5 remains a transfer-only **Update finished** result. Neither is raw-flash readback. |
| Failure | Short/failed writes, unexpected replies, timeouts or CRC mismatch stop the operation. Payload writes are never retried automatically. |

Preparation accepts only status `1`, the value observed in successful captures.
Other values stop before metadata/payload commands; their device-side meaning
has not been decoded, and preparation may already have changed contents.

If the page loses its install response or progress connection, it keeps the
operation unresolved. Use its status check for the same operation ID; do not
start a replacement just because a browser request failed. Duplicate requests
with the same ID and content return existing status. A different request cannot
reuse that ID. Each subsequent attempt requires fresh file inspection and device
discovery. Closing the helper loses its in-memory operation record; inspect the
actual board before considering any manual new attempt.

HID read polls and phase lengths are bounded. Windows HID writes use the pinned
backend's one-second wait. Linux/macOS HID writes lack a portable hard-cancel
API, so operating-system completion limits still need device qualification.
Never terminate an active helper as a substitute for a cancellation protocol.

## Local helper interface

The helper binds only `127.0.0.1` (default port 3331), accepts one exact Client
origin and generates a new random session token on every launch. All API actions
are POST with Bearer authentication; CORS preflight is limited to that origin.
Host/Origin checks, duplicate-header rejection, request limits and an absolute
read deadline protect the loopback interface. It serves no files or private
environment configuration. Tokens, device UIDs and progress logs are local data.

```sh
thy666-updater serve --port 3331 --origin http://127.0.0.1:3330
```

Match `--origin` to the actual served app origin. `file://` pages and
`Origin: null` are deliberately unsupported. The browser does not auto-discover
a helper, choose a device or start installation. No administrator permissions
are requested by the updater itself; OS HID permissions may require setup on
Linux. Do not replace a working HID driver with a different driver for this tool.

## Backup and recovery

A `.thyb` file contains supported applied settings, calibration, library and the
current song. It is **not** firmware, bootloader or raw external-flash data.
The updater does not provide raw readback, verify a full-device backup, replace
the bootloader, guarantee power-loss recovery or implement automatic rollback.
Restore coverage is limited to the captured configuration described above;
interrupted-update recovery remains unqualified. Keep the official updater and
a known working package.

## Development preview

Choose **Development preview** in the Update heading while disconnected and idle.
The shared browser/desktop layout offers Backup, Update, Transfer, Verify,
Restore, Complete and Failure scenarios. Use the scenario menu to jump directly
or the buttons to advance. Backup, Apply firmware and Restore play synthetic
progress animations before their completed state. Changing scenarios or exiting
cancels the preview animation. A completed backup advances directly to Update.
In the Update preview, choose firmware, detect the synthetic boards, select one
and apply; no hardware discovery or package transfer takes place. All content is synthetic: preview controls do not call
USB, read or save backups, select packages, install firmware or verify hardware.
**Exit preview** returns to the untouched real wizard. Preview is unavailable
while connected, busy or retaining a real update operation.

The three steps share one workspace in browser and desktop. Only browser
connection guidance differs; small screens and expanded advanced details may
still scroll to keep every action accessible.

Completion visuals distinguish the outcomes: Original firmware ends after transfer
with a white logo and no THY666 verification. Successful THY666 checks show
**Firmware verified** with a soft white glow. Only a successful, readback-verified
configuration restoration shows **Settings Restored**, a green logo and HUD
ripples (static when reduced motion is enabled). The development preview includes
these separate outcomes.

The Backup restore toggle carries into the installation. When enabled for a
THY666 update, Restore uses the exact checked backup retained in this session;
there is no second file chooser. It checks the board UID before writing and
requires explicit confirmation after firmware verification. Unchecking the toggle
or skipping backup omits restoration. Reloading the app loses this in-memory
selection; retain the downloaded backup for a later manual recovery.
