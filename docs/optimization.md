# Firmware optimization guidance

The device application and portable musical core are C, with assembly startup
and low-level support. Rust implements the off-device updater/native host;
JavaScript implements the Client. Optimize the measured device path rather than
inferring MCU performance from the browser or desktop process.

## Acceptance contract

A behavior-preserving change must retain musical ownership, captured release
routes, ordering, sustain/strum cleanup, saved formats and migration, physical
input rules, visible frames and cooperative service policy. Removing a check,
capacity or feature is a separate design change. Keep the previous artifact and
compare outputs at boundaries, malformed inputs and maximum capacity.

Read [resource budgets](firmware-budget.md), [display scheduling](display.md)
and [development setup](development.md). The first efficiency pass is
software-qualified. The later stateless rendering follow-up has focused
before/after MCU profiles; recorder overhead, maximum-load timing and guarded
restore remain separate device qualifications.

## Decisions from the research

The next pass accepts work elimination and host diagnostics. Changes to musical
traffic, physical timing or cache invalidation stay separate.

| Lead | Decision | Reason |
| --- | --- | --- |
| Sparse OLED target updates | Implemented | Reconcile only changed target bytes, preserving unknown-panel initialization, prepared chunks and all observable transfer state. No new cache or RAM. |
| Empty USB expression scans | Implemented | Reuse the existing conservative blocked-map hint to skip suppression and contributor scans when the map is proven empty. MIDI bytes, exclusions and ownership stay unchanged. |
| Page-byte text and rectangle drawing | Implemented | Preserve exact pixels, clipping, transparent/inverse text and frame cadence while removing per-pixel work. |
| Musical snapshot and status scans | Implemented | Skip empty reference/deferred blocks, combine physical-half indicator reads and reuse the formatted footer. No retained cache, saved format or ownership change. |
| Native undefined-behavior checks | Implemented | A separate diagnostic build exercises all portable suites with fatal traps and mandatory instrumentation/assertion probes. Normal firmware and WASM contain no sanitizer code. |
| Compiler optimization levels | Offline experiment only | Compare linked size/stack before considering a default change. Host timing does not establish which level improves MCU responsiveness. |
| Known-dark LED shortcut | Deferred | The renderer also maintains fade/re-enable state and validates mappings; calibration/override callbacks can change output. A shortcut must preserve those contracts as well as visible darkness. |
| Settings revision cache | Deferred | Every persistent mutation needs reliable invalidation before skipping a save-time encoding. |
| Continuous MIDI expression admission | Separate behavior design | Rate limits or coalescing can change expression detail and latency; preserve current traffic in this pass. |
| Partial rasterization, larger OLED slices, DMA or faster SPI | Device experiment later | These change repainting, bus ownership or foreground timing rather than simply removing repeated work. |

