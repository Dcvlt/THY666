# Harmony

Harmony is a playable chord explorer. Open **Engines → Harmony**, play a chord,
release it, then try a highlighted next chord. The home key guides suggestions;
it does not prohibit other notes or change automatically.

## One surface, one view

All 48 chord pads are available. Columns run **C through B**, independent of the
home key. Each column offers four chord qualities. There is no reserved suggestion
area and no silent empty slot. System → Swap clusters exchanges musical roles,
while calibration remains attached to physical pads.

The Fifths view shows the retained chord, the selected next chord and a brief
relationship. Solid and open markers distinguish current and suggested tones.
All suggestions light up together; UP/DOWN selects one silently. Selection does
not play it. Play its pad to hear it and make it the new context.

| Chord bank | Four qualities per root |
| --- | --- |
| Adaptive, default | Context-ranked qualities; surviving qualities keep their rows. |
| Basic | Major, minor, dominant seventh, major seventh. |
| Suspended | Minor seventh, sus2, sus4, diminished. |
| Colour | Diminished seventh, half-diminished, augmented, sixth. |
| Extended | Minor sixth, major ninth, dominant ninth, minor ninth. |

Adaptive changes publish only when **both halves are released**. The current map
and advice stay visible during calculation. **Lock map** keeps assignments while
advice changes. Selecting an explicit bank changes its assignments after release;
it is rejected while inputs are held. Held notes always retain their original
pitches and release routes. The Client shows the current assignments.

## Finding a next chord

The engine evaluates sixteen qualities in twelve roots, independently of the
ordinary Play library. It balances the current chord, home key, common tones,
pitch-class proximity, familiar root movement, dominant resolution and recent
history. It then highlights up to nine playable choices with varied roots.

| Setting | Effect |
| --- | --- |
| Balanced, default | Familiar movement, shared tones and varied roots. |
| Resolve | Emphasizes dominant resolution and return to the home tonic. |
| Colour | Gives more room to extensions and parallel major/minor. |
| Travel | Encourages outside-key chords and secondary dominants. |
| Outside key | Increases or decreases the cost of notes outside the home scale. This is a preference, not a probability. |
| Stay in key | Restricts advice to scale notes; other pads remain playable. |
| Lock map | Keeps the current four qualities per root. |

For example, in C major, D7 can suggest G even though its F sharp is outside the
home scale; G7 can suggest C. A different direction changes the balance, not a
fixed list associated only with the key. Choosing a chord does not guarantee a
particular next choice: recent history and other settings also affect ranking.

This is a bounded rule-based explorer, not a universal model of musical quality.
The sixteen-quality vocabulary does not cover arbitrary clusters, every altered
chord or every musical tradition. Explicit banks limit advice to their playable
qualities. The home key remains unchanged until **Apply key**.

## Controls

| Control in Harmony | Action |
| --- | --- |
| UP / DOWN | Select the previous/next suggestion silently. |
| LEFT | Cycle chord banks. From key preview, return to the main view first. |
| RIGHT | Open Harmony settings. CENTER returns to Harmony. |
| CENTER | Open global Settings; leave settings for the active workspace. |
| 5 | Switch seven lower buttons between SUG and OCT. |
| Lower buttons, SUG | Select suggestions 1–7; UP/DOWN also reaches 8–9. |
| Lower buttons, OCT | Toggle chord octave layers. |
| 12 | Switch note pads between Chromatic and Chord tones. |
| U1 / U2 / U3 | Octave down / up / choose chord or note target. |
| Upper C–B / M1–M7 | Preview destination root / assigned built-in scale. |

Inversion, Spread and Strum keep their musical roles where the selected voicing
policy permits them. Context shortcuts apply only in the Harmony workspace.

## Note input and voice leading

Chromatic keeps every semitone. Chord tones follows the retained chord and uses
the current key before a chord is played. Neither mode retunes held notes.

| Melody input | Analysis |
| --- | --- |
| Live additions | Shows held note additions immediately. A gesture held at least 180 ms enters the ranking context; new advice publishes after release. Short passing taps do not replace that context. |
| Melody only | Plays notes normally without adding them to harmonic analysis. |
| Manual capture | Add held notes captures and freezes a deliberate snapshot. |

Live labels clear on release; accepted analysis remains until the next chord or
accepted gesture. Freeze chord pins analysis. Clear input removes context/history.
Generated Arp steps, backing tracks and released sustain tails are not new input.

Voice leading is the shared Chords policy. **Auto leading** places voices near
the previous chord within a bounded register; it does not choose chord identities.
Leading bass and Register low/high constrain it before explicit transpose and
octave layers. If all voices cannot fit, the source voicing remains intact.

## Settings, light and key changes

Use RIGHT, or Settings → Engines → Settings → Harmony. Basic choices appear
above the separator; contrast, motion, register, destination and capture tools
follow. Reset options requires confirmation.

Pad hints distinguishes ordinary, current, suggested and selected chords.
Hint contrast never makes ordinary available chord pads completely dark.
Held feedback takes priority; global Lights OFF still applies. Motion OFF stops
the selection pulse. Hints OFF retains colors matching the actual pad map.

Root/scale controls preview a destination silently. Review Dest root and Dest
scale, then choose Apply key explicitly. Only supported playable scales can be
applied, affecting future attacks. Client Scale discovery and key comparison
remain auxiliary tools. Shared notes alone cannot identify an intended tonal
center: C major and A natural minor, for example, share pitch classes.

The Client projects the same C map, history and advice. Its pad clicks inspect;
selection is silent. It requires an explicit compatible device connection.
Settings V18 saves preferences, not running history or adaptive assignments.
Older settings migrate without changing songs, library or physical overrides.

## Design references and qualification

The design draws on interactive exploration in [Conchord](https://link.springer.com/chapter/10.1007/978-3-319-46282-0_15)
and [D’accord](https://doi.org/10.1145/2991145), transformational relationships
in [Cohn’s work](https://music.arts.uci.edu/abauer/3.1/notes/Cohn_Neo-Riemannian%20Operations97.pdf),
and the distinction between acoustic/contextual factors discussed by
[Harrison and Pearce](https://www.marcus-pearce.com/assets/papers/HarrisonPearce2020-MP.pdf).
The integer pitch-class centroid and rules here are a small engineering
approximation, not a reproduction or validation of those models.

Board testing confirmed useful contextual exploration, visible resting pads and
distinct suggestions at Hint contrast 100, clean DIN releases, and a held melody
note staying steady while chords changed. Saved preferences produced an identical
backup after a full power-off; the workspace restarted without automatic notes.

Host tests also check in-key filtering, all-pad availability, ownership, map
locking, lighting quantization and migration. Every policy combination, broader
restore coverage and worst-case foreground timing still need hardware testing.
