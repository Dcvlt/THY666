# MPE and ordinary expression

Optional MPE and pressure glide are OFF by default. Performance groups the
controls you use while playing; Sensitivity holds the detailed response curves
and calibration. Fixed/Strike velocity stays independent of held pressure.

## Pressure and Aftertouch profiles

In **Settings → Performance**, choose **Settings for: Both / Left / Right**,
then enable **Aftertouch stage** for that physical half or both. The same switch
is available in Sensitivity, alongside per-pad overrides and **AT start %**.
The stage is OFF by default. When OFF, primary pressure uses the full configured
floor-to-ceiling span; ordinary Aftertouch uses that same response.

When Aftertouch stage is ON, **AT start %** places a boundary inside that pad's
configured raw floor-to-ceiling span. The default is 70%, adjustable from 10% to
90%. Pressure rises from 0 to 127 below the boundary and stays at 127 above it.
Upper Aftertouch stays at zero below the boundary and rises from 0 to 127 above
it. Independent Linear, Soft and Firm curves shape each stage. MPE sends them
through the selected pair of member-channel messages below. Ordinary Aftertouch
uses the upper stage; ordinary pressure-to-bend uses the lower stage.

Both stages use their complete **0–127** output range. With AT start at 70%,
the lower 70% of the calibrated span is rescaled to 0–127 pressure; the upper
30% is rescaled to 0–127 Aftertouch, while pressure stays at 127. The split
compresses the usable physical range of each stage without truncating either
MIDI range. Turning the stage OFF returns primary pressure to the full span.

These are two logical mappings of one sensor, not two independent physical
axes. The split is a percentage of the configured raw span, not finger force.
At least two raw counts are required for two nonempty stages. Cluster defaults
and per-pad overrides are saved independently. **Use cluster** removes a pad's
profile override; **Defaults** resets the selected cluster's profile without
deleting its per-pad overrides. Apply profile changes only with the affected
pads released. The web Sensor Lab previews both curves and live values.

## Ordinary MIDI expression

In **Performance**, **Settings for** selects the physical Both, Left or Right
scope for these controls:

| Control | Choices or effect |
| --- | --- |
| Velocity mode | Fixed or Strike |
| Default velocity | Fixed attack value, 1–127 |
| AT output | Off, Poly or Channel |
| Aftertouch stage | Enable the upper pressure stage |
| Bend | Off, Up or Down |
| Bend range | 1–24 semitones for ordinary pressure bend |
| MPE output | Pressure, CC74 or CC1 for primary pressure |

**Mixed** means the selected pads have different effective values. An explicit
edit applies one value across the selected scope. The selector is temporary;
its applied values are saved. It is independent of the MIDI route editor and
Sensitivity's pad picker, and follows the physical halves when musical roles
are swapped. **MPE** and context-pad controls remain labeled **Global**.

Ordinary Aftertouch offers:

- **Off:** no contribution from that pad.
- **Poly:** per-pitch Poly Pressure (A0).
- **Channel:** Channel Pressure (D0), using the maximum held contributor on
  each destination/channel.

Older configurations retain their MIDI route's fixed velocity and expression
policies until a Performance edit sets a physical-half override. Editing
Performance leaves those stored route policies intact. The current source
retires their former MIDI editors; use Performance for new velocity and
expression edits. This menu cleanup follows the installed `db14dace` test build
and has not yet been checked on the device.

Choose an application directly; no additional pressure master switch is needed.
Aftertouch has no automatic audible effect: the receiving synth/patch must
support that message and assign it to volume, filter, vibrato or another
parameter. Strike velocity only describes the attack; these controls keep
responding while a pad is held. Use MPE OFF with a receiver in ordinary MIDI
mode. MPE ON uses its own member-channel expression mapping instead of these
ordinary route policies.

**Bend** offers Off, Up or Down. The newest held bend-enabled
pad controls a shared destination/channel; releasing it returns to the previous
held contributor or center. The 1–24-semitone **Bend range** defaults to 2.
**Send bend setup** in Performance explicitly sends Pitch Bend Sensitivity
(RPN 0) to the selected halves' channels and outputs. It requires quiet playing
and MPE OFF. If selected halves share an output/channel but request different
ranges, setup is blocked: set Both to one range, or use separate channels.
Changing a range alone never configures the receiver; match its setting before
playing. The current source also retires the former route-level MIDI setup
entry; that cleanup is not part of the `db14dace` device result below.

Channel Pressure and bend affect every note sharing that MIDI channel,
including notes whose own pad has expression Off. Sustain extends notes, not
finger ownership: releasing physical contributors returns these ordinary
controllers toward neutral. Routes and expression modes are captured at attack,
so later edits cannot redirect an existing note's release or expression.
Foreground updates coalesce; each service handles at most four messages per
destination every 8 ms, keeping each channel's update atomic.

## Playing

