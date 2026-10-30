# THY666 interface system

The Client and project site share a visual system built around open fields,
split circuit traces, fixed notches and contrasting line weights. Variation
communicates purpose; it does not identify which page implemented a component.

Open [the component reference](../web/design-system.html) through the development
server at `/design-system.html`. The site build includes the same reference.
It demonstrates real shared components with sample values and never opens a
device. Appearance changes there affect the reference only.

## Audit and decisions

The strongest existing elements were the approved icon-button container, the
layered instrument frame, width-aware captions, the guide's open reading surface
and the connected cockpit's coordinated assembly. These establish the language.

| Family | Drift found | Shared treatment |
| --- | --- | --- |
| Frames | Site skewed cover patches, guide tabs, rectangular utility panels and several unrelated corner sizes | Section, task, inset and dialog frames with fixed cuts and unequal accent lengths |
| Actions | Page-specific polygons, rounded legacy controls and inconsistent leading accents | One action foundation; primary, quiet, destructive and compact variants |
| Icon controls | Good burger frame, but different gear/close/help frames and platform-dependent glyphs | One split frame, accent-colored SVG glyphs and opposing hover/focus triangles |
| Choices | Backup, firmware and engine choices used independent active styles | Task choice with a leading rail and bottom selection accent; native state attributes |
| Fields | Independent heights, padding and focus rules | Visible labels, native controls, shared field shell, adjacent help and errors |
| Rules | Plain boundaries mixed with stretched cuts and arbitrary ornaments | Flexible rail length with fixed diagonal geometry; quiet rules for secondary division |
| Feedback | Repeated status, weak selected/unavailable differences and text-only notices | Explicit status, notice and disclosure families; color reinforces a named state |
| Motion | Multiple easing curves, feature-local transitions and older blanket overrides | Interaction, panel entry and connection roles; controls and live values stay stable |
| Assets | Correct logo/font sources, but duplicated font declarations and Unicode interface pictograms | Canonical branding retained; exact shared typography and a small SVG interface registry |

The migration removes superseded Client and site decoration rather than adding
another theme override file. Some old musical workbench selectors remain in
`styles.css`; that file still supplies the instrument's layout. New Client
components must not depend on its historical colors or rounded surfaces.

## Source ownership

| Source | Owns |
| --- | --- |
| `web/ui-system.css` | Palette, spacing/weight/cut/motion tokens and component appearance |
| `web/ui-system.js` | Named SVGs, one shared width-aware outline and bounded component observers; no timers or application state |
| `web/typography.css` | Licensed local fonts, actual glyph ranges and symbol fallbacks |
| `web/companion-theme.css` | Client appearance preferences and adapters for instrument-specific surfaces |
| Feature CSS and `site/site.css` | Layout, responsive composition and specialized diagrams |
| `web/design-system.*` | Read-only reference composed from the production library |
| `docs/assets/README.md` | Branding provenance, editable sources and regeneration |

Public landing, guide, installer and Client use the same source files. Desktop
staging includes them; the standalone workbench embeds them. Do not copy a
component into a site-specific fork or import the Client runtime into the site.

### Visual asset inventory

| Source | Intended use | Decision |
| --- | --- | --- |
| `web/assets/thy666-web-logo.svg` | Full-detail header and connection/startup choreography | Canonical supplied mark; preserve its contours |
| `docs/assets/thy666-icon.svg` and size exports | App/site icon and small avatar | Deliberate compact adaptation, not another header logo |
| `docs/assets/thy666-banner.svg` and PNG | Repository banner | Keep editable source and verified generated export together |
| `docs/assets/branding/signal-labyrinth-oled/` | Small device logo and source pixel map | Pixel-specific asset, independent of CSS ornament |
| `web/assets/fonts/manifest.json` and font licenses | Exact local Oxanium, Orbitron and Monoton faces | Reuse shared typography; no remote font requests or duplicated face declarations |
| `web/ui-system.js` | Interface navigation, action and result glyphs | Current-color, named SVG paths; keep accessible names on controls |
| C-rendered help frames and authored board diagrams | Musical examples and control instructions | Retain provenance and synthetic labels; never substitute fake live telemetry |
| `site/assets/client-preview.png` | Public disconnected Client illustration | Static marketing example; refresh intentionally after a settled layout change |

