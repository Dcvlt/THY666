# Working on THY666

THY666 is an independent open source firmware and Client for the TheoryBoard THY333.
This project is developed through human and AI cooperation. Read `README.md`,
`CONTRIBUTING.md` and the relevant maintained guide before making changes.

## Architecture and source of truth

- `core/`: portable C musical engine, ownership, settings/state, menus, codecs
  and 128 × 64 renderer. Keep behavior and validation shared here.
- `firmware/instrument/`: STM32 instrument adapter, timing, USB/DIN, scanning,
  lighting and persistence. `firmware/diagnostic/` also supplies common drivers.
- `host/wasm_bridge.c`: bindings used by the browser and host tests.
- `updater/`: shared Rust package validation and pull-driven transfer session,
  native bootloader transport/helper, and a bounded browser WASM ABI. Native and
  browser paths must use the same protocol policy; JS supplies WebHID I/O only.
- `desktop/`: Tauri host using the same updater library and scoped native CDC
  access. Keep OS handles, USB ownership and update lifetime in Rust; the shared
  client retains protocol parsing, checksums and serialized transactions.
- `web/`: connected Client and read-only documentation. Project C/device
  state; do not introduce a second musical engine or competing scheduler in JS.
- `site/`: public overview, read-only guide and explicit browser installer.
  `tools/build_site.py` stages allowlisted assets, the same `web/help*` source and
  a verified portable updater. Keep firmware, private corpus and local bindings
  out of the default bundle. No page may open devices automatically. Update its
  installation/status copy when release availability or qualification changes.
- `tools/`: verified builds, imports, packaging, capture and inspection.
- `tests/`: C, Python, PowerShell and browser regressions. Keep fixtures synthetic.

The Client requires explicit connection. Its disconnected surface is an
inactive wireframe; it must not audition notes or silently open a device.
Connected large-pad clicks inspect only. Small controls and parameter actions
can control the board. Documentation examples never control hardware.

## Changes and validation

Preserve note ownership across pads, channels and destinations, including held
route snapshots, overlap, sustain, strum cancellation and generated-note cleanup.
Keep MIDI releases and acquisition responsive during display/storage work.
Use bounded pools and cooperative work; review flash, SRAM, CCM, stack and
foreground cost for firmware features. See `docs/firmware-budget.md` and the
actual linker/build inspection, rather than copying an old size report.

Run focused meaningful checks for changed behavior. Reuse checks already
completed for the exact unchanged artifact; do not repeat every test during
transfer setup. Broaden validation when a change, failure or unresolved concern
requires it. Host/browser success is separate from device qualification.

Useful commands, from the repository root:

```sh
npm run setup
npm run build:core
rustup target add wasm32-unknown-unknown
npm run build:browser-updater
npm test
node tools/render_help_examples.mjs --check
python tools/package_workbench.py
python -m unittest discover -s tests -p test_public_privacy.py -v
python tools/build_instrument.py --output-dir build/instrument-local-01 --test
python tools/build_updater.py --test
```

Use a fresh target build directory. The connected Client builds from public source and core assets. Instrument
builds and optional archival workbench packaging require an authorized local
lossless chord archive; core-only tests and desktop builds do not.
See `docs/development.md`. Preserve musical notes, octaves, layout holes, colors
and source integrity rather than silently correcting archival data.

On Windows, sandbox restrictions can fail compiler helpers or temporary writes
before compilation. Use the build tools' workspace caches/temp directory and
distinguish environment failures from code regressions. Do not weaken compiler
identity pins, omit failing tests or claim a pass to bypass an environment issue.

## Private configuration and artifacts

Use the ignored root `.env` through `tools/local_env.py` / `tools/local_env.ps1`.
Explicit CLI arguments override process environment, then `.env`, then defaults.
Preserve the user's existing `.env`; document new keys in `.env.example` and
README without real identifiers or credentials. Do not evaluate dotenv as code.
Device identities verify the intended connection; they are not feature licenses.

