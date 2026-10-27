# Strike velocity and per-pad response

## Fixed or Strike

Fixed remains available and is the factory velocity mode: a note uses its MIDI
route's configured velocity. Strike maps the largest recent positive ADC rise
rate to the configured range. Its **32 ms lookback** uses samples already
acquired before normal attack confirmation; it adds no extra waiting period.
Millisecond timing quantizes faster publications to one millisecond.

| Option | Meaning |
| --- | --- |
| Performance → Velocity for | Both, Left or Right; separate from the calibration target |
| Performance → Velocity mode | Fixed or Strike; Mixed is shown for a scope with differing effective modes |
| Sensitivity → Edit pads | Independent Left/Right defaults or one pad override for detailed tuning |
| Strike curve | Linear, Soft or Firm |
| Soft speed / Hard speed | ADC counts/ms mapped to minimum/maximum velocity; Soft < Hard |
| Min velocity / Max velocity | MIDI bounds, 1–127 |
| Response curves | Read-only effective pressure and strike curves |

Sensor slope is not calibrated physical force. Held notes and scheduled strums
retain their acquired velocity and routes, including after settings change.
Continuous Aftertouch and strike velocity are separate mappings.

## Targeting and editing

For everyday playing, use **Settings → Performance**. Select **Both**, **Left**
or **Right**, then apply **Fixed** or **Strike**. Mode changes preserve each
pad's curves, speed bounds, velocity range and calibration. Both is preflighted
as one operation: if an affected pad is held, neither half changes. An open
draft keeps its original target, even if the selector changes through another
interface. The selector is a temporary editing target; the actual modes are
saved with the existing cluster/per-pad configuration.

In **Settings → Sensitivity**, choose Left or Right to edit that cluster's
defaults, or choose/pick an individual pad for an override. Picking is silent.
UP returns from a pad to its cluster; UP on Left/Right switches sides.
RIGHT edits/applies, UP/DOWN changes the draft, LEFT cancels/backs out and
CENTER returns to Play. Numeric edits support held repeat. A draft keeps its
original target and response edits require released affected pads.

Detector/Aftertouch overrides and velocity overrides are independent. Editing
Aftertouch does not create a velocity override. **Use cluster** clears both
override families and the learned guard on the chosen pad. **Defaults** resets
the selected cluster's defaults, retaining explicit pad overrides and guards.

**Pad sensitivity** spans the safe usable range, 0–100%, with higher values
requiring a lighter press. Cluster edits preserve relative detector fits and
the other side's response. Noise, release and learned leakage guards remain
active. An unchanged displayed percentage preserves the exact raw Trigger;
old saved raw values also survive format migration even if the revised
percentage label differs.

Auto release links Trigger/Release with a stored gap; the factory gap is 23.
A raw Release edit selects Manual. **AT floor / AT ceiling / AT curve** control
continuous Aftertouch independently of Trigger/Release.

## Calibration, preview and saving

**Calibrate cluster** learns one 48-pad half: three Soft, three Mid and three
Firm strikes, followed by a separate continuous soft-to-firm pressure hold.
Only after all 48 are complete does playable Saved/Calibrated review become
available. Tune the cluster draft, then Apply or Discard. Unseparable neighboring
activation is flagged and retains that pad's saved response. The
[calibration guide](sensitivity-calibration.md) explains the controls and limits.

C/WebAssembly development utilities and regression tests exercise timed synthetic
ADC samples, curves and scoped profiles. The connected Client edits supported
device settings and shows telemetry; playing and calibration happen on the
controller. Pointer clicks do not measure a device strike, and browser scheduling
is separate from hardware timing.

Settings store two cluster response/velocity/release defaults, per-pad
overrides and 96 learned release guards. V1–V8 imports retain exact raw values; both clusters initially
inherit the legacy global configuration. V1–V6 start velocity in Fixed, while
V7/V8 velocity choices remain intact. No strike samples, held notes or running
playback restore. The complete corrected cluster calibration workflow still needs focused device qualification.

**System → Storage** reports current record bytes and song events out of 512.
The 256 KiB rotating journal is not a named collection or external-NOR library.
Normal save/boot, completed-update preservation and interrupted-update recovery
remain distinct claims.

Read-only `pad N` diagnostics report effective raw thresholds. `pads` summarizes
Fixed/Strike/mixed velocity, while `velocity N` exposes saved inherited/overridden
velocity configuration, last strike and recent rise; it is not a readback of
the temporary audition provider or calibration progress. Calibration
flags are not a physical-force diagnosis. MPE and ordinary Aftertouch/bend applications are described in
[Expression](mpe-expression.md). Custom piecewise curves remain future work.
