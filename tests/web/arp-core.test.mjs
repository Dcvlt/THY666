import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createCore } from '../../web/core.js';

const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));
const okay = result => { assert.equal(result.ok, true, result.status); return result; };
const notesOn = events => events.filter(event => (event.status & 0xf0) === 0x90 && event.data2 > 0);
const wire = events => events.map(({ destination, status, data1, data2 }) => [destination, status, data1, data2]);
const layout = () => Array.from({ length: 96 }, (_, pad) => ({
  notes: pad < 48 ? [60, 64, 67, 0, 0, 0, 0] : [62, 0, 0, 0, 0, 0, 0],
  mask: pad < 48 ? 7 : 1, color: 1,
}));
async function fresh({ pads = layout(), degrees = [0, 2, 4, 5, 7, 9, 11] } = {}) {
  const engine = await createCore({ bytes });
  okay(engine.firmware.layout(pads, { anchor: 60, degrees, root: 0 }));
  okay(engine.firmware.route(0, 0, 1)); okay(engine.firmware.route(1, 1, 1));
  okay(engine.firmware.performance({ melodyLayout: 0 }));
  okay(engine.advance(0));
  return engine;
}
const enable = (engine, index = 0, changes = {}) => okay(engine.arp.configure(index,
  { enabled: true, channel: 2 + index, mask: 1, rateTicks: 240, gatePercent: 50, ...changes }));

test('Arps start disabled; Hold and Layer OFF emit only generated notes with exact gates', async () => {
  const engine = await fresh();
  assert.equal(engine.arp.snapshot(0).enabled, false);
  assert.equal(engine.arp.snapshot(1).enabled, false);
  enable(engine); okay(engine.firmware.pressPad(0));
  assert.equal(engine.isHeld(0), true);
  assert.deepEqual(engine.owner(0), [], 'A processed pad holds its input without owning a dry note.');
  assert.deepEqual(wire(engine.drain()), [[0, 0x92, 60, 96]]);
  assert.equal(engine.arp.snapshot(0).inputs, 1);
  assert.equal(engine.arp.snapshot(0).step, 0);
  okay(engine.advance(63));
  assert.deepEqual(wire(engine.drain()), [[0, 0x82, 60, 0]]);
  okay(engine.advance(125));
  assert.deepEqual(wire(engine.drain()), [[0, 0x92, 64, 96]]);
  okay(engine.release(0));
  assert.deepEqual(wire(engine.drain()), [[0, 0x82, 64, 0]]);
  assert.equal(engine.arp.snapshot(0).inputs, 0);
  assert.equal(engine.arp.snapshot(0).phrases, 0);
  assert.equal(engine.arp.snapshot(0).step, null);
  okay(engine.advance(1000)); assert.deepEqual(engine.drain(), []);
});

test('both enabled Arps keep independent channels, destinations and generated owners', async () => {
  const engine = await fresh();
  enable(engine, 0, { channel: 2, mask: 1 });
  enable(engine, 1, { channel: 4, mask: 4 });
  okay(engine.firmware.pressPad(0));
  assert.deepEqual(wire(engine.drain()), [[0, 0x92, 60, 96], [2, 0x94, 60, 96]]);
  assert.equal(engine.arp.snapshot(0).owners, 1);
  assert.equal(engine.arp.snapshot(1).owners, 1);
  okay(engine.arp.configure(0, { enabled: false }));
  assert.deepEqual(wire(engine.drain()), [[0, 0x82, 60, 0]]);
  assert.equal(engine.arp.snapshot(1).inputs, 1);
  assert.equal(engine.arp.snapshot(1).owners, 1);
  okay(engine.release(0));
  assert.deepEqual(wire(engine.drain()), [[2, 0x84, 60, 0]]);
});

