<p align="center">
  <img src="docs/assets/thy666-banner.png" alt="THY666 — open source firmware for TheoryBoard THY333" width="1200">
</p>

# TheoryBoard. Back from the brink.

**THY666** is independent open-source firmware for the **Irijule TheoryBoard
THY333**, with a Client for your browser and desktop. Keep playing the instrument
you own, with more control over its music, expression and behavior.

**Current accepted release: [v0.3.1](https://github.com/Dcvlt/THY666/releases/tag/v0.3.1)**
— hardware alpha, with focused board testing and remaining qualification limits.

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
available in the Client’s **Guide** tab.

## Human + AI

THY666 is developed through human–AI cooperation, including substantial
implementation and tooling work with OpenAI Codex. The human maintainer defines
the musical direction and tests the board; generated work is reviewed and tested.
Software checks and physical-device qualification are reported separately.

## What it brings to the board

- **Musical control:** chords and scales, inversions, strum, voice leading,
  octave folding, guitar voicings and a live Harmony workspace with playable
  suggestions, melody context, dedicated views and explicit key previews.
- **Expression:** strike velocity, sensitivity and calibration, aftertouch,
  pressure bend and optional MPE, with physical-half playability settings and
  selectable MPE pressure messages.
- **Rhythm and routing:** two Arps, a 21-track sequencer, DIN and USB MIDI,
  with configurable channels and destinations.
- **A connected Client:** live board views, settings, diagnostics, configuration
  backups and a shared firmware-update flow in browser and desktop.

The Client connects only when you ask. The physical board remains the playing
surface; clicking its large pads on screen inspects them. Contextual displays
show expression routing, sounding notes, Harmony movement and recording states.
An illustrated guide explains the controls, settings and musical workflows.

The firmware is optimized for the board's fixed resources. Paired board captures
confirmed lower rendering cost without reducing frame cadence or increasing
static RAM. **System → Resources** and **Client → Diagnostics** expose CPU timing,
fixed RAM allocation, observed stack peak and flash usage. Optional timed captures
export performance data to help contributors reproduce problems and evaluate changes. See
[performance profiling](docs/profiling.md) and the
[resource review](docs/firmware-budget.md).

## Current status

**v0.3.1 is the current accepted hardware-alpha release.** Focused board checks
on the v0.3.0 baseline cover playing, DIN/USB MIDI, expression, Harmony exploration, OLED views,
resources, Windows updates and saved preferences after power-off. Performance
captures have also passed timed completion, JSON export and retrieval after
Client disconnection. These checks do not establish maximum-load performance.

v0.3.1 shares the status-icon frame across all seven Play views and refines the
site’s mobile navigation. The new frames passed software checks and C-rendered
preview review; their physical OLED check remains pending.

The released feature set includes:

- **Scoped expression:** Performance → Settings for applies velocity, Aftertouch,
  bend and MPE pressure output to Both, Left or Right. MPE enable and its zone
  remain global; pressure output can use Pressure, CC74 or CC1.
  [Expression guide](docs/mpe-expression.md).
- **Harmony exploration:** a full chord surface with stable root columns,
  adaptive or locked qualities and contextual in-key/out-of-key suggestions.
  System → Swap clusters exchanges musical roles while calibration stays with
  each physical pad. [Harmony guide](docs/harmony.md).
- **Configuration recovery:** the Client converts supported older backups with
  the shared C codec, retains the original file and compares the complete saved
  payload after restoration. Matching progress on the board and Client guards
  playing and editing during recovery. [Backup guide](docs/companion-and-backup.md).

macOS/Linux USB, interrupted-update recovery and broader restore coverage remain
unqualified. Guarded restore failure/disconnect paths, detailed MPE meters,
sequencer playheads and later Client/indicator refinements have software coverage
but need broader device checks. Drums remains planned. See the
[board test checklist](docs/v0.2-board-checklist.md) and
[release notes](docs/releases.md) for the exact qualification scope.

Back up your configuration before updating. A settings export is not a complete
device image; configuration preservation and automatic rollback are not promised.
Follow the [installation guide](docs/installation.md) and the notes attached to
the exact release you choose.

The next milestone is **v0.4: sequencer expansion**. See the
[roadmap](https://dcvlt.github.io/THY666/#status-heading) for planned work.

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

## License and credits

**[GNU GPL version 3 or later](LICENSE)** (`GPL-3.0-or-later`).
Copyright © 2026 THY666 contributors. Third-party credits and retained terms are
in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Earlier copies distributed
under MIT retain their original permissions.

THY666 is independent of Irijule and is not an official manufacturer release.

*THY666 · Human + AI, developed with spite, coffee and tokens.*
