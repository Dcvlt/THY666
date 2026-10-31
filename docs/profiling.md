# Record a performance profile

Use **Client → Diagnostics → Timing capture** to collect evidence for
firmware optimization. This requires profiling-enabled firmware. Older builds
can still use the ordinary diagnostic reports.

1. Choose 15, 30 or 60 seconds and select **Start capture**.
2. Play the workload you want to measure, or leave the board idle for a baseline.
3. Let the timer finish, or select **Stop capture**.
4. Read the results and **Export JSON** before starting another capture.

Starting captures measurements only. It does not change playing settings,
generate notes or request a save. Existing background saving can still run when
the instrument is quiet. A new capture replaces the previous results. Results
stay in RAM: power-off loses them.

## Compare one change at a time

Use the same duration, song, settings and playing pattern for each comparison.
Useful pairs are idle/playing, Simple/animated display, lights on/off, and Client
disconnected/100 ms/50 ms refresh. Measure MPE, Arps and Sequencer separately
before combining them. Record receiver response and clean releases during the
DIN playing check.

The board stops automatically at the chosen duration even if the Client is
disconnected. Reconnect and open Diagnostics to retrieve that capture. Leaving
Diagnostics stops its status polling, not the board's recorder. Live report
monitoring is a separate, once-per-second Client feature.

For a disconnected baseline, start the capture, disconnect the Client, perform
the workload, then reconnect after the timer has finished. Connection setup and
teardown remain part of that session. Avoid opening menus or changing unrelated
settings during a comparison. Save or restore tests belong in a separate quiet
session; profiling does not relax their normal admission rules.

## Board qualification

Focused board checks passed timed completion, JSON export and retrieval after
Client disconnection. Initial captures cover idle, changing notes, a steady
three-note hold and MPE pressure playing; the user confirmed clean DIN releases.
These are instrumented observations on one board, not worst-case limits or a
measurement of recorder overhead. See the [initial results](firmware-budget.md#initial-board-profiles).

The stateless rendering follow-up also passed focused board comparisons for
connected idle, steady notes and MPE Pressure, with lower observed mean render
costs at the existing frame cadence. See the [measured rendering results](firmware-budget.md#stateless-rendering-follow-up)
for the resource figures and comparison limits.

Before a playing comparison, confirm the actual workspace and changing note
labels. The exported engine and view are configured preferences, not the active
menu or overlay. A static-menu capture was excluded from the MPE rendering
comparison and repeated from Simple. Human pressure/hold timelines and display
changes still vary between runs; matching saved checksums alone does not make
them identical workloads. Preserve earlier exports when capture IDs restart
after a reboot or update.

## What the results mean

| Measurement | Scope |
| --- | --- |
| Loop interval | Time between observed foreground loop starts. |
| USB service | USB task, including console work performed inside it. |
| Transport service | Each of the two foreground transport slices, including DIN service and blocked-expression synchronization. |
| Input/control dispatch | Acquisition polling, detector samples, control dispatch and readiness checks; not pure ADC conversion time. |
| Engine service | Layout refresh, musical advancement and song-revision handling. |
| OLED render | C rendering and submission of a new target frame. |
| OLED chunk | One command/data bus slice; excludes panel scanning and optical delay. |
| Lighting service | Pad and central-control lighting tasks. |
| Background service | Transfer expiry/service, permitted song recall and restore-progress service. Multiple samples can occur per loop. |
| Journal service | One complete permitted journal task, including its nested codec and flash work. |
| Settings encode/validate/CRC | Work inside observed journal operations; not every codec call elsewhere in firmware. |
| Settings program/erase | The backend callback only; programming readback is outside the program measurement. |
| ADC frame interval | Foreground observations of completed frames, not individual electrical sample timing. |
| Input service interval | Time between foreground input-service starts. |
| USB/DIN queue wait | Milliseconds from destination-event-ring admission to successful whole-message sink acceptance. |

Queue waiting excludes earlier core/pending work and later USB endpoint,
UART byte-ring, wire and receiver delays. Direct cleanup messages are excluded.
Events already in a ring when recording starts are counted as **excluded**,
not given a misleading zero wait. Destination resets or overruns can add
**discarded** counts. Queue peaks are observed depths; the DIN byte-ring peak
can miss bytes drained concurrently by its interrupt.
Wait histograms contain accepted messages only: messages still queued at Stop
have no completed wait sample. Read them alongside queue peaks and discard
counts; they do not bound the delay of unfinished messages. Millisecond
resolution also combines shorter waits into the zero bucket.

Durations use the MCU cycle counter, rounded up to microseconds. Interrupt time
and instrumentation overhead are included. The reported counter-read cost is
only the cost of a read pair, not the full recorder cost; no estimated overhead
is subtracted. Measuring recorder-on/off behavior on hardware remains necessary.

## Precision and limits

Each metric retains count, sum, minimum, maximum and a fixed 20-bin histogram.
Percentiles are nearest-rank **bucket upper bounds**, not exact samples:
0, 1, 2–3, 4–7 and successive powers of two, through 131072–262143. The last
bucket contains values of 262144 or greater in the metric's unit. An open-ended
percentile is shown as a lower limit; a saturated percentile is unavailable.
The mean is rounded down to the reported integer unit. An empty metric has no
observations, rather than a measured zero cost.
Metrics describe individual invocations. Nested journal work and multiple
transport/background slices must not be added as independent loop costs.

Counters saturate instead of wrapping. Invalid clock observations and dropped
samples are reported. A clock failure ends recording; inspect those flags before
using a capture. There is one fixed recorder, at most 60 seconds, with no heap,
per-event log, file writing or ISR callbacks. Its base accumulator uses 6144 bytes
of static RAM, plus small adapter and transport metadata. It is disabled at boot.

The export contains the firmware build, capture metadata, numeric engine/view/
tempo context, the last saved settings checksum, queue peaks and metrics. It excludes device
identifiers, full settings, musical event logs and private paths. Context is
sampled at the start; it does not describe subsequent setting changes or unsaved
changes since the last durable save.

## Developer protocol

Commands use the existing serialized CDC connection; they never open a device
automatically. Capabilities and status are read-only. Start/Stop are explicit
RAM-only diagnostic actions, and are unavailable through read-only connections.

| Command | Result |
| --- | --- |
| `profile caps` | Version, 19 metrics, 20 bins and 60-second limit. |
| `profile start N` | Start one session for 1–60 seconds; reject an active or unavailable session. |
| `profile status` | Session ID, timer, clock validity, counters, peaks and starting context. |
| `profile stop ID` | Stop the matching session; repeating Stop is harmless. |
| `profile metric ID INDEX` | One metric, indexed 0–18, after recording has stopped. |

The Client retrieves a stopped capture in one exclusive status/19-metric/status
transaction and rejects changed IDs or inconsistent results. A lost Start/Stop
reply is uncertain: it is resolved by reading status, never an automatic retry.
Disconnect does not cancel the board's recorder. No settings-version change is
required. See [optimization guidance](optimization.md) and
[resource budgets](firmware-budget.md).