The audit found no need for another logo, raster HUD background or font family.
Decorative geometry stays in code so it adapts to width, theme and motion
preferences. Historical workbench appearance remains a compatibility concern,
not a source for new Client variants.

## Visual grammar

- **Field:** pure black by default. Light appearance uses warm, muted paper
  `#e5e2da` with darker accents and stronger apparent strokes. Musical pad colors
  retain their musical meaning. The pixel OLED remains true to hardware.
- **Weights:** 1 px structure, a light hover trace, 2 px focus/corner accents and
  a retained 3 px leading selection segment.
  Secondary rules are quieter, not extra grey surfaces.
- **Geometry:** 8 px section cuts, 12 px task/dialog cuts and 6 px action cuts.
  Diagonal rises remain fixed as the component grows. Never scale a corner with
  a percentage of a wide card.
- **Layering:** an outer trace establishes the group; a shorter bright segment
  marks emphasis. Opposite corners deliberately have unequal accent lengths.
- **Width:** rails consume available space beside captions. Use intrinsic text
  width and flex/grid, not character counting, screenshots or text-pixel scanning.
  The OLED has its separate cheap C text-measurement primitives.
- **Rhythm:** spacing tokens are 4, 8, 12, 16, 24, 32, 48 and 64 px. A task gets
  more breathing room than an inset detail. Related labels and values stay close.
- **Type:** Oxanium for reading and values, Orbitron for controls and section
  labels, Monoton for a few large titles. Never use the display face for prose,
  a number table or a long button. Preserve music/symbol fallback fonts.

Useful text must remain readable at normal size and 200% zoom. Decorative ruler
labels may be small; necessary units, state and instructions must not rely on
microtext. Prefer fewer labels to shrinking a crowded interface.

## Component library

Use native HTML first. These are styling and icon primitives, not a new framework
or a replacement for existing dialog, routing and device ownership behavior.

| Component | Use and variants | Decoration slots |
| --- | --- | --- |
| `.hud-frame` | Section or data group; `--task`, `--inset`, `--dialog` variants | Owns `::before` and `::after`; never clips descendants |
| `.hud-action` | Normal action; `--primary`, `--quiet`, `--danger`, `--compact` | One generated SVG perimeter; idle, hover and selected strokes share its path. `.button` is a migration alias |
| `.hud-icon-button` | Menu, expand/collapse, close, settings or another named icon action | Split SVG outline; `createUIIcon` and `createUIIconCorners` supply the glyph and hover triangles |
| `.hud-sidebar` / `.hud-sidebar-rail` / `.hud-rail-toggle` | Sidebar divider with its collapse/expand tab | One bounded SVG divider; hover or keyboard focus traces its whole length; the arrow follows `aria-expanded` |
| `.hud-choice` | Backup/restore, firmware or another substantial choice; `--nav` is the quieter navigation/sidebar variant | Same perimeter in every state; the retained 3 px leading segment distinguishes selection from hover |
| `.hud-field` | Native input/select/textarea; label, help and error companions | No pseudo slots; keep browser keyboard behavior |
| `.hud-toggle` / `.hud-toggle-label` | Native checkbox acting as an on/off switch, with its clickable label | Keep the actual input, checked state and keyboard activation; never substitute a clickable div |
| `.hud-checkbox` | Native checkbox for inclusion, selection or an independent option | Checked, unchecked, indeterminate and disabled remain distinct |
| `.app-accent-*` / `.theme-swatch` | Shared named-color picker and preview swatches | Expanded listbox, roving focus, Escape and selected checkmark; labels accompany color |
| `.hud-rule` | Structural split rail; `--quiet` for a minor boundary | Owns `::before`; `--hud-rail-start` positions its step |
| `.hud-accent-corner` | A leading corner on an existing content boundary | Owns `::before`; use once for an intentional emphasis |
| `.hud-pattern-frame` | Restrained special-purpose label, such as the current release | Shared double broken outline; content stays readable between the rails |
| `.hud-notice` | Result or actionable message, optional `data-tone`; `--banner` is the slim global message | Banner has a static notched outline, 48 px minimum height and normal 44 px dismiss target; longer text wraps |
| `.hud-chip` | Noninteractive mode/status, optional `data-state` | No pseudo slots; never style a chip as an action |
| `.hud-disclosure` | Native `details` for optional depth | Summary indicator only; preserve native open/closed semantics |
| `.hud-meter` | A measured fraction with an accessible label and unit | Native `meter`; missing data is unavailable, never zero |
| `.hud-scroll-region` | A bounded, named scroll viewport such as an index or report | Shared scrollbar colors; make a region keyboard focusable when its content needs scrolling |
| `.hud-type-display/control/body/mono` | Explicit typography roles for titles, controls, reading and raw values | Same licensed fonts and fallback coverage as the production app |

