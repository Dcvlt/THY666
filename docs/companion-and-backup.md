# Client and current-state backup

The [desktop app](desktop.md) packages this interface with native USB serial and
the Rust updater in one window. Its device selection is explicit, and the same
identity, schema, command serialization and backup validation rules apply. The
Web Serial/browser requirements below apply to the browser version; the desktop
app does not need a localhost server or a separately launched helper.

The browser and desktop client build without the private musical archive. Public
scale names and the shared C schema support parameter editing; musical pad data
and display frames come from the connected board. Archival musical data remains
a separate input for firmware and optional standalone workbench builds.

## Three application areas

The header tabs are **Update**, **Client** and **Documentation**, in that
order. The app opens **Client → Board** by default. **Update** is a separate
application area, outside the Client sidebar. **Client** contains Board,
Parameters, Harmony, Songs & library, Backups and Diagnostics. **Documentation** contains
searchable guides, related articles and contextual help for controls/settings.
Opening the app or reading help
does not connect to the controller. Without a board, its surface is an inactive
wireframe; mouse, touch and keyboard gestures cannot play it or change a device.

Without a connected board, **Board** provides the red wireframe outline and
**Documentation** and **Update** remain available. **Parameters**, **Harmony**,
**Songs & library**, **Backups** and **Diagnostics** are disabled until connection. They do not open
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
dialog or duplicate the screen. These controls and Labels appear after
connection; the board always fits the available width. The Board heading shows **THY666** offline and
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

**Labels** reads the current pad assignments from supported firmware, including
notes, chords, control pads and sequencer steps. The read runs only while labels
are visible and refreshes when assignments change. Labels describe the next
press; notes already held keep their original pitches and release routes. Older
firmware without the label service explains that an update is needed.

Parameters reads values and edit targets from the board. **Apply to device**
sends an explicit change; Cancel sends nothing. A missing value stays Reading
rather than falling back to a preview value. Changing a physical route or pad
selection does not redirect an existing browser draft. Browsing categories in
the browser does not navigate the physical OLED menu. Performance uses a
physical **Settings for: Both / Left / Right** scope; MIDI routes and Sensitivity
keep separate targets. Mixed values remain explicit until you apply one value
to the selected scope. MPE enable and context-pad controls remain global. See
[Expression](mpe-expression.md) for pressure-message choices and screen indicators.

Harmony projects the board's cached analysis and physical pad hints. Its virtual
shortcuts require the physical Harmony workspace; opening device Settings or a
quick page disables them. M-key previews require built-in scale assignments:
Cycle layout and saved custom-scale assignments are disabled with an explanation.
See [Harmony](harmony.md) for the playing workflow and current limits.

**Reset options** opens the device's confirmation without resetting anything.
The Client shows that same C-rendered prompt with separate **Cancel** and
**Reset options** buttons. Review the captured scope before confirming. A
physical selection change is not Client consent, and a canceled or replaced
request cannot be confirmed by an older click. Other Client controls pause while
the request is pending; physical navigation remains available on the board.

Songs/library playground editors remain read-only where connected editing is
not implemented; they cannot silently replace device material. Use the board
or supported device actions for edits. Existing SONG/1 transfer remains a
separate bounded song-only protocol; full current-state transfer uses BACKUP/1.

## Firmware updates

The first header tab, **Update**, is a three-step wizard: **Backup →
Update → Verification**. It remains available without an application connection.
With the local app server running on port **3330**, double-click the Rust updater
or run it without arguments to open the wizard through `serve --open`. The page
consumes and removes its ephemeral token fragment. Manual helper connection is
available in advanced details. This still requires the served local app; it is
not a self-contained public installer. Opening Update sends no install command.

Backup can explicitly connect a running THY666 application before saving its
configuration. Update then detects the compatible held-pad update-mode HID
interface. Those descriptors identify compatible
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
Passing firmware checks produce white **Firmware verified**. If restoration
was requested, the retained backup is selected automatically; confirm restoration
to write it. Durable readback must pass before green **Settings Restored**.
The Client route becomes available when the requested restoration is resolved. Original v5 produces neutral **Update finished** after the checked
transfer; it does not have THY666 application-health verification. Simulation
never verifies a physical installation. There is no automatic retry, reconnect,
restore or rollback, and actual playing/persistence remain separate checks.

