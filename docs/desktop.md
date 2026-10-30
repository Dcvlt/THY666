# THY666 desktop

The desktop app packages the existing Client, documentation and Update wizard
in a Tauri shell. Rust owns both USB paths: CDC serial for the running THY666
application and HID for the existing update-mode bootloader. The updater is the
same Rust library used by the CLI; the interface does not contain another
bootloader implementation.

Opening the desktop app does not discover or connect to hardware. **Connect
board** opens an explicit native device selector. Only compatible THY666 serial
interfaces are offered, and the existing capability/build/schema checks still
run after connection. Connected large-pad clicks inspect only.

## Windows build and run

Use Python **3.11+**, Rust and the Windows C++/SDK build tools. The initial
development host uses Rust 1.98.1; the declared minimum is not independently
qualified. Windows also requires the Microsoft Edge WebView2 runtime. If the
release import report lists Visual C++ runtime DLLs, Microsoft's matching
redistributable is a runtime prerequisite.

The connected client builds from public source without the private chord archive.
Its scale names and menu schema are public metadata; pad notes, lights and screen
content come from the explicitly connected board. From a fresh clone, run:

```sh
python3 tools/setup_core_tools.py
python3 tools/build_core.py --test --core-only
python3 tools/build_desktop.py --test
```

Use `python` in place of `python3` on Windows if that is the installed command.
Rust/Cargo must be on PATH; after installing rustup on macOS, run
`source "$HOME/.cargo/env"` in the build shell.

The first build downloads the locked Cargo dependencies. Add **`--offline`**
after they are cached. The wrapper uses `.tools/cargo`, workspace temporary
files and `build/desktop-target`, preserves compiler flags and remaps private
paths. It stages only the public web modules/styles, licensed assets and required
generated files; old previews and the separate updater download are excluded.
The build never launches the application or opens USB.

| Output | Purpose |
| --- | --- |
| `build/desktop-target/release/thy666-desktop.exe` | Windows desktop application with embedded interface assets. |
| `build/desktop-target/release/resources/` | Optional reviewed local firmware catalog and packages; keep beside the executable. |
| `build/releases/THY666-desktop-<version>-windows-x64.zip` | Portable local Windows bundle, including runtime notes and license notices. |
| `build/desktop-target/desktop-build.json` | Artifact identity, import/runtime report and packaging result. |

Extract the entire ZIP before opening `thy666-desktop.exe`. The packaged app
needs no Python server, browser helper process, session token or manually
started localhost service. Update and Client stay in the same window.
This portable Windows build is unsigned; installer/signing work remains a
distribution milestone.

Windows exports download to the WebView's normal download location; this flow
does not display a Save As dialog. The native host adds a random filename suffix
and checks that the destination does not already exist, so repeated exports do
not reuse the same board/build filename. A save is reported only after the OS
confirms download completion. Keep device backups private.

The optional local catalog is prepared using the existing updater tools. The
desktop stage verifies each catalog file using the no-USB package inspector.
Without a catalog, Custom file remains available. “Latest THY666” means the
reviewed package supplied with this build; no hosted release feed is configured.
Local vendor packages and generated musical data keep their existing rights;
this build does not grant additional redistribution permission.

## Offline checks and simulation

```sh
python -m unittest discover -s tests -p test_desktop_build.py -v
python tools/build_desktop.py --test-only --offline
python tools/build_desktop.py --check --offline
```

`--test-only` and `--check` stage a clearly labelled minimal frontend fixture
instead of requiring the generated core assets. They never produce a usable
release ZIP. Run a normal full build afterward to restore the actual interface.
The source-check CI matrix follows this route and does not upload pretend
releases made from that fixture.

The executable also accepts:

```sh
thy666-desktop.exe --self-test
thy666-desktop.exe --simulate
```

`--self-test` exercises the in-process fake transports without opening a window.
`--simulate` opens the full interface with synthetic native devices. Both force
simulation and do not open physical USB. Simulation does not establish physical
device qualification. The normal updater workflow still requires explicit target and
installation confirmation; do not use simulation results as permission to flash.

## Native connection and responsiveness

The frontend adapter uses the existing serialized command/parser and backup
validation contracts. Native serial uses the instrument's required DTR state;
reads and writes are bounded, and UI requests are separated from the blocking
device work. Snapshot batches cross the application boundary together rather
than requiring a round trip for every USB response line.

