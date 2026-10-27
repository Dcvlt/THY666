# Contributing to THY666

Start with [Development](docs/development.md), the
[engine/state model](docs/engine-state-model.md) and the relevant feature guide.
Menus describe available controls; unavailable engine entries are not completed
implementations.

## Changes

- Keep musical behavior, setting validation and UI state in shared C. Browser
  code projects that state rather than implementing a second musical engine.
- Preserve explicit note ownership across pads, channels and destinations.
  Settings changes must retain the release route of every existing note.
- Use bounded storage and cooperative service work. Review flash, SRAM, CCM,
  stack and foreground cost when adding features.
- Add focused regressions for behavior changes. Distinguish software checks
  from physical device observations when describing validation.
- Keep code/documentation separate from generated output and local research.
  Do not commit firmware dumps, vendor packages, captures, device identifiers,
  private settings or data/assets whose redistribution terms are unresolved.

## Keep the project guide current

Update `README.md` alongside every material feature, workflow, setup or
qualification change. Keep implemented, host-tested, device-tested and planned
work distinct. Do not describe a planned menu entry as a working engine.

Update the relevant `docs/` guide and Companion help in the same change. When a
screen changes, regenerate its synthetic example with
`node tools/render_help_examples.mjs`, then run it with `--check` and the related
help tests. Keep screenshots tied to the shared C renderer and label illustrative
board diagrams clearly. Avoid volatile test totals and latest-build IDs in the
README. Agents should start with [AGENTS.md](AGENTS.md).

The [public site](docs/project-site.md) reuses Companion guidance; do not maintain
a second manual there. Keep musical examples, expected results and current
limits beside the relevant diagrams. Publishing and browser installation have
separate implementation and qualification requirements.

## Problem reports

Include build identity, board revision if known, relevant settings, the shortest
reproduction and the affected path: DIN, USB, display, controls or lighting.
Mention sensitivity-related hardware modifications. Redact serial numbers and
private routing information from logs.

A passing browser preview does not establish electrical behavior or authorize
a flash. Use the [installation guide](docs/installation.md) for device work.

Original contributions use the repository's GPL-3.0-or-later license. Preserve third-party
attribution and terms; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
