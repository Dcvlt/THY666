import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createCore } from '../../web/core.js';
import { MENU_ROWS, STUDIO_MENU } from '../../web/studio-menu.js';

const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));
const index = id => MENU_ROWS.findIndex(row => row.id === id);
const choose = (state, id, key) => state.set(index(id), MENU_ROWS[index(id)].options.findIndex(option => option.value === key));
const set = (state, id, value) => state.set(index(id), value);
const layout = Array.from({ length: 96 }, () => ({ notes: [60, 0, 0, 0, 0, 0, 0], mask: 1, color: 1 }));
async function fresh() {
  const engine = await createCore({ bytes });
  assert.ok(engine.firmware.layout(layout).ok);
  return engine;
}

test('Split Sides routes retain legacy velocity values without exposing duplicate expression editors', async () => {
  const engine = await fresh(), { state, firmware } = engine;
  state.designMode(false);
  for (const id of ['midi.routingProfile', 'midi.routeTarget', 'midi.channel', 'midi.destinations'])
    assert.equal(state.focus(STUDIO_MENU.groups.findIndex(group=>group.id==='midi'), index(id)).ok, true, id);
  for (const id of ['midi.velocity','midi.pressure','midi.pressureBend','midi.bendRange','midi.bendSetup'])
    assert.equal(state.focus(STUDIO_MENU.groups.findIndex(group=>group.id==='midi'), index(id)).ok, false, id);
  assert.ok(choose(state, 'midi.routeTarget', 'left').ok);
  assert.ok(set(state, 'midi.channel', 16).ok);
  assert.ok(choose(state, 'midi.destinations', 'din').ok);
  assert.ok(set(state, 'midi.velocity', 53).ok);
  assert.ok(set(state, 'midi.pressure', 0).ok);
  assert.ok(choose(state, 'midi.routeTarget', 'right').ok);
  assert.ok(set(state, 'midi.channel', 9).ok);
  assert.ok(choose(state, 'midi.destinations', 'both-usb').ok);
  assert.ok(set(state, 'midi.velocity', 111).ok);
  assert.ok(set(state, 'midi.pressure', 1).ok);
  assert.deepEqual(engine.drain(), [], 'Editing routes emits no MIDI.');
  assert.ok(firmware.pressPad(0).ok);
  assert.deepEqual(engine.drain().map(e => [e.destination, e.status, e.data1, e.data2]), [[2, 0x9f, 60, 53]]);
  assert.ok(firmware.pressPad(48).ok);
  assert.deepEqual(engine.drain().map(e => [e.destination, e.status, e.data1, e.data2]), [[0, 0x98, 60, 111], [1, 0x98, 60, 111]]);
  assert.ok(firmware.pressurePad(0, 75).ok);
  assert.deepEqual(engine.drain(), []);
  assert.ok(firmware.pressurePad(48, 75).ok);
  assert.deepEqual(engine.drain().map(e => [e.destination, e.status, e.data1, e.data2]), [[0, 0xa8, 60, 75], [1, 0xa8, 60, 75]]);
  assert.ok(engine.release(0).ok);
  assert.ok(engine.release(48).ok);
  assert.equal(engine.drain().length, 3);
  assert.equal(engine.stats().heldPads, 0);
});

test('a route editor retains its original target across selection and profile changes', async () => {
  const { state } = await fresh();
  assert.ok(choose(state, 'midi.routeTarget', 'left').ok);
  assert.ok(state.begin(index('midi.channel')).ok);
  assert.ok(state.stage(12).ok);
  assert.ok(choose(state, 'midi.routeTarget', 'right').ok);
  assert.ok(choose(state, 'midi.routingProfile', 'whole-board').ok);
  assert.ok(state.commit().ok);
  assert.equal(state.instance(index('midi.channel'), 1), 12);
  assert.equal(state.instance(index('midi.channel'), 2), 2);
  assert.equal(state.get(index('midi.routeTarget')), 0);
  assert.equal(state.get(index('midi.channel')), 1);
  assert.ok(choose(state, 'midi.routingProfile', 'split-sides').ok);
  assert.equal(state.get(index('midi.routeTarget')), 1);
  assert.equal(state.get(index('midi.channel')), 12);
});

