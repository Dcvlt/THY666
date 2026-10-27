<p align="center">
  <img src="docs/assets/thy666-banner.png" alt="THY666 — open source firmware for TheoryBoard THY333" width="1200">
</p>

# THY666

Independent open-source firmware for the **Irijule TheoryBoard THY333**, with a
shared C musical engine, STM32 hardware drivers and a Client for browser and desktop.
The aim is a responsive, expressive instrument with clear controls and code
that the community can understand and maintain.

**Status: hardware alpha.** Core playing, MIDI output, lighting, menus, settings,
Arps, sequencing and Client synchronization have been exercised on a device.
Host tests cover additional behavior; implementation and device qualification
are tracked separately.

## AI disclosure

**THY666 is a human–AI collaboration.** Much of the firmware implementation,
protocol analysis, test infrastructure, Client and documentation has been
developed with **OpenAI Codex** working directly in the repository. The human
maintainer defines the musical goals and controls, makes product decisions,
tests on the physical board and supplies the feedback that drives revisions.
We use both regression tests and repeated hardware sessions to assess the work.

We recommend using a coding agent to help debug your own board. Start it with
[AGENTS.md](AGENTS.md), the current build identity, relevant settings, MIDI route
and a short reproduction. It can follow the shared engine, inspect local
telemetry/captures, check configuration and add a focused regression. Keep
private bindings in `.env` and redact identifiers before posting logs publicly.
Device tests and installation should follow the documented workflow; an agent's
explanation or a passing host test is separate from a verified hardware result.

## What works now

| Area | Available functionality |
| --- | --- |
| Chords and scales | Lossless musical library, chord/layout choices, inversions and voicing, strum, Live Update, musical names and custom scales/chords/sets. |
| Playing surfaces | Chord pads plus Chromatic, Scale degrees, Guitar, Piano and other right-pad layouts; configurable context pads and physical shortcuts. |
| Expression | Per-half Fixed/Strike velocity, cluster and per-pad sensitivity, linked trigger/release, calibration tools, Poly/Channel Aftertouch, pressure bend and optional MPE/glide. |
| Rhythm | Two Hold Arps; sequencer step entry and recording, Replace/Overdub, Undo/Redo, track settings and independent MIDI routes. Capacity: 21 tracks, 48 editable steps per track and a shared 512-event pool. |
| MIDI | DIN OUT and USB MIDI, Whole Board / Split Sides / Six Zones routing, channels and destinations with explicit note ownership and clean releases. |
| Display and lights | Shared 128 × 64 C renderer, configurable play views, chord/note names, note colors, held feedback, center-control lighting and grouped menus. |
| Storage | Versioned settings, calibration overrides, musical library and current song with idle autosave; storage usage and current-state backup/restore tools. |
| Client | Explicit device connection, live Web/OLED screens, pad inspection, navigation and parameters, telemetry, backups, light/dark color themes and illustrated searchable documentation. |
| Desktop | Tauri shell with native USB serial for Client and the integrated Rust HID updater. Windows connection, screen/control synchronization, live pad feedback, DIN coexistence, backup export and restoration of the tested configuration have passed device checks. |
| Updater | Rust executable and three-step wizard: Backup, Update and Verification. Same-build THY666 installations passed through native Windows and direct browser WebHID, including application verification. The browser wizard also passed explicit configuration restoration with durable readback and complete payload comparison. The earlier native restoration survived cold reboot. Original v5 through Rust and macOS/Linux operation remain unqualified. |

The app opens **Client → Board** by default. Its header tabs are **Update**,
**Client** and **Documentation**, in that order. Board shows an **inactive
wireframe until a board connects**, with an **install the firmware on your device**
link to Update beneath it. Connected large-pad clicks inspect; physical playing
stays on the instrument. The Web and OLED views follow the same device content.
Documentation includes examples from the actual C renderer, board-action diagrams
and option tables.

