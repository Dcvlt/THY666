# Firmware resource budget

Feature work must review flash, ordinary SRAM, CCM, stack and foreground service
cost. A browser bundle's size does not describe MCU memory use.

## Optional profiling build

Build `5a732adb3bda9be2` adds the bounded
[timing recorder](profiling.md) to the behavior-preserving research baseline
`726445674e9fbe5d`. The compiled instrument and portable suites pass. Initial
instrumented board profiles are recorded below; recorder overhead and worst-case
timing remain unqualified.

| Resource | Occupied | Change from research baseline | Remaining |
| --- | ---: | ---: | ---: |
| Raw application /512 KiB | 469,884 B | +5,632 B | 54,404 B |
| Linked flash | 469,856 B | +5,632 B | 54,432 B |
| Static SRAM allocation | 108,152 B | +6,288 B | 14,724 B contiguous below the separate stack, including alignment |
| CCM /64 KiB | 55,672 B | 0 B | 9,864 B |
| Reserved stack | 8,192 B | 0 B | Actual high-water remains a device check |

The recorder itself uses 6,144 B, including 4,096 B of destination-slot
timestamps. Adapter marks, callbacks and transport bindings account for the
rest of the allocation increase. This is fixed storage reserved even while
recording is off; no heap or musical-pool sharing is introduced. Settings V19,
song capacity, ownership and display/MIDI service limits remain unchanged.

Inactive hooks skip the hardware cycle counter and journal callbacks. They
still have branch/call cost. Enabled timings include interrupts and observer
work; a measured counter-read pair does not quantify all instrumentation.
Whole-MHz clocks use an exact quotient/remainder conversion instead of a
software 64-bit division; non-whole-MHz clocks retain the reference conversion.
Host reference checks cover both paths, without claiming a target speedup.

Final linked callback-aware inspection gives 5,472 B for the selected journal
encode chain (8 B above the baseline) and 5,240 B for saved validation (40 B
above). The reviewed DIN cleanup path is 1,052 B, up 12 B. Resolved observer
callbacks surround rather than overlap the large codec body. These selected
paths exclude unreviewed callback and interrupt compositions; they are not a
global stack bound. The 8 KiB reservation and device watermark check remain.

## Initial board profiles

Five 15-second captures on the profiling build completed and exported with all
19 histograms consistent and no clock, invalid-sample, dropped-sample or
saturation flags. The Client was connected except for the disconnected-idle
comparison. All used Play/Simple at 120 BPM; the final capture enabled MPE
Pressure output. The user confirmed clean DIN releases during playing checks.

| Workload | Loop mean / max (µs) | Render mean (µs) | Render calls | Display bus chunks | Journal checks |
| --- | ---: | ---: | ---: | ---: | ---: |
| Idle, Client connected | 320 / 5,459 | 989 | 937 | 0 | 44 |
| Changing RIGHT notes | 323 / 5,387 | 1,421 | 937 | 563 | 14 |
| Idle, Client disconnected | 321 / 5,461 | 989 | 936 | 0 | 60 |
| Steady three-note RIGHT hold | 315 / 4,430 | 1,593 | 937 | 42 | 2 |
| Three-note MPE pressure playing | 321 / 4,884 | 1,183 | 937 | 225 | 15 |

Repeated rendering is the clearest work-elimination candidate. Idle rendering
continues around 62.5 times per second without display bus work. A steady hold
raises its mean cost by 61%; the playing snapshot scans the full note ledger
whenever notes sound. Different journal activity and hold durations prevent
interpreting the faster steady-hold loop or cheaper MPE render as an improvement.
No flash programming or erase was observed in these captures.

The MPE workload recorded 743 accepted messages per destination. USB had two
1 ms waits and DIN had one; all other accepted waits were in the 0 ms bucket.
Observed peaks were four events in each destination ring and ten DIN bytes.
These measurements start at destination-ring admission and end at whole-message
sink acceptance. They do not identify message types, bound unfinished waits or
measure wire/audio latency. The MPE loop p99 upper bucket was 4,095 µs versus
2,047 µs in the other captures; this is a coarse bucket crossing, not a measured
doubling of the exact percentile.

