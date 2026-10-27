import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createCore } from '../../web/core.js';
import { createRhythmLabModel } from '../../web/rhythm-lab.js';

const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));
const fresh = () => createCore({ bytes });
const okay = result => { assert.equal(result.ok, true, result.status); return result; };
const note = (source = 'arp1', extra = {}) => ({ source, note: 60, channel: 0, mask: 5, velocity: 96, ...extra });
const wire = events => events.map(({ destination, status, data1, data2 }) => [destination, status, data1, data2]);

test('internal transport reset preserves the independent musical clock phase', async () => {
  const engine = await fresh(), rhythm = engine.rhythm;
  okay(engine.advance(0)); okay(rhythm.start()); okay(engine.advance(250));
  assert.equal(rhythm.snapshot().ppqn, 960);
  assert.equal(rhythm.snapshot().tick, 480);
  assert.equal(rhythm.snapshot().songTick, 480);
  okay(rhythm.stop());
  assert.equal(rhythm.snapshot().songTick, 0);
  assert.equal(rhythm.snapshot().tick, 480);
  okay(engine.advance(500));
  assert.equal(rhythm.snapshot().tick, 960);
  assert.equal(rhythm.snapshot().songTick, 0);
  okay(rhythm.start()); okay(engine.advance(750));
  assert.equal(rhythm.snapshot().songTick, 480);
  assert.equal(rhythm.snapshot().tick, 1440);
  assert.deepEqual(engine.drain(), [], 'Transport alone does not invent notes or MIDI clock output.');
});

test('fractional tempo retains its remainder across uneven service intervals and tempo edits', async () => {
  const large = await fresh(), small = await fresh();
  for (const engine of [large, small]) { okay(engine.advance(0)); okay(engine.rhythm.configure({ bpm: 123.456 })); }
  const end = 1234567;
  okay(large.advance(end));
  for (let time = 317; time < end; time += 317) okay(small.advance(time));
  okay(small.advance(end));
  const expected = Math.floor(end * 123456 * 960 / 60000000);
  assert.equal(large.rhythm.snapshot().tick, expected);
  assert.equal(small.rhythm.snapshot().tick, expected);
  const edited = await fresh();
  okay(edited.advance(0)); okay(edited.rhythm.configure({ bpm: 123.456 })); okay(edited.advance(1));
  assert.equal(edited.rhythm.snapshot().tick, 1);
  okay(edited.rhythm.configure({ bpm: 200 })); okay(edited.advance(2));
  assert.equal(edited.rhythm.snapshot().tick, 5, 'A tempo change must retain the sub-tick remainder.');
});

test('clock, loop, source and owner validation reject atomically at the WASM boundary', async () => {
  const engine = await fresh(), rhythm = engine.rhythm;
  okay(engine.advance(1000));
  const before = rhythm.snapshot();
  for (const config of [{ bpm: 19 }, { bpm: 301 }, { bpm: NaN }, { bpm: Infinity }, { bpm: '120' },
    { meterNumerator: 0 }, { meterNumerator: 17 }, { meterDenominator: 3 }, { source: 'din' }, { quantization: 'sometime' }]) {
    assert.equal(rhythm.configure(config).ok, false);
    assert.deepEqual(rhythm.snapshot(), before);
  }
  for (const args of [[21, 0, 3840], [0, -1, 30], [0, 100, 100], [0, 0, 4294967296], [0, 0.5, 300]])
    assert.equal(rhythm.setTrackLoop(...args).ok, false);
  for (const extra of [{ note: 128 }, { mask: 0 }, { channel: 16 }, { velocity: 0 }, { source: 'unknown' }])
    assert.equal(rhythm.acquire(note('arp1', extra)).ok, false);
  assert.equal(rhythm.midi(0xf8, 1).ok, false);
  assert.equal(rhythm.midi(0xf2, 128).ok, false);
  assert.equal(engine.advance(999).status, 'time-backward');
  assert.deepEqual(rhythm.snapshot(), before);
  assert.deepEqual(engine.drain(), []);
});

test('queued track launches cancel and run at the bar; loop edits invalidate their old run', async () => {
  const engine = await fresh(), rhythm = engine.rhythm;
  okay(engine.advance(0)); okay(rhythm.start());
  okay(rhythm.toggleTrack(0));
  assert.equal(rhythm.snapshot().tracks[0].pending, 'start');
  assert.equal(rhythm.snapshot().tracks[0].pendingTick, 3840);
  okay(rhythm.toggleTrack(0));
  assert.equal(rhythm.snapshot().tracks[0].pending, null);
  okay(engine.advance(1000)); okay(rhythm.toggleTrack(0)); okay(engine.advance(1999));
  assert.equal(rhythm.snapshot().tracks[0].running, false);
  okay(engine.advance(2000));
  assert.equal(rhythm.snapshot().tracks[0].running, true);
  assert.equal(rhythm.snapshot().tracks[0].playheadTick, 0);
  okay(engine.advance(2250));
  assert.equal(rhythm.snapshot().tracks[0].playheadTick, 480);
  const generation = rhythm.snapshot().tracks[0].generation;
  okay(rhythm.setTrackLoop(0, 100, 1060));
  assert.equal(rhythm.snapshot().tracks[0].playheadTick, 100);
  assert.ok(rhythm.snapshot().tracks[0].generation > generation);
  okay(engine.advance(2500));
  assert.equal(rhythm.snapshot().tracks[0].playheadTick, 580);
  okay(rhythm.toggleTrack(0));
  assert.equal(rhythm.snapshot().tracks[0].pending, 'stop');
  okay(rhythm.toggleTrack(0));
  assert.equal(rhythm.snapshot().tracks[0].pending, null);
  okay(rhythm.stop());
  assert.equal(rhythm.snapshot().tracks[0].playheadTick, 100);
  assert.equal(rhythm.snapshot().tracks[0].running, false);
});

