import test from 'node:test';
import {CURRENT_SETTINGS_VERSION,CURRENT_EMPTY_SETTINGS_BYTES} from './current-settings-fixture.mjs';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createCore } from '../../web/core.js';
import { StudioModel } from '../../web/studio-model.js';
import { PreviewSettings, PREVIEW_SETTINGS_KEY } from '../../web/preview-settings.js';

const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));
const runtime = JSON.parse(await readFile(new URL('../../web/generated/chords.runtime.json', import.meta.url)));
const pads = Array.from({length:96}, (_, n) => ({notes:n < 48 ? [60,64,67,0,0,0,0] : [60,0,0,0,0,0,0], mask:n < 48 ? 7 : 1, color:1}));
async function fresh() {
  const engine = await createCore({bytes});
  assert.ok(engine.firmware.layout(pads).ok);
  return {engine, model:new StudioModel({engine, groups:runtime.groups})};
}
function tap(engine, slot) {
  assert.ok(engine.firmware.control(1, 7 + slot, true).ok);
  assert.ok(engine.firmware.control(1, 7 + slot, false).ok);
}
function layered(source, mask) {
  return [...new Set(Array.from({length:7}, (_, n) => n).filter(n => mask & (1 << n))
    .flatMap(n => source.map(pitch => pitch + (n - 3) * 12).filter(pitch => pitch >= 0 && pitch <= 127)))];
}

test('all seven physical lower buttons change triads through the shared C preview and routed attacks', async () => {
  for (let slot = 0; slot < 7; ++slot) {
    const {engine, model} = await fresh();
    assert.equal(model.get('controls.lowerMode'), 'octave-layers');
    assert.equal(engine.firmware.state().lowerMask, 8);
    assert.ok(engine.firmware.route(0, 6, 5, 0).ok);
    tap(engine, slot);
    const expected = layered([60,64,67], (8 ^ (1 << slot)) || 8);
    assert.deepEqual(engine.firmware.previewPad(0).notes, expected);
    assert.deepEqual(engine.firmware.previewPad(47).notes, expected);
    assert.deepEqual(engine.firmware.previewPad(48).notes, [60]);
    assert.ok(engine.firmware.pressPad(0).ok);
    assert.deepEqual(engine.drain().filter(e => e.destination === 2).map(e => [e.status,e.data1]), expected.map(n => [0x96,n]));
    tap(engine, slot);
    assert.ok(engine.release(0).ok);
    assert.deepEqual(engine.drain().filter(e => e.destination === 2).map(e => [e.status,e.data1]), expected.map(n => [0x86,n]));
    assert.equal(engine.stats().activeVoices, 0);
    assert.equal(engine.firmware.state().lowerMask, 8);
  }
});

test('real lossless corpus supports every octave layer without requiring empty stored slots', async () => {
  const {engine} = await fresh();
  let assignments = 0;
  for (const scale of runtime.scales) {
    const layout = Array.from({length:96}, (_, id) => {
      const pair = scale.pads[id], record = pair && runtime.pad_records[pair[1]];
      return {notes:record?.midi_slots ?? [0,0,0,0,0,0,0], mask:record?.notes?.reduce((m,n) => m | (1 << n.slot),0) ?? 0, color:record?.color_id ?? 0};
    });
    assert.ok(engine.firmware.layout(layout).ok);
    for (let slot = 0; slot < 7; ++slot) {
      tap(engine, slot);
      for (let id = 0; id < 48; ++id) {
        const source = layout[id].notes.filter((_, n) => layout[id].mask & (1 << n));
        const expected = layered(source, (8 ^ (1 << slot)) || 8);
        assert.deepEqual(engine.firmware.previewPad(id).notes, expected);
        ++assignments;
      }
      tap(engine, slot);
    }
  }
  assert.equal(assignments, 40320 * 7);
  assert.deepEqual(engine.drain(), []);
});

test('mode, scope, availability, indicators and cold-restored layers share the C state', async () => {
  const {engine, model} = await fresh();
  tap(engine, 6);
  assert.ok(model.set('controls.slotScope','selected-pad').ok);
  engine.firmware.performance({selectedPad:12});
  tap(engine, 4);
  assert.equal(engine.firmware.state().lowerMask,24);
  assert.deepEqual(engine.firmware.previewPad(0).notes,[60,64,67]);
  assert.deepEqual(engine.firmware.previewPad(12).notes,[60,64,67,72,76,79]);
  const cold = await fresh();
  assert.ok(cold.engine.settings.apply(engine.settings.encode()));
  cold.engine.firmware.performance({selectedPad:12});
  assert.equal(cold.engine.firmware.state().lowerMask,24);
  assert.ok(cold.model.set('controls.slotScope','whole-left').ok);
  assert.equal(cold.engine.firmware.state().lowerMask,72);
  assert.ok(cold.model.set('controls.lowerMode','stored-notes').ok);
  assert.equal(cold.engine.firmware.state().lowerAvailable,7);
  const levels = cold.engine.state.centerLevels().slice(12);
  assert.deepEqual(Array.from(levels),[4095,4095,4095,256,256,256,256]);
  const revision = cold.engine.firmware.state().globalExcludedSlots;
  tap(cold.engine,6);
  assert.equal(cold.engine.firmware.state().globalExcludedSlots,revision);
});

test('browser restores v1 safely and saves current settings without losing earlier preferences', async () => {
  const {engine, model} = await fresh();
  assert.ok(model.set('controls.left','hide').ok); // The v1 enum predates Engines.
  assert.ok(model.set('controls.lowerMode','stored-notes').ok);
  assert.ok(model.set('lights.keyDim',17).ok);
  tap(engine,1);
  const current=engine.settings.encode();
  const v1=new Uint8Array(2103);
  v1.set(current.subarray(0,3)); v1.set(current.subarray(7,2107),3);
  assert.equal(engine.settings.payloadVersion(v1),1);
  assert.ok(engine.settings.validate(v1));
  const records=new Map([[PREVIEW_SETTINGS_KEY,JSON.stringify({format:'thy666.preview-settings',version:1,crc:engine.settings.crc(v1),payload:Array.from(v1)})]]);
  const storage={getItem:key=>records.get(key)??null,setItem:(key,value)=>records.set(key,value)};
  const cold=await fresh(), prefs=new PreviewSettings(cold.engine,storage);
  assert.equal(prefs.restore(()=>true),'Saved locally');
  assert.equal(cold.model.get('controls.lowerMode'),'stored-notes');
  assert.equal(cold.model.get('lights.keyDim'),17);
  assert.equal(cold.engine.firmware.state().globalExcludedSlots,2);
  prefs.tick(0); prefs.tick(1600);
  const migrated=JSON.parse(storage.getItem(PREVIEW_SETTINGS_KEY));
  assert.equal(migrated.version,CURRENT_SETTINGS_VERSION);assert.equal(migrated.payload.length,CURRENT_EMPTY_SETTINGS_BYTES);
});