Raw captures and detailed reviews remain in ignored local evidence. This is a
single-board baseline, not a paired optimization result, maximum-load test or
recorder-overhead measurement. Guarded restoration remains a separate unfinished
device qualification.

## Stateless rendering follow-up

Device-tested build `db4aaa0ea0e62fe0` reduces work inside each existing render:
page-byte rectangle/text drawing, empty eight-key ledger blocks, a shared
physical-half indicator scan and one footer formatting pass. It introduces no
retained cache, heap, saved-format change, musical scheduling change or different
OLED transfer cadence.

| Allocation | Captured profiling baseline | Rendering build | Change |
| --- | ---: | ---: | ---: |
| Raw application | 469,884 B | 470,732 B | +848 B |
| Linked flash | 469,856 B | 470,704 B | +848 B |
| Static SRAM allocation | 108,152 B | 108,152 B | 0 B |
| Actual SRAM span, including alignment | 108,156 B | 108,156 B | 0 B |
| CCM | 55,672 B | 55,672 B | 0 B |
| Reserved stack | 8,192 B | 8,192 B | 0 B |

This build leaves 53,556 B in the raw application partition, 14,724 B of
contiguous SRAM below the separate stack and 9,864 B in CCM. The transient
indicator result occupies 20 bytes; no instance is retained in the engine.
The linked ledger helper is inlined into the existing snapshot frame and uses
ordinary word loads for its empty-block probes.

Selected linked stack chains change from 1,504 to 1,548 B for Simple Play,
1,744 to 1,752 B for the largest inspected render path, and 5,472 to 5,480 B
for the known journal chain. This leaves 2,712 B above that selected journal
path before unreviewed callbacks and interrupt/FP stacking. These are inspected
paths, not a whole-program or hardware stack bound.

Full-frame comparisons against the captured build match across 546,256 drawing
primitive cases, 1,301 ordinary UI frames, ten restore frames, 56 formatted lines
and 383 MIDI event records. Prepared OLED payloads, target/shadow buffers, dirty
maps and commit state also match.

The completed board session verified this build and repeated three 15-second
Play/Simple workloads at 120 BPM, with matching saved-settings checksums. The
user accepted the focused test. Observed mean rendering costs were:

| Workload | Baseline mean (µs) | Rendering build mean (µs) | Observed reduction | Render calls before → after |
| --- | ---: | ---: | ---: | ---: |
| Idle, Client connected | 989 | 885 | 10.5% | 937 → 937 |
| Steady three-note RIGHT hold | 1,593 | 1,031 | 35.3% | 937 → 938 |
| Three-note MPE Pressure, Simple repeat | 1,183 | 693 | 41.4% | 937 → 937 |

The lower costs did not come from reducing the render cadence. All captures
passed histogram, clock and validity checks, with no invalid, dropped,
saturated or discarded observations. The accepted MPE repeat recorded 696
accepted messages per output, all in the 0 ms destination-wait bucket; observed
peaks stayed at four events per destination and ten DIN bytes.

These are human-repeated workloads, not identical recorded-input replays.
Exact pitches and pressure/hold timelines were not logged. MPE display chunks
changed from 225 to 104, so its 41.4% observed mean reduction is not a universal
or isolated same-frame speedup. Journal activity also varied. An earlier MPE
capture had a static frame and the Performance menu was visible afterward;
it is retained as transport activity only and excluded from the rendering
comparison. The repeated capture began with Simple verified and changing
display content.

