import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createCore } from '../../web/core.js';
import { MENU_ROWS } from '../../web/studio-menu.js';

const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));
const library = JSON.parse(await readFile(new URL('../../web/generated/chords.runtime.json', import.meta.url)));
const scaleRow = MENU_ROWS.findIndex(row => row.id === 'performance.scale');
const defaults = [56,70,21,22,23,24,25,26,27,14,35,60];
const pads = Array.from({length:96}, (_, n) => ({notes:n < 48 ? [60,64,67,0,0,0,0] : [60,0,0,0,0,0,0], mask:n < 48 ? 7 : 1, color:1}));
function tap(engine, key) {
  assert.ok(engine.firmware.control(3,key,true).ok);
  assert.ok(engine.firmware.control(3,key,false).ok);
}

test('all seventy original scale families are present at all twelve roots', () => {
  assert.equal(library.groups.length,70);
  assert.equal(library.scales.length,840);
  for (const group of library.groups) {
    const scales = library.scales.filter(scale => scale.group_index === group.index);
    assert.equal(scales.length,12,group.name);
    assert.equal(new Set(scales.map(scale => scale.root_label)).size,12,group.name);
    assert.ok(scales.every(scale => scale.pads.length === 96),group.name);
  }
});

test('physical shortcuts reach the whole C catalog and U keys have usable defaults', async () => {
  const engine = await createCore({bytes});
  assert.deepEqual(engine.firmware.scaleShortcuts(),defaults);
  for (let key = 0; key < 12; ++key) {
    tap(engine,key);
    if (defaults[key] !== 70) assert.equal(engine.state.get(scaleRow),defaults[key]);
  }
  for (let scale = 0; scale < 70; ++scale) {
    assert.ok(engine.firmware.scaleShortcut(11,scale).ok);
    tap(engine,11);
    assert.equal(engine.state.get(scaleRow),scale,library.groups[scale].name);
    assert.equal(engine.firmware.state().root,0);
  }
  for (const key of [-1,12,NaN,1.5]) {
    assert.equal(engine.firmware.scaleShortcut(key),-1);
    assert.equal(engine.firmware.scaleShortcut(key,21).ok,false);
  }
  for (const assignment of [-1,71,NaN,1.5]) assert.equal(engine.firmware.scaleShortcut(0,assignment).ok,false);
});

test('layout-cycle assignments and remapping preserve held pitches and release routes', async () => {
  const engine = await createCore({bytes});
  assert.ok(engine.firmware.layout(pads).ok);
  assert.ok(engine.firmware.route(0,4,5).ok);
  assert.ok(engine.firmware.pressPad(0).ok);
  assert.deepEqual(engine.drain().filter(event => event.destination === 2).map(event => [event.status,event.data1]),[[0x94,60],[0x94,64],[0x94,67]]);
  for (let key = 0; key < 12; ++key) {
    const before = engine.firmware.state().melodyLayout;
    assert.ok(engine.firmware.scaleShortcut(key,70).ok);
    tap(engine,key);
    const layout=(before + 1) % 9;
    assert.equal(engine.firmware.state().melodyLayout,layout);
    assert.equal(engine.state.session().mode,1,'Every physical layout belongs to the same Play engine.');
  }
  assert.deepEqual(engine.drain(),[]);
  assert.ok(engine.release(0).ok);
  assert.deepEqual(engine.drain().filter(event => event.destination === 2).map(event => [event.status,event.data1]),[[0x84,60],[0x84,64],[0x84,67]]);
  assert.equal(engine.stats().activeVoices,0);
});

test('temporary Arp M controls override scales only on the enabled quick page', async () => {
  const engine = await createCore({bytes});
  assert.ok(engine.firmware.scaleShortcut(2,14).ok);
  assert.ok(engine.arp.openQuick(0).ok);
  tap(engine,2);
  assert.equal(engine.arp.snapshot(0).enabled,true);
  assert.notEqual(engine.state.get(scaleRow),14);
  assert.ok(engine.state.play().ok);
  tap(engine,2);
  assert.equal(engine.state.get(scaleRow),14);
});
