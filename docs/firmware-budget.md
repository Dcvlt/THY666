# Firmware resource budget

Feature work must review flash, ordinary SRAM, CCM, stack and foreground service
cost. A browser bundle's size does not describe MCU memory use.

## Hard limits

| Resource | Capacity / rule |
| --- | --- |
| Application flash | 512 KiB, ending before `0x080c0000` |
| Ordinary SRAM | 128 KiB total, including an 8 KiB reserved stack |
| Static SRAM ceiling | 120 KiB before alignment/stack boundary |
| CCM | 64 KiB CPU-only state; DMA cannot access it |
| Settings journal | 256 KiB, separate from executable flash |
| Musical ownership | 96 pad owners plus 128 generated owners |
| Song | 21 tracks / 512 events; 48 editable steps per track |

The linker asserts partition and static-memory boundaries. The engine uses
fixed-capacity pools rather than a heap. Keep DMA buffers in ordinary SRAM;
large CPU-only engine state may use explicitly initialized CCM.

## Build review

Use a fresh instrument output directory. Its manifest, ELF/map, memory inspection,
source hashes and stack outputs bind figures to that artifact. Check:

1. Raw application end and loadable data, separately from packaged `.333` size.
2. Actual allocated SRAM span, alignment gaps and contiguous space below stack.
3. CCM span and largest persistent buffers/owner pools.
4. Reachable callback chains plus interrupt allowance, not just individual
   function frames. Avoid retaining parser scratch across large callbacks.
5. Display, USB, lighting and storage staging: reuse bounded workspaces only
   with explicit exclusive ownership.

For orientation, the device-tested companion integration used about **409 KB
raw flash, 93 KB static SRAM and 63 KB CCM**. These are historical reference
figures, not a budget for a newly modified build. Generate fresh reports after
code changes; preserve manifests locally with their artifacts.

## Runtime cost

Inputs and MIDI releases have priority over rendering and background reads.
Dirty display slices, bounded controller updates, arithmetic late-step skipping
and explicit admission prevent catch-up bursts and unbounded foreground loops.
Saving/erasing is gated to a quiet maintenance window.

Resource reports are software bounds. Selected-path stack reviews do not prove
every interrupt composition; hardware stack watermark and worst-case mixed-load
timing remain to be measured. System → Storage reports saved configuration/song
use, not executable capacity or free RAM.

## Automatic voice-leading review

The v0.2 Auto leading policy adds a seven-note transient history plus its mask
(15 field bytes before structure alignment); settings v14 reuses the existing
voicing-policy byte, so journal payload and slot sizes do not grow. The solver
uses bounded local arrays and no heap, recursion or mutable global workspace.
At most seven rotations × seven voices × eleven octave candidates are visited;
a running prefix minimum bounds predecessor visits to the same order. This
also bounds disconnected/idle-pad preview work, which uses the same solver.

A standalone Cortex-M4 `-Oz` compile with the pinned Zig toolchain reports
320 bytes for `thy_voicing_lead` and 20 bytes for its `unique` helper: 340 bytes
on that call path before caller frames. This is a component measurement, not
an instrument linker or whole-foreground-stack result. Full firmware flash,
SRAM/CCM placement are now checked in the archive-backed build below. LTO
call-chain stack and acquisition/display timing still need hardware qualification.

## Harmonic guidance on the board and Client

The advisory C APIs operate on twelve-bit pitch-class sets with caller-owned
results capped at 24 entries. Chord search considers 12 roots × 62 templates;
scale discovery considers 99 families × 12 roots; pivot search considers
12 roots × 3 triads. Insertion into bounded result arrays has no allocation or
recursion. The board caches results only on explicit navigation, not each OLED
frame or musical clock tick. Physical capture scans at most 96 owners and their
bounded keys, plus two held Arp input bitsets per pad. No generated-owner scan,
I/O, allocation or change to the musical scheduler is involved.

The transient `thy_guidance_ui` workspace is 168 bytes in the instrument's SRAM
allocation, including a shared 144-byte result union. It adds no settings bytes
or CCM fields. The Client WASM adapter's separate result arrays are host-only.
Settings restore explicitly clears the browser; captured notes are not persisted.

Standalone Cortex-M4 `-Oz` compilation with the pinned toolchain reports local
stack of 76 bytes for chord search, 60 for scale discovery, 60 for comparison,
32 for UI refresh, 8 for UI navigation, and 64 for chord-label formatting.
`thy_engine_navigate` uses 184 bytes with the capture adapter inlined; its
selected-chord preview calls the existing instrument planner. These per-function
figures exclude callees and whole-target LTO and are not a maximum stack proof.

The C and WASM builds and physical-control regressions pass locally. The
archive-backed instrument build below establishes static placement; stack
high-water measurement and foreground latency still require hardware checks.

