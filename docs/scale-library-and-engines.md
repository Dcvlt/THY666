# Engines, scales and layouts

**Engines** is the first Settings section; LEFT from Play opens it by default.
The order is **Play → Harmony → Sequencer → Arp → Drums**. The selected workspace
shows **Active**; RIGHT on **Select** changes the workspace. Leaving settings
returns to that workspace. Arp uses **Open** because it is a processor.
Drums opens its development description; highlighting it does not leave the list.
Below the separator, **Settings** opens Harmony and Sequencer settings without
changing the active engine. Engines has no musical parameter rows or Reset entry.

Play unifies chord/scale/chromatic playing. Layout is an independent choice,
not another engine. Arp is a processor alongside Play: its Engines shortcut
enables the selected instance and opens the quick page without replacing the
musical playing layout. Opening Sequencer does not start transport.

Harmony retains the ordinary chord map by default; optional Exploration reserves
nine pads for suggestions. It adds temporary melody contributions and explicit
destination-key previews. Fifths is the default view, with Focus, Suggestions
and Details alternatives and contextual controls; see
[Harmony](harmony.md). Arps remain processors alongside it.

System → **Swap clusters** exchanges musical roles for left-handed playing.
Chord/editor pads move to the right and note/input pads to the left. Physical
sensor calibration, cluster sensitivity defaults and per-pad overrides stay
attached to the same sensors. Change only with all input released and transport
stopped. Upper-panel labels and root/scale control positions remain physical.

## Scale library

The imported catalog has **70 scale families**, selected through the shared scale
picker. Root and Scale are available with musical setup in Chords; Controls
provides upper-button assignments. The twelve small LEFT keys select roots C–B.
Changing key/scale affects future notes; existing owners keep their releases.

Eight saved custom scales are separate from the imported library. Edit and save
them through Chords → Edit scales, then choose Custom scale or assign them to a
shortcut. See [Chord playing](chord-playing.md).

## Right layouts

| Layout | Mapping |
| --- | --- |
| Library | Original serialized note assignments; holes remain silent |
| Scale degrees | Consecutive degrees of the current note pool |
| In-key notes | Chromatic geometry with out-of-pool positions silent |
| Chromatic | Consecutive semitone columns |
| All notes | Chromatic mapping with every note available and illuminated |
| Guitar | Frets across columns, four standard-tuned string rows |
| Thirds / Fourths | Transposed interval rows |
| Piano | Complementary black/white rows, repeated one octave lower |

Guitar selects E–A–D–G, A–D–G–B or D–G–B–E as its four-string window. Piano
stays anchored to its recognizable C-based geometry; explicit transpose/octave
still applies. Live Update can change scale-based note pools; see Chords.

**Key contrast**, beside Right layout and also in Lights, dims the five actual
sharp/flat pitch classes by default. It is one saved ON/OFF preference shared
by Chromatic, All notes and Harmony. Lights → Black-key level sets its strength;
OFF restores uniform resting brightness. Held feedback remains independent.

## Upper-right assignments

Controls → Edit shortcut chooses any of twelve upper-right controls. Assigned
scale selects an imported/custom scale or **Cycle layout**. Editing an assignment
does not itself switch the current scale. Assignments persist.

| Control | Factory assignment |
| --- | --- |
| 5 | Pentatonic Major |
| 12 | Cycle layout |
| M1–M7 | Major, Dorian, Phrygian, Lydian, Mixolydian, Minor, Locrian |
| U1/U2/U3 | Harmonic Minor, Melodic Minor, Pentatonic Minor |

The Arp quick page can temporarily override M controls, and Sequencer uses its
own contextual map. Leaving those pages restores the normal assignments.
