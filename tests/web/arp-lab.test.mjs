import test from 'node:test';
import assert from 'node:assert/strict';
import { createArpLabModel } from '../../web/arp-lab.js';

function fixture() {
  const calls = [];
  const instances = Array.from({ length: 2 }, () => ({ enabled: false, type: 'notes', grouping: 'hybrid',
    sourceZones: 63, layer: false, start: 'immediate', restartOnChange: false,
    rateTicks: 240, gatePercent: 50, swingPercent: 0, stepMask: 255, pattern: 'up',
    channel: 0, mask: 5, offsets: Array(8).fill(0), inputs: 0, phrases: 0, owners: 0,
    selectedChord: null, step: null, errors: 0, skippedSteps: 0 }));
  let advanceRejected = false, configureRejected = false, drains = 0;
  const arp = {
    snapshot(index) { return structuredClone(instances[index]); },
    configure(index, config) {
      calls.push(['configure', index, structuredClone(config)]);
      if (configureRejected) return { ok: false, code: 2, status: 'queue-full' };
      Object.assign(instances[index], structuredClone(config));
      return { ok: true, code: 0, status: 'ok' };
    },
    clear(index) {
      calls.push(['clear', index]);
      instances[index].inputs = 0; instances[index].phrases = 0; instances[index].owners = 0;
      return { ok: true, code: 0, status: 'ok' };
    },
  };
  const rhythm = {
    advance(timestamp) {
      calls.push(['advance', timestamp]);
      return advanceRejected ? { ok: false, status: 'time-backward', code: 7 } : { ok: true, code: 0, status: 'ok' };
    },
    snapshot() { return { ppqn: 960, clockState: 'internal', tick: 0 }; },
  };
  const model = createArpLabModel({ arp, rhythm, now: () => 1234, onEvents: () => drains++ });
  return { model, calls, instances, drains: () => drains,
    rejectAdvance() { advanceRejected = true; }, rejectConfigure() { configureRejected = true; } };
}

test('creating and selecting an Arp does not enable it, advance time or issue musical commands', () => {
  const { model, calls } = fixture();
  assert.equal(model.snapshot().selected, 0);
  assert.ok(model.snapshot().instances.every(arp => !arp.enabled));
  model.select(1);
  assert.equal(model.snapshot().selected, 1);
  assert.equal(model.snapshot().arp.type, 'notes');
  for (const invalid of [-1, 2, '1', 0.5, NaN]) assert.equal(model.select(invalid).ok, false);
  assert.deepEqual(calls, []);
});

test('partial configuration targets only the selected instance and keeps typed enums unchanged', () => {
  const { model, calls, instances, drains } = fixture();
  model.select(1);
  model.configure({ enabled: true });
  model.configure({ type: 'chords', grouping: 'combined', start: 'grid', restartOnChange: true, layer: true });
  assert.deepEqual(calls, [['advance', 1234], ['configure', 1, { enabled: true }], ['advance', 1234],
    ['configure', 1, { type: 'chords', grouping: 'combined', start: 'grid', restartOnChange: true, layer: true }]]);
  assert.equal(instances[0].enabled, false);
  assert.equal(instances[1].enabled, true);
  assert.equal(instances[1].rateTicks, 240);
  assert.equal(drains(), 2);
});

test('all note patterns and grouping modes are passed to C without JavaScript pitch processing', () => {
  const { model, calls } = fixture();
  for (const pattern of ['up', 'down', 'up-down', 'down-up', 'as-played']) model.configure({ pattern });
  for (const grouping of ['hybrid', 'combined', 'each']) model.configure({ grouping });
  assert.deepEqual(calls.filter(call => call[0] === 'configure').map(call => call[2]),
    ['up', 'down', 'up-down', 'down-up', 'as-played'].map(pattern => ({ pattern }))
      .concat(['hybrid', 'combined', 'each'].map(grouping => ({ grouping }))));
});