Keep captures, vendor firmware, settings/backups, local identifiers and work
notes in ignored local directories. Preserve historical material under
`.local-archive/`; keep current user/developer documentation in `docs/`.
Never publish a real serial/UID, hub instance, private home path or key in source,
test fixtures, screenshots or bundled artifacts. Keep upstream license notices.

Retain compiler path remapping and ELF DWARF anonymization. It must preserve
symbols, layout and loadable bytes. `--keep-debug` is local-only debug output;
it may retain private paths. Do not strip inspection symbols as a shortcut.

## Device work

Work autonomously within the user's request and existing authorization. An
analysis or prepare-only task does not authorize flashing, erasing or changing
device settings. Never send write/erase/update commands merely to inspect.
Use the user's scope for hardware tests and avoid redundant approval questions.

Start debugging from the reported build, route/channel, settings and shortest
reproduction. Read `docs/companion-and-backup.md` and `docs/installation.md` for
transport and update boundaries. Respect exclusive serial access; do not take
over a connected client or interrupt a running updater. Preserve captures and
backups, use fresh capture sessions, and never overwrite earlier evidence.

Completed `.333` updates have been exercised. An updater's final message alone
does not identify the running firmware; verify the actual build and behavior.
Do not retry a completed installation solely because it says "patching failed".
Full-state restore qualification, interrupted-update recovery and automatic
rollback remain distinct unfinished work. Do not claim a raw full-device backup
from a settings export or firmware package.

Read `docs/updater.md` for the Rust CLI/helper and Client Update workflow.
Inspection, simulation and a helper startup do not authorize installation.
Keep discovery, target selection and installation explicit; respect application
and updater USB ownership. Bootloader preparation can alter external contents
before hardware/UID checks. Treat partial writes and late activation disconnects
as uncertain until the running application is explicitly verified; never retry
an installation automatically or promise configuration preservation.

## Documentation and presentation

Treat concise, contextual copy as a project-wide requirement across firmware,
Client, updater and documentation. Give each state one clear heading and each
instruction one home. Remove repeated status text, filler and implementation
reassurance. State the result and next useful action in familiar words. Put
protocol names, checksums, identifiers and diagnostic detail in expandable
details unless needed to resolve a problem. Keep consequential warnings at the
decision they explain, and never hide errors or uncertain outcomes for brevity.


Maintain `README.md` with material feature, status, workflow and setup changes.
Update relevant `docs/` and Client help at the same time. Clearly distinguish
implemented, host-tested, device-tested and planned functionality. Avoid stale
per-build IDs and test totals in the README.

Generate example screen frames from the actual C renderer with synthetic input
using `tools/render_help_examples.mjs`; verify with `--check`. Label diagrams and
examples, and preserve the physical 96-pad / 43-control arrangement. Use tables
for choices and short actionable explanations. The default visual identity is
pure black/red, angular circuit/HUD lines, the current logo and bundled licensed
fonts. Reuse editable vector assets instead of substituting a new logo.

The native Windows Rust updater has passed one same-build THY666 installation,
including captured payload/CRC and application checks. Configuration reset to
factory defaults; its precise cause and the bootloader erase range remain unproved.
Explicit restoration of the captured backup passed saved readback and cold reboot,
with a byte-identical post-boot export. That test used an empty song/library and
default pad overrides; do not generalize it to nonempty material or calibrated
profiles. Update-aware configuration handling, Original v5 through Rust,
interrupted-update recovery and macOS/Linux USB remain release milestones.


## Branch workflow

Use `feature/XX-short-name`, `fix/XX-short-name` or `hotfix/XX-short-name` for our
branches. XX is one shared sequential number across all types, padded to at least
two digits. Check local/remote refs and reserve the next number in
`.github/branch-sequence.json`; never reset or reuse numbers after merging.
Do not use a codex/ prefix in this repository. Main and develop are long-lived
exceptions. This convention does not constrain external contributor branch
names, issue titles or pull request titles.

Target normal PRs at develop. Promote develop to main through a merge-commit PR
from the same repository, after required checks pass. Do not push directly to
main or bypass its protections. Keep deployment on main and release tags bound
to reviewed main commits. Preserve develop/main ancestry during promotions.
