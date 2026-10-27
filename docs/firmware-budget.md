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
