# Shared Arp phrase engine

## Playing contract

Both instances start disabled. Enabled instances default to Notes, Hold,
Hybrid grouping, Both sides, Immediate start, Continue on input changes and
Layer OFF. Each has its own output channel and USB/DIN destination mask.
The shared clock runs independently of sequencer transport, so main Stop and
opening or closing Settings leave live Arps running.

Hybrid preserves one phrase per held chord pad. Held single-note pads join
the newest still-held admitted chord; releasing it falls back to the previous
held chord. Without a chord, singles share a standalone phrase. Releasing the
last chord carries its phase into that standalone phrase under Continue.
Combined pools all contributions; Each gives every input its own phrase.
A one-note left chord remains a chord, and an empty input never becomes the
selected musical target. Duplicate pitches deduplicate within a phrase while
their contributing contacts retain independent lifetimes. Octaves stay distinct.

Adding/removing notes in a surviving phrase preserves its anchor and next-step
ordinal. Restart on change is optional. A new phrase starts immediately by
default; Grid starts at the next rate boundary, including the current tick if
already on that boundary. Immediate phases need not align across chord pads.

Layer OFF consumes subscribed input without an original-note copy. Layer ON
adds the original physical owner once, even when both Arps subscribe. If either
subscriber requests Layer, that one original copy is retained. Unsubscribed
pads use ordinary playing and strumming. Arp input is resolved after chord
shaping/transpose and before the original live strum scheduler.

Input zones are six four-column blocks: left 1–3 and right 1–3. The engine
filters through the instrument's logical mapping once, then retains electrical
contact IDs for release. The standalone Arp module accepts already-admitted
inputs; it cannot infer a physical zone from an electrical ID.

## Notes, chord steps and timing

Notes supports Up, Down, Up/down, Down/up and As played. The two bouncing
patterns avoid repeating endpoints. As played orders contributions by their
press order, then sorts pitches within each input; it does not invent an order
for tones resolved simultaneously from a chord.

Chord phrases emit the whole contributing pitch set. Eight editable offsets
(-24…+24 scale degrees) shift each tone through the selected scale. Offset zero
preserves exact original notes, including off-scale notes; other offsets retain
their chromatic displacement from the source degree immediately below them.
Out-of-MIDI-range results are omitted, never clamped or wrapped. All eight
offsets default to zero, producing a chord pulse. The web editor exposes these
steps directly rather than claiming a larger named-pattern collection.

Each instance has an eight-step enable mask; disabled steps are timed rests.
Rate accepts 40–3840 ticks at 960 PPQN (default 240, a sixteenth note). Gate is
1–100%, default 50%. Swing is 0–50%, default zero: an even/odd pair lasts two
rates, with the intervening onset delayed by `rate * swing / 100`. Gates use
that step's swung duration. Due releases occur before new attacks, including
100% gate singleton retriggers.

Rate, swing or start-mode edits rephase active phrases at the edit tick.
Other edits retain phase; existing generated gates keep their captured route
until release, while subsequent attacks use the new configuration. Resolved
held pitches remain captured. Chord degree shifts follow the instrument's
current root/scale; Keep Key is a separate unfinished option.

Delayed service releases overdue gates and advances arithmetic phase, then
emits at most the latest still-live step per phrase. Expired gates and overdue
attacks are skipped, not replayed as a burst. Counters report skipped steps and
failed admissions. USB timing remains pulse-stepped by the shared follower.
Browser refresh and tab throttling limit audible timing; this preview is not a
measurement of hardware jitter or DIN throughput.

The instrument foreground now advances this same shared engine. Incoming USB
F8/Start/Continue/Stop/Song Position messages use the shared clock/transport
path. Draining buffered packets updates time without running an Arp attack for
each old F8; the foreground services generators once at the final clock
position, retaining the late-service skip policy. A USB-session boundary
invalidates USB-follow timing and returns it to Waiting before new-session
packets are admitted. In Internal mode, USB reconnect does not reset independent
live/DIN generator ownership. This is implemented and host-tested behavior;
real MIDI timing and throughput still require a device session.

## Ownership and lifecycle

Generated notes use the shared 128-owner pool, outside the 96 physical owners.
A chord attack preflights aggregate owner slots, monotonically unique handles
and output reservations before acquiring any pitch. An admission failure skips
that entire attack and increments the error counter; later steps keep time.
Cleanup releases only this instance's tracked handles. It never globally panics
another pad, Arp or track. Channel sustain retains its normal ownership rules.

