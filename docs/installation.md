# Installation, backup and recovery limits

The Rust updater uses the existing held-pad bootloader with supported THY666
`.333` packages and one exact supported Original v5 package profile. Its local
**Update** wizard has four steps: **Detect → Backup → Update → Verification**.
Same-build THY666 installations through native Windows and direct browser WebHID
have passed physical transfer and application verification. The browser wizard
also passed explicit configuration restoration with durable readback and complete
payload comparison. Host and simulation
coverage remains separate; Original v5 installation through the Rust updater
and macOS/Linux operation are unqualified. The official updater is also an
exercised installation route. Building or inspecting a package never installs it.

## Before an update

1. If THY666 is running, save a configuration backup in the wizard's **Backup**
   step or Companion → Backups. Save any separate song export you need.
   Keep the downloaded files outside generated build output.
2. Keep a known working firmware package and the official updater available.
3. Close the official updater before recording USB traffic. Use a fresh capture directory
   if the update is being observed; never overwrite an earlier capture.
4. Release all pads and controls. Do not interrupt an update already in progress.

The configuration backup preserves applied parameters, calibration, library and
current song. It is not a bootloader, firmware or raw-flash image. Restoring the
captured Windows configuration passed readback and cold-boot checks: a fresh
export matched the original backup byte for byte. That sample had an empty
library and song, with default calibration overrides. Nonempty content and
nondefault overrides still require restoration qualification.
The integrated browser restore passed durable saved readback and a complete
payload comparison; its cold-boot check is pending.
**Expect configuration loss:** the Windows same-build update reset configuration
to exact factory defaults, confirmed by before/after backup payloads. Its cause
and the bootloader's erase ranges have not been established.
There is no verified stock backup path. Do not install diagnostic firmware to
obtain a backup. If configuration cannot be read, continue only with the explicit
unavailable/skipped-backup choice.

## Enter update mode

With the official updater closed and the application connection released,
disconnect USB. Hold the **top-left and bottom-left
large LEFT chord pads**, then reconnect USB. The logo-only display identifies
the existing held-pad update mode. This mode uses a different USB interface from
the application's CDC connection.

## Rust updater and the Update tab

See [Updater](updater.md) for building the executable, CLI commands and supported
package restrictions. **Update** is the first header tab, before Companion and
Documentation, and remains available without a running application connection.
The app still opens Companion Board by default. Its disconnected wireframe
includes an **install the firmware on your device** link to Update. The canonical
Update route is `#update`; older `#client/update` links remain supported. Update
is outside the Companion sidebar.

The project site's [browser installer](browser-installation.md) uses direct
WebHID without a helper download. For the optional local-helper route, start
the app server on port **3330**, then double-click the updater or
run it without arguments. It starts `serve --open` and opens the served wizard
with an ephemeral connection token; the page consumes and removes that URL
fragment. That helper route requires the local app server. Manual helper
connection is available in advanced details.

| Step | What to do |
| --- | --- |
| Detect | Explicitly connect a running THY666 application to read its build and UID, or detect the compatible update-mode HID interface. Bootloader descriptors alone cannot identify the installed firmware version. |
| Backup | Save applied THY666 settings, calibration, library and current song. If unavailable, acknowledge that choice explicitly; it creates no recovery copy. |
| Update | Select local Latest THY666, exact supported Original v5 or a validated custom package. Custom accepts supported plaintext THY666 with a bound full build, or that exact approved Original v5 file; arbitrary encrypted vendor files are rejected. Inspect it, close other updaters, enter the update mode and review the target before Apply. Install once and keep USB/helper connected. |
| Verification | For THY666, explicitly reconnect, match full build and target UID, then pass read-only status, I/O, settings and display checks. If configuration restoration was requested, separately confirm it and pass durable readback and complete payload comparison, or explicitly keep current settings. Only then show green **Update successful** and continue to Companion. Original v5 shows neutral **Update finished** after CRC-checked transfer, without application-health verification. |

A running THY666 application can be detected and backed up before entering the
update mode to apply firmware. A board already in the update mode can proceed with the
explicit unavailable-backup choice. The animated held-pad diagram is illustrative;
progress and pass states follow real reports. Reduced motion is supported, and
simulation never verifies a physical installation.

The completed Windows native and browser tests captured application reports matching the package
exactly, matching transferred/external CRC replies and successful activation
requests. The expected application build and identity returned, and read-only
status, I/O, settings and display checks passed. The browser wizard additionally
passed explicit configuration restoration with durable readback and complete
payload comparison. This does not establish
interrupted-update recovery.

The existing protocol prepares bootloader memory before checking hardware and
the optional backup UID. Preparation may change external contents; the generic
bootloader USB serial does not verify the board UID beforehand. The updater
does not automatically reconnect, restore configuration, retry an uncertain
write or roll back an installation.

## Official updater fallback

The official utility remains a device-tested fallback. With its
intended compatible `.333` package selected, install **once**.

Let the operation finish. The application normally appears automatically.
Several working custom installations have ended with the updater message
**“patching failed”**, while others reported success. Do not retry solely because
of that text if the intended application is running: verify its build identity
and current device health first. Close the updater before reconnecting the
companion or opening its console port.

## Package boundaries

THY666 application flash is `0x08040000..0x080c0000` (512 KiB); settings own the
following 256 KiB. The Rust updater accepts the supported aligned plaintext
THY666 profile, checks its complete application boundaries and expected build,
and compares device CRC replies against the transferred bytes. The only accepted
encrypted exception is the exact known Original v5 SHA-256/profile; arbitrary
encrypted images and scale sections are rejected. Packaging/review must keep
metadata and integrity checks tied to the specific built artifact. Use the
existing packaging tools and their review output rather than renaming a raw
binary to `.333`. Host package limits do not prove the bootloader's erase ranges
or preservation of settings and external memory.

The updater's message alone is not proof of the running image. Verify the build
identity, quiet startup, settings state and affected feature behavior. Keep
package/source/manifest identities together.

## Recovery status

Normal completed installation, normal cold-boot persistence and configuration
backup are distinct capabilities. There is no verified USB-only raw readback of
the entire bootloader/external flash, no qualified interrupted-update recovery
and no automatic rollback. Existing packages are not full-device backups.

The new Rust updater does not establish stronger recovery or data preservation.
Do not replace the existing bootloader or claim a fail-safe path without separate
implementation and device verification.
