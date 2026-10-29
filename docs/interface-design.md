# Client, OLED and sequencer design direction

These are three separate workstreams. Client refinement can proceed without
changing musical behavior. OLED styling needs renderer and device validation.
Sequencer expansion starts with a workflow proposal before implementation.

## Client

The Client pass is implemented and covered by browser regressions and synthetic
layout review. It does not change firmware or authorize hardware operations.

Implemented structure:

| Area | Structure | Acceptance criterion |
| --- | --- | --- |
| Backups | Choose task → file/action → result; restoration retains confirmation | One primary action at each step, no duplicated status |
| Diagnostics | Connection health, resources, then live measurements; raw reports in details | Every gauge names its source, units and freshness |
| Board | Common musical feedback plus contextual MPE/engine panels | Only applicable panels appear; no second musical engine |
| Harmony | Current music and map → suggestions/result → exploration and playing controls | Applied settings and current result remain visible together |
| Guide | Purpose → shortest useful workflow → illustrated controls → options table → troubleshooting | First useful instruction visible on phones without traversing an index |
| Mobile | One header drawer, stacked task panels and accessible touch controls | No horizontal page overflow at 320 px; focus restored after closing |

Resource displays must distinguish persistent storage capacity, linked firmware
allocation and live runtime usage. Linker allocations are not current RAM or CPU
utilization. Missing measurements display unavailable rather than a guessed gauge.
Telemetry stays in the serialized transport, pauses when hidden and yields USB
to updates. Historical charts require bounded samples and explicit stale-state
handling. The current chart retains at most 60 values from the selected report;
raw text retains only the latest reply. Changing report type or disconnecting
clears the chart. Reports expose activity, MIDI queues, saved-settings counters
and display timing. The generated-note pool and held-pad gauges use snapshot
measurements; resource reports add CPU cycle activity, linked RAM/CCM allocation and observed stack peak when the connected firmware supports them.

Use black backgrounds, accent-colored angular rails, current icons and licensed
fonts. Use decoration to group controls or communicate state. Keep technical
identities and raw protocol details in contextual disclosures. Avoid repeating
headings, connection status and instructions across parent and child panels.

## OLED theme

Translate the visual identity through a small shared set of pixel primitives:
clipped corners, interrupted rules, compact icons, and one consistent selection
marker. Preserve text baselines and musical information before adding ornament.
Do not shrink readable labels to make room for decoration.

The approved theme is implemented in the shared C renderer for Play views,
Harmony, menus, Arps, sequencing, calibration, library editors and confirmations.
It retains the existing menu cursor and menu-row content. Inversion remains reserved for parameter editing. Separator notches frame their associated text: top rules underline the title,
bottom rules dip over the legend below. Their two-pixel transitions fit within
the existing three-row band. Visible label width is a bounded character count
times the shared five-pixel font advance, with padding and a viewport clamp;
no framebuffer analysis is required. Chord-name underlines update with the
rendered name, without animation. Five-note and F-sharp minor seventh frames
check note placement and variable title width.

Generate the review with `python tools/render_oled_design.py`, then open
`build/oled-design-preview/index.html`. Each screen has current/proposed frames
at native 128 × 64 and nearest-neighbor 4×. Native physical size depends on the
browser zoom and display density. The host-only harness directly reuses the
shared C renderer, font, icons and musical projection. Its synthetic chord and
resource fixtures are illustrative, not live device measurements. Framebuffer
guards and read-only musical state are checked during
rendering. The harness is absent from firmware and WASM build source lists.

Resources bars distinguish static RAM/CCM allocation, application flash usage
and observed stack peak. CPU active includes foreground polling: 100% alone
must not imply overload. Mean/peak loop timing stays separately visible.

Host regressions cover idle, editing, long labels, error/confirmation states,
framebuffer bounds and unchanged note ownership. The Client uses the same C
frames; its Web view preserves the separator pixels while enhancing text.
Dense Harmony and Arp footers retain their controls without adding a competing
rule; the startup logo is unchanged. The clean page handoff is preserved.
Focused device checks passed on 2026-10-29: Harmony/Fifths, Settings navigation,
Resources, and three-/five-note Simple Arp feedback were readable and responsive,
with clean DIN releases. Quantitative optical latency and worst-case mixed-load
timing remain unmeasured. See [firmware budget](firmware-budget.md) for the
measurements and qualification limits.

## Sequencer expansion: next design stage

Preserve the validated baseline: explicit tracks, tap-to-select steps, the other
half as Notes/Chords input, track-local settings, solo/full playback and undo.
The expansion is not yet specified or implemented.

Design around three concrete musical tasks:

1. Build a drum/bass/chord phrase and edit its timing and duration.
2. Perform variations while other tracks continue playing.
3. Arrange several phrases into a longer piece and save/reopen it.

For each task, define the screen, physical control map, pad roles, feedback,
undo boundary and behavior while playing. Resolve clip/section ownership,
arrangement versus live launch, navigation beyond 48 visible steps, expressive
recording and storage budgets before extending the event model. First produce
interaction diagrams and a clickable/synthetic preview, then validate the flow
with the user before implementing firmware behavior.
