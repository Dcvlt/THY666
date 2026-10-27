# Lower chord controls

The seven lower controls form a V beneath inversion/spread/strum. Their factory
mode is **Octave layers**, applied to **Whole left side**. Read along the V from
outer left to bottom center to outer right: **−3, −2, −1, 0, +1, +2, +3**.

Each lit control adds a complete transformed chord at that octave. Multiple
choices stack; overlapping pitches deduplicate and out-of-range copies are
omitted. Zero is enabled initially. Turning off the final layer restores zero
so the instrument does not become unexpectedly silent.

Settings → Controls offers:

| Option | Behavior |
| --- | --- |
| Note buttons: Octave layers | Select complete chord copies at signed octaves |
| Note buttons: Stored notes | Enable/disable the seven original source-note slots |
| Apply to: Whole left side | Apply changes across left chords; default |
| Apply to: Selected chord | Change the currently selected chord's state |

Stored-note slots may be empty in a short chord; changing an empty slot adds no
new seventh/ninth/eleventh/thirteenth. These controls are not chord-extension
constructors. In Octave layers mode they always refer to full chord copies.

Control brightness derives from the active mode and scope. Changes affect
future attacks; existing notes retain the pitches/routes required for release.
The three upper voicing buttons separately select Inversions or Drop voices
through Chords → Voicing buttons. See [Chord playing](chord-playing.md).
