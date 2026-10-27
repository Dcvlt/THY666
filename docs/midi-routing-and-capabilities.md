# MIDI routing

Open **Settings → MIDI & Routing**. Choose a route profile and editing target,
then set its channel, outputs, velocity and expression policy.

| Profile | Routes |
| --- | --- |
| Whole Board | One route for all pads |
| Split Sides | Independent Left and Right routes; factory default |
| Six Zones | Three four-column × four-row zones per half |

Profiles retain independent settings. Channels are 1–16 in the interface and
0–15 in C. Outputs are nonempty combinations of DIN, USB cable 1 and USB cable 2.
Each route can override fixed velocity and ordinary Aftertouch/bend behavior.
Fields that still inherit defaults follow later default changes independently.

Changes apply to future presses. Sounding notes and pending strums keep their
captured channel, destinations, velocity and expression policy. The ownership
ledger merges overlapping routed pitches and preserves their final release.
USB disconnect does not disable an available DIN route.

## Performance and expression

Performance provides quick Both/Left/Right velocity mode and MPE access, plus
ordinary Aftertouch/Bend aliases using the same route policies. Velocity scope
is separate from the selected MIDI route and sensitivity target.

Aftertouch is Off, per-pitch Poly or shared Channel pressure. The receiver must
support/map the messages for an audible result. Pressure bend is channel-wide;
all notes on that channel hear it. Optional MPE allocates member channels
instead of using the ordinary route channel. See [Expression](mpe-expression.md).

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
