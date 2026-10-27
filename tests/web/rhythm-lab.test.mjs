import test from 'node:test';
import assert from 'node:assert/strict';
import { createRhythmLabModel } from '../../web/rhythm-lab.js';

function fixture() {
  const calls = [], notes = new Map();
  let nextHandle = 1, drains = 0, reject = null;
  const state = { clockSource: 'internal', clockState: 'internal', bpm: 120, ppqn: 96,
    tick: 0, songTick: 0, transport: 'stopped', generatedCount: 0,
    tracks: Array.from({ length: 21 }, (_, index) => ({ index, running: false, enabled: false,
      generation: 0, pending: null, loopStart: 0, loopEnd: 384 })) };
  const invoke = (name, args, action = () => {}) => {
    calls.push([name, ...args]);
    if (reject === name) return { ok: false, code: 3, status: 'queue-full' };
    action(); state.generatedCount = notes.size; return { ok: true, code: 0, status: 'ok' };
  };
  const rhythm = {
    snapshot: () => structuredClone(state),
    advance: timestamp => {
      assert.equal(timestamp, 123456);
      return reject === 'advance' ? { ok: false, code: 3, status: 'queue-full' } : { ok: true };
    },
    configure: config => invoke('configure', [config], () => {
      if (state.clockSource !== config.source) notes.clear();
      state.clockSource = config.source;
    }),
    start: () => invoke('start', [], () => { state.transport = 'playing'; }),
    stop: () => invoke('stop', [], () => {
      state.transport = 'stopped';
      for (const track of state.tracks) { track.running = false; track.generation++; }
      for (const [handle, owner] of notes) if (owner.source === 'track') notes.delete(handle);
    }),
    toggleTrack: track => invoke('toggleTrack', [track]),
    setTrackLoop: (...args) => invoke('setTrackLoop', args),
    midi: (...args) => invoke('midi', args),
    acquire: owner => {
      let handle;
      const result = invoke('acquire', [owner], () => { handle = nextHandle++; notes.set(handle, owner); });
      return { ...result, handle };
    },
    release: handle => invoke('release', [handle], () => notes.delete(handle)),
    clearSource: (source, track) => invoke('clearSource', [source, track], () => {
      for (const [handle, owner] of notes) if (owner.source === source && (source !== 'track' || owner.track === track)) notes.delete(handle);
    }),
  };
  const model = createRhythmLabModel({ rhythm, onEvents: () => drains++, now: () => 123456 });
  return { model, state, calls, notes, drains: () => drains, reject: name => { reject = name; } };
}
const owner = (source = 'arp1', track = 0) => ({ source, track, note: 60, channel: 0, mask: 5, velocity: 96 });

test('track and bank selection are local cursors, never transport commands', () => {
  const { model, calls } = fixture();
  assert.equal(model.selectTrack(10).ok, true);
  assert.equal(model.snapshot().selectedTrack, 10);
  assert.equal(model.snapshot().bank, 1);
  model.selectBank(2);
  assert.equal(model.snapshot().selectedTrack, 17);
  assert.equal(model.snapshot().bank, 2);
  for (const invalid of [-1, 21, 1.5, NaN]) assert.equal(model.selectTrack(invalid).ok, false);
  for (const invalid of [-1, 3, '1']) assert.equal(model.selectBank(invalid).ok, false);
  assert.deepEqual(calls, []);
});

test('transport, queue and loop requests preserve exact arguments and drain C events', () => {
  const { model, calls, drains } = fixture();
  const config = { source: 'internal', bpm: 143, meterNumerator: 7, meterDenominator: 8, quantization: 'beat' };
  model.configure(config); model.start(); model.selectTrack(18); model.toggleTrack(); model.setTrackLoop(96, 768); model.stop();
  assert.deepEqual(calls, [['configure', config], ['start'], ['toggleTrack', 18], ['setTrackLoop', 18, 96, 768], ['stop']]);
  assert.equal(drains(), 5);
  assert.equal(model.snapshot().transport, 'stopped');
});