MPE can be enabled from **Settings → Performance** or **Settings → Expression**;
both controls edit the same saved value. Configure the zone and pitch-bend range
in Expression to match the receiver. The **Send setup** action can explicitly
request those receiver settings. Changing a preference, booting or restoring
settings sends no automatic configuration messages.

Every resolved chord tone or melody note gets one member channel. A chord's
voices share the originating pad's measured pressure. DIN and USB
destinations still come from the pad's route; the ordinary route channel is
replaced by member-channel allocation while MPE is enabled.

The first version provides one whole-board zone, Lower or Upper, with 1–15
members. It never steals an occupied or sustained voice. An attack requiring
more members than are free is rejected as a whole. Each future strum tone also
reserves a member before the attack begins.

## MPE pressure destination

Choose **Performance → MPE output** for the selected physical half or both.
This changes the message carrying the continuous pressure gesture; it does not
change the MPE zone or member allocation.

| MPE output | Primary pressure | Upper Aftertouch, when enabled |
| --- | --- | --- |
| Pressure (default) | Channel Pressure (D0) | CC74 |
| CC74 | CC74 | Channel Pressure (D0) |
| CC1 | CC1 | Channel Pressure (D0) |

The two stages use distinct messages, so the upper stage cannot overwrite
primary CC74. Both retain their full 0–127 range. With the stage OFF, only
primary pressure moves. Pressure glide still reads the lower pressure gesture,
regardless of the chosen outgoing message. Change mappings with all pads
released; existing voices keep their captured expression destination.

The maintainer reports receiver tests with **Osmose, Micromonsta 2, Hydrasynth
and Vital**. Their patches and expression assignments respond differently.
Match the receiver's MPE mode, zone, member count and bend range, then assign the
chosen pressure message in its patch. These observations do not qualify every
preset, mapping or routing combination. Focused device checks have now confirmed
CC74/CC1 message output and upper-stage separation; see the qualification below.

## Pressure glide

Enable **Pressure glide** to use a nearby pad as a modifier. Hold the source,
then press a qualifying neighbor in the same physical row and half. The newest
eligible held source is selected. Its pitch moves according to the relative
pressure on the source and modifier; for a chord, every tone shifts together,
preserving the voicing. This is an independent gesture design, inspired by
pressure-balanced keyboard playing.

**Neighbor pads** limits physical column distance. **Glide interval** separately
limits the resolved musical interval, because adjacent scale or chord pads need
not be one semitone apart. A pad outside either limit, or beyond the configured bend range, plays normally.
The lowest resolved pitch gives an order-independent reference; changing strum
direction does not change the interval. Wide bass/voicing changes can make a
neighbor ineligible. One
modifier belongs to a source; there is no automatic re-pairing or handover.
Releasing the modifier restores the source's pitch. Releasing the source stops
its notes. A captured modifier remains silent until its own release, even if
the source ended first.

Pressure glide uses the profile's primary Pressure stage. An enabled two-stage
profile can therefore reach full glide pressure before upper Aftertouch rises.

## Menu

Basic controls are MPE, Zone, Voices, Bend range and Pressure glide. A separator
puts Neighbor pads, Glide interval, Manager range, Send setup and Quick help
below them. RIGHT edits/applies, LEFT cancels or returns, and CENTER returns to
the active playing workspace.

**Settings for** does not change the whole-board MPE zone. Velocity mode
edits preserve each pad's calibrated curves, ranges and overrides; only the
chosen half or halves change mode. A draft retains its original scope if a
selector changes elsewhere.