The native Windows wizard has passed a same-build THY666 installation and its
application checks. Configuration reset to factory defaults during that test;
the retained backup restored the previous settings and survived cold reboot.
The white firmware result alone does not establish settings preservation.
Original v5 through Rust and macOS/Linux runtime qualification remain pending. See
[Updater](updater.md) and [Installation](installation.md) for the complete workflow.

## Appearance and branding

The **App settings** gear at the bottom right has independent **Appearance**
and **Accent color** selectors. **Dark** uses pure black surfaces; **Light**
uses a muted warm off-white background with darker accents, firmer outlines
and slightly heavier small text for legibility. Choose Red,
Orange, Amber, Yellow, Light green, Green, Teal, Cyan, Blue, Violet, Purple,
Pink or Monochrome. Dark / Red remains the default, and existing browser color
preferences are retained. Both choices apply to Update, Client and Documentation
and are saved only in this browser when local storage is available. The
board chassis and touch controls follow the selected appearance. Pad hues and
held feedback remain musical; OLED alone stays black with
white display content. These preferences do not change firmware settings, live
refresh or MIDI behavior.

The Accent color menu previews every option with its own swatch, including
the currently selected color. UP/DOWN or HOME/END move through the open list;
ENTER/SPACE applies. ESC closes the list first, then the settings dialog.

The Board cockpit stays quiet offline. Its first complete device snapshot starts
a short assembly: circuit traces, the playing surface, then the live panels.
Subtle frame motion remains while connected; readings and controls stay still.
Motion pauses offscreen and respects the system's reduced-motion preference.
The sequence never delays incoming note feedback. The original physical control
layout, OLED/Web choice and connected-pad inspection behavior remain intact.

Panels adapt to what you are playing:

| Panel | What to look for |
| --- | --- |
| Expression | Left/right pressure, captured strike velocity and the selected pressure destination. Aftertouch-stage marks explain the split. |
| Arps | A dim pitch pool with sounding notes brightened. This is the combined pool, rather than an individual Arp step display. |
| Harmony | Current chord → selected suggestion, the board's explanation, shared/changed tones and recent chord history. |
| Sequencer | Selected track, stopped/playing state and distinct armed/recording feedback. |

Missing values stay unavailable. The Client does not invent MPE member activity,
Arp phase or track playheads from its own clock. Detailed per-voice and playback
position telemetry remains future work.

The startup presentation draws the logo's diamonds in sequence, then its center
numbers and broken-line ripples on black. The app appears once initialization
is ready; reduced motion uses a short static handoff. Failure offers Reload
without exposing the retired prototype page.

Documentation includes synthetic examples rendered by the same C OLED renderer,
board-action diagrams and option tables. These examples are read-only.

## What a .thyb backup contains

**Read backup** explicitly downloads the connected instrument's complete
applied settings payload (development V19): musical preferences, routes, ordinary expression
and MPE configuration, physical-half playing policies and pressure destinations,
both clusters' sensitivity/velocity/release defaults,
per-pad calibration parameters/overrides/guards, custom scales/chords/six sets
and the current song. Applied values can be awaiting the normal idle autosave;
the backup captures current applied configuration rather than only a journal
slot. Unapplied editor/calibration drafts, learned ADC baselines, held notes,
playback positions and runtime owners are excluded.

The file is **THYB version 1**, with a 64-byte header and a maximum of
**13,918 bytes**. Header and payload have independent IEEE CRC32 checks; the
transfer also checks the complete file. The header binds board UID, target,
adapter/catalog compatibility and settings version. Source build and schema
fingerprint are provenance: a compatible backup can be restored to the
same board after a firmware change without requiring the old build identity.
It is not a named on-device profile/song collection.

This file is a configuration backup. It excludes firmware, the bootloader,
raw internal/external flash and unallocated NOR contents. It provides no
interrupted-update rollback. The `.333` updater is a separate application
installation route; no bootloader replacement is included.

## Restore and verification

Choosing a file only inspects it. **Restore backup** requires a separate explicit
confirmation. The client validates checksums and connected-board compatibility
before upload. For a compatible older backup being restored to the current format, a separate shared
C/WASM instance validates, applies and re-encodes the old payload before any
device write. The original file is retained unchanged. The firmware accepts its
current format only; future formats and unsupported downgrade requests are
rejected. The device checks the complete file and settings semantics before
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
   For a migrated backup, this comparison uses the independently computed current
   payload, rather than expecting the older wire representation to stay identical.
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

