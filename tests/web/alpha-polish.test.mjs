import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createCore } from '../../web/core.js';
import { StudioModel } from '../../web/studio-model.js';
import { PreviewSettings, PREVIEW_SETTINGS_KEY } from '../../web/preview-settings.js';
const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));
const layout = Array.from({ length: 96 }, (_, pad) => ({ notes: pad < 48 ? [60, 64, 67, 71, 74, 77, 81] : [60, 0, 0, 0, 0, 0, 0], mask: pad < 48 ? 127 : 1, color: 1 }));
async function fresh() {
  const engine = await createCore({ bytes });
  assert.ok(engine.firmware.layout(layout).ok);
  const model = new StudioModel({ engine, groups: [{ index: 0, name: 'Major' }] });
  return { engine, model };
}
const memoryStorage = () => {
  const records = new Map();
  return { getItem: key => records.get(key) ?? null, setItem: (key, value) => records.set(key, value), records };
};
test('new categories and Play shortcuts execute shared C navigation', async () => {
  const { engine, model } = await fresh();
  assert.equal(engine.state.groupCount, 13);
  model.navigate('select');
  assert.equal(model.snapshot().selectedId,'engines');
  model.play();
  assert.ok(model.focus('controls'));
  model.play(); model.navigate('up');
  assert.equal(model.snapshot().groupId, 'views');
  model.play(); model.navigate('left');
  assert.equal(model.snapshot().groupId, 'engines');
  assert.equal(model.snapshot().selectedId, 'engines.play');
  model.play();
  assert.ok(model.set('controls.left', 'hide').ok);
  model.navigate('left'); assert.equal(model.snapshot().level, 'play');
  assert.equal(engine.state.centerLevels()[0], 0);
  assert.ok(model.set('controls.left', 'sensitivity').ok);
  model.navigate('left'); assert.equal(model.snapshot().groupId, 'sensitivity');
});
test('whole-left slot buttons remain global after selecting melody; per-chord scope is optional', async () => {
  const { engine, model } = await fresh();
  assert.ok(model.set('controls.lowerMode', 'stored-notes').ok);
  engine.firmware.performance({ selectedPad: 0 });
  engine.firmware.control(1, 7, true); engine.firmware.control(1, 7, false);
  assert.equal(engine.firmware.state().globalExcludedSlots, 1);
  assert.equal(engine.firmware.previewPad(1).notes.length, 6);
  engine.firmware.performance({ selectedPad: 48 });
  engine.firmware.control(1, 7, true); engine.firmware.control(1, 7, false);
  assert.equal(engine.firmware.previewPad(1).notes.length, 7);
  assert.ok(model.set('controls.slotScope', 'selected-pad').ok);
  engine.firmware.control(1, 7, true); engine.firmware.control(1, 7, false);
  assert.equal(engine.firmware.previewPad(0).notes.length, 6);
  assert.equal(engine.firmware.previewPad(1).notes.length, 7);
});
test('octave context pairs and bass control can be hidden independently', async () => {
  const { engine, model } = await fresh();
  assert.ok(model.set('performance.padOverlays', true).ok);
  assert.equal(model.get('performance.contextLocation'), 'upper-right');
  assert.equal(engine.firmware.contextPad(32), 1);
  assert.ok(model.set('performance.contextChordOctaves', false).ok);
  assert.equal(engine.firmware.contextPad(32), 0);
  assert.equal(engine.firmware.contextPad(36), 0);
  assert.equal(engine.firmware.contextPad(44), 4);
  assert.equal(engine.firmware.contextPad(88), 5);
  assert.ok(model.set('performance.contextMelodyOctaves', false).ok);
  assert.equal(engine.firmware.contextPad(88), 0);
  assert.ok(model.set('performance.contextBass', false).ok);
  assert.equal(engine.firmware.contextPad(44), 0);
  assert.ok(model.set('performance.contextLocation', 'lower-left').ok);
  assert.equal(engine.firmware.state().contextLocation, 1);
  assert.equal(engine.firmware.contextPad(11), 3);
  assert.equal(engine.firmware.contextPad(40), 0);
});
test('context corners follow upper-right/lower-left geometry and preserve saved upper/lower ordinal', async () => {
  const { engine, model } = await fresh();
  assert.ok(model.set('performance.padOverlays', true).ok);
  for (const [choice, ordinal, corners] of [
    ['upper-right', 0, [[32,1], [36,2], [40,3], [44,4], [88,5], [92,6]]],
    ['lower-left', 1, [[3,1], [7,2], [11,3], [15,4], [51,5], [55,6]]],
  ]) {
    assert.ok(model.set('performance.contextLocation', choice).ok);
    assert.equal(engine.firmware.state().contextLocation, ordinal);
    const expected = new Map(corners);
    for (let pad = 0; pad < 96; ++pad)
      assert.equal(engine.firmware.contextPad(pad), expected.get(pad) ?? 0);
    const payload = engine.settings.encode();
    const cold = await fresh();
    assert.ok(cold.engine.settings.apply(payload));
    assert.equal(cold.model.get('performance.contextLocation'), choice);
    assert.equal(cold.engine.firmware.state().contextLocation, ordinal);
    for (let pad = 0; pad < 96; ++pad)
      assert.equal(cold.engine.firmware.contextPad(pad), expected.get(pad) ?? 0);
  }
});
test('C settings payload cold-restores independent routes, view and contrast', async () => {
  const { engine, model } = await fresh();
  assert.ok(model.set('lights.view', 'chladni').ok);
  assert.ok(model.set('views.slot1', 'clock').ok);
  assert.ok(model.set('views.slot2', 'off').ok);
  assert.ok(model.set('lights.view', 'piano').ok);
  assert.ok(model.set('views.slot1', 'notes').ok);
  assert.ok(model.set('views.slot2', 'outputs').ok);
  assert.ok(model.set('lights.view', 'chladni').ok);
  assert.ok(model.set('lights.keyDim', 17).ok);
  assert.ok(model.set('midi.routeTarget', 'right').ok);
  assert.ok(model.set('midi.channel', 9).ok);
  assert.ok(model.set('midi.velocity', 113).ok);
  const payload = engine.settings.encode();
  const cold = await fresh();
  assert.ok(cold.engine.settings.validate(payload));
  assert.deepEqual(cold.engine.settings.catalog(payload), { group: 21, root: 0 });
  assert.ok(cold.engine.settings.apply(payload));
  assert.equal(cold.model.get('lights.view'), 'chladni');
  assert.equal(cold.model.get('views.slot1'), 'clock');
  assert.equal(cold.model.get('views.slot2'), 'off');
  assert.ok(cold.model.set('lights.view','piano').ok);
  assert.equal(cold.model.get('views.slot1'),'notes');
  assert.equal(cold.model.get('views.slot2'),'outputs');
  assert.equal(cold.model.get('lights.keyDim'), 17);
  assert.equal(cold.engine.firmware.padRoute(2).channel, 8);
  assert.equal(cold.engine.firmware.padRoute(2).velocity, 113);
});
test('settings imports refuse held pads, open drafts and pressed controls', async () => {
  const { engine, model } = await fresh();
  const payload = engine.settings.encode();
  engine.firmware.pressPad(0);
  assert.equal(engine.settings.canApply(), false);
  assert.equal(engine.settings.apply(payload), false);
  engine.release(0); engine.drain();
  assert.ok(model.beginEdit('lights.keyDim'));
  assert.equal(engine.settings.apply(payload), false); model.cancelEdit();
  engine.firmware.control(1, 0, true);
  assert.equal(engine.settings.canApply(), false);
  engine.firmware.control(1, 0, false);
  assert.equal(engine.settings.canApply(), true);
});
test('local autosave debounces committed settings, waits for idle, verifies storage and cold-restores', async () => {
  const { engine, model } = await fresh(); const storage = memoryStorage(); const statuses = [];
  const prefs = new PreviewSettings(engine, storage, s => statuses.push(s));
  assert.equal(prefs.restore(() => true), 'Defaults');
  model.set('lights.keyDim', 12);
  prefs.tick(0, false); prefs.tick(2000, false);
  assert.equal(storage.records.size, 0);
  prefs.tick(2001, true); assert.equal(statuses.at(-1), 'Saved locally');
  const cold = await fresh(); let loads = 0;
  const restored = new PreviewSettings(cold.engine, storage);
  assert.equal(restored.restore(() => { loads++; return true; }), 'Saved locally');
  assert.equal(loads, 1); assert.equal(cold.model.get('lights.keyDim'), 12);
});
test('unknown or corrupt browser settings are retained and never overwritten', async () => {
  const { engine } = await fresh(); const storage = memoryStorage();
  for (const text of [JSON.stringify({ version: 99 }), JSON.stringify({ format: 'thy666.preview-settings', version: 1, payload: [0], crc: 0 })]) {
    storage.setItem(PREVIEW_SETTINGS_KEY, text);
    const prefs = new PreviewSettings(engine, storage);
    prefs.restore(() => assert.fail('Invalid settings must not alter catalog'));
    prefs.tick(0); prefs.tick(9999);
    assert.equal(storage.getItem(PREVIEW_SETTINGS_KEY), text);
  }
});
test('Credits runs in shared renderer and LEFT returns without affecting music', async () => {
  const { engine, model } = await fresh();
  assert.ok(model.focus('system', 'system.about')); assert.ok(model.beginEdit('system.about'));
  assert.equal(model.snapshot().about, true);
  const frame = engine.state.frame({ now: 123 }); assert.ok(frame.some(v => v));
  model.navigate('left'); assert.equal(model.snapshot().about, false);
});