The project also includes a **GitHub Pages site** with an overview, installation, browser Client
and the same searchable illustrated guide. Its tabs are Install, Client and User guide.
Build the public core and browser updater first, then run `npm run build:site`; it needs no board, firmware or private musical archive.
The site is live at **[dcvlt.github.io/THY666](https://dcvlt.github.io/THY666/)**.
The source repository is public; firmware and desktop release downloads have not
yet been published. See [Project site](docs/project-site.md). A direct
WebHID installer has [passed a Windows device test](docs/browser-installation.md),
using the same Rust package checks and protocol as desktop. It supports explicit
configuration backup, application verification and separately confirmed restoration.
The tested browser flow passed transfer, application checks and integrated
restoration of the captured configuration. Broader material and platform coverage
remain unqualified; the desktop app retains its native updater and live Client.

## Current limits

- **MPE currently serves live pads:** it cannot be combined with Arps or the
  sequencer workspace/recording. Disable MPE to use those features.
- Arps currently use **Hold**. Latch, release tails, Keep Key, Arp-to-Strum and
  independent repeating/gated/routed strummers remain unfinished.
- Named song/profile collections, advanced sequencing, custom MIDI assignments,
  pedals, MIDI clock/transport output and remaining USB input controls are
  still in development. DIN input is outside the current scope.
- Drums, Lightboard and User are planned engines; selecting their entries opens
  descriptions. Custom musical material already works in Play.
- The corrected full-cluster calibration workflow and restoration of nonempty
  songs, custom libraries and calibration overrides need further device qualification.
  The captured configuration passed restoration, saved readback and cold reboot.
- Completed `.333` installations have been exercised through the official updater,
  native Windows Rust wizard and direct Windows browser WebHID. The native test reset settings to factory
  defaults; a retained backup restored the previous configuration exactly. The
  bootloader's erase range and the reset's precise cause remain unproved. There is
  no automatic restore or rollback, and configuration backups exclude raw flash.

## Next release work

- Extend **update-aware configuration handling**: the shared wizard now
  distinguishes healthy firmware from preserved settings and offers explicit,
  verified backup restoration, exercised through the Windows browser flow.
  Extend qualification to nonempty songs/libraries, calibration overrides and the
  Original v5 route. Interrupted-update recovery remains separate.
- Validate the complete development and update workflow on **Windows, macOS
  and Linux**. Windows is the primary tested environment today. Compiler
  downloads are pinned for several platforms, but this does not establish
  end-to-end USB, packaging or device-tool compatibility on each one.

## Build and run

Use **Python 3.10+** and **Node.js 20+**. Commands run from the repository root.
No npm dependency installation is required. npm scripts use `python3` on macOS/Linux
and `python` on Windows.

### Portable core

```sh
npm run setup
npm run build:core
```

This installs a pinned local Zig toolchain, builds C/WebAssembly and runs native
regressions. It requires no device, proprietary firmware or musical archive.

### Desktop app

The desktop app bundles Client, Update and Documentation in one window. It
uses native USB access and needs no separately started Python server or updater
helper. Opening it does not connect to or update a board automatically.

Build from a fresh clone with **Python 3.11+**,
Rust and the platform C++/WebView prerequisites:

```sh
python3 tools/setup_core_tools.py
python3 tools/build_core.py --test --core-only
python3 tools/build_desktop.py --test
```

On macOS, open `build/desktop-target/release/THY666.app` after building.
Its macOS titlebar uses compact circular controls on the left and the project
identity on the right. This is an unsigned local development app; USB testing and distribution
qualification remain pending.

The Windows executable is under `build/desktop-target/release/`; the portable
ZIP is under `build/releases/`. Keep its optional `resources/` folder and license
notices together. Windows requires WebView2 and any C runtime imports recorded
by the build. The initial package is unsigned. See [Desktop](docs/desktop.md)
for build, simulation, runtime requirements and portability status.

### Browser Client and shared data

The connected Client needs no private chord archive: it uses public menu metadata
and reads musical content from the board. Build it with `npm run build:core`, then
`npm run dev`. The optional archival workbench and STM32 build need an
**authorized local lossless chord archive**, supplied separately. The musical corpus and vendor firmware are not
redistributed with this source. Import a raw musical dump, or configure an
existing lossless archive in `.env`:

```sh
python tools/chord_dump.py import /path/to/THYMEMBK.bin
npm run build
npm run dev
```

Open **http://127.0.0.1:3330/**, or the port printed by the server. Use a browser
that provides Web Serial for connection. Reading documentation never opens a
port. **Connect board** uses an explicit port chooser and verifies the running
firmware and supported services. Other device pages stay disabled until then.
The first header tab, **Update**, remains available without that application
connection. Supported secure browsers use direct WebHID with the compiled Rust
policy; an explicit local-helper launch keeps its optional helper route.
The desktop app calls the same updater library in process. Its canonical route
is `#update`; older `#client/update` links still open it. Opening the app without
a route continues to show Client Board.

Building the STM32 target is a separate offline command; it does not flash:

```sh
python tools/build_instrument.py --output-dir build/instrument-local-01 --test
```

Use a fresh output directory. See [Development](docs/development.md) for build
details and [Installation](docs/installation.md) for package/update procedures.

### Build the updater

The updater additionally needs **Rust 1.89+** and the platform C build tools:

```sh
python tools/build_updater.py --test
```

This builds and tests the standalone executable without opening USB. With the
local app server running on port **3330**, double-click the executable or run it
without arguments to start the helper and open Update. The temporary connection
token is consumed from the URL fragment and removed. Manual connection remains
available in advanced details; this is not yet a self-contained public installer.

The wizard guides **Backup → Update → Verification**. Its shared web/desktop
layout includes a disconnected **Development preview** for synthetic flow and
layout review without hardware. A connected THY666
application can supply its build and configuration backup; the update-mode HID
interface alone identifies compatible hardware, not the installed firmware.
Backup covers settings, calibration, library and current song, not raw flash.
THY666 requires explicit application reconnection and passing read-only checks
before **Firmware verified** (white logo with a soft glow). A successful,
readback-verified settings restoration shows **Settings Restored** with a green
logo and HUD ripples. Original v5 reports **Update finished** after the
checked transfer without claiming application health.

Prepare the optional local firmware choices from authorized packages:

```sh
python tools/prepare_updater_releases.py --thy666 /path/to/THY666-instrument-alpha-12345678.333 --stock /path/to/Original-v5.333
```

**Latest THY666** means the locally prepared package; no hosted latest release is
available yet. Original v5 accepts only the exact supported SHA-256 profile.
Custom accepts supported plaintext THY666 with a bound full build or that exact
approved Original v5 package; arbitrary encrypted vendor files are rejected.
Prepared packages and the local catalog stay beside the executable in ignored
build output. To package the verified executable, notices, quick start and any
prepared catalog for local use:

```sh
python tools/package_updater.py
```

On Windows x64 this writes
`build/releases/THY666-updater-<version>-windows-x64.zip` and an identical local
download at `web/generated/updater/THY666-updater-windows-x64.zip`. Extract the
whole ZIP and keep its files together. The local app server on port **3330** is
still required; packaging does not qualify physical USB operation or grant
redistribution rights to locally supplied vendor firmware.
See [Updater](docs/updater.md) for setup, simulation and qualification limits.

## Local configuration

Copy [`.env.example`](.env.example) to **`.env`** for first-time setup, then fill
only the values needed for your workflow. Preserve any existing `.env`; it is
private and ignored by Git. No API key is required by the core or Client.

Priority is **explicit CLI argument → nonempty process environment → `.env` →
tool default**. Relative paths start at the repository root. Quoted values are
literal, including Windows backslashes; no shell execution or variable expansion
is performed.

| Key | Purpose / when required |
| --- | --- |
| `THY666_CHORD_ARCHIVE` | Lossless musical JSON; defaults to `.tools/inputs/chords.lossless.json`. Needed only for the archival workbench and instrument build. |
| `THY666_CHORD_DUMP` | Optional raw musical input for `chord_dump.py import` when no input argument is supplied. |
| `THY666_ZIG_EXECUTABLE` | Optional existing Zig executable. Set together with `THY666_ZIG_SHA256`; otherwise setup's local toolchain manifest is used. |
| `THY666_ZIG_SHA256` | SHA-256 of that executable, verified before compiler use. |
| `THY666_WORKBENCH_PORT` | Local server port, `3330` by default. `--port` overrides it. |
| `THY666_SERIAL_PORT` | Windows CDC port for device console/support tools. |
| `THY666_DEVICE_SERIAL` | Expected 24-hex-digit device serial/MCU UID for identity-bound captures. |
| `THY666_USB_HUB_PARENT` | Expected Windows USB parent instance, used to verify the physical connection. |
| `THY666_EXPECTED_BUILD` | Expected full firmware build ID for tools that verify the running image. Update this binding when changing firmware. |
| `THY666_MIDI_INPUT_NAME` | Windows MIDI input name to record. |
| `THY666_MIDI_INTERFACE_PATH` | Matching Windows MIDI/PnP interface identity for that input. |
| `THY666_USBPCAP_PATH` | Optional USBPcapCMD executable path; otherwise the standard installation is used. |
| `THY666_USBPCAP_DEVICE` | USBPcap root selected for recording. |
| `THY666_USBPCAP_ADDRESSES` | Comma-separated USB device addresses for a filtered capture; unnecessary with `-CaptureAll`. |

Device tools retain their identity checks. Recheck port/hub/capture bindings
after USB topology changes, and update the expected build after an installation.
Private configuration is never served to the browser or compiled into firmware.
Captures, backups and debug manifests can contain runtime identifiers and remain
local ignored outputs.

C/WASM builds remap local paths. STM32 ELF debug paths are anonymized while
inspection symbols are retained; `--keep-debug` is for local work and can retain
machine-specific paths. Run the publication privacy check before sharing:

```sh
python -m unittest discover -s tests -p test_public_privacy.py -v
```

## Documentation

The Client's **Documentation** tab is the illustrated user guide. Maintained
source guides cover the implementation and workflows:

| Topic | Guide |
| --- | --- |
| Build and contribute | [Development](docs/development.md), [Contributing](CONTRIBUTING.md), [Agent guide](AGENTS.md) |
| Architecture | [Engine/state](docs/engine-state-model.md), [Menus](docs/menu-structure.md), [Hardware](docs/hardware.md) |
| Playing | [Chords](docs/chord-playing.md), [Scales/layouts](docs/scale-library-and-engines.md), [Physical controls](docs/physical-control-map.md) |
| Expression | [Strike velocity](docs/strike-velocity.md), [Sensitivity/calibration](docs/sensitivity-calibration.md), [MPE/Aftertouch](docs/mpe-expression.md) |
| Rhythm | [Arps](docs/arp-engine.md), [Clock/ownership](docs/rhythm-foundation.md), [Sequencer](docs/sequencer-interface.md) |
| Display | [Views](docs/modes-and-views.md), [Display scheduling](docs/display.md) |
| Client and storage | [Client/backups](docs/companion-and-backup.md), [Settings](docs/settings-persistence.md) |
| Device builds and updates | [Installation](docs/installation.md), [Updater](docs/updater.md), [Resource budget](docs/firmware-budget.md) |
| Branding | [Banner, icon and source assets](docs/assets/README.md) |

## Contributing and maintenance

Read [CONTRIBUTING.md](CONTRIBUTING.md). When a feature, workflow, setup command
or qualification status changes, update this README and the relevant guide/help
in the same change. Refresh the generated documentation examples when screens
change. Keep implemented, host-tested, device-tested and planned work distinct.

`core/` contains the portable engine/state/renderer, `firmware/` the STM32
adapters, `updater/` the shared Rust updater policy and native host, `web/` the Client and guide,
`tools/` the build/import/debugging utilities, and `tests/` the regressions.
Generated output, vendor packages,
private data and working/research archives are excluded from Git.

## License

Original project code is licensed under GPL-3.0-or-later. Third-party dependencies and separately
supplied data/assets retain their own terms; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). THY666 is independent of Irijule
and is not an official manufacturer release.

### Client interface and updater preview

The browser and desktop share **Update / Client / Documentation**. Desktop
startup uses the saved light/dark appearance. The UI uses flat fills, angular
frames and beveled controls; motion respects reduced-motion preferences.

A saved and checked backup advances directly to Update. Choose firmware, detect
and select the board, then confirm the power/USB warning before applying once.
The custom-file control uses a styled Browse button with the selected filename.
Original firmware ends after transfer without offering THY666 verification.

While disconnected and idle, **Development preview** offers the complete visual
flow: backup animation, firmware choice, synthetic board detection/selection,
Apply confirmation, transfer, verification and restoration outcomes. File preview
uses the filename only and never reads firmware bytes or opens hardware. These
UI checks do not extend physical-device qualification.

The presentation page includes a keyboard-accessible carousel of shared C-rendered
view examples, a disconnected Client screenshot and a qualification roadmap.
Future roadmap versions are proposed targets, not release commitments.