Browser and Windows desktop Client connection, bidirectional navigation, silent
inspection and live pad/note/pressure sync have been exercised on device. DIN
delivery and clean releases were confirmed while the native Client was connected.
Configuration backup export has been captured and validated through both transports;
repeated native exports saved separate valid files without changing the earlier file.
An explicitly restored configuration passed saved readback and cold reboot; its
post-reboot export matched the entire original backup byte-for-byte. That test
contained nondefault musical/routing/expression settings but an empty song/library
and default pad profiles. Nonempty songs/libraries and calibration overrides still
need restoration tests. Interrupted-update recovery and automatic rollback remain
unqualified. A configuration round trip is not a raw firmware/bootloader backup.

The Windows V13-to-V15 update session also passed explicit restoration and a
confirmed full power-off check. Its V15 export matched every byte of the
independently migrated C payload and the complete pre-power-off V15 file. The
original V13 file was retained unchanged. This qualifies that captured
configuration; V14 migration and broader saved-material coverage remain separate.

The project site exposes the same browser Client through **Install / Client / User guide**.
It opens disconnected and needs no private musical archive. Use desktop Chrome or Edge
over HTTPS or localhost for explicit Web Serial connection. After the standalone
installer finishes, **Go to Client** releases its port and opens the Client; use
**Connect board** there to establish a new connection.

Harmony wire version 4 uses 800 bytes: the compatible prefix plus the full
48-pad pitch-class map, recent history, exploration settings and selected reason.
Older wire versions cannot drive these revised Harmony controls. Settings V18
appended four bytes after the frozen V17 prefix. V19 adds ten physical-half
playing-policy/MPE-mapping bytes after that unchanged prefix. Migration retains
musical data and physical calibration; V18 imports retain their routed behavior
and initialize MPE output to Pressure. V19 migration and restoration are
host-tested and await hardware qualification.

## Compact navigation and guided tasks

On narrow screens, use the header menu for application areas and Client pages.
The guide's **Find a topic** button opens its searchable index. Choosing an
article closes the index; **On this page** jumps between its sections.

Backups starts with **Back up** and **Restore**. Choose one to reveal its steps.
Selecting a flow does not read or change the board. Restore still validates the
file and requires explicit confirmation before applying it.

Diagnostics offers **Start live session** alongside a single **Read report**.
Live sessions request the selected report at most once per second through the
existing serialized connection. They pause while hidden and stop when leaving
Diagnostics, disconnecting, or transferring USB ownership to the updater. The chart retains up to 60 values from the selected report; raw text retains
only the latest reply. It resets for a new session, report type or disconnect.
Charts name the measurement, units, scale and last sample time. This is report
monitoring, not a recording of raw sensor or MIDI traffic.

**MIDI & scanning** charts DIN bytes sent per second between reports. Its totals
count bytes transmitted and messages accepted; they do not confirm that a synth
received them. **Backlog** is messages still waiting to be sent. Zero backlog is
normal while playing. The rate needs two reports and restarts after reconnect or
counter reset. USB accepted messages are not a USB delivery acknowledgement.

With the resource-enabled firmware, **Activity** also updates CPU activity, loop
timing, static RAM, CCM, observed stack peak and application-flash gauges. These
same measurements appear under **System → Resources**. The loop continuously
polls, so CPU activity near 100% is expected; it is not an overload meter. Mean
and peak loop times cover the last sampling window (normally one second).
RAM figures describe fixed allocations and exclude the reserved stack from free
space. The stack watermark is the deepest observed usage since boot, not a
worst-case guarantee. Flash is the application partition, not song storage.
**System → Storage** retains saved-state and song usage. Older firmware still
supports MIDI activity; resource gauges stay unavailable until updated.

Focused board checks passed the resource display and reported values. Worst-case
timing overhead and stack use still require dedicated load measurements.

Board adds contextual panels for MPE, Arps, Sequencer and Harmony when active.
Harmony groups the retained chord and physical map, suggestions, exploration
settings and playing/voicing settings. Capture controls, pad feedback and physical
shortcuts are expandable. Displayed results always come from device readback.

The guide keeps its first workflow sections open and places deeper reference
sections in expandable panels. On this page links open and focus the requested
section. Option tables become labeled rows on narrow screens.
