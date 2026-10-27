# MPE and ordinary expression

Optional MPE and pressure glide are OFF by default. Ordinary MIDI routes also
provide Aftertouch and pressure-to-bend. Fixed/Strike velocity stays independent.

## Pressure and Aftertouch profiles

In **Settings → Sensitivity**, select Left, Right or one pad. **Aftertouch stage** is
OFF by default. When OFF, the existing floor, ceiling and legacy curve give the
same response as before; MPE sends this value as member Channel Pressure (D0),
with no upper Aftertouch movement. Ordinary Poly or Channel Aftertouch uses the
existing response too.

When Aftertouch stage is ON, **AT start %** places a boundary inside that pad's
configured raw floor-to-ceiling span. The default is 70%, adjustable from 10% to
90%. Pressure rises from 0 to 127 below the boundary and stays at 127 above it.
Upper Aftertouch stays at zero below the boundary and rises from 0 to 127 above
it. Independent Linear, Soft and Firm curves shape each stage. In MPE, Pressure
is D0 and upper Aftertouch is per-member CC74. Ordinary Aftertouch uses the upper
stage; optional ordinary pressure-to-bend uses the lower stage.

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

Performance exposes **Response for**, **Aftertouch** and **Bend** below Default
velocity. They edit the same route policies as **MIDI & Routing**. Choose the
side/zone before editing its ordinary Aftertouch mode:

- **Off:** that pad contributes no Aftertouch.
- **Poly:** per-pitch Poly Pressure (A0), preserving the existing behavior.
- **Channel:** Channel Pressure (D0), taking the maximum of held contributors
  on the same destination/channel.

Choose an application directly; no additional pressure master switch is needed.
Aftertouch has no automatic audible effect: the receiving synth/patch must
support that message and assign it to volume, filter, vibrato or another
parameter. Strike velocity only describes the attack; these controls keep
responding while a pad is held. Use MPE OFF with a receiver in ordinary MIDI
mode. MPE ON uses its own member-channel pressure/CC74 path instead of these
ordinary route policies.

Advanced **Pressure bend** offers Off, Up or Down. The newest held bend-enabled
pad controls a shared destination/channel; releasing it returns to the previous
held contributor or center. The 1–24-semitone **Bend range** defaults to 2.
**Send bend setup** explicitly transmits Pitch Bend Sensitivity (RPN 0) on the
selected route's channel and outputs. Match the receiver's range; changing the
number alone does not reconfigure it. Setup requires quiet playing and MPE OFF.

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
voices share the originating pad's measured pressure. MPE member pressure is
Channel Pressure, rather than ordinary MIDI polyphonic key pressure. DIN and USB
destinations still come from the pad's route; the ordinary route channel is
replaced by member-channel allocation while MPE is enabled.

The first version provides one whole-board zone, Lower or Upper, with 1–15
members. It never steals an occupied or sustained voice. An attack requiring
more members than are free is rejected as a whole. Each future strum tone also
reserves a member before the attack begins.

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
below them. All settings use the shared C transaction system: RIGHT edits or
applies, LEFT cancels or returns, CENTER returns to Play.

Performance also provides **Velocity for: Both / Left / Right** and
**Velocity mode: Fixed / Strike**. This selector is separate from the Sensitivity
pad picker and MIDI route editor. A mode change preserves calibrated curves,
ranges and per-pad settings; only the selected half or halves change mode.
Both shows **Mixed** when their effective modes differ, until an explicit edit
sets one mode for both. MPE remains a whole-board mode, independent of this
velocity target.

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
the relevant member occupied until the deferred notes end. Pressure-zero,
CC74-zero and bend-center cleanup are retained under queue backpressure, and a member is not
reused until the reset batch has entered the ordered output queue. A new attack
also initializes expression before its Note On. Foreground updates coalesce
pressure/CC74/bend and service at most two members per 8 ms, leaving DIN bandwidth
for note traffic. The consumed neighbor receives held-pad light feedback.

This initial mode is for live pads. Enabling Arps, opening/starting the sequencer
or recording while MPE is enabled is blocked, instead of allowing ordinary
generated notes to collide with member channels. Disable MPE to use those
features. Faithful per-note expression recording and shared generator/MPE
allocation are separate follow-up work; existing track controller events do not
provide per-note expression identities.

Ordinary Channel Pressure and pressure-to-bend are live controls too. The song
codec retains its existing per-pitch Poly Pressure (A0) stream; with a split
profile, that stream captures upper Aftertouch. It does not record/replay the
new D0, bend or MPE CC74 axes as an expression timeline. Generated notes receive
channel cleanup before attack, but do not acquire physical pressure ownership.

## Saved configuration

Current **V13** settings retain MPE configuration, nine ordinary route policies,
two cluster stage profiles and 96 override/literal-profile pairs in the
**5,013-byte** parameter/library block. The maximum journal record is
**13,860 bytes**. Performance aliases edit those same fields rather than storing
duplicates. The independently versioned song follows the parameter block.
Runtime pointers, member leases, consumed contacts, expression values and held
notes are never saved.

V1–V10 import retains its previous parameters and defaults the new profile OFF,
split 70%, both curves Linear, ordinary Aftertouch inherited, bend OFF/range 2.
V1–V9 also initialize MPE and glide OFF. V11 rejects invalid ranges and combinations that enable both
MPE and an Arp or the sequencer workspace. Restore requires quiet inputs, a quiet
core and, when enabling MPE, a bound expression workspace. Restore itself emits
no notes, expression or RPN messages.
