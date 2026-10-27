import {legacyViewInfo} from './legacy-view-fixture.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createCore } from '../../web/core.js';
import { StudioModel } from '../../web/studio-model.js';
import { PreviewSettings, PREVIEW_SETTINGS_KEY } from '../../web/preview-settings.js';

const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));
const runtime = JSON.parse(await readFile(new URL('../../web/generated/chords.runtime.json', import.meta.url)));
const okay = result => assert.equal(result.ok, true, result.status);
const pads = Array.from({ length: 96 }, (_, n) => ({ notes: n < 48 ? [60, 64, 67, 0, 0, 0, 0] : [62, 0, 0, 0, 0, 0, 0],
  mask: n < 48 ? 7 : 1, color: 1 }));
async function fresh() {
  const engine = await createCore({ bytes });
  okay(engine.firmware.layout(pads));
  return { engine, model: new StudioModel({ engine, groups: runtime.groups }) };
}
function storageFor(version, engine, payload) {
  const records = new Map();
  if (payload) records.set(PREVIEW_SETTINGS_KEY, JSON.stringify({ format: 'thy666.preview-settings', version,
    crc: engine.settings.crc(payload), payload: Array.from(payload) }));
  return { records, getItem: key => records.get(key) ?? null, setItem: (key, value) => records.set(key, value) };
}
function parameters(arp) {
  return Object.fromEntries(['enabled', 'type', 'grouping', 'sourceZones', 'layer', 'start', 'restartOnChange',
    'rateTicks', 'gatePercent', 'swingPercent', 'stepMask', 'pattern', 'channel', 'mask', 'offsets'].map(key => [key, arp[key]]));
}

test('two Arps, shared clock and quick-page preferences cold restore without musical runtime', async () => {
  const { engine, model } = await fresh();
  okay(engine.arp.configure(0, { enabled: true, type: 'notes', grouping: 'hybrid', sourceZones: 7, layer: true,
    start: 'grid', restartOnChange: true, rateTicks: 320, gatePercent: 77, swingPercent: 31,
    stepMask: 165, pattern: 'down-up', channel: 11, mask: 4, offsets: [-7, -6, -5, -4, -3, -2, -1, 0] }));
  okay(engine.arp.configure(1, { enabled: true, type: 'chords', grouping: 'each', sourceZones: 56, layer: false,
    start: 'immediate', rateTicks: 1440, gatePercent: 100, swingPercent: 0, stepMask: 90, pattern: 'as-played',
    channel: 15, mask: 3, offsets: [24, 18, 12, 6, 0, -6, -12, -18] }));
  okay(model.set('arps.target', 2)); okay(model.set('arps.quickKeys', false));
  okay(model.set('controls.left', 'arps'));
  okay(engine.rhythm.configure({ bpm: 139.125, source: 'usb', meterNumerator: 5, meterDenominator: 8, quantization: 'beat' }));
  const expected = [0, 1].map(n => parameters(engine.arp.snapshot(n)));
  const encoded = engine.settings.encode(); assert.equal(encoded.length, 5633);
  assert.equal(engine.settings.version, 13); assert.equal(engine.settings.payloadVersion(encoded), 13);
  const storage = storageFor(13, engine, encoded), cold = await fresh();
  const prefs = new PreviewSettings(cold.engine, storage);
  assert.equal(prefs.restore(() => true), 'Saved locally');
  assert.deepEqual([0, 1].map(n => parameters(cold.engine.arp.snapshot(n))), expected);
  assert.equal(cold.model.get('arps.target'), 2); assert.equal(cold.model.get('arps.quickKeys'), false);
  assert.equal(cold.model.get('controls.left'), 'arps');
  const clock = cold.engine.rhythm.snapshot();
  assert.equal(clock.bpm, 139.125); assert.equal(clock.clockSource, 'usb'); assert.equal(clock.clockState, 'waiting');
  assert.equal(clock.meterNumerator, 5); assert.equal(clock.meterDenominator, 8); assert.equal(clock.quantization, 'beat');
  assert.equal(clock.tick, 0); assert.equal(clock.songTick, 0); assert.equal(clock.transport, 'stopped');
  assert.equal(clock.generatedCount, 0); assert.ok(clock.tracks.every(t => !t.running && !t.enabled && !t.pending && t.playheadTick === 0));
  for (const n of [0, 1]) {
    const arp = cold.engine.arp.snapshot(n);
    assert.equal(arp.inputs, 0); assert.equal(arp.phrases, 0); assert.equal(arp.owners, 0); assert.equal(arp.attacks, 0);
  }
  assert.deepEqual(cold.engine.drain(), []);
  okay(cold.engine.advance(100)); assert.deepEqual(cold.engine.drain(), []);
  const after = cold.engine.settings.encode(); assert.deepEqual(after, encoded);
});