test('physical pads, Arp contexts and track owners share notes; Stop cleans only track ownership', async () => {
  const engine = await fresh(), rhythm = engine.rhythm;
  okay(engine.advance(0)); okay(rhythm.toggleTrack(0)); okay(rhythm.start());
  okay(engine.press({ pad: 0, notes: [60], channel: 0, mask: 5, velocity: 96 }));
  const arp = okay(rhythm.acquire(note())).handle;
  const track = okay(rhythm.acquire(note('track', { track: 0 }))).handle;
  assert.notEqual(arp, track);
  assert.deepEqual(wire(engine.drain()), [[0, 0x90, 60, 96], [2, 0x90, 60, 96]]);
  assert.ok(engine.voices().every(voice => voice.owners === 3));
  okay(rhythm.stop());
  assert.equal(rhythm.snapshot().generatedCount, 1);
  assert.ok(engine.voices().every(voice => voice.owners === 2));
  assert.deepEqual(engine.drain(), []);
  okay(rhythm.release(arp));
  assert.ok(engine.voices().every(voice => voice.owners === 1));
  assert.deepEqual(engine.drain(), []);
  okay(engine.release(0));
  assert.deepEqual(wire(engine.drain()), [[0, 0x80, 60, 0], [2, 0x80, 60, 0]]);
  assert.equal(engine.stats().activeVoices, 0);
});

test('clear and panic reject stale generator runs without cancelling replacement owners', async () => {
  const engine = await fresh(), rhythm = engine.rhythm;
  okay(engine.advance(0));
  const oldRun = rhythm.generation('arp1');
  const first = okay(rhythm.acquire(note('arp1', { run: oldRun }))).handle;
  okay(rhythm.clearSource('arp1'));
  assert.equal(rhythm.acquire(note('arp1', { run: oldRun })).status, 'stale-run');
  const nextRun = rhythm.generation('arp1');
  assert.notEqual(nextRun, oldRun);
  const second = okay(rhythm.acquire(note('arp1', { run: nextRun }))).handle;
  assert.notEqual(first, second);
  okay(rhythm.release(first));
  assert.equal(rhythm.snapshot().generatedCount, 1, 'A stale handle cannot release a replacement owner.');
  okay(engine.panic());
  assert.equal(rhythm.snapshot().generatedCount, 0);
  assert.equal(rhythm.acquire(note('arp1', { run: nextRun })).status, 'stale-run');
  assert.equal(rhythm.snapshot().transport, 'stopped');
  assert.equal(engine.stats().activeVoices, 0);
});

test('USB follows F8 exactly and applies Start, Stop, Continue and song position separately', async () => {
  const engine = await fresh(), rhythm = engine.rhythm;
  okay(engine.advance(0)); okay(rhythm.configure({ source: 'usb' }));
  assert.equal(rhythm.snapshot().clockState, 'waiting');
  assert.equal(rhythm.acquire(note()).status, 'clock-not-ready');
  okay(rhythm.midi(0xf8, 0, 0, 0));
  assert.equal(rhythm.snapshot().clockState, 'locked');
  okay(rhythm.midi(0xfa, 0, 0, 0));
  for (let pulse = 1; pulse <= 24; pulse++) okay(rhythm.midi(0xf8, 0, 0, pulse * 20));
  assert.equal(rhythm.snapshot().songTick, 960);
  okay(rhythm.midi(0xfc, 0, 0, 480));
  assert.equal(rhythm.snapshot().transport, 'paused');
  okay(rhythm.midi(0xf8, 0, 0, 500));
  assert.equal(rhythm.snapshot().songTick, 960);
  okay(rhythm.midi(0xfb, 0, 0, 500)); okay(rhythm.midi(0xf8, 0, 0, 520));
  assert.equal(rhythm.snapshot().songTick, 1000);
  const clockTick = rhythm.snapshot().tick;
  okay(rhythm.midi(0xf2, 7, 1, 520));
  assert.equal(rhythm.snapshot().songTick, 135 * 240);
  assert.equal(rhythm.snapshot().tick, clockTick, 'Seeking song position never seeks the shared clock.');
  okay(rhythm.midi(0xfa, 0, 0, 520));
  assert.equal(rhythm.snapshot().songTick, 0);
  assert.equal(rhythm.snapshot().tick, clockTick);
  assert.deepEqual(engine.drain(), [], 'Received clock/transport must not be echoed to output.');
});