test('Layer ON adds one original chord copy even when both Arps subscribe', async () => {
  const engine = await fresh();
  enable(engine, 0, { channel: 0, layer: true });
  enable(engine, 1, { channel: 0, layer: true });
  okay(engine.firmware.pressPad(0));
  assert.deepEqual(wire(engine.drain()), [[0, 0x90, 60, 96], [0, 0x90, 64, 96], [0, 0x90, 67, 96]]);
  assert.deepEqual(engine.owner(0).map(owner => owner.note), [60, 64, 67]);
  assert.equal(engine.voices().find(voice => voice.note === 60).owners, 3, 'One dry owner plus two Arp owners.');
  assert.equal(engine.voices().find(voice => voice.note === 64).owners, 1);
  okay(engine.advance(63));
  assert.deepEqual(engine.drain(), [], 'Generated gate release must preserve the dry owner.');
  assert.equal(engine.voices().find(voice => voice.note === 60).owners, 1);
  okay(engine.release(0));
  assert.deepEqual(wire(engine.drain()), [[0, 0x80, 60, 0], [0, 0x80, 64, 0], [0, 0x80, 67, 0]]);
});

test('hybrid grouping joins melody to the newest held chord and falls back without restarting its phase', async () => {
  const pads = layout(); pads[1].notes = [65, 69, 72, 0, 0, 0, 0];
  const engine = await fresh({ pads });
  enable(engine); okay(engine.firmware.pressPad(0)); engine.drain();
  okay(engine.advance(10)); okay(engine.firmware.pressPad(48));
  assert.equal(engine.arp.snapshot(0).inputs, 2);
  assert.equal(engine.arp.snapshot(0).phrases, 1);
  assert.equal(engine.arp.snapshot(0).selectedChord, 0);
  okay(engine.advance(125));
  assert.deepEqual(notesOn(engine.drain()).map(event => event.data1), [62], 'D joins C–E–G without resetting the C phrase.');
  okay(engine.advance(130)); okay(engine.firmware.pressPad(1));
  assert.equal(engine.arp.snapshot(0).selectedChord, 1);
  assert.equal(engine.arp.snapshot(0).phrases, 2);
  okay(engine.release(1));
  assert.equal(engine.arp.snapshot(0).selectedChord, 0);
  assert.equal(engine.arp.snapshot(0).phrases, 1);
  okay(engine.release(0));
  assert.equal(engine.arp.snapshot(0).selectedChord, null);
  assert.equal(engine.arp.snapshot(0).phrases, 1, 'The still-held single note gets its own phrase.');
  okay(engine.release(48));
  assert.equal(engine.arp.snapshot(0).phrases, 0);
  assert.equal(engine.stats().activeVoices, 0);
});

test('normal menu navigation and sequencer Stop leave the live Arp phrase running', async () => {
  const engine = await fresh();
  enable(engine); okay(engine.firmware.pressPad(0)); engine.drain();
  okay(engine.state.navigate('select')); okay(engine.state.navigate('right'));
  okay(engine.rhythm.start()); okay(engine.advance(125));
  assert.deepEqual(notesOn(engine.drain()).map(event => event.data1), [64]);
  okay(engine.rhythm.stop());
  assert.equal(engine.arp.snapshot(0).inputs, 1);
  okay(engine.state.navigate('select')); okay(engine.advance(250));
  assert.deepEqual(notesOn(engine.drain()).map(event => event.data1), [67]);
  okay(engine.release(0));
  assert.equal(engine.stats().activeVoices, 0);
});

test('Clear and disable forget held inputs; re-enabling never re-adopts a still-held pad', async () => {
  const engine = await fresh();
  enable(engine); okay(engine.firmware.pressPad(0)); engine.drain();
  okay(engine.arp.clear(0));
  assert.deepEqual(wire(engine.drain()), [[0, 0x82, 60, 0]]);
  assert.equal(engine.isHeld(0), true);
  assert.equal(engine.arp.snapshot(0).enabled, true);
  okay(engine.advance(125)); okay(engine.firmware.pressPad(0));
  assert.deepEqual(engine.drain(), []);
  assert.equal(engine.arp.snapshot(0).inputs, 0);
  okay(engine.release(0)); okay(engine.firmware.pressPad(0));
  assert.deepEqual(notesOn(engine.drain()).map(event => event.data1), [60]);
  okay(engine.arp.configure(0, { enabled: false })); engine.drain();
  okay(engine.arp.configure(0, { enabled: true })); okay(engine.advance(250));
  assert.equal(engine.arp.snapshot(0).inputs, 0);
  assert.deepEqual(engine.drain(), []);
  okay(engine.release(0)); okay(engine.firmware.pressPad(0));
  assert.deepEqual(notesOn(engine.drain()).map(event => event.data1), [60]);
});

