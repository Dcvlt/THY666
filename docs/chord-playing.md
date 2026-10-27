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

**Chords → Voicing** selects between the existing I/II/III prefix-up
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

## Automatic voice leading

Select **Chords → Voicing → Auto leading**. The first successful left chord
keeps its source register. Subsequent chords choose octave placements near the
previous successful source voicing. This is optional and defaults to Inversions.
It is available in the shared core; device timing and musical feel still need
hardware qualification.

For example, C–E–G at 60–64–67 followed by F–A–C at 65–69–72 becomes
65–69–60. The common C stays at 60, while the other voices move by one and two
semitones. Held notes keep their original pitches and release destinations.

The bounded search compares ascending unique voices across cyclic inversions,
minimizing total movement; ties favor the authored register. Different chord
sizes match voice ranks proportionally. Duplicate source slots stay duplicates,
inactive slots stay inactive, and only octave placements change. It does not
choose a new chord or substitute a harmony.

Auto leading replaces manual Inversions/Drop voices; I/II/III and the context
inversion action are inactive in this mode. Spread, bass octave, transpose,
octave copies and strum still apply afterwards. History therefore follows the
source voicing before these effects, not a reconstruction of the final layered
MIDI output. Previewing a pad does not advance history; failed attacks, empty
chords and MPE glide-only gestures do not replace it. Releasing a chord retains
history. Changing key/scale, chord layout/family/set or voicing mode, restoring
settings and Panic reset it.

Settings v14 introduced this policy; current v15 also supports the register modes below. The codec saves the policy, never its transient history. Earlier settings
retain their original inversion/drop behavior. Older firmware cannot import a
v15 settings export.

## Octave fold and guitar register

**Chords → Voicing → Octave fold** places enabled source pitches into one octave
starting at the lowest enabled source note, capped at MIDI 116 so the octave
fits below MIDI 128. It preserves pitch classes, slot order, duplicate slots
and inactive holes. Octave doublings can become the same pitch. This is an
optional compact voicing; it does not change the archived chord data.

**Guitar register** assigns each distinct chord pitch class to a separate string
in standard E2–A2–D3–G3–B3–E4 tuning, using frets 0–11. A bounded search minimizes
total fret numbers, then register; unused strings are muted. Duplicate pitch
classes share their selected pitch. A chord with seven distinct pitch classes
passes through unchanged so no tone is silently discarded. This supplies a
guitar-like register, not a guarantee of comfortable human fingering. The
right-pad Guitar layout remains an independent setting.

Like Auto leading, these modes replace manual I/II/III inversions; those buttons
and the context inversion action are inactive. Spread, bass octave, transpose,
octave layers and strum still apply afterwards and can move notes outside the
compact/guitar register. All changes affect future attacks. Held pitches and
release routes remain unchanged, and Live Update follows the resolved chord.
Settings v15 stores the selection in the existing policy byte. It accepts older
v1–v14 data with their original limits; older firmware cannot import v15 exports.

## Harmony guidance in the Client

Open **Client → Harmony** in the browser or desktop app. This page also works
without a board or the private musical archive. Its input is a local sketch,
initially C major with C–E–G selected; it does not listen to the board or infer
its current key. It neither plays notes nor changes device settings. The C core
performs the analysis; JavaScript renders results and holds the unsaved sketch.

1. Choose a **working key** and select the pitch classes you want to explore.
2. **Chord suggestions** lists supported in-key chords, excluding
   the exact selected set. Familiar triads/sevenths rank before extended shapes;
   within each tier shared-tone count ranks first, and tonic and fifth-root
   options break ties. Click a chord to replace the sketch notes and explore
   another move. This is common-tone guidance, not a functional-harmony model
   or a claim that the highest-ranked chord is musically best.
3. **Scale discovery** finds collections containing every selected note. It
   searches 99 merged families in all twelve roots, showing up to 24 matches
   ranked by fewest extra notes and reporting the total. The families include
   the seven major modes, harmonic and ascending melodic minor, major/minor
   pentatonic, blues, whole tone, both diminished collections and chromatic.
   The merged interval vocabulary combines the original collection with the
   extended scale/chord collection; exact duplicate interval sets share one
   family and aliases remain in the local model. The board's existing 70-family
   playable catalog and its saved IDs/layouts remain unchanged. Modes with identical notes remain separate possibilities; sparse
   input cannot establish an intended tonal center. Silence produces no match.
4. Select a match to compare it in **Modulation**, or choose a destination
   directly. The comparison identifies common, introduced and removed notes,
   whether the sketch fits both collections, and a possible shared major,
   minor or diminished triad. No pivot is invented when none fits. The pivot
   is a common-tone option, not a guarantee of a convincing modulation.
5. **Use destination as working key** changes this local sketch only. Change
   the board through Parameters separately when desired. Sketches are not
   saved or included in backups.

For C major → G major, the comparison retains C–D–E–G–A–B, introduces F♯,
and leaves F behind. C major is one possible shared chord. C major → A natural
minor keeps the same notes but changes the tonal center, so the guidance calls
out that distinction. Pitches use sharp spellings for compact presentation;
octave/register information is deliberately absent from this pitch-class tool.

These tools have native C and browser regressions. The Client sketch remains
independent of the board's Harmony menu; neither automatically transforms music.

## Harmony guidance on the board

Open **Chords → Harmony guidance**. The source key is a snapshot of the
configured root and effective scale (including a custom scale), taken on entry.
Reopen the menu after changing the key. Live Update's temporary right-hand pool
does not replace this source key. Captured notes start empty on every entry.

| Control or action | Behavior |
| --- | --- |
| UP / DOWN | Move through tools or results; in destination editing, change the value. No hold-repeat. |
| RIGHT | Open a tool, capture notes, or enter/finish destination editing. Selecting a discovered scale opens its modulation comparison. |
| LEFT | Leave destination editing, return to the tool list, then return to Settings. |
| CENTER | Immediately return to Play from every guidance screen. |
| Add held notes | Add the pitch classes of currently held physical owners and held Arp inputs. Repeat across chords to discover scales for a progression. |
| Use selected chord | Replace the captured set with the last selected left pad's current resolved preview, including voicing/transpose. This also works with direct chords muted; it is a preview, not a capture of a previous sounding attack. |
| Clear captured notes | Empty the analysis input without affecting sounding notes. |

Physical pads and performance controls keep their ordinary roles. No pad is
repurposed to audition or select a suggestion. Held notes keep their original
release routes; generated Arp/sequencer output and released sustain tails are
excluded from **Add held notes**. Capturing includes the complete admitted
physical chord even while a strum is still delivering its notes. MPE silent or
glide-only gestures without a physical note owner are not independent evidence.

Chord suggestions show their pitch classes below the selected result. Scale
search shows the top 24 matches, preserving separate modal identities. Modulation
lets you edit the destination root and family, inspect common/introduced/removed
notes, and inspect a shared pivot triad. Browsing or selecting advice does not
apply a root, scale, chord mapping, voice-leading history, library record or MIDI
message. Apply musical changes separately through the existing controls/settings.
The 16 advisory families are not interchangeable with archival catalog IDs.

Calibration, library editing and sequencer editor entry cannot overlap this
browser. An open Arp quick page closes when guidance opens, but Arps keep playing.
Settings restore closes guidance and clears its transient snapshot. Nothing from
this browser is saved or included in backups. Native control/navigation and
ownership regressions pass locally; board timing and hardware qualification
remain pending.


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
is described in [Client](companion-and-backup.md). The current custom library
is bounded and saved with settings; it does not depend on named collections to
survive reboot.