test('v2 restore retains old preferences, defaults new Arp fields and migrates only at idle', async () => {
  const { engine, model } = await fresh();
  okay(model.set('controls.left', 'hide')); // Engines did not exist in the v2 shortcut enum.
  okay(model.set('lights.keyDim', 17)); okay(model.set('controls.lowerMode', 'stored-notes'));
  okay(engine.arp.configure(0, { enabled: true, channel: 9, rateTicks: 480 }));
  okay(engine.rhythm.configure({ bpm: 170, source: 'usb', meterNumerator: 7, meterDenominator: 8, quantization: 'immediate' }));
  const legacy = engine.settings.encode().slice(0, 2157); legacy[6] = 2;
  assert.equal(engine.settings.payloadVersion(legacy), 2); assert.ok(engine.settings.validate(legacy));
  const storage = storageFor(2, engine, legacy), original = storage.getItem(PREVIEW_SETTINGS_KEY), cold = await fresh();
  const prefs = new PreviewSettings(cold.engine, storage);
  assert.equal(prefs.restore(() => true), 'Saved locally'); assert.equal(storage.getItem(PREVIEW_SETTINGS_KEY), original);
  assert.equal(cold.model.get('lights.keyDim'), 17); assert.equal(cold.model.get('controls.lowerMode'), 'stored-notes');
  assert.equal(cold.model.get('arps.target'), 1); assert.equal(cold.model.get('arps.quickKeys'), true);
  assert.equal(cold.engine.arp.snapshot(0).enabled, false); assert.equal(cold.engine.arp.snapshot(1).enabled, false);
  const clock = cold.engine.rhythm.snapshot();
  assert.equal(clock.clockSource, 'internal'); assert.equal(clock.bpm, 120); assert.equal(clock.meterNumerator, 4);
  assert.equal(clock.meterDenominator, 4); assert.equal(clock.quantization, 'bar');
  prefs.tick(0, false); prefs.tick(2000, false); assert.equal(storage.getItem(PREVIEW_SETTINGS_KEY), original);
  prefs.tick(2001, true);
  const migrated = JSON.parse(storage.getItem(PREVIEW_SETTINGS_KEY));
  assert.equal(migrated.version, 13); assert.equal(migrated.payload.length, 5633);
  assert.deepEqual(cold.engine.drain(), []);
});

test('v3 restores enabled Arps and clock while idle migration appends presentation preferences', async () => {
  const { engine, model } = await fresh();
  okay(model.set('controls.left', 'hide')); // Preserve the historical enum before extracting a v3 fixture.
  okay(engine.arp.configure(0, { enabled: true, rateTicks: 320, gatePercent: 71, channel: 7 }));
  okay(engine.arp.configure(1, { enabled: true, type: 'chords', stepMask: 165, sourceZones: 56 }));
  okay(model.set('arps.target', 2)); okay(model.set('arps.quickKeys', false));
  okay(engine.rhythm.configure({ bpm: 131.125, source: 'usb', meterNumerator: 7, meterDenominator: 8, quantization: 'beat' }));
  const expected = [0, 1].map(n => parameters(engine.arp.snapshot(n)));
  const legacy = engine.settings.encode().slice(0, 2217); legacy[6] = 3;
  assert.equal(engine.settings.payloadVersion(legacy), 3); assert.ok(engine.settings.validate(legacy));
  const storage = storageFor(3, engine, legacy), original = storage.getItem(PREVIEW_SETTINGS_KEY), cold = await fresh();
  const prefs = new PreviewSettings(cold.engine, storage);
  assert.equal(prefs.restore(() => true), 'Saved locally');
  assert.equal(storage.getItem(PREVIEW_SETTINGS_KEY), original);
  assert.deepEqual([0, 1].map(n => parameters(cold.engine.arp.snapshot(n))), expected);
  assert.equal(cold.model.get('arps.target'), 2); assert.equal(cold.model.get('arps.quickKeys'), false);
  const clock = cold.engine.rhythm.snapshot();
  assert.equal(clock.bpm, 131.125); assert.equal(clock.clockSource, 'usb'); assert.equal(clock.clockState, 'waiting');
  assert.equal(clock.tick, 0); assert.equal(clock.transport, 'stopped'); assert.equal(clock.generatedCount, 0);
  prefs.tick(0, false); prefs.tick(2000, false);
  assert.equal(storage.getItem(PREVIEW_SETTINGS_KEY), original);
  prefs.tick(2001, true);
  const migrated = JSON.parse(storage.getItem(PREVIEW_SETTINGS_KEY));
  assert.equal(migrated.version, 13); assert.equal(migrated.payload.length, 5633);
  assert.deepEqual(cold.engine.drain(), []);
});

