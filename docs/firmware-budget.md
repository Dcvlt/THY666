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
