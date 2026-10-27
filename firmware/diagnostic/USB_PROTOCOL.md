# Diagnostic USB transport

This guide describes the diagnostic target. The instrument target adds the
companion and musical engine; see [companion and backup](../../docs/companion-and-backup.md).

The pinned TinyUSB 0.18.0 stack uses the STM32F405 OTG FS internal PHY, PA11/PA12
AF10 and a recovered 48 MHz PLLQ clock. Enumeration requires clock readiness.
The candidate does not assume PA9 VBUS wiring; TinyUSB's B-session-valid override
and the F405 no-sensing mode are used for a USB-connected laboratory prototype.
The candidate holds a 20 ms soft disconnect before resetting USB, so an
inherited updater personality can detach before the descriptor identity changes.
The local tick wait is bounded; upstream TinyUSB PHY/core reset waits are not.
Enumeration and telemetry have been exercised on the development unit.
Electrical margins and suspend behavior remain untested.

The composite device exposes CDC ACM plus MIDI 1.0 with two virtual cables.
Endpoints: `81` interrupt CDC notification (8 bytes), `02/82` bulk CDC (64 bytes),
`03/83` bulk MIDI (64 bytes). Its `cafe:4009` identity uses TinyUSB's example VID
and CDC/MIDI product bitmap. It is explicitly a local prototype identity, not
an allocated production USB identity. It does not impersonate `0333:0333`
update HID or the legacy `115c:572b` MIDI identity. Windows 10/11 normally bind CDC ACM using
the in-box [`usbser` driver](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-driver-installation-based-on-compatible-ids);
the first device test observed the expected CDC/MIDI composite binding.

Open the CDC port with DTR asserted. Baud rate is ignored; 115200 is a convenient
terminal setting. Wait for the fresh greeting
`HELLO THYDIAG/1 diagnostic; type help` before
sending commands. Commands end in LF; CR
is ignored. Each command produces exactly one complete CRLF-terminated line.
The console accepts at most 95 characters per command. Invalid/oversized lines
are rejected with `ERR invalid-character` or `ERR line-too-long`, respectively.
An error remains latched until LF, then a subsequent command can succeed.
Replies are retained across USB FIFO backpressure; command
processing pauses until the preceding response has entered the CDC FIFO.
Closing/reopening DTR resets the parser, discards buffered RX and clears pending
TX. Input arriving before the complete greeting is queued is also discarded.
Repeated DTR-high/RTS changes do not reset an active session. TinyUSB cannot
recall an IN packet already submitted to USB; on reopening, the host must
discard stale input until a fresh complete greeting arrives. USB mount/unmount
also resets the application session. Native regressions cover those boundaries;
device testing also confirms first commands and normal DTR reopens.
MIDI RX is validated by our portable C parser, counted, and discarded. No echo,
Note On, DIN transmission or SysEx assembly exists in this diagnostic.

| Command | Result |
| --- | --- |
| `help` | Protocol version and command list |
| `status` | Build, chip UID/ID, clock readiness, flash size, faults, NOR identity/status and center-light mask/brightness/errors |
| `startup` | Incoming CPU/mask/stack/vector/MPU, FPU, priority, fault, clock and watchdog register snapshot |
| `touch` | Four recovered I2C sensor-bank bitmasks, error counters and attempted/current/history masks |
| `i2c` | Per-bank latest result, counters and retained last failure stage/registers/time |
| `sensors` | Identity, readiness state/result, wake retries, recovery counts and successful status-read timestamps |
| `controls` | Four banks' raw/logical masks, latched edges and coverage of 43 known capacitive controls |
| `controls clear` | Clear volatile control history/counts, preserving valid held controls and their baseline |
| `led status` | Read the current LED test state and retained transfer failures |
| `led off` | Queue a complete zero frame |
| `led all <component>` | Illuminate all 96 pad LED slots at fixed brightness with a 10-second off deadline |
| `led walk <component>` | Walk one illuminated slot through stream order 0..95, approximately 100 ms per step, until the off deadline |
| `led pixel <index> <component>` | Illuminate one LED stream slot with a 10-second off deadline |
| `center` or `center status` | Read center-backlight state, applied channel mask and transfer counts |
| `center all` | Illuminate all 19 known center channels at the fixed dim level 128/4095 |
| `center off` | Queue a zero-brightness center frame |
| `center channel <index>` | Illuminate one center serial channel at 128/4095; decimal index 0..23, with unused 0..4 always off |
| `pads status` | Read raw pad-acquisition state, validity masks, counts and failures |
| `pads start` | Start an explicit 10-second raw acquisition test of 96 mux/ADC slots |
| `pads stop` | Request bounded acquisition shutdown and restore ordinary ADC snapshots |
| `pads bank <bank>` | Read 16 raw/minimum/maximum values for one acquisition bank |
| `adc` | Six raw ADC snapshots, with ownership and active/paused reporting |
| `midi` | Valid/invalid/unsupported MIDI packet counts |
| `core-test` | Note ownership, note-zero release and bend encoding test in RAM |
| `nor 00000000 256` | Read 1..256 bytes at a relative external flash hexadecimal address |
| `internal 08040000 256` | Read 1..256 bytes within the internal flash address range |