test('USB source loss cancels generated owners once, preserves live pads and resumes without catch-up', async () => {
  const engine = await fresh(), rhythm = engine.rhythm;
  okay(engine.advance(0));
  okay(engine.press({ pad: 0, notes: [60], channel: 0, mask: 1, velocity: 96 }));
  okay(rhythm.acquire(note('arp1', { mask: 1 })));
  engine.drain();
  okay(rhythm.configure({ source: 'usb' }));
  assert.equal(rhythm.snapshot().generatedCount, 0);
  assert.equal(engine.isHeld(0), true);
  assert.deepEqual(engine.drain(), []);
  okay(rhythm.midi(0xf8, 0, 0, 10));
  okay(rhythm.acquire(note('arp1', { mask: 1 })));
  const run = rhythm.generation('arp1');
  okay(engine.advance(509));
  assert.equal(rhythm.snapshot().clockState, 'locked');
  okay(engine.advance(510));
  assert.equal(rhythm.snapshot().clockState, 'lost');
  assert.equal(rhythm.snapshot().generatedCount, 0);
  assert.equal(engine.voices()[0].owners, 1);
  assert.equal(rhythm.acquire(note('arp1', { run })).status, 'stale-run');
  const generation = rhythm.generation('arp1'), tick = rhythm.snapshot().tick;
  okay(engine.advance(1510));
  assert.equal(rhythm.generation('arp1'), generation, 'Loss cleanup must occur once.');
  assert.equal(rhythm.snapshot().tick, tick, 'Lost USB clock never falls back to internal time.');
  okay(rhythm.midi(0xf8, 0, 0, 1520));
  assert.equal(rhythm.snapshot().tick, tick + 40);
  assert.equal(rhythm.snapshot().clockState, 'locked');
  assert.deepEqual(engine.drain(), []);
  okay(engine.release(0));
  assert.deepEqual(wire(engine.drain()), [[0, 0x80, 60, 0]]);
});

test('parsed USB MIDI times only cable zero, retains fragments and rejects backward timestamps', async () => {
  const engine = await fresh(), rhythm = engine.rhythm;
  okay(engine.advance(0)); okay(rhythm.configure({ source: 'usb' }));
  let received = engine.receive([0x1f, 0xf8, 0, 0], { now: 1 });
  assert.equal(received.length, 1);
  assert.equal(received[0].destination, 1);
  assert.equal(rhythm.snapshot().clockState, 'waiting');
  assert.equal(rhythm.snapshot().tick, 0);
  assert.deepEqual(engine.receive([0x0f, 0xf8], { now: 10 }), []);
  assert.equal(engine.receiveStats().fragmentBytes, 2);
  received = engine.receive([0, 0], { now: 10 });
  assert.equal(received.length, 1);
  assert.equal(rhythm.snapshot().tick, 40);
  assert.equal(rhythm.snapshot().clockState, 'locked');
  engine.receive([0x03, 0xf2, 6, 0], { now: 10 });
  assert.equal(rhythm.snapshot().songTick, 1440);
  engine.receive([0x0f, 0xfa, 0, 0], { now: 10 });
  assert.equal(rhythm.snapshot().songTick, 0);
  assert.equal(rhythm.snapshot().transport, 'playing');
  engine.receive([0x1f, 0xf8, 0, 0], { now: 20 });
  assert.equal(rhythm.snapshot().tick, 40);
  assert.throws(() => engine.receive([0x0f, 0xf8, 0, 0], { now: 19 }), /time-backward/);
  assert.equal(rhythm.snapshot().nowMs, 20);
});

test('the browser command adapter synchronizes C before queueing just after a bar boundary', async () => {
  const engine = await fresh();
  let time = 0;
  const model = createRhythmLabModel({ rhythm: engine.rhythm, now: () => time, onEvents: () => engine.drain() });
  okay(model.start()); okay(engine.advance(1999));
  time = 2001; okay(model.toggleTrack(0));
  assert.equal(engine.rhythm.snapshot().tracks[0].pendingTick, 7680,
    'A fresh input must queue the next bar, not the boundary already crossed since the last render.');
  assert.ok(engine.rhythm.snapshot().songTick >= 3840);
});

test('the browser retires externally invalidated source labels while retaining other source owners', async () => {
  const engine = await fresh();
  const model = createRhythmLabModel({ rhythm: engine.rhythm, now: () => 0, onEvents: () => engine.drain() });
  okay(model.acquire(note('arp1'))); okay(model.acquire(note('arp2')));
  okay(engine.rhythm.clearSource('arp1'));
  assert.equal(engine.rhythm.snapshot().generatedCount, 1);
  assert.deepEqual(model.snapshot().previewOwners.map(owner => owner.source), ['arp2']);
});
