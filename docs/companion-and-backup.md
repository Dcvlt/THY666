# Companion and current-state backup

The [desktop app](desktop.md) packages this interface with native USB serial and
the Rust updater in one window. Its device selection is explicit, and the same
identity, schema, command serialization and backup validation rules apply. The
Web Serial/browser requirements below apply to the browser version; the desktop
app does not need a localhost server or a separately launched helper.

## Three application areas

The header tabs are **Update**, **Companion** and **Documentation**, in that
order. The app opens **Companion → Board** by default. **Update** is a separate
application area, outside the Companion sidebar. **Companion** contains Board,
Parameters, Songs & library, Backups and Diagnostics. **Documentation** contains
searchable guides, related articles and contextual help for controls/settings.
Opening the app or reading help
does not connect to the controller. Without a board, its surface is an inactive
wireframe; mouse, touch and keyboard gestures cannot play it or change a device.

Without a connected board, **Board** provides the red wireframe outline and
**Documentation** and **Update** remain available. **Parameters**, **Songs & library**,
**Backups** and **Diagnostics** are disabled until connection. They do not open
as offline editors, and disconnecting returns any of those pages to Board.

Beneath the disconnected Board wireframe, the prompt reads “Connect a
theoryboard with THY666 firmware or **install the firmware on your device**”.
The final words link to the top-level Update screen. Its canonical route is
`#update`; existing `#client/update` links remain supported. Opening Update or
following that onboarding link does not connect to hardware or start the helper.

Choose **Connect board** to open one explicit Web Serial connection to the
THY666 application. The client checks instrument role, supported services,
build, board UID and menu schema. Older firmware without these services stays
unsupported; use Board without connecting until compatible firmware is installed
through the [installation workflow](installation.md). A menu-schema mismatch leaves live editing disabled.
Temporary journal/staging work reports Busy without becoming a permanent
compatibility failure.

On Board, **OLED** shows the 128 × 64 image copied from the driver's sent panel
buffer, including panel visibility. Pad colors, center lights, held inputs and
inspection values come from the same frozen device snapshot. This is a sampled
USB view of driver state, not an optical refresh-rate measurement. **Web**
presents that same C-rendered frame as scalable graphics, with recognized text
lifted for readability. It does not invent menu labels or substitute another
musical display.

The **Web / OLED** switch sits directly above the center screen. The diagonal
arrow beside it expands that existing screen in place between the pad halves;
press it again to return to the compact layout. Expansion does not open a
dialog or duplicate the screen. These controls appear after connection, along
with Fit, 1:1 and pad labels. The Board heading shows **THY666** offline and
**THY666 / running build** when connected. The larger header logo is centered
between the two interrupted header rails. Application tabs sit on the left and
the connection action on the right, with balanced space above and below the
buttons.

The sidebar arrow switches between full page labels and compact icons. Each
icon retains its accessible name and tooltip; unavailable pages explain that a
board connection is required. Sidebar size is remembered only in this browser
when local storage is available.

While connected, clicking a large pad **selects it for inspection only**; it
does not play or inject a note. The 43 small controls, navigation and supported
Parameters edits use the device's existing actions and validation. Physical
playing remains owned by the controller. The wireframe shown before connection
has no interactive controls or browser audio preview.

Parameters reads values and edit targets from the board. **Apply to device**
sends an explicit change; Cancel sends nothing. A missing value stays Reading
rather than falling back to a preview value. Changing a physical route or pad
selection does not redirect an existing browser draft. Browsing categories in
the browser does not navigate the physical OLED menu.

Songs/library playground editors remain read-only where connected editing is
not implemented; they cannot silently replace device material. Use the board
or supported device actions for edits. Existing SONG/1 transfer remains a
separate bounded song-only protocol; full current-state transfer uses BACKUP/1.

## Firmware updates

The first header tab, **Update**, is a four-step wizard: **Detect → Backup →
Update → Verification**. It remains available without an application connection.
With the local app server running on port **3330**, double-click the Rust updater
or run it without arguments to open the wizard through `serve --open`. The page
consumes and removes its ephemeral token fragment. Manual helper connection is
available in advanced details. This still requires the served local app; it is
not a self-contained public installer. Opening Update sends no install command.

Detect can explicitly connect a running THY666 application and show its build,
then advance to its configuration backup. Alternatively, detect the compatible
held-pad update-mode HID interface. Those descriptors identify compatible
hardware, not the installed firmware version. Hold the top-left and bottom-left
large LEFT chord pads while reconnecting USB to enter **the update mode**.
The animated 96-pad/43-control diagram is an instruction, not live input data;
reduced motion uses a static illustration.

Backup saves applied settings, calibration, library and current song as `.thyb`.
It excludes firmware and raw flash. If the application is unavailable, the wizard
requires an explicit unavailable/skipped-backup choice. Stock backup is not
verified; never install diagnostic firmware to obtain a backup.

The Update step offers local **Latest THY666**, **Original v5** and **Custom** choices.
The first two require a prepared local release catalog; there is no hosted latest
release. Original v5 must match the exact supported SHA-256/profile. Custom accepts
supported plaintext THY666 with a bound full build or the same approved Original
v5 package; arbitrary encrypted vendor files are rejected. Disconnect the application
and enter the update mode before Apply. Memory preparation may change external
contents before hardware and optional backup UID checks. Animation supplies no
invented transfer progress.

Verification requires an explicit THY666 application reconnect, matching full build and
target UID, then passing read-only status, I/O, settings and display checks.
Only all passing checks produce green **Update successful** and a route to
Companion. Original v5 produces neutral **Update finished** after the checked
transfer; it does not have THY666 application-health verification. Simulation
never verifies a physical installation. There is no automatic retry, reconnect,
restore or rollback, and actual playing/persistence remain separate checks.