Do not combine frame and icon-button classes on the same element: they express
different roles. Apply a frame to a wrapper when a group needs its own boundary.
Feature styles set padding and grid layout; they must not replace the shared
border recipe or introduce another cut size for the same role.

### Geometry and component lifecycle

Call `initializeUIComponents({root})` once for a page or application root. It
returns `refresh()` and `destroy()`. One size observer handles created/resized
controls; one mutation observer discovers additions and releases removed
controls. There is no animation loop, periodic measurement or per-button timer.

`uiOutlineGeometry(width, height, {kind, origin})` supplies the same perimeter to
all strokes of a component. `data-trace-origin="top"`, `"bottom"`, `"corner"` or
`"left"` changes where the trace starts, never the outline itself. Left begins
up the leading edge. A 1.5 px inset contains the strongest 3 px segment inside
the component's hit box. Hover and selection
must not switch to a different path or add a second CSS border. The generated
`.hud-outline*` and `.hud-traced` classes are internal decoration, not new
variants for a feature to assemble manually.

Dither uses its own generated `.hud-dither-grid` child. It does not reuse a
frame's corner pseudo-element or place an animated pattern over its text.

`createUIIcon(name)` creates a decorative glyph from `UI_ICON_NAMES`.
`createUIIconCorners()` supplies an icon container's opposing hover triangles.
The triangles sit close to the notched outline, within its corner cutouts; they do not cover
the glyph or fill its cut. Keep accessible names on the
button/link, not on these decorative children. The settings glyph uses a
symmetric eight-tooth gear with a circular hub.

The sidebar tab uses the shared `rail` outline: a square top-left corner, fixed
8 px cuts at top-right and bottom-left, and an open
trailing edge. The sidebar divider completes that edge. Keep the tab smaller than
page choices and aligned with the divider in both expanded and compact states.
Hover or keyboard focus continues the highlight along the entire divider, with
no movement of the line or its fixed cuts. Reduced motion makes the change immediate.
Its 44 px target, accessible name and expansion state remain available when its
visible label is hidden. The component reference includes a working local example.

The banner's `notice` geometry is static: 8 px corner cuts and centered 4 px
notches supply the shared language without treating the whole message as a
button. Only its real dismiss control gets interaction feedback.

### Inventory and coverage contract

The reference publishes a machine-readable `component-contract` JSON block in
`web/design-system.html`. It maps each family to its shared selectors, production
owners, example anchor and supported states. The visible production inventory is
rendered from that same block. Intentional instrument/window exceptions have
their own owners and a reason for retaining specialized geometry.

`tests/web/design-system.test.mjs` checks that every shared `hud-*` selector is
registered or explicitly internal, each public helper is documented, every
public variant has a real example and a production use, and specialized owner
paths are current. Generated glyph/outline classes are exercised through their
factories rather than duplicated as handmade SVG. The icon catalog follows
`UI_ICON_NAMES`, so adding an icon automatically adds its specimen.

A new style requires both an appropriate production use and an example. Do not
add a class to an unrelated element merely to satisfy coverage. If a variation
has no meaningful consumer, remove it or keep it outside the shared library
until its purpose is established.

Default action targets are at least 44 px high. Compact actions become 44 px on
small screens. The physical board replica and its tightly grouped display tools
are deliberate specialized controls; their geometry must preserve the device,
and expansion/mobile layout must keep them usable.

## States and semantics

| State | Visual cue | Required behavior |
| --- | --- | --- |
| Rest | Quiet outline and a short accent | Clear accessible name |
| Hover | Brighter outline or extended accent | No content/layout movement; touch does not depend on hover |
| Keyboard focus | Separate 2 px outline outside the control | Never clipped or obscured; focus returns after a dialog closes |
| Pressed | Firmer continuous outline | Native activation, no artificial delay |
| Selected choice | Retained thick emphasis segment plus a light complete outline | `aria-current` for navigation, `aria-pressed` for a toggle, `aria-selected` for listbox options; an expanded task choice also retains its marker |
| Expanded | Explicit expanded state and indicator | `aria-expanded` plus `aria-controls`, or native `details` |
| Disabled/unavailable | Reduced emphasis and named reason nearby when needed | Prefer native `disabled`; an ARIA attribute alone does not prevent activation |
| Busy | Stable geometry and an action-specific label | `aria-busy`; disable only conflicting operations, not unrelated polling |
| Invalid/error | Error text, icon and error accent | Associate field error with `aria-describedby`; keep the recovery action visible |
| Success | Result text/checkmark, optional success accent | Report the actual completed result; animation never marks a process complete |

