import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createCore } from '../../web/core.js';

const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));
const fresh = () => createCore({ bytes });
const press = (engine, pad, notes, options = {}) => engine.press({ pad, notes, channel: 0, mask: 1, velocity: 96, ...options });
const compact = events => events.map(({ destination, status, data1, data2 }) => [destination, status, data1, data2]);
const invariant = engine => {
  const stats = engine.stats();
  assert.ok(stats.queued + stats.reserved <= stats.capacity);
  assert.equal(engine.voices().length, stats.activeVoices);
};

test('shape preview and emitted notes preserve serialized slots and frozen owners', async () => {
  const engine = await fresh();
  const notes = [60, 64, 67];
  const shape = { spread: 1, inversion: 2, excluded: 2, upper: 1 };
  assert.deepEqual(engine.preview(notes, { shape }).notes, [60, 67, 72, 79]);
  assert.deepEqual(notes, [60, 64, 67]);
  assert.ok(press(engine, 0, notes, { shape }).ok);
  assert.deepEqual(engine.drain().map(event => event.data1), [60, 67, 72, 79]);
  assert.ok(press(engine, 0, [127], { shape: { upper: 3 }, mask: 4 }).ok);
  assert.deepEqual(engine.owner(0).map(owner => owner.note), [60, 67, 72, 79]);
  engine.release(0);
  assert.deepEqual(engine.drain().map(event => event.data1), [60, 67, 72, 79]);
  for (const shape of [null, 3, [], { delay: -1 }, { octave: 128 }, { excluded: 128 }]) {
    assert.equal(press(engine, 1, notes, { shape }).status, 'invalid');
    assert.equal(engine.preview(notes, { shape }).status, 'invalid');
  }
  invariant(engine);
});

test('WASM strum uses an explicit clock and cancels all unplayed notes on release', async () => {
  const engine = await fresh();
  assert.ok(engine.advance(1000).ok);
  assert.ok(press(engine, 0, [67, 60, 64], { mask: 5, channel: 2, shape: { order: 1, delay: 40 } }).ok);
  assert.deepEqual(engine.drain().map(event => event.data1), [60, 60]);
  assert.equal(engine.stats().pending, 4);
  assert.equal(engine.stats().reserved, 10);
  assert.ok(engine.advance(1039).ok);
  assert.deepEqual(engine.drain(), []);
  assert.ok(engine.advance(1040).ok);
  assert.deepEqual(engine.drain().map(event => event.data1), [64, 64]);
  engine.release(0);
  const release = engine.drain();
  assert.equal(release.length, 4);
  assert.ok(release.every(event => (event.status & 0xf0) === 0x80 && event.channel === 2));
  assert.ok(engine.advance(10000).ok);
  assert.deepEqual(engine.drain(), []);
  assert.equal(engine.stats().pending, 0);
  assert.equal(engine.stats().activeVoices, 0);
  invariant(engine);
});

test('pending strum reservations cannot strand another pad at queue capacity', async () => {
  const engine = await fresh();
  engine.advance(0);
  press(engine, 0, [60]);
  press(engine, 1, [64, 60], { shape: { delay: 10 } });
  while (engine.control(1, 0, 1, 2).ok) {}
  assert.ok(engine.release(0).ok);
  assert.ok(engine.advance(10).ok);
  assert.ok(engine.release(1).ok);
  const events = engine.drain().filter(event => event.data1 === 60 && (event.status & 0xf0) !== 0xb0);
  assert.deepEqual(events.map(event => event.status & 0xf0), [0x90, 0x80, 0x90, 0x80]);
  invariant(engine);
});

test('generated melody layouts retain note zero and report out-of-range cells', async () => {
  const engine = await fresh();
  const degrees = [0, 2, 4, 5, 7, 9, 11];
  assert.equal(engine.melodyPitch({ layout: 1, anchor: 36, degrees, column: 0, row: 3 }).note, 0);
  assert.equal(engine.melodyPitch({ layout: 2, anchor: 60, degrees, column: 1, row: 0 }).note, 64);
  assert.equal(engine.melodyPitch({ layout: 3, anchor: 60, degrees, column: 1, row: 0 }).note, 65);
  assert.equal(engine.melodyPitch({ layout: 3, anchor: 72, degrees, column: 11, row: 0 }).status, 'invalid');
  assert.equal(engine.melodyPitch({ layout: 2, anchor: 60, degrees: [0, 4, 2], column: 0, row: 0 }).status, 'invalid');
  assert.equal(engine.melodyPitch({ layout: 1, anchor: 60, degrees, column: 256, row: 0 }).status, 'invalid');
});