test('input zones follow physical column-major pad layout and leave unselected pads musical', async () => {
  const engine = await fresh();
  enable(engine, 0, { sourceZones: 1 });
  okay(engine.firmware.pressPad(4)); // Left column 2, row 1: still in the first four-column block.
  assert.deepEqual(wire(engine.drain()), [[0, 0x92, 60, 96]]);
  assert.equal(engine.arp.snapshot(0).inputs, 1);
  okay(engine.release(4)); engine.drain();
  okay(engine.firmware.pressPad(16)); // Left column 5: next block, outside the source zone.
  assert.deepEqual(wire(engine.drain()), [[0, 0x90, 60, 96], [0, 0x90, 64, 96], [0, 0x90, 67, 96]]);
  assert.equal(engine.arp.snapshot(0).inputs, 0);
  okay(engine.release(16));
});

test('USB waiting and loss clear Arp input, preserve unrelated live pads and never replay after relock', async () => {
  const engine = await fresh();
  enable(engine, 0, { sourceZones: 7 });
  okay(engine.rhythm.configure({ source: 'usb' }));
  okay(engine.firmware.pressPad(0));
  assert.equal(engine.arp.snapshot(0).inputs, 0);
  assert.deepEqual(engine.drain(), []);
  okay(engine.rhythm.midi(0xf8, 0, 0, 0));
  assert.deepEqual(engine.drain(), []);
  okay(engine.release(0)); okay(engine.firmware.pressPad(0));
  assert.deepEqual(wire(engine.drain()), [[0, 0x92, 60, 96]]);
  okay(engine.advance(500));
  assert.equal(engine.rhythm.snapshot().clockState, 'lost');
  assert.equal(engine.arp.snapshot(0).inputs, 0);
  assert.equal(engine.arp.snapshot(0).owners, 0);
  assert.deepEqual(wire(engine.drain()), [[0, 0x82, 60, 0]]);
  okay(engine.firmware.pressPad(48));
  assert.deepEqual(wire(engine.drain()), [[0, 0x91, 62, 96]], 'An unprocessed melody pad still plays during clock loss.');
  okay(engine.rhythm.midi(0xf8, 0, 0, 510));
  assert.equal(engine.arp.snapshot(0).inputs, 0);
  assert.deepEqual(engine.drain(), []);
  okay(engine.release(0)); okay(engine.release(48)); engine.drain();
  okay(engine.firmware.pressPad(0));
  assert.deepEqual(wire(engine.drain()), [[0, 0x92, 60, 96]]);
});

test('Chord phrases apply signed scale-degree offsets to every chord tone', async () => {
  const engine = await fresh();
  enable(engine, 0, { type: 'chords', offsets: [0, 1, 2, -1, 7, 0, 0, 0] });
  okay(engine.firmware.pressPad(0));
  assert.deepEqual(notesOn(engine.drain()).map(event => event.data1), [60, 64, 67]);
  for (const [time, expected] of [[125, [62, 65, 69]], [250, [64, 67, 71]], [375, [59, 62, 65]]]) {
    okay(engine.advance(time));
    assert.deepEqual(notesOn(engine.drain()).map(event => event.data1), expected);
  }
  okay(engine.release(0));
  assert.equal(engine.arp.snapshot(0).owners, 0);
});

test('Chord phrases use the selected scale rather than a hard-coded major scale', async () => {
  const pads = layout(); pads[0].notes = [60, 63, 67, 0, 0, 0, 0];
  const engine = await fresh({ pads, degrees: [0, 2, 3, 5, 7, 8, 10] });
  enable(engine, 0, { type: 'chords', offsets: Array(8).fill(1) });
  okay(engine.firmware.pressPad(0));
  assert.deepEqual(notesOn(engine.drain()).map(event => event.data1), [62, 65, 68]);
});