test('synthetic MIDI forwards one message per action, including SPP byte order and injected timestamp', () => {
  const { model, calls, drains } = fixture();
  for (const command of ['pulse', 'start', 'continue', 'stop']) model.midi(command);
  model.midi('position', 8193);
  assert.deepEqual(calls, [['midi', 0xf8, 0, 0, 123456], ['midi', 0xfa, 0, 0, 123456],
    ['midi', 0xfb, 0, 0, 123456], ['midi', 0xfc, 0, 0, 123456], ['midi', 0xf2, 1, 64, 123456]]);
  assert.equal(drains(), 5);
  for (const [command, position] of [['other', 0], ['toString', 0], ['position', -1], ['position', 16384], ['position', 1.5]])
    assert.equal(model.midi(command, position).ok, false);
  assert.equal(calls.length, 5);
});

test('overlapping preview owners keep distinct C handles and release independently', () => {
  const { model, calls, notes, drains } = fixture();
  const first = model.acquire(owner('arp1')).handle;
  const second = model.acquire(owner('arp2')).handle;
  assert.notEqual(first, second);
  assert.equal(model.snapshot().previewOwners.length, 2);
  model.release(first);
  assert.equal(notes.has(second), true);
  assert.deepEqual(model.snapshot().previewOwners.map(item => item.handle), [second]);
  assert.equal(calls.at(-1)[0], 'release');
  assert.equal(drains(), 3);
});

test('C stop cleanup removes track labels and preserves independent Arp owner labels', () => {
  const { model, state, notes } = fixture();
  state.tracks[3].running = true;
  model.acquire(owner('track', 3));
  const independent = model.acquire(owner('arp1')).handle;
  model.stop();
  assert.deepEqual(model.snapshot().previewOwners.map(item => item.handle), [independent]);
  assert.equal(notes.size, 1);
});

test('track generation changes invalidate local labels without attempting a second release', () => {
  const { model, state, calls } = fixture();
  state.tracks[5].running = true;
  model.acquire(owner('track', 5)); model.acquire(owner('arp2'));
  state.tracks[5].generation++;
  assert.deepEqual(model.snapshot().previewOwners.map(item => item.source), ['arp2']);
  assert.equal(calls.some(call => call[0] === 'release'), false);
});

test('source clearing is scoped to the selected track and source changes retire all labels', () => {
  const { model, state, notes } = fixture();
  state.tracks[0].running = true; state.tracks[8].running = true;
  model.acquire(owner('track', 0)); model.acquire(owner('track', 8)); model.acquire(owner('arp1'));
  model.selectTrack(8); model.clearSource('track');
  assert.equal(notes.size, 2);
  assert.deepEqual(model.snapshot().previewOwners.map(item => [item.source, item.track]), [['track', 0], ['arp1', 0]]);
  model.configure({ source: 'usb', bpm: 120, meterNumerator: 4, meterDenominator: 4, quantization: 'bar' });
  assert.equal(model.snapshot().previewOwners.length, 0);
});

test('rejected C operations expose the actual status and still drain events without removing owners', () => {
  const { model, reject, drains } = fixture();
  const handle = model.acquire(owner()).handle;
  reject('release');
  assert.equal(model.release(handle).ok, false);
  assert.equal(model.snapshot().previewOwners.length, 1);
  assert.match(model.snapshot().notice, /queue-full/);
  assert.equal(drains(), 2);
  reject('acquire'); model.acquire(owner('arp2'));
  assert.equal(model.snapshot().previewOwners.length, 1);
});

test('refreshing the model observes C snapshots without advancing time or scheduling music', () => {
  const { model, state, calls } = fixture();
  state.songTick = 2000; state.tick = 900; state.clockState = 'lost';
  for (let i = 0; i < 10; i++) {
    assert.equal(model.snapshot().songTick, 2000);
    assert.equal(model.snapshot().clockState, 'lost');
  }
  assert.deepEqual(calls, []);
});

test('an input first synchronizes C time, and an advance rejection aborts the command while draining', () => {
  const { model, calls, reject, drains } = fixture();
  reject('advance');
  assert.equal(model.start().ok, false);
  assert.deepEqual(calls, []);
  assert.equal(drains(), 1);
  assert.match(model.snapshot().notice, /queue-full/);
});