## Local archive-backed audit — 2026-10-27

Build `4c8c52964e91d79b` includes automatic voice leading and board Harmony
guidance. Its ignored artifacts are under
`build/instrument-hellmac-v02-20261027-01/`, including the ELF, binary,
inspection, source snapshot, manifest and `memory-budget.json`.

| Region | Occupied span | Limit | Remaining |
| --- | ---: | ---: | ---: |
| Application flash, including initialized data and image gaps | 414,696 B | 524,288 B | 109,592 B |
| Static SRAM, including alignment gaps | 93,460 B | 122,880 B | 29,420 B |
| CCM | 62,840 B | 65,536 B | 2,696 B |

The SRAM limit leaves the separate 8,192-byte stack reservation intact. Occupied
spans use the linked addresses; summed section/segment sizes alone omit small
gaps (the inspection sums are 414,668 B flash and 93,456 B SRAM). CCM contains
the CPU-only high-level engine and is the tightest remaining static budget.
The linker passed its settings-partition, stack-boundary and CCM assertions.

All 35 build-integrated native suites and 23 instrument Python checks passed,
including every archive-backed packing check. Compiler identity/dependency pins
and source stability were verified; DWARF anonymization preserved loadable bytes
and symbols. This is a local software qualification, not a flashed/device-tested
release or a measured worst-case stack/foreground-latency result.


## CCM split and merged vocabulary — 2026-10-27

The engine retains its 512 live events, 256-event take, 128 playback voices,
21 tracks and full 512-event Undo/Redo. Only the undo event arena moves from
inline engine storage in CCM to an exclusive caller-owned 8,192-byte SRAM
array. This is bank redistribution, not a reduction in total RAM. Each engine
and standalone sequencer must receive its own arena at initialization; song
import and settings reset retain the binding and clear history. No heap,
shared scratch, capacity reduction or DMA placement change is involved.

The fresh local artifact is under `build/instrument-hellmac-v02-20261027-03/`.
It also includes the combined 99-family / 62-chord-set advisory vocabulary;
the original playable catalog is unchanged.

| Resource | Previous span | New span | Free after change |
| --- | ---: | ---: | ---: |
| Application flash | 414,696 B | 416,004 B | 108,284 B |
| Static SRAM | 93,460 B | 101,652 B | 21,228 B below stack |
| CCM | 62,840 B | 54,656 B | 10,880 B |

CCM gains 8,184 bytes after pointer/alignment overhead. SRAM retains its separate
8,192-byte stack reservation. The generated interval tables reside in flash;
the 168-byte board guidance workspace and 24-result cap do not grow. Search
work remains bounded but increases to 744 chord candidates and 1,188 scale
candidates per explicit request, outside the clock/render loop. The refreshed
component stack report measures 76 bytes for chord search and 60 for discovery;
whole-call-chain stack and device service latency remain unqualified.

Regressions cover a full-size undo/redo history in an external arena, adjacent
canaries, independent sequencers, song import and settings reset; discovery is
checked against all 4,095 nonempty pitch sets. These are local software checks,
not a device qualification. No flashing or update package is part of this audit.


## Complete local v0.2 musical build

Build `8a09d4804ab104f1`, under `build/instrument-hellmac-v02-20261027-05/`,
adds Octave fold and Guitar register to the merged vocabulary and CCM split.
The raw flash span is **416,940 B**, leaving **107,348 B**. Static SRAM remains
**101,652 B**, leaving **21,228 B** below the reserved 8 KiB stack, and CCM
remains **54,656 B**, leaving **10,880 B**. Settings v15 extends the existing
policy byte without growing configuration or song storage.

Octave fold uses one bounded seven-slot pass after validation. Guitar register
uses at most 6 strings × 64 pitch subsets × 6 candidates, with a fixed local
DP workspace and no recursion/heap; a seven-class source remains unchanged.
The pinned Cortex-M4 `-Oz` component compile reports **20 B** stack for fold
and **724 B** for guitar register. This is per-function stack, not a complete
LTO/interrupt stack proof. The transforms run during future-attack planning and
preview; worst-case dense-chord preview/lighting cost still needs board timing.

All 35 instrument-native suites passed, including settings v1–v14 migration to
v15, nonempty song restoration, Undo/Redo and the actual firmware app harness.
The 548 browser/WASM tests also passed with the real local archive projection.
The source identities stayed unchanged during the instrument build. Follow the
[v0.2 board checklist](v0.2-board-checklist.md) before marking hardware behavior
qualified. No device access, flashing, commits or publication occurred.

## Windows preparation and backup-version repair — 2026-10-27

Build `9cd5732b73075931`, in `build/instrument-v02-board-20261027-01/`, reports
the current settings version consistently over BACKUP/1. Strict current-version
device restore remains unchanged; older backups migrate through a separate host
C/WASM instance before upload. No device buffer, pool or state field was added.