`status` reports `mailbox_version=3`; the textual protocol stays `THYDIAG/1`.
The mailbox's older offsets are retained, with sensor readiness appended after
the version-2 I2C extension.
`attempted`, `ok` and `history` use bits 0..3 for addresses `36`, `35`, `39`, `38`
(navigation, functions, left roots, right shortcuts). `ok` requires a successful
`AA` status read and clears on failure or re-probe. `history` retains every raw
transaction failure since startup, including a recovered wake NACK. Neither
zero-initialized values nor a retained touch mask establish a successful read.
`i2c initialized` marks banks which completed identity and status readiness.

For each bank, `i2c` returns `bankN_addr`, `attempts`, `successes`, `code`,
`errors`, `phase`, `last_code`, `sr1`, `sr2`, `at_ms` and `flags`, each with
the `bankN_` prefix. `code` describes the latest raw transaction, which can be an
identity read. **A zero raw code alone does not establish status readiness.**
Use `initialized`, `ok` and `sensors` for that decision. `last_code`, `phase`,
registers and timestamp describe the last failure and survive later successes.
Result codes are positive magnitudes of `diag_result`: `0` success, `1` invalid
or unsupported, `2` timeout, `3` unavailable/peripheral error, `4` busy.
Consult `attempted` and `history` before interpreting
zero fields. Counters and milliseconds are 32-bit, volatile and wrap naturally.
The failure phase identifies the wait that ended the transaction: bus free,
write START/address, pointer ready/sent, read START/address, or receive.

SR1 is the observation that ended the wait; a BUSY timeout samples SR1 afterward.
SR2 is then sampled before STOP/reset. They are sequential samples, not an
atomic bus trace. Flags decode `BERR`, `ARLO`, `NACK` (AF), `OVR` and SR2 `BUSY`;
a timeout without those flags remains a timeout, with no invented cause.
BUSY is normal while a master transaction is active; only a `bus-free` timeout
identifies failure to observe an idle bus.
Normal successful waits add no SR2 sampling: reading SR1 then SR2 can clear
ADDR. The driver reads identity and status at 100 kHz, using only register-pointer
writes followed by reads. It never writes sensor configuration or a sensor
command.