Send setup transmits MPE Configuration (RPN 6) and Pitch Bend Sensitivity (RPN 0)
only after that explicit action. It can change the connected receiver's channel
configuration. The standard starting ranges are 2 semitones for the manager and
48 for members; configure the receiver to agree. Protocol reference:
[MIDI Association MPE specification](https://d30pueezughrda.cloudfront.net/campaigns/mpe/mpespec.pdf).

## Ownership and boundaries

Match zone, member count, bend ranges, receiver mode and the complete MIDI
routing path. Visible data flow alone does not prove a synth is accepting the
channels or using its internal sound engine for that input.

Member leases retain their captured channel, note and destinations. Releasing a
pad cancels pending strum attacks and releases its existing notes. Sustain keeps
the relevant member occupied until the deferred notes end. Expression-neutral
and bend-center cleanup are retained under queue backpressure. A member is not
reused until the reset batch has entered the ordered output queue. A new attack
also initializes expression before its Note On. Foreground updates coalesce
primary pressure, upper Aftertouch and bend, servicing at most two members per
8 ms to leave DIN bandwidth for note traffic. The consumed neighbor receives held-pad light feedback.

This initial mode is for live pads. Enabling Arps, opening/starting the sequencer
or recording while MPE is enabled is blocked, instead of allowing ordinary
generated notes to collide with member channels. Disable MPE to use those
features. Faithful per-note expression recording and shared generator/MPE
allocation are separate follow-up work; existing track controller events do not
provide per-note expression identities.

Ordinary Channel Pressure and pressure-to-bend are live controls too. The song
codec retains its existing per-pitch Poly Pressure (A0) stream; with a split
profile, that stream captures upper Aftertouch. It does not record/replay the
new D0, bend or MPE controller axes as an expression timeline. Generated notes
receive channel cleanup before attack, but do not acquire physical pressure ownership.

## Client member output meters

Development firmware adds a read-only performance snapshot for the Client's
Board view. Alongside physical Left/Right input peaks, the MPE panel can show
each member's captured note, channel, output destinations and pressure mapping.
Its pressure, upper-axis and pitch-bend readings are the last values accepted by
the firmware's MIDI queue; they are not measurements from the receiving synth.
Desired changes waiting for queue space remain distinguishable from accepted
values. The upper axis retains the existing mapping: CC74 with Pressure output,
or Channel Pressure when primary output is CC74/CC1.

Reserved strum attacks, sounding owners, sustained notes and retiring cleanup
remain distinct. A channel does not become free merely because its physical
pad was released. Routing and pressure-message labels follow the voice's
captured assignment, including while later settings differ.

The Client samples these values without changing musical state or inventing
per-note movement between packets. Missing/stale data stays unavailable; an
older firmware build explains that detailed meters require an update. This is
local development work, not included in published v0.2.2, and awaits physical
playing, receiver comparison and mixed-load timing checks.

## Play-screen indicators

The installed `db14dace` build's USB, DIN, MPE and Arp indicators, ordinary AT
P/C markers, VEL L/R markers and Simple's dotted separator passed visual checks
on the board.

The following transport-marker revision is host-tested source and has not yet
been installed for a device check. The side rail shows USB only when its output
is enabled and a host is mounted.
DIN appears when its output is enabled and its driver is ready; the board cannot
sense whether a DIN cable or receiving synth is connected. Each icon covers all
active routes: changing one side does not disable the other side's output. Ticks
on either side of an icon identify the physical half: one tick means all of its
active zones use that output; split ticks mean only some do; no tick means none.
Cluster inversion moves these route markers to the corresponding physical side.
MPE and Arp indicators
show their enabled modes. **VEL** marks Strike velocity, with side ticks showing
the physical halves. Ordinary **AT** uses P/C markers for Poly/Channel and is
hidden while MPE is active. Mixed scopes retain a distinct mixed marker.

## Saved configuration and qualification

**V19** adds ten bytes after the unchanged V18 preferences: four playing
policies for each physical half and two MPE pressure destinations. Its parameter
and library block is **5,042 bytes**; the maximum journal record is **13,888
bytes**. The independent song codec follows this block. Runtime voices,
expression values, held notes and the temporary Settings for selector are not
saved.

V18 and earlier import with the new half policies inheriting their existing
routes and MPE output set to Pressure. Legacy velocity modes, calibration,
profiles, songs and musical libraries retain their supported values. Invalid
fields and combinations enabling MPE with Arps or Sequencer are rejected before
mutation. Restore requires released inputs and quiet ownership; enabling MPE
also requires its bound workspace. Restore sends no notes, expression or RPN.

The `db14dace` build was installed and the following focused playing checks
passed. Peaks describe the recorded gestures, not limits on the available
0–127 range.

| Check | Observed result |
| --- | --- |
| Ordinary MIDI, physical Left | Velocity 80, Poly AT and upward bend; C/E/G produced three Note Ons and three Note Offs, with expression returning to neutral. |
| Ordinary MIDI, physical Right | Velocity 96, Channel AT and downward bend; one Note On/Off pair, with expression returning to neutral. |
| MPE CC1, both physical halves | Primary CC1 and upper Channel Pressure worked with Aftertouch stage ON. |
| MPE CC74, physical Left | Three voices on member channels 2/3/4; primary CC74 reached 127 and upper Channel Pressure reached 59. Releases were clean; CC74 reset to 64. |
| MPE CC1, physical Right, stage OFF | Primary CC1 reached 118; Channel Pressure stayed at neutral 0. |
| MPE CC1, physical Right, stage ON | Primary CC1 reached 127 and upper Channel Pressure reached 101, with clean release. |

A MIDI ledger snapshot contained **190 Note Ons and 190 Note Offs**, with no
drops, errors, unmatched releases or remaining notes. This supports the tested
gestures and mappings, not every receiver patch or routing combination.

Complete power-off and normal startup passed, with no automatic notes. Native
post-boot checks verified the running `db14dace` build and healthy operation.
The player deliberately changed settings between snapshots, so their comparison
does not establish byte-identical persistence of unchanged settings. The current
configuration was kept at the player's request; no backup restoration was
performed. The later transport side-tick revision and MIDI-row retirement
remain host-tested only.
