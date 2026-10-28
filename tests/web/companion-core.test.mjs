import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createCore } from '../../web/core.js';
import { MENU_ROWS, STUDIO_MENU } from '../../web/studio-menu.js';
import { decodeCompanionSnapshot } from '../../web/companion-protocol.js';

const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));
const row = id => {
  const index = MENU_ROWS.findIndex(value => value.id === id);
  assert.notEqual(index, -1, id);
  return index;
};
const okay = result => assert.equal(result.ok, true, result.status || result.message);
const field = (packet, offset, count) => new TextDecoder().decode(packet.subarray(offset, offset + count)).split('\0')[0];
const word = (packet, offset) => new DataView(packet.buffer, packet.byteOffset).getUint32(offset, true);
const short = (packet, offset) => new DataView(packet.buffer, packet.byteOffset).getUint16(offset, true);
const pitches = (packet, offset) => Array.from({ length: 128 }, (_, note) => note)
  .filter(note => packet[offset + Math.floor(note / 8)] & (1 << (note % 8)));
const grbToRGB = color => [(color >>> 8) & 255, color >>> 16, color & 255];
async function fresh() {
  const engine = await createCore({ bytes });
  okay(engine.firmware.layout(Array.from({ length: 96 }, (_, pad) => ({
    notes: pad < 48 ? [60, 64, 67, 0, 0, 0, 0] : [62, 0, 0, 0, 0, 0, 0],
    mask: pad < 48 ? 7 : 1, color: 1,
  }))));
  okay(engine.advance(0));
  return engine;
}

test('offline companion copies the latest C OLED and exact musical state without touching held ownership', async () => {
  const engine = await fresh();
  assert.equal(engine.state.companionSnapshot(0), null, 'An unrendered preview has no actual C frame.');
  for (const value of [-1, 2 ** 32, 1.5, NaN, null, '1'])
    assert.equal(engine.state.companionSnapshot(value), null);
  okay(engine.expression.configure({ enabled: true }));
  okay(engine.firmware.pressPad(0));
  engine.drain();
  const oled = engine.state.frame({ now: 25, animations: false });
  const owner = engine.owner(0), voices = engine.expression.voices();
  const settings = engine.settings.encode(), state = engine.state.info();
  const packet = engine.state.companionSnapshot(27);
  assert.ok(packet instanceof Uint8Array);
  assert.equal(packet.length, 1680);
  assert.equal(field(packet, 0, 8), 'THYCMP1');
  assert.equal(short(packet, 8), 1);
  assert.equal(short(packet, 10), 1680);
  assert.equal(word(packet, 16), 27);
  assert.equal(word(packet, 20), 25, 'Snapshotting does not render a newer OLED frame.');
  assert.deepEqual(packet.subarray(136, 1160), oled);
  assert.equal(field(packet, 32, 16), '0'.repeat(16));
  assert.equal(field(packet, 48, 24), '0'.repeat(24));
  assert.equal(field(packet, 72, 64), engine.state.schemaHash());
  assert.deepEqual(pitches(packet, 1485), [60, 64, 67]);
  assert.equal(field(packet, 1517, 32), 'C MAJ');
  assert.equal(packet[1604] & 1, 1);
  assert.equal(packet[1618], 1);
  assert.equal(packet[1619], 3);
  assert.equal(packet[1658], 1);
  assert.deepEqual(engine.owner(0), owner);
  assert.deepEqual(engine.expression.voices(), voices);
  assert.deepEqual(engine.state.info(), state);
  assert.deepEqual(engine.settings.encode(), settings);
  assert.deepEqual(engine.drain(), []);
  const savedCopy = packet.slice();
  okay(engine.release(0)); engine.drain();
  engine.state.frame({ now: 28, animations: false });
  const next = engine.state.companionSnapshot(28);
  assert.deepEqual(pitches(next, 1485), []);
  assert.equal(next[1618], 0);
  assert.deepEqual(packet, savedCopy, 'Returned JS bytes do not alias reusable WASM staging.');
});

test('offline companion carries the physical menu draft, explicit target and actual center levels', async () => {
  const engine = await fresh();
  const channel = row('midi.channel');
  okay(engine.state.set(row('midi.routeTarget'), 2));
  okay(engine.state.focus(STUDIO_MENU.groups.findIndex(group => group.id === MENU_ROWS[channel].group), channel));
  okay(engine.state.begin(channel));
  okay(engine.state.stage(5));
  const oled = engine.state.frame({ now: 100, animations: false });
  const before = engine.state.info(), levels = engine.state.centerLevels();
  const packet = engine.state.companionSnapshot(100);
  assert.deepEqual(packet.subarray(136, 1160), oled);
  assert.equal(packet[1583], 1);
  assert.equal(short(packet, 1591), channel);
  assert.equal(short(packet, 1593), channel);
  assert.equal(packet[1595], 2);
  assert.equal(packet[1596], 2);
  assert.equal(packet[1599], 2);
  assert.equal(word(packet, 1600), 5);
  assert.equal(word(packet, 1664), 2, 'Committed route differs from the still-unapplied draft.');
  assert.deepEqual(Array.from(packet.subarray(1448, 1467)),
    Array.from(levels, value => Math.floor((value * 255 + 2047) / 4095)));
  assert.deepEqual(engine.state.info(), before);
  assert.deepEqual(engine.drain(), []);
  okay(engine.state.cancel());
});