test('v5 preferences migrate without sequencer runtime and save a complete v13 song payload at idle', async () => {
  const {engine,model}=await fresh();
  okay(model.set('controls.left','arps'));okay(engine.arp.configure(1,{enabled:true,channel:8,rateTicks:480}));
  okay(engine.sequencer.add(0,[{tick:0,duration:240,pitch:60,value:96}]));
  const legacy=legacyViewInfo(engine.settings.encode()).slice(0,2320);legacy[6]=5;
  assert.equal(engine.settings.payloadVersion(legacy),5);assert.ok(engine.settings.validate(legacy));
  const cold=await fresh(),storage=storageFor(5,engine,legacy),original=storage.getItem(PREVIEW_SETTINGS_KEY),prefs=new PreviewSettings(cold.engine,storage);
  assert.equal(prefs.restore(()=>true),'Saved locally');assert.equal(cold.model.get('controls.left'),'arps');assert.equal(cold.engine.arp.snapshot(1).channel,8);
  assert.equal(cold.engine.sequencer.snapshot().count,0);assert.equal(cold.engine.sequencer.snapshot().capture.state,'off');
  assert.deepEqual(cold.engine.sequencer.snapshot().remote,{record:false,position:false,channel:0});
  prefs.tick(0,false);prefs.tick(2000,false);assert.equal(storage.getItem(PREVIEW_SETTINGS_KEY),original);prefs.tick(2001,true);
  const migrated=JSON.parse(storage.getItem(PREVIEW_SETTINGS_KEY));assert.equal(migrated.version,13);assert.equal(migrated.payload.length,5633);assert.deepEqual(cold.engine.drain(),[]);
});

test('truncated v13 and unknown versions cannot masquerade as legacy or overwrite storage', async () => {
  const { engine } = await fresh(), payload = engine.settings.encode();
  for (const count of [2103, 2157, 2217, 2309, 2320, 2345, 2880, 2965, 3217, 3508,3548,3740,3748,4263,4883,5013]) {
    const truncated = payload.slice(0, count);
    assert.equal(engine.settings.validate(truncated), false);
    assert.equal(engine.settings.apply(truncated), false);
  }
  for (const mutate of [record => { record.version = 14; }, record => { record.payload[record.payload.length - 1] = 2; },
    record => { record.payload[2207] ^= 1; }]) {
    const storage = storageFor(13, engine, payload), record = JSON.parse(storage.getItem(PREVIEW_SETTINGS_KEY));
    mutate(record); storage.setItem(PREVIEW_SETTINGS_KEY, JSON.stringify(record));
    const original = storage.getItem(PREVIEW_SETTINGS_KEY), cold = await fresh(), prefs = new PreviewSettings(cold.engine, storage);
    prefs.restore(() => assert.fail('Corrupt or future records must not load a catalog'));
    prefs.tick(0); prefs.tick(99999);
    assert.equal(storage.getItem(PREVIEW_SETTINGS_KEY), original);
    assert.equal(cold.engine.arp.snapshot(0).enabled, false); assert.deepEqual(cold.engine.drain(), []);
  }
});


test('v4 Scales and Chromatic preferences merge into Play without losing the selected layout', async () => {
  for (const legacyEngine of [0,1]) {
    const {engine,model}=await fresh();
    okay(model.set('performance.melodyLayout',legacyEngine ? 'guitar' : 'condensed'));
    okay(model.set('lights.view','waves')); okay(model.set('views.slot1','clock'));
    okay(engine.firmware.scaleShortcut(11,61));
    const layout=engine.firmware.state().melodyLayout;
    const legacy=legacyViewInfo(engine.settings.encode()).slice(0,2310); legacy[6]=4; legacy[2217]=legacyEngine;
    assert.equal(engine.settings.payloadVersion(legacy),4); assert.ok(engine.settings.validate(legacy));
    const cold=await fresh(), storage=storageFor(4,engine,legacy), prefs=new PreviewSettings(cold.engine,storage);
    assert.equal(prefs.restore(()=>true),'Saved locally');
    assert.equal(cold.engine.state.session().mode,1);
    assert.equal(cold.engine.firmware.state().melodyLayout,layout);
    assert.equal(cold.model.get('lights.view'),'waves'); assert.equal(cold.model.get('views.slot1'),'clock');
    assert.equal(cold.engine.firmware.scaleShortcut(11),61);
    // Fifths had no saved configuration in V4, so it receives current defaults.
    okay(cold.model.set('lights.view','fifths')); assert.equal(cold.model.get('views.slot1'),'musical-names');
    assert.equal(cold.engine.stats().activeVoices,0); assert.deepEqual(cold.engine.drain(),[]);
  }
});