Final telemetry reported 108,156 B SRAM span, 55,672 B CCM and 470,732 B firmware
flash. The observed stack peak was 5,688 / 8,192 B, leaving 2,504 B not observed
in use during this session; this is not a worst-case guarantee. Startup was
ready with zero errors, 24 presses matched 24 releases, and no active voices
remained. Cleanup returned to Simple READY with MPE and Arps OFF and settings
saved. No settings backup was restored. Recorder overhead, maximum-load timing,
wire/audio latency and guarded restoration remain separate qualifications.

Native Windows `-O2` comparisons used seven interleaved baseline/candidate pairs
per case, with matching output checksums. Idle, held-note and MPE Simple display
states, rectangles, glyphs and rules all took less time in that host experiment.
The host experiment alone does not establish Cortex-M4 `-Oz`/LTO timing; the
board observations above provide the focused target comparison. Native core,
instrument, browser and fatal-UBSan checks pass, including the separate legacy
UI boundary suite.

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
timing remain to be qualified on hardware. System â†’ Resources now exposes linked
application flash, static SRAM and CCM spans, the 8 KiB stack reservation and its
observed boot watermark. System â†’ Storage continues to report configuration/song
use separately.

The instrument startup fills only the reserved stack, before calling C code and
with interrupts disabled. Foreground monitoring reads at most 16 words per loop,
never writes a live stack, and retains the deepest observed change. It does not
prove worst-case stack safety or detect every possible overflow.

DWT cycle counts against SysTick time report CPU active percentage, including
interrupts and polling. The firmware does not idle, so this normally approaches
100%; it must not be presented as useful-work load or overload. Mean/peak loop
intervals provide a separate responsiveness measurement over approximately one
second. Counter arithmetic handles wrap and rejects sampling gaps over five
seconds. Metrics use fixed state and no heap; hardware timing remains pending.

## Guarded restoration baseline

The fresh recovery build uses 464,432 raw application bytes, 101,864 allocated
SRAM bytes and 55,672 CCM bytes. The 8,192-byte stack reservation leaves 21,012
contiguous SRAM bytes below the stack. Relative to the preceding telemetry build,
guarded restoration and its scanner fix add 3,936 flash bytes and 72 SRAM bytes;
CCM is unchanged. The restore renderer reuses the existing framebuffer.

Inspection of the linked restore callback path estimates 5,536 stack bytes,
8 bytes above the preceding build; the selected save path remains 5,448 bytes.
These figures leave additional interrupt, FPU and unreviewed callback allowance
to qualify within the 8 KiB reservation. They are not a whole-program stack bound.

The build inspection checks source/artifact identity, startup stack selection and
the separate settings partition. These are host measurements, not hardware timing
or worst-case stack qualification. Retain the corresponding local manifest and
review with the package. Use this corrected recovery build as the next efficiency
baseline, and compare MIDI output, saved payloads, rendered frames and bounded
service times before accepting an optimization. Do not use polling-inclusive CPU
activity as the gain metric.

## Behavior-preserving efficiency pass

The follow-up build removes repeated work while retaining the recovery guard,
musical capacities, settings/song formats, scheduling and rendered appearance.
Compared with the guarded-restoration baseline:

| Resource | Recovery baseline | Efficiency build | Change |
| --- | ---: | ---: | ---: |
| Raw application image | 464,432 B | 464,264 B | −168 B |
| Linked flash allocation | 464,404 B | 464,236 B | −168 B |
| Allocated static SRAM | 101,864 B | 101,864 B | 0 B |
| CCM | 55,672 B | 55,672 B | 0 B |
| Contiguous SRAM below reserved stack | 21,012 B | 21,012 B | 0 B |

The application image leaves 60,024 bytes before the separate settings partition.
No feature, migration, note pool, undo capacity or asset was removed. One
conservative USB hint fits existing structure padding; the shared CRC table is
64 immutable flash bytes. No heap or persistent cache was introduced.

The main savings are work rather than storage:

