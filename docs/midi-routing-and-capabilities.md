# MIDI routing

Open **Settings → MIDI & Routing**. Choose a route profile and editing target,
then set its channel and outputs. Velocity and expression live in Performance.

| Profile | Routes |
| --- | --- |
| Whole Board | One route for all pads |
| Split Sides | Independent Left and Right routes; factory default |
| Six Zones | Three four-column × four-row zones per half |

Profiles retain independent settings. Channels are 1–16 in the interface and
0–15 in C. Outputs are nonempty combinations of DIN, USB cable 1 and USB cable 2.
Older settings retain their stored route velocity and expression values as
inheritance baselines. New playing-feel edits use Performance; its physical-half
settings do not change channel or output routing.

Changes apply to future presses. Sounding notes and pending strums keep their
captured channel, destinations, velocity and expression policy. The ownership
ledger merges overlapping routed pitches and preserves their final release.
USB disconnect does not disable an available DIN route.

## Performance and expression

Performance → Settings for selects physical Both, Left or Right for velocity,
Aftertouch output/stage, bend/range and MPE pressure output. This scope is
independent of the MIDI route editor and Sensitivity picker. MPE enable and
context-pad controls remain global. The former MIDI expression editors are
retired; their saved values remain available to imported configurations.

Aftertouch is Off, per-pitch Poly or shared Channel pressure. The receiver must
support/map the messages for an audible result. Pressure bend is channel-wide;
all notes on that channel hear it. Optional MPE allocates member channels
instead of using the ordinary route channel. See [Expression](mpe-expression.md).

The Play-screen USB and DIN icons combine all active pad routes. USB requires a
mounted host; DIN reports driver readiness, not cable detection. A tick beside
an icon marks each physical half whose routes all use it. Split ticks mean mixed
zones; no tick means that half does not use it. To disable an icon, remove that
output from every active side/zone, not only the current Edit route target.

## Sequencer and generated routes

Tracks have their own channel and outputs in their dedicated settings. T1
starts on channel 1, T2 on channel 2, and so on. They do not automatically
follow the live halves. Arps also have independent routes. If live playing
sounds but playback is silent, check the processor/track route and receiver
filters before changing the live channel.

## Input and timing

USB MIDI supplies the implemented incoming timing path. Internal/USB clock
selection, tempo and meter use shared C time; see
[Clock and transport](rhythm-foundation.md). DIN input is outside the current
scope. Clock/transport output, pedal integration, custom CC/Program Change
assignments and remaining USB remote-input controls are incomplete.
