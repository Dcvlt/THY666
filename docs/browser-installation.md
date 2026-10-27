# Browser installation

Direct installation from the project website is implemented alongside the
[native updater](updater.md). A same-build Windows browser installation passed
physical WebHID transfer, application identity/health checks and integrated
configuration restoration with durable readback and complete payload comparison.
Synthetic transport and actual-WASM tests cover additional behavior separately.
The desktop app remains valuable for its integrated Companion, native device
access and offline workflow. Publishing a website does not qualify a new USB
transport or authorize a device write.

## Current capability and qualification

| Capability | Existing THY666 implementation | Browser route | Device qualification |
| --- | --- | --- | --- |
| Firmware validation | Shared Rust validation of supported `.333` packages, application bounds, full build identity and integrity | Same Rust parser and pull-driven protocol session compiled to WASM; no second JS parser or protocol | Same-build THY666 installations passed through Windows native and direct browser routes |
| Update-mode transport | Native Rust HID, called by desktop, CLI or explicit local helper | Implemented WebHID adapter with an explicit chooser, strict descriptors, bounded I/O and an exclusive origin Web Lock | Windows browser transfer passed: captured application payload matched the selected package and both loader CRC replies |
| Application connection | Native CDC in desktop; Web Serial in the browser Companion | Reuse the explicit browser connection and capability/identity checks | Windows browser update-to-application handoff passed full build/board matching and basic telemetry checks |
| Configuration export | `.thyb` applied-state export with checksums and board/format binding | Download and retain a validated copy before entering update mode | The captured Windows configuration exported successfully |
| Configuration restore | Explicit staged restore, durable readback and host payload comparison | Shared wizard retains the backup, records the preservation choice and requires a separate restore review after firmware verification | Integrated Windows browser restoration passed durable saved readback and complete payload comparison; its cold-boot check is pending. Earlier native restoration survived cold reboot. Nonempty material and calibrated overrides remain unqualified |
| Recovery | Existing held-pad loader and known working packages | Retain native/official fallback; show uncertain outcomes without automatic retry | Interrupted-update recovery, automatic rollback, Original v5 through Rust and macOS/Linux USB remain unqualified |

The successful native same-build update reset configuration to exact factory
defaults. A healthy application and matching firmware CRCs therefore do not
establish preservation. A separately authorized restore recovered the captured
configuration, and its post-cold-boot export matched the original file. That
sample had an empty song/library and default pad overrides; it is not evidence
that every possible saved object has completed the same round trip.

The browser test independently confirmed the selected application bytes and
both loader CRC replies, then verified the running build and board before
restoring the retained backup. Its durable saved readback and re-exported
payload matched. These results do not establish raw-flash readback or recovery.

## Preserve configuration as part of the workflow

The implemented sequence is:

1. Connect the running THY666 application explicitly. Export configuration,
   validate it and save a user-accessible backup before releasing CDC access.
2. Enter held-pad update mode, select the physical target and review the exact
   package before installation. Never substitute a generic USB descriptor for
   a verified board identity.
3. Transfer once and check the loader's CRC replies. A disconnect during late
   activation remains uncertain until the running application is identified;
   it must not trigger an automatic installation retry.
4. Reconnect explicitly, match the expected build and board UID, and check
   application health. Report firmware verification separately from settings
   preservation.
5. For a compatible retained backup and an explicitly chosen restore, use the
   existing semantic validation, musical-idle gate and durable-save/readback
   protocol. Re-export and compare the complete configuration payload before
   reporting preservation. If restoration fails, retain the backup and explain
   which stage failed; do not label the whole operation fully successful.

If the original application cannot export THY666 configuration, the page must
offer the existing explicit unavailable/skipped-backup choice. It must not
describe a firmware package or settings export as a full-device recovery image.
See [backup contents and verification](companion-and-backup.md#what-a-thyb-backup-contains)
and [installation limits](installation.md).

## Browser transport and compatibility

WebHID exposes HID reports after the user selects a device. WebAssembly alone
does not grant USB access. `updater/src/session.rs` now owns protocol transitions
for both native and browser paths; `updater/src/browser.rs` exposes a bounded raw
ABI. `web/browser-hid.js` supplies asynchronous I/O and `browser-updater-client.js`
adapts that session to the existing wizard. Loading the hash-checked core and
catalog opens no USB device. There is no browser-only transfer policy.

| Environment | Behavior and limitations |
| --- | --- |
| Desktop Chrome / Edge | Detect WebHID support, request access from an explicit user action and use a secure HTTPS origin. Browser support does not establish THY666 device qualification. |
| Linux | Device access may require a narrowly scoped `hidraw` udev rule. Browser packaging or sandbox policy can still deny access; document and test actual distributions/packages. |
| Safari / Firefox or unavailable API | Explain that this installation route is unavailable and offer the desktop/native route. Do not silently fall back to a different USB interface. |
| Application reconnect | Browser Companion additionally needs Web Serial and its own explicit permission. Release application/update handles in sequence; do not compete with other clients. |

Use feature detection rather than only a browser-name test. Permission denial,
unplugging, tab suspension, a busy interface and an unavailable application must
have bounded, truthful outcomes. Keep exclusive USB ownership, progress based
on protocol results and the existing no-automatic-retry rule.

Primary API references: [Chrome WebHID guide](https://developer.chrome.com/docs/capabilities/hid),
[WebHID availability and secure contexts](https://developer.mozilla.org/en-US/docs/Web/API/HID)
and [device chooser requirements](https://developer.mozilla.org/en-US/docs/Web/API/HID/requestDevice).

## Build and preview

Install the official Rust target, then run from the repository root:

```sh
rustup target add wasm32-unknown-unknown
python tools/build_browser_updater.py
npm run build:site
python -m http.server 3332 --bind 127.0.0.1 --directory build/site
```

The builder uses workspace Cargo caches and source path remapping. Add
`--offline` after dependencies are cached. Output is an import-free portable
core plus SHA256/source manifest under `web/generated/` and an ignored copy
under `build/browser-updater/`. The site packager checks both artifact and source
identity before staging. It includes neither native USB handles nor firmware.

Open `/install.html`. The existing local browser Companion also selects direct
WebHID on a supported secure browser; explicit desktop and helper launches keep
their original backends. The native transfer sequence, including preparation,
has not been replaced by the alternative upstream sequence.

The public catalog is currently empty. Custom supported `.333` files work after
Rust validation. Future published entries must be same-origin sibling files,
bounded and SHA256/build-bound. No upstream or vendor image is downloaded or
redistributed automatically. A matching hash is integrity, not a signature.

Before installation, the wizard still requires target selection, backup or
explicit skip, package review and final confirmation. After verified firmware,
requested preservation remains pending until the separately confirmed restore
passes durable readback, or the user explicitly chooses current settings.
Closing or reloading the browser during an update can interrupt it; the page
warns on unload but cannot guarantee that the operating system keeps it alive.

## Remaining qualification

1. Complete the browser restoration cold-boot check and extend restoration
   coverage to nonempty songs/libraries and calibrated pad overrides. The new
   integrated continuation also needs separate native-route qualification.
2. Qualify explicit browser installation and application/backup handoff on
   macOS and Linux. Test failure reporting separately from any
   future interrupted-update recovery claim.
3. Publish firmware choices only with reviewed, redistributable
   release artifacts and clear capability/qualification labels. Keep desktop
   downloads available. A same-origin checksum checks artifact integrity; it
   must not be presented as publisher authentication or secure rollback.
