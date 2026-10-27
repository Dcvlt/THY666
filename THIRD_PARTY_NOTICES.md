# Third-party notices and local inputs

The [GNU GPL version 3 or later](LICENSE) covers original THY666 source code, documentation and
branding. It does not relicense reference firmware, recovered musical data,
extracted fonts or downloaded tools. Those materials are not included in this
source repository.

## Historical THY666 notice

Earlier public THY666 snapshots were distributed under MIT. The original notice
is retained in [THY666-MIT-legacy.txt](docs/licenses/THY666-MIT-legacy.txt).
The cleaned repository history and current releases use GPL-3.0-or-later; this historical notice
does not offer current changes under MIT.

## Bundled browser fonts

The browser bundles the following unmodified TrueType fonts under the **SIL
Open Font License 1.1**, separately from THY666's GPL-3.0-or-later license:

| Typeface | Copyright | Retained license |
| --- | --- | --- |
| Oxanium | Copyright 2019 The Oxanium Project Authors | [OFL.txt](web/assets/fonts/oxanium/OFL.txt) |
| Orbitron | Copyright 2018 The Orbitron Project Authors; Reserved Font Name: Orbitron | [OFL.txt](web/assets/fonts/orbitron/OFL.txt) |
| Monoton | Copyright 2011 Vernon Adams; Reserved Font Name: Monoton | [OFL.txt](web/assets/fonts/monoton/OFL.txt) |

These exact font bytes and their metadata come from
[Google Fonts commit e7db6ca603fd4862010383ff17ffd6cc4ffac1c0](https://github.com/google/fonts/tree/e7db6ca603fd4862010383ff17ffd6cc4ffac1c0/ofl).
The [font manifest](web/assets/fonts/manifest.json) records every source URL,
size and SHA-256. Each font's original `OFL.txt` and `METADATA.pb` are retained
beside it. No font outlines, tables or character sets were modified. The OFL
permits embedding and redistribution with software when its conditions and
copyright/license notices are retained; the font files remain under the OFL.
See the [official OFL FAQ](https://openfontlicense.org/ofl-faq/).

Platform font names in fallback stacks refer to fonts already available
on the user's system; those font files are not bundled.

## Rust updater

The updater's direct Rust dependencies are `getrandom` 0.3.4 (MIT OR
Apache-2.0), `hidapi` 2.6.7 (MIT for the Rust wrapper), and `serde_json` 1.0.145
(MIT OR Apache-2.0). Exact direct and transitive versions and package checksums
are pinned in [Cargo.lock](updater/Cargo.lock).

The [updater license bundle](updater/THIRD_PARTY_LICENSES.txt) retains the full
original license texts, copyright notices and declared license expressions for
every registry package in that lockfile, including dependencies used only while
building or on other targets. Identical texts are shared while their individual
source attributions are retained. The inventory records the exact Cargo package
checksums and original-text hashes.

The compiled native HIDAPI C backend uses its BSD-style license option. Its
upstream license-selection statement, BSD-style license, original license and
GPLv3 alternative are all retained in the bundle, together with the platform
source copyright attributions. These third-party terms remain separate from
THY666's GPL-3.0-or-later license.

The updater build wrapper copies the project `LICENSE` and
`THIRD_PARTY_LICENSES.txt` beside each validated release executable. Keep both
files with any redistributed updater. The bundle covers the locked Cargo
dependencies; it does not relicense unrelated firmware, musical inputs, fonts,
separately distributed toolchains or system libraries.

## Desktop app

The Tauri shell, native serial transport and reused updater dependencies are
pinned in [the desktop Cargo.lock](desktop/Cargo.lock). The desktop build wrapper
creates a complete `THIRD_PARTY_LICENSES.txt` inventory from those exact package
versions, crate archive checksums, original license files and platform source
attributions. It also retains the shared updater/HIDAPI license bundle.

Some crate archives omit their upstream repository license files. Their original
notices are retained under [desktop license supplements](docs/licenses/desktop/README.md),
with exact source revisions, crate identities and text hashes. The packaged
`FONT_LICENSES.txt` also keeps each bundled typeface's original OFL text. Keep
these notices and the project `LICENSE` with redistributed desktop packages.

The OS WebView/runtime and system libraries retain their own terms. Original
vendor firmware and generated musical data remain separate local inputs; none
of these dependency notices changes their redistribution rights.

## Build dependencies

The setup scripts download dependencies into the ignored `.tools/` directory,
check pinned identities and retain upstream license files:

| Dependency | Upstream license | Source and pin |
| --- | --- | --- |
| TinyUSB 0.18.0 | MIT, copyright Ha Thach and contributors | [hathach/tinyusb](https://github.com/hathach/tinyusb/tree/86ad6e56c1700e85f1c5678607a762cfe3aa2f47) |
| ARM CMSIS Core 5.9.0 headers | Apache-2.0 | [ARM-software/CMSIS_5](https://github.com/ARM-software/CMSIS_5/tree/2b7495b8535bdcb306dac29b9ded4cfb679d7e5c) |
| ST CMSIS Device F4 headers | Apache-2.0 | [STMicroelectronics/cmsis-device-f4](https://github.com/STMicroelectronics/cmsis-device-f4/tree/9192c7b9df75a142f2027ab266601fe061fc00b3) |

See [the dependency setup script](tools/setup_diagnostic_deps.py) for the exact
files, hashes and retained license paths. The separately downloaded Zig
toolchain retains its upstream notices; it is not vendored here. Distributing a
compiled release requires retaining the notices applicable to its dependencies
and included data.

## Local inputs and redistribution

The ignored `.tools/`, `Resources/`, `analysis/`, `build/`, `web/generated/` and
`.local-archive/` directories are not part of the source distribution. Local
inputs may include owner-supplied firmware, captures, private identifiers,
recovered chord data and generated artifacts. Keeping a local input does not
grant redistribution rights or change its license.

The [lossless chord format](docs/chord-data-format.md) describes the optional
musical input. The maintainer has authorized the reviewed lossless musical archive
for the corresponding firmware source release. This explicit release input is
separate from arbitrary local inputs, vendor firmware and private captures.
Retain its original metadata; the project license does not relicense unrelated
third-party material. The shared C renderer and browser OLED profile use
original THY666 glyphs. Compiled releases must retain notices applicable to
all dependencies and included data.

TheoryBoard and Irijule names identify compatible hardware. THY666 is an
independent community project and is not an official manufacturer release.

## Musical vocabulary references

The extended harmonic interval collection was cross-checked against the scale
and chord tables in [TBOSS](https://github.com/armandhammer/TBOSS), by its
contributors, at commit `306910af4b171c5c056bd49072781cd519089a8d`. Its source
license is GPL-3.0-or-later. THY666 uses normalized musical names and interval
facts in its own catalog model; it does not embed that project's engine,
scheduler, UI or installer implementation. The original playable archive keeps
its notes, layout slots, octaves and colors. Research comparisons remain local.

The contribution and conduct guides were informed by that project's concise
community documentation and written independently for THY666.