Sensor initialization waits 15 ms, reads two bytes at `8F` (family and device-ID
low byte), two at `91` (device-ID high byte and revision), then two at `AA`
(`BUTTON_STAT`). It accepts the documented family `9A` without assuming an
exact part. The family register contract, little-endian IDs and status map are
documented in the [Infineon register TRM](https://www.infineon.com/assets/row/public/documents/30/57/infineon-cy8cmbr3xxx-capsense-express-controllers-registers-trm-additionaltechnicalinformation-en.pdf).

A clean address AF/NACK gets STOP, AF clear and bounded STOP/BUSY waits. If
cleanup completes, the next attempt is scheduled at least 2 ms later without
resetting I2C. Each register allows up to five consecutive attempts; readiness
has a 1-second deadline. Other faults, data-phase NACKs or unsuccessful cleanup
use the bounded peripheral-reset path and remain errors. Failed/unsupported
banks get another readiness episode after 1 second, while the main loop and
other banks continue. Ready banks schedule status reads at 80 ms intervals.
These scheduling budgets are implementation policy requiring device validation.

Infineon documents first-address NACKs during wake and a 15 ms maximum I2C boot
time; first sensor scans can take substantially longer. This fits the observed
startup failures but does not prove their physical cause. I2C readiness does
not establish calibration. See the [official datasheet, pages 28 and 32](https://www.infineon.com/assets/row/public/documents/30/49/infineon-cy8cmbr3002-cy8cmbr3102-cy8cmbr3106s-cy8cmbr3108-cy8cmbr3110-cy8cmbr3116-datasheet-en.pdf).

`sensors` returns `ready`/`failed` masks and per-bank `state`, `code`, `identity`,
`family`, `device`, `revision`, `ready_ms`, `wake_nacks`, `retries`, `init_failures`,
`last_reg`, `resets`, `status_reads` and `status_ms`. States are `settle`, `family`,
`identity`, `status`, `ready`, `failed`, `unsupported`. Its `code` describes the
readiness result, unlike `i2c code`: `4` means probing, `0` means ready, and a
failure records its reason. `identity=1` means the family and complete ID were
read. `status_reads` counts successful `AA` reads only; `status_ms` is the last
successful status time. `ready_ms` retains the first success across re-probes.
`wake_nacks` counts clean address-NACK cleanup, not proof that sleep caused it.
`resets` counts hard peripheral recovery. Raw failure history is retained even
when readiness later succeeds; the sticky I2C error bit records hard recovery
or exhausted readiness rather than every successful wake retry.

`controls` consumes only fresh successful `AA` samples. Each bank reports `raw`,
`seen`, `rose`, `fell`, `logical`, `known_seen`, `changes`, `samples`, `changed_ms`
and `sample_ms`. Unknown raw bits are retained. Multiple simultaneous bits are
decoded independently. `covered` counts unique known inputs observed pressed
out of `total=43`: 5 navigation, 14 functions and two sets of 12 top controls.
Logical order follows [the descriptor map](../../docs/physical-control-map.md);
individual center-symbol routing still needs observation. Invalid banks retain
their last raw value with `valid` cleared. Initial/recovery samples establish a
baseline without invented edges; valid held inputs count toward coverage.
`controls clear` clears latched edges and counts, preserves that baseline and
keeps valid held bits seen. Counters count consumed samples and individual bit
transitions; they cannot reconstruct changes between unobserved samples.

RGB and pad acquisition tests are **explicit opt-in volatile hardware outputs**.
They remain idle after boot. Center backlights have the explicit boot default
described below. RGB components are decimal `0..2`, indices `0..95`;
color order and physical position must be observed. Every lit component is fixed
at **8/255**, with a **10-second** deadline which queues an off frame. `led off`
can also queue it during a transfer. `led status` reports `state`, `mode`, frame
counts, errors and `off_frame_sent`; the latter confirms a completed zero-frame
transmission, not an optical measurement. Transfer failures retain DMA/SPI
registers and time. A failed off transmission is not reported as confirmed off.

The diagnostic target's RGB frame is **2,816 halfwords**: 256 zero halfwords,
96 × 24 encoded data halfwords, then 256 zero halfwords. Each zero region is
approximately **390 microseconds** at the recovered 10.5 MHz SPI bit clock.
SPI is primed with zero while PC3 remains GPIO-low, then exposed through a
known-low GPIO-to-AF handoff. Shutdown takes PC3 back to GPIO-low before
disabling SPI. These changes target the third build's residual first-pixel
green light; they are not yet an optical validation of OFF or of color order.

Center indicators are a separate 24-channel, 12-bit brightness stream:
**PE5 data, PE4 clock, PE3 latch**. After accepted startup, the diagnostic target
queues **128/4095** for known channels **5..23**, leaving **0..4 off**. The CPU
driver uses an 888-word frame plus initial/final low writes, with explicit data
setup, clock edges and the recovered latch polarity; it uses no TIM1, DMA or
ADC resources. All three
pins are preloaded low before becoming outputs, and unrelated mux pins remain
untouched. Center illumination persists until another center command; it does
not share the RGB test's 10-second deadline.

`CENTER` returns numeric `state` (`0` idle, `1` queued, `2` ready, `3` fault),
`mode` (`0` off, `1` all, `2` channel), `channel`, `brightness`, `enabled_mask`,
`frames`, `errors`, `updated_ms` and `code`, followed by
`driver=cpu lighting_dma=none nvm_writes=none`. `enabled_mask` describes the last
completed frame, with expected all-on mask `00FFFFE0`; `frames=0` means nothing
has yet been transmitted. The short fields in `status` are `center_mask`,
`center_brightness` and `center_errors`. Completion confirms GPIO serialization,
not visible illumination. Fixed dim brightness is exposed through the console;
arbitrary 12-bit brightness is not a console setting.

Pad banks are decimal `0..5`; each contains 16 raw acquisition slots. `pads start`
clears the previous raw/min/max/valid data and starts the **10-second** test.
`pads bank <bank>` reads saved values and never starts acquisition. `valid0..2`
are 96 validity bits in raw slot order; slots do not yet imply a physical pad
permutation. The diagnostic target uses **ADC1/DMA2 stream0 and the six muxes
only**. It has no TIM1, DMA2 stream5 or PE3..5 lighting dependency; the former
600-word GPIO stream was center-light serialization, not established analog
excitation. A regression seeds the old stream5 FEIF condition and verifies that
it neither aborts ADC-only scanning nor gets read or cleared by the pad driver.
Each raw value is the integer mean of three ADC subscans; min/max
retain those published means. Velocity remains uncalibrated and no musical
output is generated. Acquisition owns ADC1 until bounded stop/restore
finishes, including a 1 ms ADC settling step. Ordinary `adc` snapshots pause
during ownership and resume afterward. DMA/ADC errors remain visible in `pads`
status; a cleanup failure retains ownership rather than resuming competing ADC
reads. Cleanup may reset the ADC block if stream0 will not stop, but never resets
DMA2 as a whole. `adc_resets` counts those scoped attempts;
`lighting_decoupled=1` marks the independent path. The legacy `dma_resets` and
`excite_dma_sr` fields remain zero. `active`/`busy` distinguish retained FAULT
state from ongoing acquisition. Raw/min/max values remain available after stop;
the next `pads start` clears them. These tests do not write nonvolatile memory.

Read response example: `DATA nor address=0x00000000 bytes=256 hex=... crc32=0x...`.
CRC32 is IEEE/zlib (polynomial `edb88320`, initial/final XOR `ffffffff`). A host
reader must verify address, count, exact hex length and CRC before retaining a
chunk, then verify a complete second read and whole-image SHA-256. Addresses
outside external `[0,01000000)` or internal `[08000000,08100000)`, arithmetic
overflow, empty or >256-byte requests are rejected before a hardware read.
The board driver also validates ranges and readiness. `ERR read code=4` means
the NOR reports a write/erase operation in progress; retry after a delay. This
image never starts such an operation. Other codes correspond to
`enum diag_result` in `board.h`.

No console command writes nonvolatile memory, unlocks flash, erases storage,
modifies option bytes, configures touch chips, starts the updater, resets the
MCU, exposes a filesystem, transmits MIDI, or reads arbitrary RAM/peripherals.
`core-test`, `controls clear` and counters change volatile RAM only. RGB, center
and pad requests additionally change GPIO/peripheral state as described above;
the center all-on request is also the diagnostic target's startup default.
This candidate intentionally
does not implement a `.333` update protocol.

`startup` reports `valid=1` only when the early assembly snapshot has the
expected magic/version. The nine CPU/vector/MPU values precede normalization;
the remaining MMIO samples follow DMA reset and precede clock/peripheral reset.
Watchdog registers are read without starting or refreshing a watchdog. Their
values do not establish that IWDG is inactive. `status` stage6 is the immediate
pre-USB checkpoint and stage5 means USB initialization returned; bit8 records
an initialization failure. Unsupported entry sets bit7 and halts at OLED
stage`0BAD` before IRQ/USB enable. See [hardware and startup](../../docs/hardware.md).

Official dependency inputs are pinned and SHA-verified by
`tools/setup_diagnostic_deps.py`. TinyUSB is MIT, ARM CMSIS 5.9.0 and ST CMSIS
device headers are Apache-2.0; their unchanged license texts are distributed
under `licenses/`. Independent transport/protocol/descriptor code is MIT.

Source references: [TinyUSB 0.18.0 release](https://github.com/hathach/tinyusb/releases/tag/0.18.0),
[descriptor macros](https://github.com/hathach/tinyusb/blob/86ad6e56c1700e85f1c5678607a762cfe3aa2f47/src/device/usbd.h),
[STM32 DWC2 port](https://github.com/hathach/tinyusb/blob/86ad6e56c1700e85f1c5678607a762cfe3aa2f47/src/portable/synopsys/dwc2/dwc2_stm32.h),
[ARM CMSIS 5.9.0](https://github.com/ARM-software/CMSIS_5/tree/2b7495b8535bdcb306dac29b9ded4cfb679d7e5c),
and [ST F405 header](https://github.com/STMicroelectronics/cmsis-device-f4/blob/9192c7b9df75a142f2027ab266601fe061fc00b3/Include/stm32f405xx.h).