The native Windows wizard has passed a same-build THY666 installation and its
application checks. Configuration reset to factory defaults during that test;
the retained backup restored the previous settings and survived cold reboot.
The wizard's green firmware result alone does not establish settings preservation.
Original v5 through Rust and macOS/Linux runtime qualification remain pending. See
[Updater](updater.md) and [Installation](installation.md) for the complete workflow.

## Appearance and branding

The **App settings** gear at the bottom right has independent **Appearance**
and **Accent color** selectors. **Dark** uses pure black surfaces; **Light**
uses a muted warm off-white background with darker accents, firmer outlines
and slightly heavier small text for legibility. Choose Red,
Orange, Amber, Yellow, Light green, Green, Teal, Cyan, Blue, Violet, Purple,
Pink or Monochrome. Dark / Red remains the default, and existing browser color
preferences are retained. Both choices apply to Update, Companion and Documentation
and are saved only in this browser when local storage is available. The
board chassis and touch controls follow the selected appearance. Pad hues and
held feedback remain musical; the OLED/Web framebuffer alone stays black with
white display content. These preferences do not change firmware settings, live
refresh or MIDI behavior.

The Accent color menu previews every option with its own swatch, including
the currently selected color. UP/DOWN or HOME/END move through the open list;
ENTER/SPACE applies. ESC closes the list first, then the settings dialog.

The Board cockpit stays quiet offline, with a restrained wireframe in the
selected accent. An explicit board connection reveals the fuller HUD for that session:
the contact matrix, pressure gauges, circle of fifths and clock/engine readings
use the received device snapshot. Missing values remain unavailable; decoration
does not supply simulated device telemetry. The original physical control
layout, OLED/Web choice and connected-pad inspection behavior remain intact.

The startup presentation draws the logo's diamonds in sequence, then its center
numbers and broken-line ripples on black. The app appears once initialization
is ready; reduced motion uses a short static handoff. Failure offers Reload
without exposing the retired prototype page.

Documentation includes synthetic examples rendered by the same C OLED renderer,
board-action diagrams and option tables. These examples are read-only.

## What a .thyb backup contains

**Read backup** explicitly downloads the connected instrument's complete
applied V13 settings payload: musical preferences, routes, ordinary expression
and MPE configuration, both clusters' sensitivity/velocity/release defaults,
per-pad calibration parameters/overrides/guards, custom scales/chords/six sets
and the current song. Applied values can be awaiting the normal idle autosave;
the backup captures current applied configuration rather than only a journal
slot. Unapplied editor/calibration drafts, learned ADC baselines, held notes,
playback positions and runtime owners are excluded.

The file is **THYB version 1**, with a 64-byte header and a maximum of
**13,889 bytes**. Header and payload have independent IEEE CRC32 checks; the
transfer also checks the complete file. The header binds board UID, target,
adapter/catalog compatibility and settings version. Source build and schema
fingerprint are provenance: a compatible V13 backup can be restored to the
same board after a firmware change without requiring the old build identity.
It is not a named on-device profile/song collection.

This file is a configuration backup. It excludes firmware, the bootloader,
raw internal/external flash and unallocated NOR contents. It provides no
interrupted-update rollback. The `.333` updater is a separate application
installation route; no bootloader replacement is included.

## Restore and verification

Choosing a file only inspects it. **Restore backup** requires a separate explicit
confirmation. The client validates checksums and connected-board compatibility
before upload. The device checks the complete file and settings semantics before
changing live state. Restore requires released inputs, stopped musical activity,
quiet expression/output ownership and closed calibration/library drafts.
Busy rejection does not panic or cancel the user's playing.

The status distinction is deliberate:

1. **Applied:** validated settings have replaced live configuration. Catalog,
   per-pad masks and hardware preferences are reapplied; sensors establish new
   quiet baselines. Restore emits no notes or receiver-setup messages.
2. **Queued:** the normal idle journal still owns durable saving. Host staging,
   song transfer, companion snapshots and journal writes have exclusive use of
   one existing buffer; no additional full-settings RAM copy is allocated.
3. **SavedVerified:** journal commit/readback and an independently re-encoded
   current payload match the intended size/CRC. The client then re-exports the
   configuration and compares **every payload byte** before reporting success.
   Header provenance can legitimately differ after an update.

An error or timeout never becomes verified success. An unacknowledged commit
may already have applied: keep the file, reconnect and inspect/read back the
device instead of assuming rollback. An abandoned pre-commit transfer expires;
disconnect releases its staging. Once applied, ordinary journal maintenance
continues even if host verification is interrupted.


## Refresh and qualification

App settings → Live refresh defaults to **100 ms**, with 50, 250, 500 and
1,000 ms alternatives. The app stores this preference locally. Faster polling
reduces sampling delay but does not change the physical OLED refresh or MIDI
timing. Serial requests remain serialized; hidden/minimized windows and transfer work
retain their scheduling limits.

Browser and Windows desktop Companion connection, bidirectional navigation, silent
inspection and live pad/note/pressure sync have been exercised on device. DIN
delivery and clean releases were confirmed while the native Companion was connected.
Configuration backup export has been captured and validated through both transports;
repeated native exports saved separate valid files without changing the earlier file.
An explicitly restored configuration passed saved readback and cold reboot; its
post-reboot export matched the entire original backup byte-for-byte. That test
contained nondefault musical/routing/expression settings but an empty song/library
and default pad profiles. Nonempty songs/libraries and calibration overrides still
need restoration tests. Interrupted-update recovery and automatic rollback remain
unqualified. A configuration round trip is not a raw firmware/bootloader backup.
