# THY666 Rust updater

This crate supplies strict `.333` inspection, a bounded bootloader exchange,
native HID access, a browser WASM policy ABI and the local Companion helper. See the maintained
[updater guide](../docs/updater.md) for commands, consent and result handling.
Building and testing never opens a controller.

Native `runpkg` and the browser adapter drive the same pull-driven `Session`.
Build the host-import-free browser core with
`python tools/build_browser_updater.py` after installing the official Rust
`wasm32-unknown-unknown` target. See [Browser installation](../docs/browser-installation.md)
for the direct WebHID route, actual-WASM tests and remaining device qualification.

## Native builds

Use Python 3.10+ and an installed Rust toolchain. The declared Rust minimum is
1.89; local software checks currently use 1.98.1. The minimum has not been
separately qualified. First builds download dependencies pinned by Cargo.lock.

| Host | Additional build requirements | Native HID backend |
| --- | --- | --- |
| Windows MSVC | Visual Studio C++ build tools and Windows SDK | Statically linked HIDAPI C backend; release C runtime is static. |
| macOS | Xcode command line tools | HIDAPI with system IOKit/CoreFoundation/AppKit frameworks. |
| Linux | C compiler, `pkg-config`, `libudev` development headers | Statically linked HIDAPI hidraw backend; system `libudev` remains a dependency. |

```sh
python tools/build_updater.py --test
python tools/build_updater.py --test-only --offline
```

Run from the repository root. Cargo downloads live in `.tools/cargo`, temporary
files in `.tools/updater-temp`, and outputs in `build/updater-target`. The
release executable is under `build/updater-target/release/`, with `.exe` on
Windows. The wrapper never installs toolchains or changes global Cargo settings.

The wrapper appends local-path remaps to existing Rust/Rustdoc environment flags,
uses locked dependencies, and checks the release executable for private build
paths. On Windows MSVC release builds it enables `crt-static`; the pinned HIDAPI
build also selects the static C runtime. The wrapper inspects PE imports and
rejects a release that still requires a dynamic Visual C++/Universal C runtime.
Windows system DLLs remain required. The ignored `updater-build.json` records
artifact identity, relative output path and these checks.

Direct Cargo commands are useful during development, but do not apply all of
the wrapper's packaging/privacy checks. `--offline` requires an already populated
workspace Cargo cache. Existing compiler flags are preserved; flags that embed
private paths can make the release privacy check fail.

## Local download package

After the verified build, optionally prepare the local firmware catalog using
`tools/prepare_updater_releases.py`, then run from the repository root:

```sh
python tools/package_updater.py
```

For Windows x64, outputs are
`build/releases/THY666-updater-<version>-windows-x64.zip` and the identical local
download copy `web/generated/updater/THY666-updater-windows-x64.zip`. Both paths
are ignored output. Packaging rechecks the build, notices, optional sibling
catalog and firmware files, then verifies the ZIP contents without opening USB.

Extract the whole ZIP and keep the executable, notices and optional catalog/files
together. Double-clicking the executable starts the helper and opens Update;
the local app server must already be running on port **3330**. The ZIP is not a
self-contained public installer and does not qualify physical USB behavior.
Locally supplied vendor firmware retains its existing redistribution rights.
See the [updater guide](../docs/updater.md) for the wizard and source restrictions.

## Validation limits

Tests use synthetic packages, reports and devices. They cover rejection rules,
CRC values, protocol state and failures, report framing and interface selection.
They do not perform HID enumeration, flash writes, raw readback or device recovery.

A same-build THY666 installation through the native Windows desktop app has
passed a physical test: successfully paired application reports matched the
package exactly, both bootloader CRC replies matched, activation completed and
the expected application build/identity and read-only status, I/O, settings and
display checks passed. Configuration reset to exact factory defaults, confirmed
by before/after backup payloads; the cause and erase ranges remain unproven.
Restoring the captured configuration passed readback and cold-boot checks; a
fresh export matched the original backup byte for byte. The sample had an empty
library and song, with default calibration overrides. Nonempty content and
nondefault overrides still require restoration qualification. This does not
qualify Original v5 installation through this updater, interrupted-update
recovery or automatic rollback.

The source CI matrix is prepared for Windows, macOS and Linux. It has not been
executed as part of the local Windows implementation. Compilation and synthetic
tests are separate from USB permissions, real transfer behavior, cable removal,
interrupted-update recovery and automatic rollback qualification on each OS.

HID reads use finite protocol polls. The pinned Windows C backend bounds its
write wait to one second. HIDAPI's Linux and macOS write APIs do not expose a
portable application timeout/cancel operation; their OS-level completion bounds
need qualification. Failed or short writes are never retried automatically.
Descriptor serial strings may be generic; they do not authenticate an MCU UID.

The workflow pins its checkout action to the verified
[v7.0.1 commit](https://github.com/actions/checkout/commit/3d3c42e5aac5ba805825da76410c181273ba90b1)
and requests Rust 1.98.1 without changing a developer's global default toolchain.
Hosted runner images and OS package repositories still change over time.