Run `python tools/build_core.py --sanitize=undefined` for the new diagnostic
configuration. It writes isolated results and fails on a trap; it does not
generate a release asset. See [development instructions](development.md#undefined-behavior-checks).
Exact-output differentials and normal target checks remain necessary alongside
sanitizers; neither replaces the other.

The first diagnostic run passes the full portable suite with no reported trap.
Frozen-baseline comparisons also match display state/panel bytes and actual-app
MIDI, exclusions, ownership, settings and journal contents. The offline compiler
comparison gives no reason to change the default: `-Os` is slightly larger with
the same selected stack paths, while `-O2` fails the reserved-partition boundary.
Keep `-Oz`/LTO. These results qualify software behavior and resource placement,
not target timing; see the [measured budget](firmware-budget.md#research-follow-up).

## Research and engineering implications

These sources inform the proposals below. Their findings do not demonstrate a
speedup on this board. The distinction matters especially for host benchmarks,
old platform measurements and a controller whose exact identity is unconfirmed.

| Area | Primary source | Implication for THY666 |
| --- | --- | --- |
| C correctness | Wang et al., [Towards Optimization-Safe Systems](https://pdos.csail.mit.edu/papers/stack:sosp13.pdf), SOSP 2013 | Compiler assumptions about undefined behavior can remove intended checks. Validate arithmetic, shifts, bounds and aliasing before relying on a faster formulation. |
| Compiler policy | [Clang code-generation options](https://clang.llvm.org/docs/CommandGuide/clang.html#code-generation-options) and [LLVM LTO design](https://llvm.org/docs/LinkTimeOptimization.html) | Size and speed settings make different tradeoffs. Judge the linked artifact, where inlining and cross-module optimization can change duplication and stack lifetimes. |
| Musical latency | McPherson, Jack and Moro, [Action-Sound Latency: Are Our Tools Fast Enough?](https://www.nime.org/proceedings/2016/nime2016_paper0005.pdf), NIME 2016 | Measure end-to-end delay and jitter as well as component cost. Historical computer results are not a latency estimate for this firmware. |
| MIDI transport | [MIDI message formats](https://midi.org/summary-of-midi-1-0-messages) and [USB-MIDI 1.0 specification](https://www.usb.org/sites/default/files/midi10.pdf) | Account for message byte lengths and USB packet/cable semantics. USB bulk endpoints do not acquire a delivery deadline by changing `bInterval`. |
| Bitmap work | Pike, [Graphics in Overlapping Bitmap Layers](https://www.cs.tufts.edu/~nr/cs257/archive/rob-pike/pike-bitmap.pdf), 1983 | Region operations and retained bitmaps trade memory for work. This is general bitmap research, not evidence that a layer system helps our 1 KiB display. |
| Partial updates | [U8g2 reference](https://github.com/olikraus/u8g2/wiki/u8g2reference#updatedisplayarea) | Clipping rasterization and limiting display transfer are distinct operations. Our existing byte-level dirty slices already avoid full transfers for small changes. |
| DMA | ST, [AN4031](https://www.st.com/resource/en/application_note/an4031-using-the-stm32f2-stm32f4-and-stm32f7-series-dma-controller-stmicroelectronics.pdf) | DMA shares buses and needs explicit ownership, completion and error handling. It can reduce CPU service work without reducing SPI wire time. |

The panel uses an SSD1306-compatible command contract. The
[manufacturer reference](https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf)
helps explain page addressing, but does not establish the installed controller
or a wired scan-synchronization signal. Preserve the current addressing and
visibility fence until an alternative is independently qualified.

## Next measurements

The development firmware now provides an optional RAM-only recorder and Client
Start/Stop/Export controls. Use the [profiling guide](profiling.md) for repeatable
workloads and the limits of each measurement. The [initial board profiles](firmware-budget.md#initial-board-profiles)
identified repeated display/snapshot work for optimization: idle
rendering continues without display transfers, and a steady three-note hold costs
more to render even when its content barely changes. Client disconnection did
not produce a clear loop-timing reduction in that comparison.

The measured MPE workload had small destination queues and accepted waits of
0–1 ms. It does not justify altering expression traffic or MIDI scheduling;
wire/receiver delay and higher voice loads remain separate measurements.
Recorder overhead has not been measured on the MCU.

The rendering follow-up uses stateless work elimination within each
frame. Reference comparisons retain the existing ledger and scoped-query
semantics, including sustain, generated notes and malformed inputs. Focused MCU
comparisons now show lower observed mean render costs in connected idle,
steady-note and MPE Pressure workloads without reducing frame cadence; see the
[measured rendering results](firmware-budget.md#stateless-rendering-follow-up).
The pressure timeline and display churn differed between runs, and a static-menu
sample was excluded and repeated from Simple. These observations are not a
universal speedup, recorder-overhead measurement or worst-case timing bound.
Whole-frame invalidation, MIDI rate changes and scheduling changes remain
separate work.

Use identical synthetic empty/full-song and mixed-playing workloads. Record:

- DWT cycles per scan, musical service, transport, render, lighting and storage
  operation; include instrumentation overhead and interrupt time.
- Mean, tail and longest observed acquisition/release service gaps. A bounded
  number of items does not establish a bound on elapsed service time.
- Event age through enqueue, destination staging and UART/USB acceptance, with
  an external measurement for actual wire or sound latency.
- Linked flash, SRAM, CCM, selected callback stack paths and a device stack
  watermark. Neither a host stack report nor a watermark proves every path.

The existing CPU-active metric includes busy polling and interrupts; values near
100% do not show useful-work utilization or overload. The
[Arm Cortex-M4 reference](https://documentation-service.arm.com/static/5fce431be167456a35b36ade)
describes counters and timing assumptions; actual memory/peripheral contention
still needs measurement on the board.

## Candidates to evaluate next

The accepted subset above is implemented. These broader proposals still need
measurement or design work.

| Candidate | Benefit to test | Required boundary |
| --- | --- | --- |
| Extend host [UBSan](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) coverage as code changes | Expose overflow, shift, bounds and alignment defects before further transformations. | Preserve intentional unsigned wrap; host coverage cannot validate MMIO, DMA or ISR ordering. |
| Controlled `-Oz` / `-Os` / `-O2` comparison | Find measured hot paths worth a size/speed tradeoff. | Keep the pinned compiler and existing `-Oz`/LTO baseline; compare final flash, stack, exact outputs and target cycles. |
| Bounded display comparisons or damage hints | Avoid repeated work in expensive views. | Redraw old and new bounds, including variable labels and notched rules; keep a full-frame reference and fallback. Never mutate a prepared transfer. |
| Settings revision tracking | Skip unchanged full encodings. | Cover every persistent mutation, import, reset and library/catalog refresh. Preserve save sampling and debounce; a stale cache can lose settings. |
| Destination-aware continuous-expression admission | Prevent excessive pressure/controller updates building DIN delay. | Requires a separate behavior design: retain latest desired values, fairness, attack initialization, release/reset batches, sustain edges and MPE channel reuse ordering. Do not drop or reorder arbitrary queued events. |

DIN is limited to 31.25 kbaud, or 320 µs per byte, by the
[MIDI electrical specification](https://midi.org/wp-content/uploads/wpforo/default_attachments/1709416667-ca33-MIDI-10-Electrical-Specification-Update.pdf).
Thus one full three-byte message occupies 0.96 ms on the wire; a 255-byte ring
takes 81.6 ms to drain when full. These are derived wire times, not measured
firmware latency. Larger queues cannot solve a sustained bandwidth deficit.

Larger OLED slices, faster SPI, DMA, running-status compression or priority
reordering need separate experiments and receiver/device qualification. Keep
the current path as the baseline rather than combining such changes with a
claim of identical behavior.