test('snapshot ownership isolates pads, channels and destinations after settings changes', async () => {
  const engine = await fresh();
  assert.ok(press(engine, 0, [60, 64]).ok);
  assert.ok(press(engine, 1, [60], { channel: 1, mask: 4 }).ok);
  assert.ok(press(engine, 2, [60]).ok);
  assert.equal(engine.drain().length, 3);
  assert.ok(press(engine, 0, [72], { channel: 12, mask: 2, transpose: 12 }).ok);
  assert.deepEqual(engine.owner(0), [{ destination: 0, channel: 0, note: 60 }, { destination: 0, channel: 0, note: 64 }]);
  engine.release(0);
  assert.deepEqual(compact(engine.drain()), [[0, 0x80, 64, 0]]);
  engine.release(1);
  assert.deepEqual(compact(engine.drain()), [[2, 0x81, 60, 0]]);
  engine.release(2);
  assert.deepEqual(compact(engine.drain()), [[0, 0x80, 60, 0]]);
  assert.equal(engine.stats().activeVoices, 0);
});

test('note zero, upper endpoint and original pad identity are retained', async () => {
  const engine = await fresh();
  press(engine, 0, [0, 127], { mask: 7 });
  press(engine, 8, [12]);
  const starts = engine.drain();
  assert.equal(starts.length, 7);
  assert.deepEqual(starts[0].usb, [9, 0x90, 0, 96]);
  assert.deepEqual(starts.find(e => e.destination === 2 && e.data1 === 0).din, [0x90, 0, 96]);
  engine.release(0);
  assert.equal(engine.drain().length, 6);
  assert.deepEqual(engine.voices(), [{ destination: 0, channel: 0, note: 12, owners: 1, sustained: false }]);
  engine.release(8);
  engine.drain();
  invariant(engine);
});

test('invalid transformed pitches and ABI values reject without acquiring ownership', async () => {
  const engine = await fresh();
  const before = engine.stats();
  for (const notes of [[60, 128], [-1], [127]]) {
    assert.equal(press(engine, 0, notes, { transpose: notes[0] === 127 ? 1 : 0 }).status, 'invalid');
    assert.deepEqual(engine.stats(), before);
  }
  assert.equal(press(engine, 256, [60]).status, 'invalid');
  assert.equal(press(engine, 0, new DataView(new ArrayBuffer(4))).status, 'invalid');
  assert.equal(press(engine, 0, [60], { channel: 256 }).status, 'invalid');
  assert.equal(engine.release(256).status, 'invalid');
  assert.throws(() => engine.receive(new DataView(new ArrayBuffer(4))), TypeError);
  assert.deepEqual(engine.stats(), before);
});

test('release uses only acquired optional octave notes, without touching another owner', async () => {
  const engine = await fresh();
  press(engine, 0, [60, 60, 72, 48]);
  press(engine, 1, [0, 12]);
  engine.drain();
  engine.release(0);
  assert.deepEqual(compact(engine.drain()), [[0, 0x80, 60, 0], [0, 0x80, 72, 0], [0, 0x80, 48, 0]]);
  assert.deepEqual(engine.voices().map(v => v.note), [0, 12]);
  engine.release(0);
  assert.deepEqual(engine.drain(), []);
  engine.panic();
  engine.drain();
  invariant(engine);
});

test('sustain releases old routes, and deferred-note reacquisition does not retrigger', async () => {
  const engine = await fresh();
  engine.sustain(7, 3, true);
  press(engine, 0, [60], { mask: 7, channel: 3 });
  engine.drain();
  engine.release(0);
  assert.deepEqual(engine.drain(), []);
  assert.ok(engine.voices().every(v => v.owners === 0 && v.sustained));
  press(engine, 1, [60], { mask: 7, channel: 3 });
  assert.deepEqual(engine.drain(), []);
  engine.sustain(7, 3, false);
  assert.deepEqual(compact(engine.drain()), [[0, 0xb3, 64, 0], [1, 0xb3, 64, 0], [2, 0xb3, 64, 0]]);
  engine.release(1);
  assert.equal(engine.drain().length, 3);
  assert.equal(engine.stats().reserved, 0);
});

test('a saturated queue rejects a press atomically and still admits its reserved releases', async () => {
  const engine = await fresh();
  press(engine, 0, [60]);
  while (engine.control(1, 0, 71, 42).ok) {}
  const before = engine.stats();
  assert.equal(before.queued + before.reserved, 1024);
  assert.equal(press(engine, 1, [62]).status, 'queue-full');
  assert.deepEqual(engine.stats(), before);
  assert.equal(engine.isHeld(1), false);
  assert.ok(engine.release(0).ok);
  const events = engine.drain();
  assert.equal(events.length, 1024);
  assert.deepEqual(compact(events).at(-1), [0, 0x80, 60, 0]);
  assert.equal(engine.stats().reserved, 0);
});