test('standard Notes patterns have exact traversal and As Played follows input order', async () => {
  const patterns = new Map([
    ['up', [60, 64, 67, 60, 64, 67, 60, 64]],
    ['down', [67, 64, 60, 67, 64, 60, 67, 64]],
    ['up-down', [60, 64, 67, 64, 60, 64, 67, 64]],
    ['down-up', [67, 64, 60, 64, 67, 64, 60, 64]],
  ]);
  for (const [pattern, expected] of patterns) {
    const engine = await fresh();
    enable(engine, 0, { pattern }); okay(engine.firmware.pressPad(0));
    const played = notesOn(engine.drain()).map(event => event.data1);
    for (let step = 1; step < 8; step++) { okay(engine.advance(step * 125)); played.push(...notesOn(engine.drain()).map(event => event.data1)); }
    assert.deepEqual(played, expected, pattern);
  }
  const pads = layout();
  for (const [pad, pitch] of [[48, 67], [49, 60], [50, 64]]) pads[pad].notes[0] = pitch;
  const engine = await fresh({ pads });
  enable(engine, 0, { pattern: 'as-played' });
  okay(engine.firmware.pressPad(48)); okay(engine.firmware.pressPad(49)); okay(engine.firmware.pressPad(50));
  const played = notesOn(engine.drain()).map(event => event.data1);
  for (const time of [125, 250]) { okay(engine.advance(time)); played.push(...notesOn(engine.drain()).map(event => event.data1)); }
  assert.deepEqual(played, [67, 60, 64]);
});

test('step-mask rests advance phrase position and swing moves only the intervening onset', async () => {
  const masked = await fresh();
  enable(masked, 0, { stepMask: 1 }); okay(masked.firmware.pressPad(0));
  assert.deepEqual(notesOn(masked.drain()).map(event => event.data1), [60]);
  for (let step = 1; step < 8; step++) {
    okay(masked.advance(step * 125));
    assert.deepEqual(notesOn(masked.drain()), []);
    assert.equal(masked.arp.snapshot(0).step, step);
  }
  okay(masked.advance(1000));
  assert.deepEqual(notesOn(masked.drain()).map(event => event.data1), [67]);
  const swung = await fresh();
  enable(swung, 0, { swingPercent: 50 }); okay(swung.firmware.pressPad(0)); swung.drain();
  okay(swung.advance(125)); assert.deepEqual(notesOn(swung.drain()), []);
  okay(swung.advance(188)); assert.deepEqual(notesOn(swung.drain()).map(event => event.data1), [64]);
  okay(swung.advance(250)); assert.deepEqual(notesOn(swung.drain()).map(event => event.data1), [67]);
});

test('invalid Arp changes are atomic while a generated note is sounding', async () => {
  const engine = await fresh();
  enable(engine); okay(engine.firmware.pressPad(0)); engine.drain();
  const before = engine.arp.snapshot(0), voices = engine.voices();
  for (const changes of [{ enabled: 1 }, { type: 'unknown' }, { grouping: 'none' }, { rateTicks: 39 },
    { rateTicks: 3841 }, { gatePercent: 0 }, { swingPercent: 51 }, { sourceZones: 64 },
    { sourceZones: -1 }, { start: 'later' }, { restartOnChange: 'false' }, { layer: 1 },
    { channel: 16 }, { mask: 0, gatePercent: 75 }, { stepMask: 256 }, { pattern: 'random' },
    { offsets: [0] }, { offsets: Array(8).fill(25) }, { offsets: Array(8).fill(1.5) }]) {
    assert.equal(engine.arp.configure(0, changes).ok, false, JSON.stringify(changes));
    assert.deepEqual(engine.arp.snapshot(0), before);
    assert.deepEqual(engine.voices(), voices);
  }
  assert.equal(engine.arp.configure(2, { enabled: true }).ok, false);
  assert.equal(engine.arp.clear(-1).ok, false);
  assert.deepEqual(engine.drain(), []);
});

test('the original shaped press API resolves transpose and slot exclusions before Arp admission', async () => {
  const engine = await fresh();
  enable(engine);
  const shape = { spread: 1, inversion: 1, excluded: 2 };
  assert.deepEqual(engine.preview([60, 64, 67], { transpose: 2, shape }).notes, [62, 69]);
  okay(engine.press({ pad: 0, notes: [60, 64, 67], transpose: 2, shape, channel: 0, mask: 1, velocity: 90 }));
  assert.deepEqual(wire(engine.drain()), [[0, 0x92, 62, 90]]);
  okay(engine.advance(125));
  assert.deepEqual(notesOn(engine.drain()).map(event => event.data1), [69]);
  okay(engine.release(0));
  assert.equal(engine.arp.snapshot(0).inputs, 0);
  assert.equal(engine.stats().activeVoices, 0);
});
