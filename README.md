<p align="center">
  <img src="docs/assets/thy666-banner.png" alt="THY666 — open source firmware for TheoryBoard THY333" width="1200">
</p>

# TheoryBoard. Back from the brink.

**THY666** is independent open-source firmware for the **Irijule TheoryBoard
THY333**, with a Client for your browser and desktop. Keep playing the instrument
you own, with more control over its music, expression and behavior.

## Start on the project site

### [Explore THY666 →](https://dcvlt.github.io/THY666/)

See the features, screen previews and roadmap, then choose your next step:

**[Install firmware](https://dcvlt.github.io/THY666/install.html)** ·
**[Open the browser Client](https://dcvlt.github.io/THY666/client/index.html)** ·
**[Read the user guide](https://dcvlt.github.io/THY666/manual.html)** ·
**[Releases and downloads](https://github.com/Dcvlt/THY666/releases)**

Open a release’s **Assets** for the standalone `.333` firmware, Windows installer
and portable ZIP, or macOS disk image. The source-code archives are separate.

New to THY666? Start with the [Quick start guide](https://dcvlt.github.io/THY666/manual.html#getting-started):
read the screen, learn the controls and find your settings. The same guide is
available in Client → Documentation.

## What it brings to the board

- **Musical control:** chords and scales, inversions, strum, voice leading,
  octave folding, guitar voicings and a live Harmony workspace with playable
  suggestions, melody context, dedicated views and explicit key previews.
- **Expression:** strike velocity, sensitivity and calibration, aftertouch,
  pressure bend and optional MPE.
- **Rhythm and routing:** two Arps, a 21-track sequencer, DIN and USB MIDI,
  with configurable channels and destinations.
- **A connected Client:** live board views, settings, diagnostics, configuration
  backups and a shared firmware-update flow in browser and desktop.

The Client connects only when you ask. The physical board remains the playing
surface; screen pads inspect its state. On phones, a compact navigation drawer
keeps pages accessible. Backups separate saving from restoration; Diagnostics
offers bounded live report charts, including DIN output activity. New firmware
also exposes CPU timing, fixed RAM allocation, observed stack peak and firmware
flash in Diagnostics and System → Resources; focused board checks passed. Board feedback adapts
to active engines; Harmony groups suggestions and playing controls. The guide
uses illustrations, expandable reference sections and phone-friendly tables.

## Current status

**Hardware alpha. The v0.2 series is a prerelease for board testing.** Existing playing,
MIDI, display, storage and Windows update paths have device evidence. The latest
musical additions have passed software checks and focused board tests, including
Harmony exploration, held-note stability and saved preferences after power-off.
macOS/Linux USB, interrupted-update recovery and broader restore
coverage remain unqualified. See the [board test checklist](docs/v0.2-board-checklist.md).

Back up your configuration before updating. A settings export is not a complete
device image, and configuration preservation or automatic rollback is not
promised. Follow the [installation guide](docs/installation.md) and the notes
attached to the exact release you choose.

The development build uses settings V18. Harmony offers a full 48-pad chord
surface, fixed root columns, adaptive or locked qualities and contextual in/out-key
suggestions in one Fifths view. Exploration, readable pad guidance, clean DIN
releases and cold-boot persistence have been exercised on a board; broader
policy and load tests remain. **System → Swap clusters** exchanges musical roles;
sensor calibration stays on its physical pads. See [Harmony](docs/harmony.md).

The development menus also provide confirmed, scoped **Reset options** actions.
They retain saved musical material and physical calibration. Auto leading is
anchored to its base register, and the shared Key contrast switch covers
Chromatic, All notes and Harmony lighting.

Engines opens Play, Harmony, Sequencer, Arp or the planned Drums description.
The active workspace is retained when leaving settings. Harmony and Sequencer
settings are under Engines → Settings. Root and Scale live in Chords; upper-button
scale assignments live in Controls. System stays last in global Settings.

When restoring a compatible older backup, the Client uses an isolated shared C
codec to migrate it before upload,
keeps the original file, and verifies the complete saved payload afterward.
V13-to-V15 restoration of the captured Windows configuration passed saved
readback and a full power-off check. V14 migration and broader saved-material
coverage still need physical qualification.

## Build or contribute

The public core and connected Client build without a private musical archive:

```sh
npm run setup
npm run build:core
npm run dev
```

Open the local URL printed by the server. Firmware builds additionally need the
reviewed musical input supplied with the matching firmware source release.

[Development setup](docs/development.md) · [Desktop builds](docs/desktop.md) ·
[Architecture](docs/engine-state-model.md) · [Release procedure](docs/releases.md)

Musical ideas, bug reports, hardware findings, documentation and code are all
welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) and the
[Code of Conduct](CODE_OF_CONDUCT.md). Normal pull requests target **develop**;
reviewed promotions go to protected **main**. Agents should also read [AGENTS.md](AGENTS.md).

## Human + AI

THY666 is developed through human–AI cooperation, including substantial
implementation and tooling work with OpenAI Codex. The human maintainer defines
the musical direction and tests the board; generated work is reviewed and tested.
Software checks and physical-device qualification are reported separately.

## License and credits

**[GNU GPL version 3 or later](LICENSE)** (`GPL-3.0-or-later`).
Copyright © 2026 THY666 contributors. Third-party credits and retained terms are
in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Earlier copies distributed
under MIT retain their original permissions.

THY666 is independent of Irijule and is not an official manufacturer release.

*THY666 · Human + AI, developed with spite, coffee and tokens.*
