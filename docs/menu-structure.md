# Menus and navigation

CENTER opens global Settings from the active workspace and returns to that
workspace from menus. Track settings return to their sequencer editor. RIGHT enters a page, edits a setting, applies a draft or runs the
selected action. UP/DOWN moves selection or changes the draft. LEFT cancels an
unfinished edit; otherwise it returns one level.

Apply with RIGHT before using CENTER to leave. An unapplied value is discarded
on quick exit. Each category remembers its selection. Numeric editors support
held UP/DOWN repeat; invalid changes leave applied state unchanged and explain
the reason.

## Sections

| Section | Concern |
| --- | --- |
| Engines | Play, Harmony, Sequencer, Arp and Drums; nested engine settings below the separator |
| Performance | Velocity scope/mode, defaults, MPE and response applications, octave/context controls |
| Chords | Root/scale, note and chord layouts, Live Update, voicing, strum and custom musical library |
| Arpeggiators | Two instance configurations, source grouping, timing, patterns and output |
| Expression | MPE zone, per-voice pressure, glide and receiver setup |
| Sensitivity | Cluster/per-pad response, calibration, Aftertouch profiles and advanced thresholds |
| MIDI & Routing | Pad route profile, side/zone, channels, destinations, clock and USB input |
| Lights & Display | Note palettes, brightness, held feedback, key contrast and center lights |
| Views | Play view, independent information slots/rows, headers and motion |
| Controls | Play arrow shortcuts and lower-button mode/scope |
| Profiles | Collection features not yet implemented |
| System | Storage use, build/about/credits and available system controls |

Broad choices appear before a thin separator; precise/advanced controls follow.
The separator is visual, with no selectable blank row. Unavailable information
opens only after RIGHT; merely highlighting it never changes pages.

## Playing while editing

Settings do not stop live music. Existing notes retain their captured route,
velocity and release. Apply response changes only after affected pads release.
Connected companion editors use the same target identities and validation.

Play arrows are configurable shortcuts. Factory LEFT opens Engines, UP opens
Views, DOWN opens Performance and RIGHT opens Chords. A hidden shortcut has no
action/backlight. CENTER always remains the Settings/workspace control.

Sequencer track options are a dedicated page reached with RIGHT from the step
editor. LEFT returns to that editor and step; its left step grid and right
playing surface remain active. See [Sequencer](sequencer-interface.md).

The schema in `web/studio-menu.js` generates C metadata. Normal menus expose
working bindings and explicit information; future design rows are not claims
of implemented behavior. See [Development](development.md) for regeneration.

Harmony and global Sequencer settings are under **Engines → Settings**; opening
these menus does not switch the active engine. System is always the last global
section. Engine selection uses **Active/Select**, menus use **Open**, and value
edits use **Apply**. Arp opens a processor page without replacing the workspace.