| Resource | Occupied span | Remaining |
| --- | ---: | ---: |
| Raw application flash | 416,964 B | 107,324 B |
| Static SRAM | 101,652 B | 21,228 B below reserved stack |
| CCM | 54,656 B | 10,880 B |

The application is padded to 417,024 bytes for 64-byte update reports; its `.333`
file is 417,102 bytes including the container header. Packaging size is separate
from linked flash/RAM use. ELF bounds and source stability passed, as did focused
native backup-transfer regressions and offline package inspection/simulation.
The earlier musical software checks are retained; they were not repeated during
transfer preparation. This build has not yet been flashed. Runtime stack and
worst-case musical/UI latency remain part of the physical checklist.

## Live Harmony, centered leading and scoped resets — 2026-10-28

The fresh archive-backed build `041e39047a91813b` is under
`build/instrument-harmony-20261028-07/`. An independent ELF audit reconstructs
its load image, checks the binary and manifest hashes, verifies the frozen
source snapshot, and confirms that current compiler inputs still match it.
The local audit, component commands and stack reports are retained under
`build/harmony-resource-audit-20261028-02/`.

| Resource | Occupied span | Limit | Remaining |
| --- | ---: | ---: | ---: |
| Application flash, including initialized data and image gaps | 445,352 B | 524,288 B | 78,936 B |
| Static SRAM, including alignment gaps | 101,684 B | 122,880 B | 21,196 B |
| CCM | 55,288 B | 65,536 B | 10,248 B |

The separate 8,192-byte stack reservation remains intact. Segment sums report
445,324 B application flash and 101,680 B SRAM; the occupied spans above also
include the 28-byte initialized-data load and four-byte SRAM alignment gap.
Against the earlier device-tested v15 artifact, growth is 28,388 B flash,
32 B SRAM and 632 B CCM. The SRAM growth is 12 B instrument state, 12 B settings
journal state and 8 B Companion state. These totals include all intervening
Harmony, menu, settings, protocol and rendering changes; they do not isolate
the cost of one feature.

The largest flash assets remain the disjoint chord pad blocks (69,728 B),
lossless records (47,106 B) and layout blocks (16,800 B). Menu rows add 17,680 B
and menu options 4,008 B. The largest SRAM objects remain the note core
(27,088 B), settings workspace (13,944 B), instrument (12,760 B), USB transport
(8,780 B) and external Undo/Redo arena (8,192 B). The CPU-only engine occupies
55,288 B CCM. Its live Harmony workspace is 616 B; the shared menu state is
5,908 B on ARM. Reset confirmation adds 17 explicit transient field bytes
before alignment, not a second engine or settings snapshot. No new heap,
recursion, event-pool reduction or shared mutable solver workspace is involved.

Both leading APIs now use one bounded solver: at most seven rotations × seven
voices × eleven octave candidates with prefix minima. Integer centroid passes
keep temporary history and the resulting whole voicing near the authored
register, preventing accumulated octave drift without an extra DP dimension.
The single linked `lead_centered` function occupies 1,294 B. Register correction
can intentionally move a whole chord by an octave; held note owners and the
stored history are never rewritten by planning.

Live Harmony rebuilds at most four exact chord-pad plans per foreground
advance after invalidation, then ranks its 48 cached pads into seven choices.
Idle hint lighting bypasses ordinary per-pad voicing searches; regression
coverage checks zero voicing-provider calls for a settled 96-pad idle frame
and for Lights OFF. Scale discovery remains a bounded 99-family × 12-root
search when its visible page is dirty, with cached results between changes.
OLED rendering and serialization perform no harmonic search. These bounds are
not a measured CPU percentage or a guarantee of worst-case musical latency.

Separate pinned Cortex-M4 soft-float `-Oz` compiles, with LTO disabled, report:

| Component | Local stack frame |
| --- | ---: |
| Shared centered leading solver | 336 B |
| Ordinary / constrained leading wrappers | 32 / 40 B |
| Unique-voice helper | 20 B |
| Live Harmony tick | 256 B |
| Studio renderer / Harmony page | 208 / 168 B |
| Reset application / begin / resolve | 160 / 32 / 56 B |

These are individual component frames, excluding callers, callees and
interrupts. The largest measured settings encode/apply frames remain 4,824 B;
that storage path needs continued stack attention. The instrument uses LTO,
so neither adding these figures nor comparing one frame to the stack reserve
proves the linked program's maximum stack use. Stack high-water measurement
and dense-chord, generator, display and storage latency remain device work.

The successful source-stable artifact includes all 36 instrument-native suites
and the build's instrument checks. The expanded live Harmony workspace,
centered progression behavior and scoped resets still require physical
qualification; this audit performs no device access or flashing.

### Engine-menu revision — 2026-10-28

