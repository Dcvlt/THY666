# Sensitivity and cluster calibration

## Everyday controls

Open **Settings → Sensitivity → Edit pads**. **Left** and **Right** have
independent defaults. Select an individual pad for an override, or use silent
**Pick pad**. UP on an individual pad returns to its containing cluster; UP on
a cluster switches between Left and Right. DOWN walks the individual pads.
RIGHT applies the target selection.

**Pad sensitivity** spans **0–100% of the target's safe range**. Higher means a
lighter press. Quiet noise, release hysteresis and applicable learned leakage
guards define that range; it is not a percentage of finger force. Changing one
cluster preserves the other half. Cluster adjustments scale explicit detector
overrides together while retaining their relative fit, Aftertouch, velocity and
release policies. Advanced raw Trigger edits affect inheriting defaults only.

Saved raw thresholds retain their response across format migration even if a
revised percentage scale displays a different label. Percentage is a normalized
control range rather than a measurement of physical finger force.

RIGHT edits/applies; UP/DOWN changes a draft; LEFT cancels/backs out; CENTER
returns to Play. Numeric hold-repeat starts after 400 ms, repeats every 60 ms
and accelerates to 30 ms after 1.5 seconds. Release affected pads before applying
response changes. Applying an unchanged percentage preserves its exact raw fit.

## Four phases for one cluster

Choose **Left** or **Right**, stop playback and release the pads, then open
**Calibrate cluster**. The wizard learns the selected half's **48 pads** and
keeps all large-pad musical input silent during capture. The other half's
saved response is not recalibrated.

Follow the pulsing target. It begins at that half's top-left pad and proceeds
left-to-right across each row, from the top row to the bottom. Each pad has:

1. **Soft:** three soft strikes, fully releasing between each.
2. **Mid:** three medium strikes.
3. **Firm:** three firm strikes.
4. **Pressure:** keep the finger down softly for about one second, increase to
   firm pressure while watching the live gauge, then release. The internal
   minimum is 800 ms; use a deliberate soft-to-firm hold rather than a fast tap.

The first three phases measure strike speed. They do not require three ordered
pressure peaks. Medians tolerate one unusual strike; a group with no consistent
pair repeats the three strikes. Ordered median speeds may have overlapping
ranges. The fourth phase measures continuous Aftertouch separately, using a
settled soft level and robust firm peak. It fits the range with a linear
pressure curve; the free ramp is not a measurement of physical force.

During capture the only actions are **Learn** and **Cancel**. To choose a pad,
select Learn with UP/DOWN and press RIGHT, then press/release that pad. This
selection gesture is not a calibration strike. An unfinished pad begins its
learning steps; selecting a completed pad restarts it. Unrelated pads cannot
silently steal the target or count as presses on it.

Completed pads show progress lighting and the counter advances. A retry states
what needs repeating. LEFT or CENTER closes the wizard while retaining its RAM
draft for this boot. Cancel discards it. Apply or discard one cluster's draft
before starting the other cluster; closing does not create a second draft.

## Playable review, then Apply or Discard

After **48/48**, the wizard opens a whole-instrument playing review. The
**Response** row switches **Saved / Calibrated** for the selected half; the
other half keeps its saved response. Play normally through the existing MIDI
routes to compare both. This is a temporary sampling overlay, not a settings
write. Release notes before changing response mode or sensitivity.

The review has four rows:

| Row | Action |
| --- | --- |
| Response | RIGHT switches Saved/Calibrated |
| Sensitivity | RIGHT edits the cluster's safe 0–100% draft; UP/DOWN changes it; RIGHT accepts the draft, LEFT cancels |
| Apply | Validates the whole cluster, installs its response/velocity fit and lets normal idle saving persist it |
| Discard | Removes the unsaved draft and returns to the saved response |

The review map marks responses outside the selected cluster's typical range.
Its percentages compare Aftertouch span and firm-strike response with that
cluster's medians. They help identify pads worth checking, including shim
placement, but cannot prove a mechanical fault or equal finger force.

Trusted target-dominant pressure holds also measure neighboring activation
inside the selected cluster. A safe guard raises Trigger/Release above observed
leakage while retaining hysteresis and soft response. If those signals cannot
be separated, **Nearby response** flags the pad and its saved detector,
Aftertouch and velocity configuration remain in use for comparison and Apply.
The derived release guard is saved, so later basic sensitivity edits and reboot
retain the protection. Other weak-response outliers still use their learned normalization. This is
not a general crosstalk cancellation matrix or a polyphony suppressor.

Apply changes only the chosen cluster and preflights every affected pad before
committing. The resulting cluster defaults and per-pad overrides save through
the current settings journal.
Leave the instrument idle until Saved before rebooting. Raw learning samples,
leak observations and review flags are session data; power loss discards an
unapplied draft. Named profile collections are separate future work.

## Advanced response settings

| Setting | Meaning |
| --- | --- |
| Trigger | Raw ADC level that starts attack confirmation |
| Auto release / Release gap | Linked hysteresis; factory gap 23 |
| Release | Raw release threshold; editing explicitly selects Manual |
| AT floor / AT ceiling / AT curve | Independent continuous Aftertouch range and curve |
| Strike curve / Soft speed / Hard speed | Recent-rise mapping, independent of Aftertouch |
| Min velocity / Max velocity | MIDI bounds, 1–127 |

Linked Release is `max(0, Trigger − gap)`. Manual Release remains literal when
Trigger changes. Aftertouch only requires floor < ceiling; those levels may
sit above or below the detector thresholds. **Use cluster** clears a pad's
detector and velocity overrides and its learned guard. **Defaults** resets the
selected cluster's defaults while keeping explicit pad overrides and guards.

The web Sensor Lab and OLED preview run the same C state and calibration code.
Browser ADC gestures are simulations, not device latency or force measurements.
Current settings store the applied defaults, overrides and guards; older
imports retain raw values. See the [persistence contract](settings-persistence.md).
The complete corrected four-phase cluster workflow still needs focused device
qualification. Browser gestures do not establish equal physical force.
