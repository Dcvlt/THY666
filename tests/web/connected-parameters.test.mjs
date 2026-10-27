import test from 'node:test';
import assert from 'node:assert/strict';
import { ConnectedParameterEditor } from '../../web/engine-studio.js';
import { MENU_BY_ID } from '../../web/studio-menu.js';

const channel = MENU_BY_ID.get('midi.channel');
const velocity = MENU_BY_ID.get('sensitivity.velocityMode');
const toggle = MENU_BY_ID.get('performance.mpe');
const action = MENU_BY_ID.get('system.about');
const encode = (row, value) => row.type === 'toggle' ? Number(value)
  : row.type === 'select' ? row.options.findIndex(option => option.value === value) : value;
function harness() {
  const calls = [], errors = [];
  let record = { value: 2, raw: 2, instance: 2 }, target = 2;
  const bridge = {
    connected: true, compatible: true,
    get(id) { calls.push(['read', id]); return record; },
    select(id) { calls.push(['select', id]); },
    instance() { return target; },
    async commit(...args) { calls.push(['commit', ...args]); return { ok: true }; },
    async action(...args) { calls.push(['action', ...args]); return { ok: true }; },
    error(error) { errors.push(error); },
  };
  const editor = new ConnectedParameterEditor(encode);
  editor.setBridge(bridge);
  return { editor, bridge, calls, errors, setRecord(value) { record = value; }, setTarget(value) { target = value; } };
}

test('a missing device record remains unreadable and cannot fall back to preview values', async () => {
  const h = harness();
  h.setRecord(null);
  assert.equal(h.editor.get(channel.id), null);
  h.editor.select(channel.id);
  await assert.rejects(h.editor.commit(channel, 7, null), /Reading/);
  assert.equal(h.calls.some(call => call[0] === 'commit'), false);
  assert.deepEqual(h.calls, [['read', channel.id], ['select', channel.id]]);
  h.setRecord({ value: 2, raw: 2, instance: 255 });
  assert.equal(h.editor.get(channel.id), null, 'Current-instance sentinel is not a captured explicit target.');
});

test('a device draft retains the displayed side while physical controls change the current route', async () => {
  const h = harness(), shown = h.editor.get(channel.id);
  h.editor.change(channel, 4, shown);
  h.setRecord({ value: 12, raw: 12, instance: 1 });
  assert.deepEqual(h.editor.get(channel.id), { value: 12, raw: 12, instance: 1 });
  assert.deepEqual(h.editor.draft, { id: channel.id, value: 4, raw: 4, instance: 2 });
  h.editor.change(channel, 5, h.editor.get(channel.id));
  await h.editor.commit(channel, 5, h.editor.get(channel.id));
  assert.deepEqual(h.calls.filter(call => call[0] === 'commit'), [['commit', channel.id, 2, 5]]);
  assert.equal(h.editor.draft, null);
  assert.equal(h.editor.pending, false);
});

test('cancel clears only the browser draft and never sends a parameter command', () => {
  const h = harness();
  h.editor.change(channel, 9, h.editor.get(channel.id));
  h.editor.cancel();
  assert.equal(h.editor.draft, null);
  assert.deepEqual(h.calls.filter(call => call[0] === 'commit' || call[0] === 'action'), []);
  assert.equal(h.editor.get(channel.id).value, 2);
});

test('schema mismatch and disconnect block reads and all remote mutations without invoking an encoder', async () => {
  const h = harness();
  h.editor.change(channel, 5, h.editor.get(channel.id));
  h.bridge.compatible = false;
  const count = h.calls.length;
  assert.equal(h.editor.get(channel.id), null);
  h.editor.select(channel.id);
  await assert.rejects(h.editor.commit(channel, 6, { instance: 2 }), /schema/);
  await assert.rejects(h.editor.action(action, 0), /schema/);
  assert.equal(h.calls.length, count);
  assert.equal(h.editor.draft.raw, 5);
  h.bridge.connected = false;
  await assert.rejects(h.editor.commit(channel, 6, { instance: 2 }), /disconnected/);
  await assert.rejects(h.editor.action(action, 0), /disconnected/);
  assert.equal(h.calls.length, count);
});

test('a rejected device commit keeps its draft, reports an error and blocks duplicate in-flight writes', async () => {
  const h = harness();
  let resolve;
  h.bridge.commit = (...args) => { h.calls.push(['commit', ...args]); return new Promise(done => { resolve = done; }); };
  const write = h.editor.commit(channel, 6, h.editor.get(channel.id));
  assert.equal(h.editor.pending, true);
  await assert.rejects(h.editor.commit(channel, 7, h.editor.get(channel.id)), /Wait/);
  assert.throws(() => h.editor.change(channel, 8, { instance: 1 }), /Wait/);
  h.editor.cancel();
  assert.equal(h.editor.draft.raw, 6);
  resolve({ ok: false, message: 'Release affected pads first.' });
  await assert.rejects(write, /Release affected pads/);
  assert.equal(h.editor.pending, false);
  assert.equal(h.editor.draft.raw, 6);
  h.editor.error(new Error('Device rejected the setting'));
  assert.equal(h.errors.length, 1);
  assert.equal(h.calls.filter(call => call[0] === 'commit').length, 1);
});

test('actions are explicit, keep the displayed action scope and never become parameter edits', async () => {
  const h = harness();
  const captured = h.editor.actionInstance(action);
  assert.equal(captured, 2);
  h.setTarget(1);
  await h.editor.action(action, captured);
  assert.deepEqual(h.calls, [['action', action.id, 2]]);
  assert.equal(h.editor.draft, null);
  assert.equal(h.editor.actionInstance(channel), null);
  await assert.rejects(h.editor.action(channel, 0), /action target/);
  await assert.rejects(h.editor.action(action, 255), /action target/);
});

test('typed values encode once, while Mixed and invalid values cannot be applied', async () => {
  const h = harness(), shown = { value: false, raw: 0, instance: 0 };
  await h.editor.commit(toggle, true, shown);
  assert.deepEqual(h.calls.filter(call => call[0] === 'commit'), [['commit', toggle.id, 0, 1]]);
  assert.throws(() => h.editor.change(velocity, 2, shown), /Mixed/);
  assert.throws(() => h.editor.change(channel, NaN, shown), /valid/);
  assert.throws(() => h.editor.change(channel, 2147483648, shown), /valid/);
  assert.throws(() => h.editor.change({ ...channel, status: 'design' }, 5, shown), /not editable/);
});

test('completing an old connection write does not overwrite a new connection draft', async () => {
  const h = harness();
  let finish;
  h.bridge.commit = () => new Promise(resolve => { finish = resolve; });
  const oldWrite = h.editor.commit(channel, 6, h.editor.get(channel.id));
  const next = { ...h.bridge, get: () => ({ value: 3, raw: 3, instance: 1 }) };
  h.editor.setBridge(next);
  assert.equal(h.editor.draft, null);
  h.editor.change(channel, 10, h.editor.get(channel.id));
  finish({ ok: true }); await oldWrite;
  assert.deepEqual(h.editor.draft, { id: channel.id, value: 10, raw: 10, instance: 1 });
  assert.equal(h.editor.pending, false);
});
