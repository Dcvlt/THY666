# Reset a menu's options

Every settings list ends with **Reset options**, marked by the same restore-arrow
icon. Track settings have their own entry, separate from the global Sequencer
settings. Engines is a launcher with no settings to reset.

1. Select **Reset options** and press RIGHT. Nothing is reset yet.
2. The confirmation names the menu and captured editing scope. **Cancel** is
   selected initially. UP/DOWN switches between Cancel and Reset options.
3. Press RIGHT to choose the highlighted action. LEFT cancels; CENTER cancels
   and returns to playing.

Release all pads and controls and stop transport before confirming. A held note,
active recording, pending gesture or busy musical owner blocks the whole reset;
it never applies only some of the defaults. The RIGHT edge used to confirm is
allowed. A rejected reset leaves the confirmation open for retry or cancellation.

The scope is captured when the confirmation opens. Changing an editor selector
cannot redirect the reset to another physical-half scope, Arp, view, route or
track. The connected Client uses this same C confirmation and a transaction identity; a stale browser
dialog cannot confirm a newer or different reset.

## What each menu resets

| Menu | Scope and defaults |
| --- | --- |
| Performance | The captured Settings for physical scope: Fixed mode, default velocity 96, ordinary Aftertouch/stage OFF, bend OFF/range 2 and MPE output Pressure. The other half and all MIDI route policies are retained. Global context controls return to defaults and MPE turns OFF for the whole board. |
| Chords | Key, scale selection, layouts, voicing, transpose/octaves, Live Update, strum and the shared key-contrast switch. Saved user scales/chords/sets are retained. |
| Arps | The selected Arp's configuration and the menu's Quick M controls option. The other Arp is retained. |
| Sequencer | Global tempo and start timing. Tracks and recorded content are retained. |
| Track settings | Only that track's configuration, input mode and recording-region options. The default loop is 16 sixteenth-note steps. All events retain their pitches, durations, timing and identities, including material outside the restored loop. |
| MIDI | Global routing/clock/input options and the captured route's channel/outputs. Retained route velocity and expression values, Performance overrides and other routes are unchanged. |
| Expression | The default MPE configuration, with MPE disabled. No receiver-setup messages are sent. |
| Sensitivity | The selected physical cluster's inherited response/velocity/aftertouch defaults and button response. If a single pad is selected, it identifies that pad's physical cluster. Individual overrides, calibration tables and learned guards are retained; use the separate explicit pad-inheritance action to remove a pad override. |
| Lights | Factory note palette, contrast, idle/held brightness, highlight/fade and center brightness. |
| Views | The captured view's information/header/motion options and global UI animations. The selected view and other views' independent preferences are retained. |
| Controls | Default Play shortcuts, the whole-chord octave-layer button policy and the selected upper-button scale assignment. Other scale-button assignments are retained. |
| Profiles | Editable menu fields. This does not delete a library, song or calibration profile. Named profile collections remain a separate unfinished feature. |
| System | System options, including turning cluster inversion OFF. Songs, the musical library and physical calibration remain intact. |
| Harmony | Harmony preferences and its shared voicing selection. The working key and captured base chord are retained. |

Aliases refer to one setting: resetting the Chords key-contrast option also
changes its Lights entry; resetting Harmony's voicing changes the Chords voicing
entry. There are no competing copies.

Confirmed changes use ordinary settings persistence. Wait for **Saved** before
powering down. A menu reset does not erase flash, install firmware or delete
musical content. These reset workflows are host-tested and await physical-device
qualification.
