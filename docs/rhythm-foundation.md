# Clock, transport and generated ownership

## Musical time and transport

- `thy_clock` uses 960 ticks per quarter note and a 64-bit musical timeline.
  Internal tempo accepts 20–300 BPM in 0.001 BPM units. Integer conversion
  retains its rational remainder across irregular service calls and tempo edits.
- The caller supplies a wrapping uint32 millisecond timestamp. Backward calls
  and gaps larger than INT32_MAX are rejected. The shared engine's first advance
  establishes its caller epoch; lower-level clocks take an explicit init epoch.
- The clock continues while the sequencer is stopped. `thy_transport` has its
  own position, 21 independently enabled/running tracks and independent loops.
  Selection never starts or stops a track. New tracks are initially disabled;
  toggling one while stopped selects its eligibility for the next Play.
- Individual launches/stops use the next bar by default, with Beat/Immediate
  options. A second pending request cancels it. Meter is 1–16 over a power-of-two
  denominator from 1 through 32; pending boundaries keep their requested tick
  when meter changes. Default meter is 4/4.
- Main Stop is immediate: cancel pending operations, invalidate track runs and
  reset positions to loop starts. It does not reset the clock, release physical
  pad owners or clear independent live-generator owners. Emergency Stop does
  not first execute overdue pending launches, even at timeline exhaustion.
- Transport loop operations invalidate affected run generations. The sequencer
  layer owns pattern/grid edits and retained content; see the sequencer guide.

## USB clock following

Selecting USB enters Waiting. F8 locks the source and advances exactly 40 ticks
per pulse: 24 pulses equal one quarter note. This initial follower is pulse
stepped; it does not interpolate sub-pulse timing or estimate external tempo.
The configured BPM remains the internal tempo parameter, not a measured USB BPM.

FA starts/resets sequencer transport; FB continues; FC pauses position for
Continue; F2 validates its two 7-bit bytes and seeks in MIDI sixteenth-note
units. Main 5 Stop remains the separately approved Stop/reset behavior. Musical
clock ticks never seek when the sequencer position changes.

The host packet adapter follows cable 0 only; cable 1 still parses normally but
does not drive time. Incoming timing is ignored when Internal is selected.
No input command echoes clock, transport or notes to an output.

The default missing-pulse timeout is 500 ms. Loss freezes advancement, invalidates
generated runs and removes their owners. Pads remain usable, and generated
attacks are rejected in Waiting/Lost. There is no fallback to internal tempo.
Relock counts only new pulses, with the same result whether timeout was serviced
before a recovery pulse or detected inside that pulse. A transport that remained
Playing resumes time; stopped/paused state is retained. Old owners are never
recreated or overdue note attacks replayed.

## Generated ownership and cancellation

`thy_core` retains all 96 physical owners and adds 128 compact generated owners.
Each generated owner represents one pitch with immutable destination/channel
keys. They share the existing reference counts and queue reservations, so a
track, an independent generator and a pad can own the same routed pitch.

Ownership identities include a source, run generation and unique 32-bit handle.
Source/run cleanup is exact; a stale release cannot kill a reused slot. Handles
never wrap: exhaustion is reported. Engine acquisition also rejects stale run
callbacks before touching the ledger. Exhausted source generations retire to
zero instead of becoming aliases for previous runs. Initialization is a fresh
lifetime; external callbacks must be discarded at that boundary.

Engine source IDs are Arp 1 = 1, Arp 2 = 2, tracks = 16–36 and preview = 64.
Arp IDs also identify the [Arp processors](arp-engine.md); the
foundation ownership lab can acquire test notes without executing a pattern.
Track acquisition additionally requires an actually running, unsuspended track.
Clock source changes/loss clear generated ownership; track Stop/seek/restart
invalidates only the affected track runs. Global user Panic invalidates every
generated run, then uses the existing full instrument panic.

The queue invariant remains `queued + sounding + enabled sustain channels +
2 * pending live-strum keys <= 1024`. Acquisition is atomic across all routes;
pool/queue rejection leaves its handle output and ledger unchanged. Cleanup has
reserved space. At most 96 + 128 = 224 owners can reference one key, within the
existing 8-bit count. Live strums retain their existing scheduler and releases.

Generated cleanup honors the shared channel sustain state: the last owner can
defer Note Off until pedal-up. It does not send a global pedal-off that would
disturb other owners. This generic API guarantees one pitch acquisition at a
time. The Arp layer preflights whole-chord admission above this API; each processor
must reserve its own complete batch before emitting it.


## Adapters

The WebAssembly adapter synchronizes C time before commands and renders C
snapshots. Synthetic clock controls submit decoded messages to the same C
follower; they do not implement a second JavaScript clock. Device USB timing
is drained before one generator service at the latest tick, avoiding replay
of every buffered overdue attack. No held owner, clock position or running
transport restores at boot. See [Sequencer](sequencer-interface.md).
