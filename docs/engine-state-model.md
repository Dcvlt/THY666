# Engine and state model

The portable C core owns musical behavior and setting validation. STM32 and
WebAssembly adapters supply inputs, time, output availability and persistence.
The browser renders snapshots; it does not run a separate musical engine.

## State lifetimes

| State | Contents | Lifetime |
| --- | --- | --- |
| Applied settings | Musical preferences, routes, expression, response, lighting, shortcuts and views | Versioned settings journal and configuration export |
| Musical library | Immutable imported catalog plus bounded user scales/chords/sets | Catalog build input; user library in settings |
| Song | Created tracks, grid, events, processing and routes | Versioned THYS format; included in settings |
| Runtime | Baselines, contacts, owned notes, queues, clock, phrases, transport and capture | Current session only |
| Presentation | Page, selected row, editor draft, frame and transition | UI state; no ownership of MIDI releases |

Named profile/song collections are not implemented. Current persistence is the
applied instrument state, bounded user library and current song.

## Input and timing

Adapters serialize control edges, pad samples, setting commits, MIDI input and
time advancement through the engine. Interrupts publish hardware work rather
than independently changing musical ownership. Responses are bounded and
validated before commit. Setting editors retain their original target while a
draft is open.

Musical time is distinct from milliseconds. The shared clock continues while
sequencer transport is stopped. Display work reads state without consuming
musical events; navigation never consumes a pad's release.

## Note ownership

There are 96 physical pad owners and 128 generated-note owners. Each successful
attack captures pitches, velocity, MIDI channel and output destinations.
Ledger keys are **destination × channel × pitch**, including MIDI note zero.
The first owner attacks a key; the final owner releases it. Pending strum tones
reserve capacity and are canceled when their source is released.

Generated owners have source/run identities and unique handles. Stale releases
cannot kill a newer run. Track Stop releases track-owned output; independent
live pads and Arps retain their owners. Sustain uses the same ledger.

Routes and musical settings affect future attacks. Existing notes keep their
captured release keys even after root, layout, route or UI changes. Capacity
failure rejects the complete new attack rather than emitting a partial chord
or dropping a required release.

## Integration boundaries

- `thy_instrument`: note resolution, detector/response and physical controls.
- `thy_engine`: shared state, timing and processor integration.
- `thy_core`: ownership, reservations, output queues and live strum scheduling.
- `thy_clock`, `thy_transport`, `thy_arp`, `thy_sequencer`: musical time and runs.
- `thy_state`, menu bindings and `thy_studio_ui`: editing and presentation.
- Settings, song, library and backup codecs: explicit bounded byte formats.

The C core uses fixed capacities rather than a runtime heap. CPU-only engine
state may reside in CCM; DMA buffers must remain in ordinary SRAM. See
[Resource budget](firmware-budget.md), [Clock/ownership](rhythm-foundation.md)
and [Settings](settings-persistence.md).