test('offline companion separates the Arp phrase pool from the notes sounding in each gate', async () => {
  const engine = await fresh();
  okay(engine.arp.configure(0, { enabled: true, layer: false, rateTicks: 240, gatePercent: 50 }));
  okay(engine.firmware.pressPad(0)); engine.drain();
  engine.state.frame({ now: 0, animations: false });
  const first = engine.state.companionSnapshot(0);
  assert.deepEqual(pitches(first, 1485), [60]);
  assert.deepEqual(pitches(first, 1501), [60, 64, 67]);
  assert.equal(first[1604] & 2, 2);
  assert.equal(field(first, 1517, 32), 'C MAJ');
  okay(engine.advance(63)); engine.drain();
  engine.state.frame({ now: 63, animations: false });
  const gap = engine.state.companionSnapshot(63);
  assert.deepEqual(pitches(gap, 1485), []);
  assert.deepEqual(pitches(gap, 1501), [60, 64, 67]);
  assert.equal(gap[1618], 1);
  assert.equal(field(gap, 1517, 32), 'C MAJ');
  assert.deepEqual(engine.drain(), []);
  okay(engine.release(0)); engine.drain();
});

test('offline companion uses C sequencer pad colors while preserving its editor and song', async () => {
  const engine = await fresh();
  okay(engine.sequencer.createTrack());
  okay(engine.sequencer.configureView({ selectedStep: 3 }));
  engine.state.frame({ now: 50, animations: false });
  const base = engine.firmware.colors(50), sequence = engine.sequencer.colors();
  const song = engine.sequencer.exportBytes(), state = engine.sequencer.snapshot();
  const packet = engine.state.companionSnapshot(50);
  assert.equal(packet[1589], 1);
  assert.equal(packet[1660], state.selected);
  for (let pad = 0; pad < 96; ++pad) {
    const expected = sequence[pad * 4]
      ? Array.from(sequence.subarray(pad * 4 + 1, pad * 4 + 4), value => Math.floor((value * 8 + 2047) / 4095))
      : grbToRGB(base[pad]);
    assert.deepEqual(Array.from(packet.subarray(1160 + pad * 3, 1163 + pad * 3)), expected);
  }
  assert.deepEqual(engine.sequencer.exportBytes(), song);
  assert.deepEqual(engine.sequencer.snapshot(), state);
  assert.deepEqual(engine.drain(), []);
});

test('actual C reset confirmation projects its frozen scope and nonce without treating a highlighted choice as consent', async () => {
  const engine = await fresh(), center = row('lights.center');
  const original = engine.state.get(center), custom = original === 4 ? 5 : 4;
  okay(engine.state.set(center, custom));
  const configured = engine.settings.encode();
  const capture = now => {
    engine.state.frame({ now, animations: false });
    return decodeCompanionSnapshot(engine.state.companionSnapshot(now));
  };
  const before = capture(10);
  assert.equal(before.resetOpen, false);
  okay(engine.state.begin(row('lights.resetOptions')));
  const pending = capture(11);
  assert.equal(pending.resetOpen, true);
  assert.equal(pending.resetChoice, 0);
  assert.equal(pending.resetGroup, STUDIO_MENU.groups.findIndex(group => group.id === 'lights'));
  assert.equal(pending.resetTrack, 255);
  assert.ok(pending.resetEpoch > before.resetEpoch);
  assert.deepEqual(engine.settings.encode(), configured, 'Opening a C confirmation changes no persisted option.');
  okay(engine.state.navigate('down'));
  const selected = capture(12);
  assert.equal(selected.resetChoice, 1);
  assert.equal(selected.resetEpoch, pending.resetEpoch);
  assert.deepEqual(engine.settings.encode(), configured, 'Physical selection alone is not confirmation.');
  assert.equal(engine.state.set(center, original).ok, false, 'The frozen confirmation blocks competing writes.');
  okay(engine.state.navigate('left'));
  const cancelled = capture(13);
  assert.equal(cancelled.resetOpen, false);
  assert.equal(engine.state.get(center), custom);
  okay(engine.state.begin(row('lights.resetOptions')));
  const replacement = capture(14);
  assert.ok(replacement.resetEpoch > pending.resetEpoch, 'A new C confirmation receives a new transaction identity.');
  okay(engine.state.navigate('down'));
  okay(engine.state.navigate('right'));
  assert.equal(capture(15).resetOpen, false);
  assert.equal(engine.state.get(center), original);
  assert.deepEqual(engine.drain(), [], 'Inspection and menu reset never audition notes.');
});