test('panic consumes all three destinations’ sustain and note-off reservations at capacity', async () => {
  const engine = await fresh();
  engine.sustain(7, 0, true);
  press(engine, 0, [60, 64], { mask: 7 });
  engine.release(0);
  while (engine.control(1, 1, 71, 42).ok) {}
  invariant(engine);
  assert.equal(engine.stats().queued + engine.stats().reserved, 1024);
  engine.panic();
  assert.deepEqual({ held: engine.stats().heldPads, sounding: engine.stats().activeVoices, reserved: engine.stats().reserved }, { held: 0, sounding: 0, reserved: 0 });
  const events = engine.drain();
  assert.equal(events.length, 1024);
  const releases = events.slice(-9);
  assert.ok(releases.slice(0, 3).every(e => (e.status & 0xf0) === 0xb0 && e.data1 === 64 && e.data2 === 0));
  assert.ok(releases.slice(3).every(e => (e.status & 0xf0) === 0x80));
  engine.panic();
  assert.deepEqual(engine.drain(), []);
});

test('bend saturates at 16383 and DIN pressure includes its pressure byte', async () => {
  const engine = await fresh();
  engine.bend(7, 0, 16384);
  const bends = engine.drain();
  assert.ok(bends.every(e => e.data1 === 127 && e.data2 === 127));
  assert.deepEqual(bends[0].usb, [0xe, 0xe0, 127, 127]);
  assert.deepEqual(bends[2].din, [0xe0, 127, 127]);
  press(engine, 0, [60], { mask: 4, channel: 2 });
  engine.drain();
  engine.pressure(0, 91);
  assert.deepEqual(engine.drain()[0].din, [0xa2, 60, 91]);
  engine.control(4, 2, 123, 0);
  assert.deepEqual(engine.drain()[0].din, [0xb2, 123, 0]);
  assert.equal(engine.isHeld(0), true); // CC123 wire command is distinct from ledger panic.
});

test('receive preserves back-to-back short packets and fragments, with correct F1/F2 events', async () => {
  const engine = await fresh();
  assert.deepEqual(engine.receive([9, 0x90]), []);
  assert.equal(engine.receiveStats().fragmentBytes, 2);
  assert.deepEqual(compact(engine.receive([0, 64])), [[0, 0x90, 0, 64]]);
  const received = engine.receive([0x19, 0x91, 60, 80, 3, 0xf2, 127, 127, 2, 0xf1, 7, 0]);
  assert.deepEqual(compact(received), [[1, 0x91, 60, 80], [0, 0xf2, 127, 127], [0, 0xf1, 7, 0]]);
  assert.deepEqual(received[1].usb, [3, 0xf2, 127, 127]);
  assert.deepEqual(received[2].usb, [2, 0xf1, 7, 0]);
  assert.deepEqual(engine.receive([8, 0x90, 60, 80]), []);
  assert.equal(engine.receiveStats().invalid, 1);
  assert.equal(engine.receiveStats().received, 4);
});

test('large receive feeds are bounded without losing whole short events', async () => {
  const engine = await fresh();
  const packets = Array.from({ length: 1000 }, (_, i) => [9, 0x90, i % 128, 64]).flat();
  assert.equal(engine.receive(packets).length, 1000);
  assert.deepEqual(engine.receiveStats(), { received: 1000, invalid: 0, unsupported: 0, fragmentBytes: 0, overflow: 0 });
});

test('all 840 original layouts play and release through the actual WebAssembly engine', async () => {
  const runtime = JSON.parse(await readFile(new URL('../../web/generated/chords.runtime.json', import.meta.url)));
  assert.equal(runtime.scales.length, 840);
  const engine = await fresh();
  let compared = 0;
  for (const scale of runtime.scales) {
    for (let pad = 0; pad < 80; ++pad) {
      const record = runtime.pad_records[scale.pads[pad][1]];
      const notes = record.midi_slots.slice(0, record.used_slot_count);
      const channel = pad < 48 ? 0 : 1;
      const mask = pad < 48 ? 1 : 4;
      assert.ok(press(engine, pad, notes, { channel, mask }).ok, `${scale.name}/${pad}`);
      assert.deepEqual(engine.owner(pad).map(v => v.note), [...new Set(notes)]);
      engine.drain();
      compared++;
    }
    for (let pad = 79; pad >= 0; --pad) { engine.release(pad); engine.drain(); }
    assert.equal(engine.stats().activeVoices, 0, scale.name);
    assert.equal(engine.stats().heldPads, 0, scale.name);
    assert.equal(engine.stats().reserved, 0, scale.name);
  }
  assert.equal(compared, 67200);
  const anomaly = runtime.pad_records[runtime.scales[257].pads[57][1]];
  assert.equal(anomaly.midi_slots[0], 55);
  assert.equal(anomaly.name, 'F#/Gb');
});
