# Contributing to THY666

Help make the board more useful to the people playing it. Musical ideas,
reproducible bugs, hardware observations, accessibility improvements, clearer
words and code are all welcome. You do not need a connected board to contribute.
Read our [Code of Conduct](CODE_OF_CONDUCT.md) before joining project discussions.

## Choose a useful starting point

- **Something behaves incorrectly:** use the bug report form and give the
  shortest reproduction. A screenshot or a few redacted log lines often help
  more than a large dump.
- **A musical idea:** describe what you want to play, the controls you imagine
  using, and how it should interact with existing settings. Start a discussion
  in an issue before implementing a substantial new engine or workflow.
- **Hardware observations:** record the setup, method and limits of the finding.
  Distinguish a measurement from an interpretation. Testing hardware is optional.
- **Documentation or interface work:** make the next action clearer and keep
  browser and desktop behavior aligned. Preserve the flat black/red, angular
  visual language, bundled fonts and accessible keyboard interactions.

## Human and AI contributions

This project openly uses human–AI cooperation. AI-assisted contributions are
welcome, but submitting them means taking responsibility for the result.
Explain the behavior, inspect the diff, run relevant checks and disclose material
AI assistance in your pull request. Generated claims are not test evidence.
Do not submit changes you cannot explain or verify, or private information in
prompts, screenshots or logs.

## Development and pull requests

1. Fork the repository and make a focused branch. Target **develop** with your PR. Keep unrelated cleanup out of
   a behavior change so reviewers can understand its effect.
2. Build the public core with `npm run setup` and `npm run build:core`.
   The connected Client and core tests need no private chord archive. Instrument
   builds require a separately supplied local archive; see the development guide.
3. Run checks that exercise your change, and record their commands and results.
   Say explicitly when hardware, a platform or a data-dependent test was not run.
4. Update affected documentation/help and include synthetic screenshots for
   interface changes. Keep private board state out of examples.
5. Open a pull request describing the problem, resulting behavior, validation
   and remaining limits. Small, reviewable changes are easier to integrate.


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

Update the relevant `docs/` guide and Client help in the same change. When a
screen changes, regenerate its synthetic example with
`node tools/render_help_examples.mjs`, then run it with `--check` and the related
help tests. Keep screenshots tied to the shared C renderer and label illustrative
board diagrams clearly. Avoid volatile test totals and latest-build IDs in the
README. Agents should start with [AGENTS.md](AGENTS.md).

The [public site](docs/project-site.md) reuses Client guidance; do not maintain
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

## Licensing and material to leave out

By submitting original contributions for inclusion, you license them under
**GPL-3.0-or-later**, the repository's license. You retain your
copyright. Identify reused material and preserve its original attribution and
compatible terms; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Do not submit vendor firmware, flash dumps, personal backups, device identifiers,
credentials, proprietary disassembly or copied proprietary implementation.
Describe hardware observations in your own words. Keep local research and
unreviewed generated output out of pull requests.

Release tags use **vX.X.X**. A prerelease is a testing milestone, not a claim that
all hardware paths are qualified. Publishing, firmware installation and settings
restoration are separate actions; a review or build never implicitly requests
writing to a board.


## Maintainer branches and integration

Our internal branch convention is `<type>/<XX>-<short-name>`, where type is
`feature`, `fix` or `hotfix`. Use lowercase words joined by hyphens. Numbers
are sequential across all three types: after `feature/01-example`, the next
branch could be `fix/02-another-example`, not `fix/01-another-example`.
Pad numbers to at least two digits; do not reset or reuse them after merging.

Maintainers reserve the next number in `.github/branch-sequence.json` and check
both local and remote branches before starting work. `main` and `develop` are
long-lived exceptions to this convention. Contributor fork branch names, issue
numbers and issue/PR titles are not required to follow it.

Normal PRs merge into **develop**. Only a promotion PR from this repository's
**develop** branch may target **main**. Main requires passing checks and a PR;
direct pushes, force-pushes and branch deletion are blocked. Resolve review
threads before merging. Do not impose a mandatory second-person approval on a
solo-maintainer project; passing CI and explicit review remain required.

Keep Pages deployment on main. Release tags point at reviewed main commits.
Use merge commits for develop-to-main promotions so both branches retain their
shared ancestry; do not squash or rebase a long-lived branch promotion.
