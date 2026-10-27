# Physical controls

The panel contains two 4 × 12 musical pad clusters, 24 upper controls and
19 center controls around a 128 × 64 OLED. The browser replica preserves this
arrangement.

## Upper controls

The left twelve controls choose chromatic root C through B. The right labels
are `5, 12, M1–M7, U1–U3`. In ordinary Play their scale/layout assignments are
configurable in Engines; see [Scales and layouts](scale-library-and-engines.md).

Arps can temporarily use M1–M7 on their quick page. Sequencer upper controls
instead select recording/editing/transport actions while that workspace is
open. Leaving the workspace restores ordinary assignments; a held control is
not reinterpreted as a new press.

## Center controls

The navigation row is LEFT, UP, CENTER, DOWN, RIGHT. CENTER opens Settings or
returns directly to Play. RIGHT enters/edits/applies; LEFT backs out/cancels.
UP/DOWN selects or changes values. See [Navigation](center-navigation.md).

The fourteen function controls comprise three voicing buttons and Strum,
three Spread buttons, and seven lower chord controls. Their brightness reflects
active state at the configured center brightness. Inversions and Drop voices
are alternative voicing policies; lower buttons default to Octave layers.
See [Chords](chord-playing.md) and [Lower controls](lower-chord-controls.md).

## Large pads and context controls

Play uses the left cluster for chords and the right for notes. Logical IDs are
column-major: `side × 48 + column × 4 + row`. Hardware acquisition uses an
explicit electrical-to-logical map; display order never substitutes for it.

Optional context pads replace chosen musical positions at Upper right or
Lower left. Chord-octave, melody-octave and bass-octave controls are independently
enabled. Replaced positions are controls rather than additional musical pads.

Sequencer Tracks uses only the OLED, keeping large pads silent. An opened track
uses left pads as 48 chronological steps and right pads as Notes/Chords input,
including while its dedicated settings page is open.