Keep each instruction and result in one place. Protocol details, identifiers and
raw telemetry belong in contextual disclosures. Consequential errors and update
uncertainty remain visible. A visual refactor must preserve confirmation defaults,
focus trapping, Escape behavior and asynchronous-operation boundaries.

Use a retained marker where a current choice helps orientation. Sidebar and
parameter rows and the active backup/restore flow start up the left edge. Main
navigation, local tabs and category selectors start along the bottom edge.
Primary actions retain a short importance accent. Quiet actions and icon-only
controls must not acquire an unrelated sidebar-style marker. Hovering a current
choice never hides its thick segment; focus remains an independent outer ring.
Toggle actions keep their active outline without becoming navigation rows.

Use `.hud-notice--banner` for brief global feedback. Keep the roomy default notice
for an explanation attached to a task. A wrapped banner grows with its text;
never truncate an error to force a compact height. Dismissing an example returns
focus to a stable control, and no message disappears on an automatic timer.

## Motion

| Role | Token / timing | Use |
| --- | --- | --- |
| Immediate feedback | `--hud-motion-fast`, 140 ms | Color and border response |
| Interaction | `--hud-motion-interact`, 180 ms | Corner fades and disclosure indicators |
| Outline trace | `--hud-motion-trace`, 360 ms | Continuous reveal from a chosen perimeter origin |
| Panel entry | `--hud-motion-panel`, 260 ms | One small settling motion when a panel mounts |
| Connection | `--hud-motion-connect`, 700 ms | Coordinated board, trace and widget assembly once per connection |

Use `--hud-ease` for settling. The cockpit's longer ambient cycles are a deliberate
specialized layer: restrained decorative arcs and traces while connected. Real
playing can brighten input accents; text, controls and measured values stay still.
Do not fabricate output, per-voice MPE data or playheads to make motion interesting.

No `transition: all`, layout animation, per-widget timers or entry replay on a
poll. Existing cockpit DOM pools and changed-value updates remain bounded. Hidden
pages/native-inactive windows pause the ambient layer. Reduced motion removes
entrances and fades; all information and actions remain available immediately.

`.hud-motion-trace`, `.hud-motion-thicken` and `.hud-motion-dither` identify the
three interaction roles demonstrated by real production components: reveal a
boundary, strengthen its weight, or expose a restrained pattern. Apply motion
to decoration; text, focus targets and measured values must not flicker. The
reference's combined-state example uses actual pointer/keyboard events and
native states, so hover + focus + selection can be inspected together.

## Intentional exceptions

- The instrument replica preserves 96 pads, 43 controls, musical colors and
  physical mapping; it is not a collection of generic form buttons.
- OLED previews come from the C renderer. Styling the Web screen does not change
  hardware pixels or invent musical state.
- The connected cockpit has richer structural traces because it represents the
  live instrument; disconnected mode remains an inactive wireframe.
- Reading articles use open sections and figures rather than boxing every
  paragraph. Quiet table rules aid scanning without competing with section rails.
- Desktop window clipping, resize hit areas and transparent corners are owned by
  `desktop-chrome.css`; generic frame geometry must not alter them.

## Review and maintenance

For a new variation, first identify its role in the table above. Extend a named
variant only when an existing role cannot express it. Include its states in the
component reference and update this guide in the same change.

Review the reference and affected real pages at desktop, tablet, 393 px and 320 px,
in dark/light appearance and more than one accent. Check keyboard order, visible
focus, long labels, unavailable/busy states, 200% zoom and reduced motion. Inspect
the actual site under a project subpath and the packaged Client; source inspection
alone cannot establish rendered contrast or absence of clipping.

Run the relevant browser regressions and site/desktop/standalone packaging checks.
Shared font/asset checks preserve upstream notices. Visual software validation
does not qualify firmware or authorize device operations.
