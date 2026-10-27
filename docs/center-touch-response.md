# Center-button response

The center controls are capacitive buttons. Their response depends on hardware
touch detection and the application's status-polling interval. A shorter polling
interval improves input latency; it does not directly change sensor capacitance
thresholds.

**Sensitivity → Center response ms** controls navigation/function polling,
default **8 ms**, with an allowed range of **8–80 ms**. Upper banks have a separate
20 ms interval. Due status reads run before background configuration work, with
at most one I2C transaction per board service call. Pads and MIDI keep priority.

The driver reads four CY8CMBR3116-family banks with bounded retry/deadline handling.
Readback captures each bank's complete 128-byte configuration, identity, byte
validity, timestamps and provenance checksum. Configuration-read commands send
register pointers, not sensor values or nonvolatile writes.

## Read-only console

| Command | Result |
| --- | --- |
| `help touch` | Interface description |
| `touch` | Polling policy, readiness/capture masks and board identity |
| `touch capture 15` | Background configuration reads for all four banks |
| `touch bank 0` | Bank identity, state, validity and checksums |
| `touch page 0 0` | Sixteen bytes and validity at an aligned offset |

Bank indices are 0–3; page offsets 0–112 in steps of 16. COMPLETE and FAILED
are distinct, and missing bytes never become a successful configuration backup.
A completed original is not overwritten by a repeat request during the same
boot. Partial failures can be retried explicitly.

There is no exposed sensor-configuration apply/restore or hardware threshold
calibration. `tools/read_touch_configuration.ps1` exports readback with explicit
port/build/device binding and independent checksum verification. Keep identity
records private. See [Hardware](hardware.md).