Layer OFF retains a silent physical owner plus resolved visual pitch/root/bass
snapshots. Pads still brighten correctly without acquiring dry MIDI voices.
The same interception path is used by real instrument planning, raw sensor
attacks and the explicit-note host API.

Source/Layer edits affect future presses. Existing admitted contacts remain
until release or Clear; enabling an Arp never adopts an already-held pad.
Disabling or Clear releases this instance and forgets inputs. It does not
restore dry notes or re-adopt a contact until release and a fresh press. Input
disable and all instrument panic entry points also clear pending Arp work.

Clock source/loss generation changes forget Arp inputs and release their owners.
Waiting/Lost admits no generated attacks, with no fallback clock or automatic
replay on relock. Unsubscribed live notes and an existing Layer copy remain
usable. New presses after relock start new phrases. Release edges update input
contributions before servicing the Arp, preventing an attack at the exact step
boundary where its last contact is released.

## APIs and preview

- `core/thy_arp.h` exposes bounded instance/configuration structs and init,
  configure, press, release, advance and clear functions. Ticks are uint64;
  callers serialize operations and supply a source/run identity and readiness.
- `thy_engine_advance()` services the clock, transport, enabled Arps and live
  strums. `thy_engine_arp_configure()` and `thy_engine_arp_clear()` are shared
  application operations. Musical handlers are bound by engine initialization.
- `engine.arp.configure(index, partialConfig)`, `.snapshot(index)` and
  `.clear(index)` are the WebAssembly adapter. Indices/channels are zero-based.
  Browser controls synchronize C time before commands. JavaScript does not
  calculate pitches, move playheads or run an independent musical scheduler.

The Companion's connected Parameters page edits the device's Arp configuration.
Use the controller and a receiving instrument to audition it. Host/WebAssembly
regressions exercise both instances, grouping, timing, step masks and routing
without opening hardware. Documentation examples show synthetic states rendered
by the same C UI; they do not represent a live device playhead.

## Device controls and persistence

Settings → Arpeggiators now edits the same two C configurations used by the
web preview. It includes instance selection, enable, source filters, Layer,
timing, implemented Notes patterns, eight Chord offsets/step toggles and
independent output channels/destinations. Clock source, tempo and meter are
live settings too. Unsupported optional features stay unavailable rather than
accepting a value that silently does nothing.

Open Arps enters the shared quick page; a Play navigation arrow can also be
assigned to Arps through Controls. UP/DOWN selects its rows, RIGHT edits or
applies, LEFT cancels/back, and CENTER returns directly to Play without stopping
live phrases. Its optional M-key layer applies only while that page is open:

| Control | Current action |
| --- | --- |
| M1 / M2 | Enable/disable Arp 1 / Arp 2 |
| M3 / M4 | Show the current Hold-only limitation; Latch is not implemented |
| M5 / M6 | Clear Arp 1 / Arp 2 inputs and owners, retaining configuration |
| M7 | Switch the edited Arp |

The ordinary M-key actions return outside the quick page. Consumed contacts
remain consumed until release, so leaving while holding an M key does not
reinterpret it as a new musical shortcut. The OLED displays the temporary
legend; this does not assume independent M-key backlights.

[Settings](settings-persistence.md) persists both independent Arp
configurations, the shared tempo/source/meter/launch quantization, editor target
and quick-key preference. Saved enable flags restore configuration only:
inputs, phrases, gates, owners, clock position, transport playback and capture
are never restored. USB-follow boots in Waiting and needs fresh clock pulses
and fresh pad input. Root/scale derive from the instrument catalog. Legacy
v1/v2 settings initialize both Arps disabled and the clock to Internal 120 BPM,
4/4, without writing a migration record until the normal safe idle window.

The instrument's save window excludes a running/paused transport, held inputs,
Arp outputs, core owners/reservations and pending MIDI work, including silent
rest steps inside a held phrase. An enabled but idle Arp can be saved. Flash
programming/sector erase still stall this single-bank MCU; the idle policy
avoids claiming uninterrupted musical timing during those operations.


## Current limits

Latch, release tails, Keep Key and Arp-to-Strum are incomplete. Hold is the
working input policy. Independent repeating/gated strummer engines remain
separate work. MPE and generated Arp/sequencer playback cannot run together;
MPE live playing uses a separate member-channel allocator.