test('step-mask edits toggle one configured bit without advancing or inventing a playhead', () => {
  const { model, instances, calls } = fixture();
  model.toggleStep(2);
  assert.equal(instances[0].stepMask, 251);
  assert.equal(model.snapshot().arp.step, null);
  model.toggleStep(2);
  assert.equal(instances[0].stepMask, 255);
  for (const invalid of [-1, 8, '2', 1.5]) assert.equal(model.toggleStep(invalid).ok, false);
  assert.equal(calls.filter(call => call[0] === 'configure').length, 2);
});

test('a chord offset edit preserves seven other signed scale-degree offsets', () => {
  const { model, instances, calls } = fixture();
  instances[0].offsets = [-7, 0, 2, 4, 7, 9, 11, 14];
  model.setOffset(3, -24);
  assert.deepEqual(calls.at(-1), ['configure', 0, { offsets: [-7, 0, 2, -24, 7, 9, 11, 14] }]);
  assert.equal(model.setOffset(8, 2).ok, false);
  assert.equal(model.setOffset(0, 2.5).ok, false);
  assert.deepEqual(instances[1].offsets, Array(8).fill(0));
});

test('source-zone changes preserve other zones and remain separate from the output route', () => {
  const { model, instances, calls } = fixture();
  model.configure({ sourceZones: 7 });
  model.setZone(3, true);
  assert.equal(instances[0].sourceZones, 15);
  model.setZone(0, false);
  assert.equal(instances[0].sourceZones, 14);
  assert.deepEqual(calls.at(-1), ['configure', 0, { sourceZones: 14 }]);
  assert.equal(instances[0].channel, 0);
  assert.equal(instances[0].mask, 5);
  assert.equal(model.setZone(6, true).ok, false);
  assert.equal(model.setZone(0, 1).ok, false);
});

test('clear acts only on the selected C instance and retains its settings', () => {
  const { model, instances, calls, drains } = fixture();
  instances[0].inputs = 2; instances[0].phrases = 1; instances[0].owners = 3;
  instances[0].enabled = true; instances[0].rateTicks = 480;
  instances[1].inputs = 1;
  model.clear();
  assert.deepEqual(calls, [['advance', 1234], ['clear', 0]]);
  assert.equal(instances[0].inputs, 0);
  assert.equal(instances[0].enabled, true);
  assert.equal(instances[0].rateTicks, 480);
  assert.equal(instances[1].inputs, 1);
  assert.equal(drains(), 1);
});

test('a rejected advance aborts configuration and clear, but drains queued C events', () => {
  const { model, instances, calls, drains, rejectAdvance } = fixture();
  rejectAdvance();
  assert.equal(model.configure({ enabled: true }).status, 'time-backward');
  assert.equal(model.clear().ok, false);
  assert.equal(instances[0].enabled, false);
  assert.deepEqual(calls, [['advance', 1234], ['advance', 1234]]);
  assert.equal(drains(), 2);
  assert.match(model.snapshot().notice, /time-backward/);
});

test('C configuration rejections surface the real status and do not change the displayed engine state', () => {
  const { model, instances, calls, drains, rejectConfigure } = fixture();
  rejectConfigure();
  assert.equal(model.configure({ enabled: true }).ok, false);
  assert.equal(instances[0].enabled, false);
  assert.match(model.snapshot().notice, /queue-full/);
  assert.deepEqual(calls, [['advance', 1234], ['configure', 0, { enabled: true }]]);
  assert.equal(drains(), 1);
  for (const invalid of [null, [], 1, 'chords']) assert.equal(model.configure(invalid).ok, false);
  assert.equal(calls.length, 2);
});

test('refresh snapshots expose C runtime counters without scheduling events or moving steps', () => {
  const { model, instances, calls } = fixture();
  Object.assign(instances[0], { enabled: true, inputs: 3, phrases: 2, owners: 1, selectedChord: 7, step: 4,
    errors: 2, skippedSteps: 11 });
  for (let iteration = 0; iteration < 10; iteration++) {
    const snapshot = model.snapshot();
    assert.equal(snapshot.arp.step, 4);
    assert.equal(snapshot.arp.phrases, 2);
    assert.equal(snapshot.arp.selectedChord, 7);
    assert.equal(snapshot.arp.skippedSteps, 11);
  }
  assert.deepEqual(calls, []);
});