Build `aea9f961bfc05273` (`build/instrument-harmony-20261028-08/`) passes
all 36 instrument-native suites. Its raw application span is 444,908 B,
444 B smaller than `041e3904`, leaving 79,380 B of application flash.
Static SRAM remains 101,684 B including alignment, leaving 21,196 B below
the separate 8 KiB stack reservation. CCM remains 55,288 B, with 10,248 B free.
This menu-only revision adds no runtime pool. These are build measurements;
physical testing remains on the preceding artifact.

### Harmony interaction revision — 2026-10-28

Build `dc9aaed6cf35868b` (`build/instrument-harmony-20261028-13/`) passes all
36 instrument-native suites. The ELF, reconstructed raw image, source snapshot
and artifact hashes were independently checked. These measurements supersede
the earlier Harmony cache and stack figures above for this revision.

| Resource | Occupied span | Remaining |
| --- | ---: | ---: |
| Application flash | 448,852 B | 75,436 B |
| Static SRAM, including alignment | 101,684 B | 21,196 B below the reserved stack |
| CCM | 55,480 B | 10,056 B |
| Reserved stack | 8,192 B | Runtime high-water remains unmeasured |

Compared with `aea9f961`, this adds 3,944 B flash and 192 B CCM, with no
increase in the static SRAM span. The Harmony workspace is 808 B. Its pending
pad cache keeps published hints intact during rebuilding. Rebuilding now plans
one authored pad per foreground advance, then ranks at most 48 Stable-map or
96 Exploration candidates into nine choices. Publication waits until input
gestures have ended. No dynamic allocation or extra musical owner pool is used.

The largest flash assets remain pad blocks (69,728 B), lossless records
(47,106 B), menu rows (17,836 B) and layout blocks (16,800 B). Settings V17 adds
three payload bytes; alignment keeps the maximum journal record at 13,872 B.
The backup transfer limit includes those new bytes and passes full-size transfer
and readback regression coverage.

Fresh standalone Cortex-M4 `-Oz`, non-LTO stack measurements report Harmony
tick 432 B, centered leading 336 B, renderer 208 B and Harmony page 168 B.
The largest settings encode/apply frames are 4,832 B; menu rendering is 936 B.
These are local frames, not linked call-chain totals. Stack high-water and
worst-case MIDI/scan/display latency still require device measurement.

The Client passes 589 tests and all 33 synthetic C-rendered guide illustrations
reproduce. The `.333` package was checked offline only. Suggestion quality,
contrast, whole-board LED continuity and responsiveness remain physical checks;
this preparation did not connect to or flash the device.

### Full-surface Harmony exploration — 2026-10-28

Build `ee26c4ac8b301b39` (`build/instrument-harmony-exploration-03/`) passes
the instrument-native suites. The shared-core suites, 590 browser tests,
eight public-source privacy checks and all 33 generated guide illustrations
also pass. Focused board tests subsequently passed exploration, lighting at Hint
contrast 100, held-note stability, clean DIN releases and cold-boot persistence.

| Resource | Occupied span | Remaining |
| --- | ---: | ---: |
| Complete application image, including initialized data | 448,380 B | 75,908 B |
| Static SRAM, including alignment | 101,692 B | 21,188 B below the reserved stack |
| CCM | 55,656 B | 9,880 B |
| Reserved stack | 8,192 B | Runtime high-water remains unmeasured |

Compared with `dc9aaed6`, the complete image shrinks by 472 B; static SRAM
increases by 8 B and CCM by 176 B. The inspection's section sum excludes
28 B of initialized-data load bytes from its flash figure and 4 B of alignment
from SRAM; the occupied spans above include both. The container adds four
`0xFF` alignment bytes to the application payload.

Harmony scores 16 chord qualities for each of 12 roots, one root per
foreground advance. It publishes a 48-pad map and nine diversified suggestions
only after input release. Recent history is bounded to four chords. There is
no heap allocation or new note-owner pool. The Client skips rebuilding its
Harmony DOM when the projection has not changed.

Fresh standalone Cortex-M4 `-Oz`, non-LTO measurements report local stack
frames of 136 B for Harmony tick, 112 B for candidate scoring, 208 B for the
studio renderer, 72 B for chord-cell lookup, 4,824 B for settings encoding and
4,832 B for settings application. These are not linked call-chain totals or
runtime stack high-water measurements. Quantitative MIDI/scan latency and
map-update timing still need hardware measurement; the playing check does not
establish a worst-case bound.

Settings V18 appends four bytes after the frozen V17 prefix. The largest
journal record is 13,876 B and the complete backup limit is 13,908 B. The
Harmony projection is 800 B, still within the existing 1,680 B shared staging
buffer. The package passed offline Rust-updater inspection with the established
THY666 metadata profile; this does not prove installation or restoration on
hardware.
