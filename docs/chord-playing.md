# Chord playing and custom library

## Live Update

In **Chords**, enable **Live Update**. The right playing surface then
follows the last successfully selected left chord. Releasing that chord leaves
the selected harmony available for subsequent right-hand presses.

- **Keep key notes ON:** retain the configured scale's pitch classes, starting
  from the selected chord root or the next available key tone.
- **Keep key notes OFF:** use the selected chord's actual resolved pitch classes.
- **Mute direct chord:** select harmony without sounding the ordinary direct
  left-chord attack. Arp inputs and Pads-source recording still receive the
  chord. Existing notes retain their original releases.

Scale-degree layouts traverse the selected pool. Chromatic and Guitar layouts
quantize future pitches upward to its next available tone. Fixed Piano geometry
is the deliberate exception: its physical natural/accidental arrangement stays
recognizable. Explicit transpose and octave settings still affect Piano notes.

Already-held notes, queued strum tones and their release routes remain captured
at attack. A new chord, key, layout or menu does not retune an existing owner.
Idle note lighting follows the current map; held lighting follows that owner's
snapshot. In-key lighting uses the effective Live Update pool when following.

**Keep transpose** defaults ON. OFF resets explicit left/right transpose on an
actual root or scale change, including a different custom scale. Selecting the
same key/scale, changing a layout or selecting a chord preserves it. Settings
restore loads the saved transpose rather than treating recall as a fresh key
gesture. Live Update's selected harmony is runtime and does not restore at boot.

## Left chord layouts

Choose **Chords → Chord layout**. The root/scale and layout are independent.

| Layout | Physical arrangement |
| --- | --- |
| Classic | The lossless original seven-slot records and pad positions. |
| Chromatic | Twelve semitone columns. Rows contain three, four, five and six tones from the chosen chord family. |
| Diatonic | Columns advance through the selected scale degrees. Rows stack three, four, five and six alternating scale tones. |
| Six Default | Six quality columns in each six-column block. Rows and blocks provide eight successive scale-degree roots. |
| Six User | The same eight-root geometry, using the six saved chord templates referenced by the selected named set. |
| User Chords | Twenty-four authored records: each column's upper pair holds two records; its lower pair repeats those records one octave down. |

Chromatic families are Major, Minor, Dominant, Diminished, Augmented, Sus2 and
Sus4. Higher rows extend the explicit family; unusual scales are not forced into
conventional chord names. Six Default columns are Major, Minor, Dominant7,
Major7, Minor7 and Diminished. In each row, the left block uses one scale root
and the right block the next; subsequent rows advance by two degrees.

Six User takes each saved chord's intervals relative to its authored root and
places them on these scale-degree roots. User Chords instead preserves the
authored key-relative pitches. Records are authored relative to master key C;
the selected master root is added during playback, with the lower two rows also
subtracting twelve semitones. Empty records or six-set references stay silent.

Generated notes outside MIDI range are omitted by their validity mask, never
clamped. Original inactive slots remain distinct from sounding notes. Optional
context controls still replace their configured pad positions explicitly.

## Piano and custom scales

The right **Piano** layout uses twelve chromatic columns,
with complementary black/white row pairs. Black-key and white-key rows have complementary silent gaps.

```text
Column: C  C# D  D# E  F  F# G  G# A  A# B
Black:  .  C# .  D# .  .  F# .  G# .  A# .
White:  C  .  D  .  E  F  .  G  .  A  .  B
```

Rows three/four repeat one octave lower: 24 playable positions and 24 silent
gaps before optional context overlays and MIDI-range omissions. Root selection
keeps the keyboard anchored to C; explicit transpose/octave changes still apply.

**Chords → Custom scale** selects one of eight saved scales; **Built-in** returns
to the retained catalog selection. Custom degrees drive generated chords and
scale-based right layouts. Classic remains the lossless catalog layout.
Assign custom scales to the upper controls through **Engines → Scale shortcuts**;
the existing Cycle layout assignment remains available. A scale must be saved
before it can be selected or assigned.

## Voicing buttons and lower controls

**Chords → Voicing buttons** selects between the existing I/II/III prefix-up
inversions and **Drop voices**. In Drop voices mode these are independent toggles
that lower the second, third and fourth distinct ascending enabled tones by an
octave. Duplicate original slots at one pitch move together. Chords with fewer
distinct tones simply lack the corresponding physical toggle/indicator.

The corresponding **Lower voice 2/3/4** settings retain the policy across chords.
Live menus show the controls for the selected policy; Chord family appears only
for the Chromatic layout. A Play-screen Chords shortcut remains usable in either
voicing mode.
All edits affect future attacks. Invalid downward transformations reject the
planned attack atomically instead of clamping a low pitch.