Initial Windows device checks have passed for connection, status/I/O telemetry,
physical-to-client screen changes, client navigation, live pad/light/pressure
feedback and clean DIN releases while connected. Repeated native configuration
backup exports produced distinct files with valid checksums and matching contents;
the earlier file remained unchanged. The app's local 50 ms refresh option was used.
A same-build THY666 installation through the native Windows Update tab has also
passed payload/CRC and application checks. It reset configuration to factory
defaults. Explicit restoration of the captured backup passed saved readback;
after a cold reboot, a fresh export matched the entire original file exactly.
That configuration had an empty library/song and default calibration overrides,
so restoration of nonempty material and tuned overrides still needs qualification.
Original v5 through Rust, other operating systems and interrupted-update recovery
remain separate tests. The precise reset cause and bootloader erase range are
not established; do not assume an application-sized payload preserves settings.

Run live-device tests outside restrictive development sandboxes: a restricted
child process can enumerate the USB interface yet be denied access when opening
its serial port. A normal desktop launch does not require administrator rights.

Visible windows keep the chosen live refresh interval even when a DAW or other
app has keyboard focus. Hidden/minimized windows pause display sampling and
decorative animation; restoring the window resumes them. The native update
worker continues while the window is hidden or minimized.

The desktop window uses the application's own themed frame and scrollbars.
Its solid background and border share one notched outline, with transparency
outside the frame and no gaps through its interior. Windows and macOS both
enable native window/WebView transparency so the existing SVG silhouette clips
the backdrop as well as the border.
On macOS, compact circular Close, Minimize and Maximize/Restore controls sit
on the left, with the THY666 identity on the right. Other platforms retain the
original titlebar order. All controls use the same native actions and update guard.

Drag its title strip to move it, or the marked edge or corner grips to resize it. The window
buttons minimize, maximize/restore and close through native window commands.
The custom Close button follows the same active-update guard as an ordinary
window-close request; changing the frame does not bypass USB ownership rules.

Only one operation owns USB at a time. Client must release its serial session
before the updater opens HID. An update worker belongs to the native backend and
continues across interface navigation. The app prevents ordinary window closure
while an update is active or unresolved. An OS crash or forced termination can
still interrupt a transfer; automatic rollback is not available.

The shell exposes narrow commands to its bundled main window; arbitrary shell,
filesystem and network access are not granted to page scripts. External pages do
not become trusted desktop content. Browser builds retain Web Serial and the
optional loopback helper; desktop builds use the native adapter without that
extra process.

## macOS and Linux

The native library and desktop configuration retain Windows, macOS and Linux
targets. macOS builds need Xcode Command Line Tools and the platform WebView;
Linux builds need the Tauri WebKitGTK 4.1/GTK prerequisites, pkg-config and libudev.
Linux device permissions must permit the selected CDC and HID interfaces without
running the whole app as root. Platform-specific packaging, macOS signing and
notarization, and real USB behavior still need qualification on their respective
systems. A source compile or CI pass does not establish these runtime results.
For local macOS development, open
`build/desktop-target/release/THY666.app` after the full build. This unsigned
development bundle is intended for local testing; signing/notarization and
distribution qualification remain pending. The standalone executable also works
and uses its sibling `resources/` directory when no application bundle exists. The pinned Zig native tests target macOS 13 or later.
The archive is still required for building instrument firmware and the optional
archival workbench, neither of which is bundled into the connected client.
The build wrapper creates a Windows portable ZIP or a local macOS app. The
release packaging step adds an unsigned Windows EXE installer or macOS ARM64
DMG after a full tested build. Linux builds record a checked native binary.
See [Release procedure](releases.md) for CI, checksums and matching source.
Packaging does not qualify device access or provide signing/notarization.

See [Tauri prerequisites](https://v2.tauri.app/start/prerequisites/) for current
platform packages, and [Updater](updater.md) for firmware validation and recovery
boundaries. Full flash backup, interrupted-update recovery and automatic rollback
remain distinct unfinished work.

## Attribution

The packaged `THIRD_PARTY_LICENSES.txt` is generated from the exact desktop
`Cargo.lock`. It retains original crate license/notice texts and checksums.
Some upstream crate archives omit their repository license files; the reviewed,
hash-bound originals are retained under `docs/licenses/desktop/`, with recorded
source revisions in its manifest. Fonts keep their bundled OFL notices. Vendor
firmware, musical material and OS-provided runtimes are separate from the project
GPL-3.0-or-later license.

The startup splash applies the saved light/dark appearance before rendering.
The public app area is named **Client**; existing preference keys and internal
`companion` module paths remain compatible with prior builds.

The bundled component library reuses the desktop window frame, controls and
resize handles. Its examples remain read-only; opening the library does not
discover or connect a board. The browser reference loads no native window code.
