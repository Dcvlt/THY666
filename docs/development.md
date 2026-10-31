# Development

Use Python 3.10+ and Node.js 20+. Run commands from the repository root.
No npm dependency installation or package hooks are required.

## Portable core

```sh
npm run setup
npm run build:core
python -m unittest discover -s tests -p test_studio_schema.py -v
```

Setup downloads SHA-verified Zig 0.14.1 into `.tools/` without changing the system
compiler or PATH. Initial setup needs network access; subsequent builds use the
verified local compiler. Windows/Linux/macOS x64 and Linux/macOS ARM64 packages
are pinned. Windows x64 is the primary development platform.

The core-only build compiles the C engine/state/renderer to WebAssembly and runs
native regression suites with synthetic data. It requires no controller,
firmware image or musical archive. `web/studio-menu.js`,
`core/scale_catalog.json`, `core/menu_compatibility.json` and
`core/feature_contract.json` supply the public
menu/catalog-name/feature contracts. The display profile is generated from the
project's independent core glyphs, rather than a firmware binary.

Generated output includes `build/core/`, `web/generated/core.wasm`,
`build-info.json`, `menu-schema.js` and `oled-profile.json`. Committed
`core/thy_menu_data.c` and `.h` are deterministic schema projections. A suitable
Clang/WASM installation can substitute for Zig:

```sh
python tools/build_core.py --cc clang --test --core-only
```

## Connected Client and optional archival data

The normal browser and desktop Client start using the shared C core and public
scale-name/menu metadata. They do not fetch or package `chords.runtime.json`.
Run the portable core build above, then `npm run dev`, or build the desktop app.
Pad notes, lights and screens come from the connected instrument. The private
archive remains necessary only for instrument builds and the optional standalone
archival workbench below. The merged advisory interval vocabulary is included in the shared core; it does not replace the playable archival layouts. See [combined harmonic model](chord-data-format.md#combined-harmonic-model).

On macOS, native regressions use the pinned Zig compiler with an explicit macOS 13
deployment target; WebAssembly remains freestanding. This avoids unresolved libc
symbols when Zig 0.14.1 detects a newer host OS.

The public source omits the original musical corpus. Supply an authorized
`THYMEMBK.bin` input explicitly and import it losslessly:

```sh
python tools/chord_dump.py import /path/to/THYMEMBK.bin
npm run build
npm run dev
```

The build consumes `.tools/inputs/chords.lossless.json`; importer/build options
allow explicit input/output paths. Local chord data and generated musical
projections are ignored by Git. Never replace archival notes, octaves, holes or
color IDs with a lossy pitch-class-only projection. See
[Chord format](chord-data-format.md).

The server binds to **http://127.0.0.1:3330/**, serves `web/`, and does not itself
open a controller. Web Serial connection is explicit in the client. Board stays an inactive
wireframe until a device connects; Documentation and Update remain available
without the application connection. Supported secure browsers can update through
WebHID using the shared Rust policy; explicit local-helper launches remain available.
Ctrl+C stops the server. `python tools/package_workbench.py` creates the standalone
browser bundle when its generated inputs are present.

The [desktop app](desktop.md) bundles this interface in Tauri and uses native CDC
and HID access through Rust. It does not need the local server/helper at runtime.
Desktop build tooling requires Python **3.11+** for TOML parsing. After building
the portable core, run `python tools/build_desktop.py --test`.
`--test-only` and `--check` use a labelled compile-only frontend fixture and
cannot produce a usable release package. Their source-check CI matrix does not
require generated core assets or publish a substitute release.

`npm test` runs browser/WASM tests. Some require full generated musical data;
the native/core-only path remains usable without private inputs. Tests requiring
local device evidence must identify that prerequisite rather than invent it.

The updater tests additionally use its actual compiled portable Rust core:

```sh
rustup target add wasm32-unknown-unknown
python tools/build_browser_updater.py
```

This needs no board, native HID library or private musical data. The generated
WASM/manifest pair contains only updater policy and is checked before site or
desktop packaging. See [Browser installation](browser-installation.md).

### Local performance telemetry preview

After building the portable core and browser updater, stage a separate local
preview with synthetic input captured from the actual C engine:

```sh
python tools/build_site.py --output build/performance-preview
node tools/render_performance_preview.mjs
python -m http.server 3356 --bind 127.0.0.1 --directory build/performance-preview
```

Open `http://127.0.0.1:3356/performance-preview.html`. Its scene selector covers
15 MPE members, upper-zone assignment, reserved strum voices, sustain, 21 track
playheads, external-clock pause/loss and stale/unsupported detail. Snapshots are
frozen; the preview never advances positions in JavaScript or opens a device.
Dark and light appearances use the production Client widgets. The preview files
stay in the ignored build directory and are excluded from normal site/desktop
bundles. Physical MIDI delivery, foreground timing and display polling still
require the [board checks](v0.2-board-checklist.md).

## STM32 instrument

```sh
python tools/build_instrument.py --output-dir build/instrument-local-01 --test
```

Use a new directory inside `build/`. The build uses the local lossless catalog
and pinned compiler, fetching SHA-verified TinyUSB and ARM/ST CMSIS sources at
immutable revisions into `.tools/`. Dependency terms and manifests stay with
those inputs. Local configuration/identity manifests are under `.tools/config/`.

Outputs include ELF, raw application, memory inspection, source snapshots and
manifest. This command does not package or flash a controller. Packaging and
device installation use separate tools described in
[Installation](installation.md).

## Rust updater

The standalone updater and Client helper live in `updater/`. Use **Rust
1.89+** and the platform C build tools required by its HID backend. Windows host
validation currently uses Rust 1.98.1; the declared minimum and macOS/Linux
runtime workflows have not been qualified.

```sh
python tools/build_updater.py --test
```

The wrapper uses the committed `updater/Cargo.lock`, workspace Cargo cache and
temporary directories, remaps build paths and produces the release executable
under `build/updater-target/release/`. It does not install a toolchain, enumerate
USB or start the updater. The first run downloads locked dependencies; add
`--offline` once those dependencies are cached. `--test-only` runs host tests
without producing a release build.

Run the executable's `inspect` and `simulate` commands for package review and
synthetic transfer testing without USB. `serve` starts the loopback helper only
when explicitly invoked; its allowed Client origin must match the browser's
exact origin, including port. Enter its printed address and private session token
in **Update**. Starting either the Client or helper never starts an installation.
See [Updater](updater.md) for CLI examples, native prerequisites and API boundaries.

In the desktop app, the updater library is called through scoped in-process
commands instead of loopback HTTP. The shared validation, operation ownership
and no-replay rules remain the same. Build outputs, staged firmware resources,
dependency attribution and the Windows portable ZIP are described in
[Desktop](desktop.md).

Windows builds, host tests and simulated exchanges are separate from physical
USB qualification. The new updater has not been qualified on hardware or through
end-to-end macOS/Linux workflows. Configuration restoration, interrupted-update
recovery and automatic rollback remain separate unqualified capabilities.

## Verification

Run focused native and browser suites for changed behavior, then the applicable
target integration checks. Inspect generated ELF/map/stack reports for memory
changes; see [Resource budget](firmware-budget.md). Host tests establish software
behavior, not electrical timing or interrupted-update recovery.

Keep maintained docs in `docs/`; generated output, private inputs, captures and
working/research archives stay ignored. Preserve upstream license notices.