test('route and pressure edits cannot redirect a held owner or its release', async () => {
  const engine = await fresh(), { state, firmware } = engine;
  assert.ok(set(state, 'midi.pressure', 1).ok);
  assert.ok(firmware.pressPad(0).ok);
  const original = engine.owner(0);
  engine.drain();
  assert.ok(set(state, 'midi.channel', 13).ok);
  assert.ok(choose(state, 'midi.destinations', 'din').ok);
  assert.ok(set(state, 'midi.pressure', 0).ok);
  assert.ok(choose(state, 'midi.routingProfile', 'whole-board').ok);
  assert.deepEqual(engine.owner(0), original);
  assert.deepEqual(engine.drain(), []);
  assert.ok(firmware.pressurePad(0, 65).ok);
  assert.deepEqual(engine.drain().map(e => [e.destination, e.status]), [[0, 0xa0]]);
  assert.ok(engine.release(0).ok);
  assert.deepEqual(engine.drain().map(e => [e.destination, e.status]), [[0, 0x80]]);
  assert.ok(choose(state, 'midi.routingProfile', 'split-sides').ok);
  assert.ok(firmware.pressPad(0).ok);
  assert.deepEqual(engine.drain().map(e => [e.destination, e.status]), [[2, 0x9c]]);
  assert.ok(firmware.pressurePad(0, 80).ok);
  assert.deepEqual(engine.drain(), []);
  assert.ok(engine.release(0).ok);
  assert.deepEqual(engine.drain().map(e => [e.destination, e.status]), [[2, 0x8c]]);
});

test('Six Zones follow four physical columns per zone across all four rows', async () => {
  const engine = await fresh(), { state, firmware } = engine;
  assert.ok(choose(state, 'midi.routingProfile', 'six-zones').ok);
  const targets = ['left-1', 'left-2', 'left-3', 'right-1', 'right-2', 'right-3'];
  for (const [zone, target] of targets.entries()) {
    assert.ok(choose(state, 'midi.routeTarget', target).ok);
    assert.ok(set(state, 'midi.channel', zone + 4).ok);
    assert.ok(choose(state, 'midi.destinations', 'din').ok);
  }
  for (let side = 0; side < 2; side++) for (let column = 0; column < 12; column++) for (let row = 0; row < 4; row++) {
    const pad = side * 48 + column * 4 + row;
    const channel = side * 3 + Math.floor(column / 4) + 3;
    assert.ok(firmware.pressPad(pad).ok);
    assert.deepEqual(engine.drain().map(e => [e.destination, e.status]), [[2, 0x90 | channel]], `side ${side}, column ${column}, row ${row}`);
    assert.ok(engine.release(pad).ok);
    assert.deepEqual(engine.drain().map(e => [e.destination, e.status]), [[2, 0x80 | channel]]);
  }
  assert.equal(engine.stats().heldPads, 0);
});

test('editing one route expression field preserves inheritance of the other', async () => {
  const { state, firmware } = await fresh();
  assert.ok(set(state, 'midi.velocity', 71).ok);
  assert.ok(set(state, 'performance.aftertouch', 1).ok);
  assert.equal(firmware.padRoute(1).pressureEnabled, 1);
  assert.ok(choose(state, 'midi.routeTarget', 'right').ok);
  assert.ok(set(state, 'midi.pressure', 0).ok);
  assert.ok(set(state, 'performance.velocityTarget', 2).ok);
  assert.ok(set(state, 'performance.velocity', 105).ok);
  assert.equal(firmware.padRoute(1).velocity, 71);
  assert.equal(firmware.padRoute(2).velocity, 96);
  assert.equal(state.get(index('performance.velocity')), 105);
  assert.equal(firmware.padRoute(2).pressureEnabled, 0);
});

test('bench transpose and split-side edits preserve independently configured Whole and Zone routes', async () => {
  const { state, firmware } = await fresh();
  assert.ok(choose(state, 'midi.routingProfile', 'whole-board').ok);
  assert.ok(set(state, 'midi.channel', 8).ok);
  assert.ok(choose(state, 'midi.routingProfile', 'six-zones').ok);
  assert.ok(choose(state, 'midi.routeTarget', 'left-2').ok);
  assert.ok(set(state, 'midi.channel', 12).ok);
  assert.ok(choose(state, 'midi.destinations', 'din').ok);
  const whole = firmware.padRoute(0), zone = firmware.padRoute(4);
  assert.ok(firmware.editSideRoute(0, 15, 5).ok);
  assert.ok(firmware.editSideRoute(1, 8, 1).ok);
  assert.ok(firmware.transpose(7).ok);
  assert.deepEqual(firmware.padRoute(0), whole);
  assert.deepEqual(firmware.padRoute(4), zone);
  assert.equal(firmware.padRoute(1).channel, 15);
  assert.equal(firmware.padRoute(2).channel, 8);
  assert.equal(firmware.activeRoutes().length, 6);
  assert.deepEqual(firmware.routeForPad(16), zone);
  assert.deepEqual(firmware.previewPad(16).notes, [67]);
  assert.ok(choose(state, 'midi.routingProfile', 'whole-board').ok);
  assert.deepEqual(firmware.activeRoutes(), [whole]);
  assert.deepEqual(firmware.routeForPad(95), whole);
});
