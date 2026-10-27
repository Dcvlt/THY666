import test from 'node:test';
import assert from 'node:assert/strict';
import { createMpeLabModel } from '../../web/mpe-lab.js';

function fixture() {
  const calls = [];
  const config = { enabled: false, upperZone: false, members: 15, bendRange: 48,
    managerBendRange: 2, glide: false, padWindow: 1, semitoneWindow: 2 };
  const voices = [];
  const stats = { active: 0, consumed: 0 };
  let advanceRejected = false, commandRejected = false, drains = 0;
  const result = () => commandRejected ? { ok: false, code: 20, status: 'busy' } : { ok: true, code: 0, status: 'ok' };
  const expression = {
    config: () => ({ ...config }), voices: () => structuredClone(voices), stats: () => ({ ...stats }),
    configure(changes) {
      calls.push(['configure', { ...changes }]);
      const response = result(); if (response.ok) Object.assign(config, changes); return response;
    },
    sendSetup() { calls.push(['setup']); return result(); },
    pressure(pad, value) { calls.push(['pressure', pad, value]); return result(); },
    bend(pad, value) { calls.push(['bend', pad, value]); return result(); },
  };
  const rhythm = { advance(time) { calls.push(['advance', time]); return advanceRejected ? { ok: false, status: 'time-backward' } : { ok: true }; } };
  const model = createMpeLabModel({ expression, rhythm, now: () => 1234, onEvents: () => drains++ });
  return { model, calls, config, voices, stats, drains: () => drains,
    rejectAdvance() { advanceRejected = true; }, rejectCommand() { commandRejected = true; } };
}

test('MPE panel creation and inspection issue no musical commands', () => {
  const { model, calls } = fixture();
  assert.equal(model.snapshot().config.enabled, false);
  assert.equal(model.snapshot().config.bendRange, 48);
  assert.equal(model.snapshot().selectedPad, null);
  assert.deepEqual(model.snapshot().voices, []);
  assert.deepEqual(calls, []);
  assert.throws(() => createMpeLabModel({}), /Shared C/);
});

test('configuration preserves typed partial values and advances C timing first', () => {
  const { model, calls, config, drains } = fixture();
  const changes = { enabled: true, upperZone: true, members: 7, glide: true, padWindow: 2, semitoneWindow: 5 };
  assert.equal(model.configure(changes).ok, true);
  assert.deepEqual(calls, [['advance', 1234], ['configure', changes]]);
  assert.equal(config.bendRange, 48);
  assert.equal(config.managerBendRange, 2);
  assert.equal(drains(), 1);
});

test('MPE field validation rejects wrong types, unknown fields and out-of-range values before dispatch', () => {
  const { model, calls } = fixture();
  for (const changes of [null, [], 3, 'enabled', { enabled: 1 }, { glide: 'true' }, { upperZone: 0 },
    { members: 0 }, { members: 16 }, { members: 1.5 }, { bendRange: 0 }, { bendRange: 97 },
    { managerBendRange: NaN }, { managerBendRange: 97 }, { padWindow: 0 }, { padWindow: 12 },
    { semitoneWindow: 0 }, { semitoneWindow: 25 }, { unknown: true }, { channel: 2 }]) {
    assert.equal(model.configure(changes).ok, false, JSON.stringify(changes));
  }
  assert.deepEqual(calls, []);
});

test('all documented config endpoints remain available to the C validator', () => {
  const { model, calls } = fixture();
  const minimum = { members: 1, bendRange: 1, managerBendRange: 1, padWindow: 1, semitoneWindow: 1 };
  const maximum = { members: 15, bendRange: 96, managerBendRange: 96, padWindow: 11, semitoneWindow: 24 };
  assert.equal(model.configure(minimum).ok, true);
  assert.equal(model.configure(maximum).ok, true);
  assert.deepEqual(calls.filter(call => call[0] === 'configure'), [['configure', minimum], ['configure', maximum]]);
});

test('receiver setup is an explicit command, never an effect of inspection or selection', () => {
  const { model, calls, drains } = fixture();
  model.snapshot(); model.selectPad(61); model.snapshot();
  assert.deepEqual(calls, []);
  assert.equal(model.sendSetup().ok, true);
  assert.deepEqual(calls, [['advance', 1234], ['setup']]);
  assert.equal(drains(), 1);
  assert.match(model.snapshot().notice, /preview MIDI timeline/);
});

test('pad selection is local and pressure uses the exact selected source identity', () => {
  const { model, calls } = fixture();
  assert.equal(model.pressure(64).ok, false);
  model.selectPad(73);
  assert.deepEqual(calls, []);
  assert.equal(model.pressure(127).ok, true);
  assert.equal(model.pressure(0, 9).ok, true);
  assert.equal(model.snapshot().selectedPad, 73);
  assert.deepEqual(calls, [['advance', 1234], ['pressure', 73, 127], ['advance', 1234], ['pressure', 9, 0]]);
  for (const value of [-1, 96, 0.5, '1', NaN]) assert.equal(model.selectPad(value).ok, false);
});

test('bend dispatch forwards complete 14-bit values without calculating pitches or glide', () => {
  const { model, calls } = fixture();
  model.selectPad(0);
  for (const value of [0, 8192, 16383]) assert.equal(model.bend(value).ok, true);
  assert.deepEqual(calls.filter(call => call[0] === 'bend'), [['bend', 0, 0], ['bend', 0, 8192], ['bend', 0, 16383]]);
  for (const value of [-1, 16384, 0.5, '8192', NaN]) assert.equal(model.bend(value).ok, false);
  for (const value of [-1, 128, 0.5, '64', NaN]) assert.equal(model.pressure(value).ok, false);
  assert.equal(model.bend(8192, 96).ok, false);
  assert.equal(calls.length, 6);
});

test('clock failure prevents every expression command and still drains pending output', () => {
  const { model, calls, drains, rejectAdvance } = fixture();
  rejectAdvance(); model.selectPad(3);
  assert.equal(model.configure({ enabled: true }).ok, false);
  assert.equal(model.sendSetup().ok, false);
  assert.equal(model.pressure(50).ok, false);
  assert.equal(model.bend(8192).ok, false);
  assert.deepEqual(calls, Array.from({ length: 4 }, () => ['advance', 1234]));
  assert.equal(drains(), 4);
  assert.match(model.snapshot().notice, /time-backward/);
});

test('C rejection is displayed without claiming the new configuration was applied', () => {
  const { model, config, calls, drains, rejectCommand } = fixture();
  rejectCommand();
  assert.equal(model.configure({ enabled: true }).status, 'busy');
  assert.equal(config.enabled, false);
  assert.match(model.snapshot().notice, /busy/);
  assert.deepEqual(calls, [['advance', 1234], ['configure', { enabled: true }]]);
  assert.equal(drains(), 1);
});

test('the member ledger exposes C state unchanged without scheduling or deriving expression', () => {
  const { model, voices, stats, calls } = fixture();
  voices.push({ pad: 17, logical: 48, pitch: 60, channel: 4, destinations: 5, state: 1,
    pressure: 90, sentPressure: 85, bend: 8300, sentBend: 8250, modifier: 32, modifierPressure: 61, interval: 2 });
  stats.active = 1; stats.consumed = 1;
  for (let iteration = 0; iteration < 10; iteration++) {
    const snapshot = model.snapshot();
    assert.deepEqual(snapshot.voices[0], voices[0]);
    assert.deepEqual(snapshot.stats, stats);
  }
  assert.deepEqual(calls, []);
});