| Path | Removed work |
| --- | --- |
| Song encoding | 21 track-occupancy scans become one event pass: 10,752 visits become 512 at capacity. |
| Song validation | A new maximum event ID needs no duplicate scan. For 512 increasing IDs, 130,816 prior-ID comparisons disappear; arbitrary ordering keeps the exact fallback. |
| USB polling | Skip canceled-key reconciliation when a conservative hint proves the blocked map empty. |
| Idle musical feedback | Skip 6,144-key ledger scans when the authoritative sounding count is zero; base Companion snapshots avoid two such scans. |
| Unchanged display target | Skip 1,024 dirty-map reconciliations, retaining target comparison, timestamps, counters and prepared-chunk behavior. |
| Checksums | Share a two-nibble-per-byte CRC implementation across settings, songs, library, backup and Companion codecs. |

Frozen-baseline differentials match complete song encodings/rejections, MIDI
bytes and ownership/counters, Companion snapshots, C frames and display chunk
state. The full native core/instrument and browser suites pass, as do the actual
C documentation-frame check and package inspection/simulation. Dense-view test
fixtures now create real generated owners rather than editing the reference
ledger without maintaining its sounding count; their pixel expectations remain.

Native-host medians show a 512-event increasing-ID validation at 38.3 µs versus
101.7 µs, and encoding at 65.0 µs versus 118.3 µs. Two million settled empty USB
polls take 35.4 ms versus 821 ms. These isolate synthetic host workloads; they
are not target speedups or bounds on musical latency.

Selected linked stack inspection keeps the restore-apply path at 5,536 bytes.
The save path becomes 5,464 bytes, 16 bytes above the recovery baseline; the
8,192-byte reservation is unchanged. These estimates exclude unreviewed indirect
callbacks and interrupt/FPU composition. Device stack watermark, ADC cadence,
mixed-load service gaps and actual MIDI/display timing remain unmeasured for this
build. See [research and next measurements](optimization.md).

### Research follow-up

The accepted research subset further narrows sparse OLED updates and empty-map
USB expression checks. It retains the first pass's formats, pools, generated
ownership, output rates, display transfer order and scene fences.

| Resource | First efficiency build | Research follow-up | Change |
| --- | ---: | ---: | ---: |
| Raw application | 464,264 B | 464,252 B | −12 B |
| Linked flash | 464,236 B | 464,224 B | −12 B |
| Allocated SRAM | 101,864 B | 101,864 B | 0 B |
| CCM | 55,672 B | 55,672 B | 0 B |
| Reserved stack | 8,192 B | 8,192 B | 0 B |

There are 60,036 bytes of application flash remaining and 21,012 contiguous SRAM
bytes below the stack. No new persistent fields or caches are needed. Later
display targets copy/reconcile only the bytes that differ; initial unknown-panel
bytes are still all serviced. The existing conservative USB hint bypasses
expression suppression and both contributor synchronization calls only when the
blocked map is proven empty. Existing exclusions remain intact.

Complete display-state/panel traces match across 424 synthetic records, including
unknown pixels, page edges, prepared old payloads and failed writes. Transport
and actual-main-loop comparisons match MIDI, ownership/exclusions, counters and
serialized settings. Focused native benchmarks show cheaper sparse submissions
and empty-map expression handling, but do not establish device latency or a
whole-application CPU reduction.

Selected linked stack estimates are unchanged for restoration (5,536 B), saving
(5,464 B) and UI (1,744 B). The reviewed transport path grows from 1,024 B to
1,040 B through compiler frame allocation. These remain selected software
estimates without interrupt/FPU or unreviewed callback coverage.