Drop voices operates on enabled source slots before Spread, bass-octave
adjustment and octave copies. Inversions and Drop voices remain separate modes.
Spread retains its original first-slot operation; bass octave shifts the
resulting lowest enabled tone. The lower seven controls retain their selectable
whole-left/selected-chord scope:

- **Octave layers:** complete transformed chord copies at the chosen octaves.
- **Stored notes:** enable/omit original source slots. Availability follows the
  current generated/custom chord map, not a stale Classic mask.

Upper stacks apply in Stored notes mode. Octave layers use the independent
seven-layer mask, avoiding multiplication of both copy systems. One-shot strum
ordering/delay continues to use the same resolved chord and owned releases.
Independent repeating/gated/routed strummer engines remain later work.

## Editing the custom library

Open **Chords → Edit scales**, **Edit chords** or **Edit six sets**. Each browser
offers **Slot**, **Edit**, **Copy current** and **Delete**. RIGHT enters/applies;
UP/DOWN selects rows or adjusts a field. LEFT cancels a field, then a record
draft, or returns to the Chords menu. CENTER always discards the unapplied draft
and returns directly to Play. Holding UP/DOWN repeats numeric/name adjustments.

An empty scale draft starts as Major; an empty chord starts as a C-major triad;
an empty six-set starts with six silent references. The web panel can also start
a minimal root-only scale draft. Only explicit Save/Apply commits a draft.

| Record | Capacity and editable content |
| --- | --- |
| Scale | 8 slots; a name and eleven optional pitch classes above the required tonic. |
| Chord | 24 slots; a name, authored MIDI root, seven MIDI note slots and independent enabled bits. MIDI zero is a valid note. |
| Six-chord set | 3 slots; a name and six references to saved chord records, each optionally Empty. |

Names contain at most twelve printable ASCII characters. Device name entry has
twelve character positions and a final **Done** position: LEFT/RIGHT moves the
cursor, UP/DOWN cycles characters, and RIGHT on Done finishes the name draft.
LEFT at the first character cancels that name edit. Save the record afterward.

**Copy current** captures base material, not the expanded or currently sounding
performance. Scale capture copies configured degrees. Chord capture copies the
selected base seven-slot chord, excluding explicit transpose, inversions, drops,
Spread, bass/octave changes and expanded layers. Active tones and root are
normalized to master key C; original inactive slot literals are retained.
An unrepresentable source is rejected rather than clamped. Six-set capture
requires a saved selected user set and copies its references.

Save, delete and library import require released inputs, stopped playback and
quiet musical/expression ownership. A busy result retains the draft for retry.
Deletion has a separate confirmation. A selected/shortcut-assigned scale,
referenced chord, active User Chords record or active six-set cannot be removed
until its uses are cleared or that layout/selection is changed.

Save commits the record to runtime. Close the library editor and allow the
ordinary debounced idle save to finish before removing power; Save is not an
instant flash operation. Close the OLED library editor and any normal settings
draft before importing a file in the web preview.

## Files, persistence and resource limits

The local **Custom library** web panel edits the same C records, previews their
playing maps and exports/imports **THYL** files. A file contains the complete
saved library; unapplied drafts are excluded. Import validates its version,
CRC, names, masks, ranges and references before changing anything. It also
rejects a replacement that would remove an active scale/shortcut/set reference.
These are local preview files; separate device USB library-file transfer is not
implemented. Full configuration backups include the applied library.

The canonical library is **740 bytes**: eight 16-byte scale records, twenty-four
23-byte chord records and three 20-byte six-set records. The THYL header adds
sixteen bytes, producing a fixed **756-byte file**. Unused records are zeroed;
active chords retain all seven note literals and their explicit validity mask.

Applied records and ten chord-policy bytes use the layout introduced in **V12**,
retained by current **V13**. Its
5,013 parameter bytes include the frozen 4,263-byte V11 prefix, ten policy bytes
and the streamed 740-byte library. Maximum aligned payload/record sizes are
13,828 / 13,860 bytes, within the existing four-slot journal reservation.
No full library is added to the large settings configuration stack snapshot.
Record editing uses a **52-byte** caller-owned UI workspace.

No heap is introduced. Review fresh build output against the
[resource budget](firmware-budget.md) when changing capacities or workspaces.

Named song/profile collections, arbitrary custom pad assignment engines and
external-NOR allocation remain separate future work. Full applied-state backup
is described in [Companion](companion-and-backup.md). The current custom library
is bounded and saved with settings; it does not depend on named collections to
survive reboot.
