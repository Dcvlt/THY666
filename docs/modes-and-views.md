# Play views

Settings → Views selects the Play presentation and its independent options.
**Simple** is the factory default. Views change presentation, not musical engine,
layout or output. The C renderer is shared by device and browser.

| View | Presentation |
| --- | --- |
| Detailed | Configurable header and six independent information rows |
| Simple | Musical heading and octave note names, large for common chords |
| Piano | Keyboard with available/active note feedback and musical heading |
| Chladni | Integer nodal pattern driven by sounding pitches |
| Orbit | Pitch-class ring and moving harmonic constellation |
| Waves | Bounded harmonic trace selected by sounding notes |
| Circle of fifths | Fixed fifths order, root marker and chord connections |

Simple supports all sounding notes within its bounded display, with compact
text/overflow indication for large sets. Five notes use equal-size corner pairs
and one center note. Chord names appear above note/keyboard content; a release
clears them when ownership ends.

## Configurable information

Compact/graphical views have two independent information slots. Detailed has
six independent rows. Choices include Off, Musical names, Velocity, Counts,
Notes, Scale, Channels, Outputs, Modifiers, Pressure, Octaves, Clock and Arps.
Each view retains its own header/slot/motion preferences. The first modular
information line defaults to musical chord/note names.

All seven Play views share the dotted, notched frame beside the status icons.
Its top and bottom rules follow the width of their text. Detailed keeps five
information rows inside the frame and its sixth row below the lower rule.

Velocity reflects per-side/zone settings rather than one misleading global
number. Counts distinguish held physical pad owners from unique sounding
pitches; sending one pitch to multiple channels does not multiply its pitch
count. DIN/USB readiness and enabled MPE/Arp modes have compact side indicators.

Simple, Piano and Fifths preserve the union of admitted Arp input pitches between
gates using stable dim dithering. Sounding owner pitches brighten. Layer notes,
other live notes and simultaneous generated output remain bright while owned.
The display does not advance timing or consume MIDI output events.

## Motion and limits

Graphical motion can be disabled per view or through global animation settings.
These are MIDI-driven visualizations, not audio analysis or physical plate
simulations. Fifths labels remain fixed in musical order while active notes
pulse/connect and the root marker moves.

Stored identities reserve the retired Routes/Layouts views for migration.
Chladni retains the former Pretty identity. Changing names never silently
reinterprets a saved view as another visualization.

The OLED is binary monochrome. Dimming uses dithering rather than extra pixel
colors. Panel transport and browser polling have separate schedules; see
[Display](display.md) and [Client](companion-and-backup.md).

## Pad and control lighting

Lights & Display separates musical pad colors from OLED presentation. Note
lights enable resting colors. Idle and held brightness are adjustable, with
held highlights in the note color or white and an optional fade. A released pad
returns to its resting state; switching note lights off leaves center markings
available.

The left **Chord color source** can follow harmonic root, bass note or fixed
scale degree I–VII. The right follows the resolved pitch class. **Classic** and
**Distinct 12** are selectable palettes; nearby hues may still appear similar on
physical RGB hardware. **Black-key contrast** dims accidental pitches by a
configurable percentage rather than assuming color alone distinguishes them.

Center brightness controls the white markings. Voicing, Spread, Strum and lower
buttons reflect enabled state with brightness, following the selected lower-
button mode/scope. Play shortcut arrows hide their marking when configured Off.
These indicators derive from applied C state, including in the client.