An isolated native UBSan trap build passes the combined core/MIDI suite and all
40 additional suites, with assertions active. Clean, signed-overflow and failing
assertion probes first verify that the diagnostic configuration works. Normal
firmware/WASM receive no sanitizer instrumentation. The pinned Windows compiler
uses trap mode without the standalone symbolic-report runtime; see
[development checks](development.md#undefined-behavior-checks).

Offline compiler experiments retained the current source and non-optimization
flags. `-Os` produced a 464,268-byte raw image, 16 bytes larger than the current
`-Oz` image, with unchanged static memory and selected stack frames. `-O2` failed
the existing application/settings partition assertion; it produced no valid
image and the limit was not relaxed. These experiments were not packaged or
device-qualified. The shipping build retains `-Oz`/LTO.

Normal core/instrument tests, Client tests, C-rendered help frames, artifact/source
inspection and offline package checks remain separate from UBSan. Hardware
recovery, mixed-load timing and runtime stack qualification are still pending.

## Automatic voice-leading review

The v0.2 Auto leading policy adds a seven-note transient history plus its mask
(15 field bytes before structure alignment); settings v14 reuses the existing
voicing-policy byte, so journal payload and slot sizes do not grow. The solver
uses bounded local arrays and no heap, recursion or mutable global workspace.
At most seven rotations Ã— seven voices Ã— eleven octave candidates are visited;
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
results capped at 24 entries. Chord search considers 12 roots Ã— 62 templates;
scale discovery considers 99 families Ã— 12 roots; pivot search considers
12 roots Ã— 3 triads. Insertion into bounded result arrays has no allocation or
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

## Local archive-backed audit â€” 2026-10-27

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


## CCM split and merged vocabulary â€” 2026-10-27

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
uses at most 6 strings Ã— 64 pitch subsets Ã— 6 candidates, with a fixed local
DP workspace and no recursion/heap; a seven-class source remains unchanged.
The pinned Cortex-M4 `-Oz` component compile reports **20 B** stack for fold
and **724 B** for guitar register. This is per-function stack, not a complete
LTO/interrupt stack proof. The transforms run during future-attack planning and
preview; worst-case dense-chord preview/lighting cost still needs board timing.

All 35 instrument-native suites passed, including settings v1â€“v14 migration to
v15, nonempty song restoration, Undo/Redo and the actual firmware app harness.
The 548 browser/WASM tests also passed with the real local archive projection.
The source identities stayed unchanged during the instrument build. Follow the
[v0.2 board checklist](v0.2-board-checklist.md) before marking hardware behavior
qualified. No device access, flashing, commits or publication occurred.

## Windows preparation and backup-version repair â€” 2026-10-27

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

## Live Harmony, centered leading and scoped resets â€” 2026-10-28

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

Both leading APIs now use one bounded solver: at most seven rotations Ã— seven
voices Ã— eleven octave candidates with prefix minima. Integer centroid passes
keep temporary history and the resulting whole voicing near the authored
register, preventing accumulated octave drift without an extra DP dimension.
The single linked `lead_centered` function occupies 1,294 B. Register correction
can intentionally move a whole chord by an octave; held note owners and the
stored history are never rewritten by planning.

Live Harmony rebuilds at most four exact chord-pad plans per foreground
advance after invalidation, then ranks its 48 cached pads into seven choices.
Idle hint lighting bypasses ordinary per-pad voicing searches; regression
coverage checks zero voicing-provider calls for a settled 96-pad idle frame
and for Lights OFF. Scale discovery remains a bounded 99-family Ã— 12-root
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

### Engine-menu revision â€” 2026-10-28

Build `aea9f961bfc05273` (`build/instrument-harmony-20261028-08/`) passes
all 36 instrument-native suites. Its raw application span is 444,908 B,
444 B smaller than `041e3904`, leaving 79,380 B of application flash.
Static SRAM remains 101,684 B including alignment, leaving 21,196 B below
the separate 8 KiB stack reservation. CCM remains 55,288 B, with 10,248 B free.
This menu-only revision adds no runtime pool. These are build measurements;
physical testing remains on the preceding artifact.

### Harmony interaction revision â€” 2026-10-28

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

### Full-surface Harmony exploration â€” 2026-10-28

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


## Resource telemetry build review (2026-10-28)

Build `9a9368a6ca8d0b02` adds read-only resources and System â†’ Resources.
These are linker/host results; device measurements remain pending.

| Region | Used span | Remaining |
| --- | ---: | ---: |
| Application flash | 451,052 B / 524,288 B | 73,236 B |
| SRAM, through static allocation end | 101,772 B / 131,072 B | 21,108 B before the separate 8,192 B stack |
| CCM | 55,656 B / 65,536 B | 9,880 B |

Compared with device-tested `ee26c4ac8b301b39`, allocated section sizes increase
by 2,672 B flash and 80 B SRAM; CCM is unchanged. Spans above include alignment,
so they differ slightly from section sums. Instrument core/application tests,
resource clock wrap tests and execution of the actual reset bytes in the bounded
host model pass. The reset test confirms the stack paint is confined to the
reserved region, after DMA quiescence and before the first C call. Hardware
qualification should compare these values against the linked spans, observe
stack growth during playing/display/backup work and check MIDI release timing.

## Shared OLED theme build review (2026-10-28)

Build `42d9c90579eb31bb` applies the content-aligned header/footer rules across
firmware screens and updates the shared Client renderer and guide illustrations.
Host validation passed; focused device checks subsequently passed on
2026-10-29 as described below.

| Region | Used span | Remaining |
| --- | ---: | ---: |
| Application flash, including initialized data | 452,164 B / 524,288 B | 72,124 B |
| Static SRAM, including alignment | 101,772 B / 131,072 B | 21,108 B below the separate 8,192 B stack |
| CCM | 55,656 B / 65,536 B | 9,880 B |

Compared with the resource-telemetry build above, the application grows by
1,112 B. Static SRAM and CCM are unchanged. The `.333` application payload adds
60 bytes of `0xFF` report alignment; its package is 452,302 B and passes offline
Rust-updater validation with the expected full build identity.

The separator measures at most 26 glyphs and draws at most 128 columns. It
allocates no heap or persistent state. Standalone Cortex-M4 `-Oz`, non-LTO
assembly reports local frames of 56 B for the rule, 40 B for the header helper,
208 B for the studio renderer and 40 B for display preparation. These exclude
callees and interrupts; they are not a linked worst-case stack bound.

Display scheduling now sends outer note rows before the title within the
non-priority group. Chunk size, group budgets and fairness limits are unchanged.
The existing modeled Arp timing check reports a maximum 25 ms for three notes
and 42 ms for five notes, under its unchanged 45 ms requirement. The MIDI and
ownership traces match with display work enabled and disabled. This is host
simulation evidence, not a physical latency measurement.

Core and instrument suites pass, as do 602 Client tests, 33 reproducible guide
frames and the actual reset-byte stack-paint check. Six production-rendered
review frames pass framebuffer guards and musical-state immutability checks.
Focused physical review passed Harmony/Fifths, Settings/Performance selection,
continuous separators/icons, page handoff and Resources bars/loop values. Simple
Play's three- and five-note 1/16 Arp highlights followed the audible notes;
DIN notes and the display cleared on release. No display-transfer failures,
DIN errors, USB drops, blocked pads or transport/LED faults were reported.

Actual resource values matched the linked spans. The observed stack peak was
5,376 / 8,192 B and remained unchanged during these checks. This is an observed
sample, not a maximum-stack proof. CPU active was 100% as expected for polling.
The latest sampled display drains were 10 ms during the first check and 99 ms
in a later idle snapshot. The latter was not captured while the pad was held;
these counters do not measure optical note-on-to-highlight latency. Lifetime
maxima retained startup/page-transition work. Quantitative worst-case scan,
MIDI and optical display latency still need a dedicated measurement.

The fresh USB capture contains 7,066 successful application data reports,
byte-identical to the selected 452,224-byte aligned payload; both CRC replies
matched `a300e53f`. Activation and the expected running build were verified.
The recorder required forced own-child cleanup; four failed endpoint completions
are retained in the audit and excluded from the successful transcript. No claim
of complete bus capture or raw-flash readback follows. Temporary Arp settings
were restored and saved. A new cold-boot or full-state-restoration check was not
part of this focused theme test.


## Scoped expression and status rail

The prepared `1c5bad7c961c4d8c` build adds physical-half Performance policies,
Pressure/CC74/CC1 MPE mapping and the revised status icons. It has not been
flashed or measured on hardware.

| Allocation | Used | Remaining | Change from tested OLED build |
| --- | ---: | ---: | ---: |
| Application image / 512 KiB | 455,628 B | 68,660 B | +3,464 B |
| Static SRAM below the reserved stack | 101,784 B | 21,092 B contiguous | +12 B |
| CCM / 64 KiB | 55,672 B | 9,864 B | +16 B |
| Reserved stack | 8,192 B | Runtime peak unmeasured for this build | Unchanged |

Static SRAM occupies a 101,788-byte span, including four alignment bytes. The
application's allocated flash sections total 455,600 B; the raw image includes
28 additional address-gap bytes. Settings V19 appends ten bytes after the frozen
V18 prefix; the aligned maximum journal record grows by twelve bytes to 13,888 B.
The 256 KiB journal reservation and four-slot write allocation are unchanged.

The MPE member structure remains 24 B. Its target workspace grows by four bytes
to 492 B; the maximum nine-event service batch and two-member service limit are
unchanged. Status glyph rows use 252 B of constant data and no heap. Per-half
indicator reads inspect at most 96 fixed pad records; neither new input policy
resolution nor pressure-output selection introduces a scheduler or unbounded loop.

A standalone Cortex-M4 `-Oz`, non-LTO inspection reports an 80-byte local frame
for Performance readback and 744 B for the setter, including its inlined explicit
bend-setup batch. This is not a linked worst-case stack bound and excludes
callees/interrupts. Bend setup deduplicates at most nine routes across three
outputs, rejects contradictory ranges on a shared selected channel, and queues
its complete RPN batch atomically. It runs only after explicit selection while
musical ownership is quiet. Ordinary policy edits and restores emit no setup.

Host tests cover split scope, swapped roles, sequencer input routes, held-owner
snapshots, queue backpressure, release cleanup, stage/profile atomicity and
V18-to-V19 migration. The unchanged modeled three-/five-note Arp display timing
checks remain part of the instrument tests. Hardware latency, expression
interoperability and current-build stack high-water still require device tests.


### Simple separator correction

The `db14dace389c1a93` follow-up changes only Simple's right frame: the rules
meet the dotted spine through the supplied three-pixel diagonal elbows.
Existing content-aligned notches and glyph columns are preserved, including
long labels. The frame adds bounded work: two three-row junction checks,
two 3 × 3 clears and fixed pixel loops; no heap or persistent allocation.

Its application image is 455,816 B (+188 B from `1c5bad7c`), leaving 68,472 B
before settings storage. Allocated flash sections total 455,788 B. Static SRAM
remains 101,784 B (21,092 B contiguous below the stack); CCM remains 55,672 B
with 9,864 B free. The stack reservation and settings format are unchanged.
Pixel-coordinate and text-boundary tests cover the corrected joins. The Client
and documentation use regenerated C frames. Focused board checks subsequently
passed the frame, expression indicators, split velocity/AT/bend and MPE CC74/CC1
output. Recorded notes released cleanly. The observed post-boot stack peak was
5,312 / 8,192 B; this sample is not a worst-case bound.

### MIDI menu and output-side feedback

Prepared build `1e43a70e0667aae9` retires the duplicate MIDI expression rows while
preserving their stored values and IDs. USB/DIN side ticks distinguish a whole
physical half from mixed output zones. Their readback visits at most six active
routes per output icon and allocates no persistent state.

The raw application is **456,080 B**, leaving **68,208 B** of application flash:
264 B more than the tested separator build. Static SRAM remains **101,784 B**
(101,788 B including alignment), leaving **21,092 B** below the reserved 8 KiB
stack. CCM remains **55,672 / 65,536 B**. Settings V19 and its journal allocation
are unchanged. The package adds 48 B of report alignment and a 78 B header;
offline Rust inspection and synthetic transfer pass. Core and instrument tests
pass. This build has not been flashed; its side ticks and menu cleanup still
need a focused device check.

### Read-only Client pad labels

Prepared build `bce2819acb736568` adds the optional physical-pad label map.
It reuses the existing 1,680-byte transfer staging area for a 1,672-byte frame.
One pure pad plan runs per foreground pass; no heap, extra owner pool or
musical scheduler is added. Pending work expires or cancels on map changes.

| Resource | Occupied span | Remaining |
| --- | ---: | ---: |
| Application image / 512 KiB | 458,464 B | 65,824 B |
| Static SRAM, including alignment | 101,796 B | 21,084 B below the reserved stack |
| CCM / 64 KiB | 55,672 B | 9,864 B |

The image grows by 2,384 B and SRAM by 8 B from the preceding prepared menu/icon
build. The 8 KiB stack reservation, settings V19 and journal allocation stay
unchanged. A pinned Cortex-M4 `-Oz` non-LTO assembly inspection gives local
frames of 24 B for service, 104 B for its map key and 240 B for label planning.
These exclude callees, main and interrupts, and are not a linked worst-case bound.

Core/instrument suites, current-frame reproduction, frozen-artifact/reset review
and offline Rust package validation pass. Client label requests are optional,
revision-cached and limited to a visible label overlay; pending work is bounded
before returning to base feedback. Actual device timing, label refresh during
playing and the new artifact's stack watermark remain hardware checks.

## Local MPE member and sequencer playhead telemetry

Development build `121c7cff4fb51de6`, under
`build/instrument-performance-local-01/`, adds a 1,024-byte optional performance
snapshot. It uses the existing 1,680-byte caller-owned staging rather than
allocating another frame. Fifteen member records and 21 track records have fixed
24-byte layouts. Captured MPE velocity fills an existing padding byte: each
voice remains 24 bytes and the Companion service remains 88 bytes. Settings V19,
owner pools, track/event capacities and playback scheduling are unchanged.

The frozen-build review and budget comparison against the published v0.2.2
firmware (`d792e046105e196e`) pass locally:

| Resource | Occupied | Change from v0.2.2 | Remaining |
| --- | ---: | ---: | ---: |
| Raw application image / 512 KiB | 460,496 B | +2,032 B | 63,792 B |
| Linked application flash | 460,468 B | +2,032 B | 63,820 B |
| Static SRAM allocation | 101,792 B | 0 B | 21,084 B contiguous before the separate stack, after alignment |
| CCM / 64 KiB | 55,672 B | 0 B | 9,864 B |
| Reserved stack | 8,192 B | 0 B | Runtime high-water not measured for this build |

Capture scans at most 15 members, their bounded owner keys and three output
destinations, then copies 21 actual track/transport states. It does not render,
advance musical time, service MIDI, allocate memory or mutate ownership. The
Client admits at most one optional performance transaction per 200 ms through
the existing serialized poll, after base feedback and only for relevant visible
Board panels. Label and Harmony staging retains exclusive ownership.

Pinned Zig 0.14.1 Cortex-M4 soft-float `-Oz`, non-LTO stack reports give 72 bytes
for `thy_companion_command`, 80 for the performance snapshot with its voice
helper inlined, 16 for the common header and 20 for finalization. The new
command/snapshot/finalization component path totals 172 bytes before callers and
interrupts. This is not a linked LTO worst-case-stack or timing proof.

The full core and instrument suites, real C-to-WASM-to-Client decoding tests,
frozen-source audit and capacity checks pass. A local `.333` candidate passed
Rust package inspection and a synthetic transfer. No hardware was accessed;
maximum polling/playing load, receiver comparison, input/release latency and
the actual stack watermark remain on the board checklist.
