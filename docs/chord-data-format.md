# Lossless chord archive

`tools/chord_dump.py` imports a supplied `THYMEMBK.bin` and reconstructs its bytes
from structured records. It preserves names, seven note slots per pad, octaves,
numeric color/degree IDs, serialized positions, directory tokens, padding,
unused regions and unknown tails. It has no controller I/O.

The source corpus is not distributed in this repository. Supply data under
appropriate terms; see [Third-party notices](../THIRD_PARTY_NOTICES.md).

```sh
python tools/chord_dump.py import /path/to/THYMEMBK.bin --output-dir .tools/inputs
python tools/chord_dump.py export .tools/inputs/chords.lossless.json /path/to/roundtrip.bin
```

Import performs structural validation and a byte-exact round trip. Outputs and
originals remain local/ignored. Optional comparison inputs require explicit
paths. Invalid structure stops import rather than guessing. Use a separate
derived dataset for intentional musical corrections; never edit an archival
field while still claiming byte-exact preservation.

## Supported layout

Offsets are relative to the supplied dump; range ends are exclusive.

| Range | Records | Meaning |
| --- | ---: | --- |
| `0x000000..0x000a00` | 10 × 256 bytes | Categories |
| `0x000a00..0x005000` | 70 × 256 bytes | Scale groups |
| `0x005000..0x039800` | 840 × 256 bytes | Scale/root variants |
| `0x039800..0x070000` | 223,232 bytes | Preserved unused region |
| `0x070000..0x27d000` | 840 × 2,560 bytes | Pad blocks |
| `0x27d000..0x280000` | 12,288 bytes | Preserved unknown tail |

Directory text is semicolon-separated ASCII ending at NUL. Category/group
entries name an absolute child offset and count. Variant entries name an
absolute pad-block offset, opaque auxiliary token and variant index. Original
token spelling/case and suffix bytes are preserved independently of typed fields.
Child bounds, alignment, ownership and overlap validate before traversal.

The supported group ordering has twelve variants in this root order:
`A, Ab, B, Bb, C, D, Db, E, Eb, F, G, Gb`. Do not substitute chromatic indexing
or UI order for the serialized identity. Auxiliary tokens are opaque, not
invented color or scale data.

## Pad records

Each variant block has **80 × 32-byte records**: 48 chord records then 32 melody
records. Each record contains:

| Bytes | Encoding |
| --- | --- |
| 0–13 | Seven two-digit decimal ASCII note slots |
| 14–15 | Exact two-digit decimal color/degree token |
| 16–31 | ASCII name, NUL and original remaining bytes |

Absolute MIDI pitch is authoritative; displayed names use **MIDI 60 = C4**.
This legacy two-digit encoding cannot represent MIDI 100–127. Trailing zero
slots are padding, while their raw literals remain retained. Interior zero or
all-zero non-NULL records are preserved and flagged as ambiguous.

Identity combines scale index and serialized pad index, with exact byte offset.
Four consecutive records form one column. No records are compacted/reordered;
NULL positions remain holes. The archive has only 32 serialized melody pads;
it does not invent assignments for the other sixteen hardware positions.

Color IDs are preserved numerically without assuming a particular RGB palette.
UI note coloring is a separate derived policy. Enharmonic spelling and original
names remain independent of canonical display labels.

## Format and projections

The JSON schema is `thy333.chord-dump`, version 1. It includes typed directory
relations, original text tokens, note/color/name literals, positional fields,
and exact fill/hex bytes for unclaimed ranges. It need not embed a redundant
copy of the entire source binary.

Export requires complete byte coverage, checks source digests, reconstructs the
binary, reparses it and verifies archive fields against that interpretation.
Altered pitches, octaves, positions, color IDs, padding or unknown bytes are
rejected. The runtime/embedded projections are generated from this validated
archive; archival data is not rewritten by layout, palette or display choices.


## Combined harmonic model

`tools/merge_harmony_catalog.py` reads the validated archive and the public
`core/harmony-intervals.json` collection. It validates literal interval data;
no external checkout or executable source is needed. Source credits are in
[third-party notices](../THIRD_PARTY_NOTICES.md). The complete merged JSON stays
under ignored `build/`; original records, names, seven-slot voicings, colors
and all 840 layout references are retained. The archive itself is never changed.

```sh
python3 tools/merge_harmony_catalog.py --archive .tools/inputs/chords.lossless.json
python3 tools/merge_harmony_catalog.py --archive .tools/inputs/chords.lossless.json --check
```

The checked-in `core/thy_harmony_catalog.h` is the compact interval projection
used by board and Client guidance. Regular builds require neither upstream
checkout nor archive to compile this header. It contains 99 distinct root-relative
scale sets and 62 chord sets. The extended collection adds 32 scale families and six chord
sets beyond the original archive plus the initial advisory vocabulary. The first 16 advisory family IDs stay stable;
additional original families precede new extended families. Equivalent scales
share an entry, with alternative names retained in the full JSON. Different
voicings collapse only in this pitch-class advisory projection, never in the
playable catalog or full model. Rootless archive records stay in the full model
but do not invent a chord root for suggestions. MIDI notes override misleading
archival quality suffixes when determining interval sets.

This expands suggestions, discovery and modulation on board and Client. It does
not renumber the 70 existing playable scale families or automatically substitute
new pad layouts for them. Results remain bounded to 24, with discovery reporting
the total match count. The new interval families are advisory choices, not newly
assigned playable presets. Adding playable presets is a separate catalog/settings
change so existing songs, shortcuts and settings keep their meaning.
