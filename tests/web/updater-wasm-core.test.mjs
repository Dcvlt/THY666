/* Actual portable Rust ABI exercised with synthetic loader replies only. */
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';

const signature = new TextEncoder().encode('sSJFfddSAD$srAffy84JappEKL3$SALgK@3J4hwd5920xg1!8hs7J6Kg');
const marker = (token, prefixOnly = false) => {
  const data = new Uint8Array(64); data.set(new TextEncoder().encode(token));
  if (prefixOnly) data.fill(0xa5, 8); else data.set(signature, 8);
  return [...data];
};
const number = value => { const data = new Uint8Array(64).fill(0x5a); new DataView(data.buffer).setUint32(0, value, true); return [...data]; };
function fixture() {
  const app = new Uint8Array(512), view = new DataView(app.buffer);
  view.setUint32(0, 0x20020000, true); view.setUint32(4, 0x08040191, true);
  app.set(new TextEncoder().encode('deadbeef12345678'), 400);
  const fields = [[6,1],[6,0],[6,1],[6,0],[6,512],[8,0xebf3a7f7],[8,0x429b51d6],[8,0xaa017d8c],[6,4],[6,0],[6,1]];
  const header = new TextEncoder().encode('THY333' + fields.map(([width,value]) => value.toString(16).padStart(width, '0')).join(''));
  const raw = new Uint8Array(header.length + app.length); raw.set(header); raw.set(app, header.length);
  return { raw, app };
}
async function core() {
  const root = new URL('../../', import.meta.url);
  const bytes = await readFile(new URL('web/generated/browser-updater.wasm', root));
  const manifest = JSON.parse(await readFile(new URL('web/generated/browser-updater.json', root), 'utf8'));
  assert.equal(bytes.length, manifest.artifact.bytes);
  assert.equal(createHash('sha256').update(bytes).digest('hex'), manifest.artifact.sha256);
  const module = await WebAssembly.compile(bytes);
  assert.deepEqual(WebAssembly.Module.imports(module), []);
  const exports = (await WebAssembly.instantiate(module, {})).exports;
  assert.equal(exports.thy_update_abi_version(), 1);
  return { exports, call(metadata, payload = new Uint8Array(), now = 0) {
    const json = new TextEncoder().encode(JSON.stringify(metadata));
    const ptr = exports.thy_update_input_ptr(), length = 4 + json.length + payload.length;
    assert.ok(length <= exports.thy_update_input_capacity());
    const input = new Uint8Array(exports.memory.buffer, ptr, length);
    new DataView(exports.memory.buffer).setUint32(ptr, json.length, true);
    input.set(json, 4); input.set(payload, 4 + json.length);
    const size = exports.thy_update_request(length, now);
    assert.ok(size > 0 && size <= 65536);
    return JSON.parse(new TextDecoder().decode(new Uint8Array(exports.memory.buffer, exports.thy_update_output_ptr(), size)));
  }};
}
function replies(crc) {
  return [marker('FILSTATU'), number(1), marker('SFIFDONE'), marker('FWMETABG', true), number(4),
    ...Array.from({ length:14 }, (_,i) => number(i === 11 ? 0x11111111 : i === 12 ? 0x22222222 : i === 13 ? 0x33333333 : i)),
    marker('FWMETADN', true), marker('FCMDDONE'), marker('FCMDDONE'), marker('FWSETRD1'), marker('FWSETRD2'),
    marker('TRACRCVL'), number(crc), marker('EXTCRCVL'), number(crc), marker('FWDLYRDY')];
}
function prepared(engine) {
  const { raw, app } = fixture();
  const info = engine.call({ cmd:'inspect', name:'THY666-instrument-alpha-deadbeef.333' }, raw);
  assert.equal(info.ok, true); assert.equal(info.package.build_id, 'deadbeef12345678');
  return { app, queue:replies(parseInt(info.package.crc32, 16)) };
}
function replay(engine, queue, { failWrite = -1, partialWrite = -1, beforeFeed = () => {} } = {}) {
  const writes = []; let now = 0;
  let previousPhase = '', phaseStart = 0;
  let value = engine.call({ cmd:'begin', expected_uid:'111111112222222233333333' });
  for (let actions = 0; actions < 200 && value.ok && value.action.kind !== 'done'; actions++) {
    if (value.progress.phase !== previousPhase) { previousPhase = value.progress.phase; phaseStart = now; }
    const action = value.action;
    const feed = { cmd:'feed', id:action.id, kind:action.kind };
    if (action.kind === 'write') {
      assert.equal(action.report.length, 65); assert.equal(action.report[0], 0);
      writes.push(action.report);
      if (writes.length - 1 === failWrite) feed.error = true;
      else feed.count = writes.length - 1 === partialWrite ? 12 : 65;
    } else if (action.kind === 'read') {
      assert.ok(action.timeout_ms >= 1 && action.timeout_ms <= 250);
      feed.data = queue.shift() || [];
    } else {
      assert.equal(action.kind, 'sleep'); assert.equal(action.ms, 500); now += action.ms;
    }
    const changed = beforeFeed({ action, feed, value, now, phaseStart, writes });
    if (changed !== undefined) now = changed;
    value = engine.call(feed, undefined, now);
  }
  return { value, writes, queue };
}

test('browser WASM and native session share exact successful framing and full identity checks', async () => {
  const engine = await core(), { app, queue } = prepared(engine);
  const result = replay(engine, queue);
  assert.equal(result.value.ok, true); assert.equal(result.value.action.kind, 'done');
  assert.equal(result.value.action.outcome.target_uid, '111111112222222233333333');
  assert.equal(result.value.action.outcome.activation_writes, 5);
  assert.equal(result.value.progress.state, 'awaiting-verification');
  assert.equal(result.queue.length, 0); assert.equal(result.writes.length, 26);
  assert.deepEqual(new Uint8Array(result.writes.slice(12,20).flatMap(report => report.slice(1))), app);
  assert.equal(engine.exports.thy_update_reset(), 0);
  assert.equal(engine.call({ cmd:'begin' }).ok, false);
});

test('WASM CRC mismatch and partial payload failure never request activation or retry', async () => {
  const badCrc = await core(), state = prepared(badCrc);
  state.queue[27] = number(0);
  const rejected = replay(badCrc, state.queue);
  assert.equal(rejected.value.ok, false); assert.equal(rejected.value.error.phase, 'checksums');
  assert.equal(rejected.writes.length, 21);
  const partial = await core(), other = prepared(partial), stopped = replay(partial, other.queue, { partialWrite:14 });
  assert.equal(stopped.value.ok, false); assert.equal(stopped.value.error.phase, 'application');
  assert.equal(stopped.value.error.indeterminate, true); assert.equal(stopped.writes.length, 15);
});

test('WASM late activation loss preserves uncertainty and explicit application verification', async () => {
  const engine = await core(), state = prepared(engine);
  const result = replay(engine, state.queue, { failWrite:23 });
  assert.equal(result.value.ok, true); assert.equal(result.value.action.kind, 'done');
  assert.equal(result.value.action.outcome.activation_writes, 2);
  assert.match(result.value.action.outcome.warning, /verify the application/);
  assert.equal(result.writes.length, 24);
});

test('WASM acknowledgement deadlines, replacement inspection and input bounds fail closed', async () => {
  const engine = await core(); prepared(engine);
  const first = engine.call({ cmd:'begin' });
  assert.equal(engine.exports.thy_update_reset(), 1);
  assert.equal(engine.call({ cmd:'inspect' }, fixture().raw).ok, false);
  const read = engine.call({ cmd:'feed', id:first.action.id, kind:'write', count:65 }, undefined, 1);
  const expired = engine.call({ cmd:'feed', id:read.action.id, kind:'read', data:[] }, undefined, 60000);
  assert.equal(expired.ok, false); assert.match(expired.error.message, /timed out/);
  assert.equal(expired.error.indeterminate, true); assert.equal(engine.exports.thy_update_reset(), 0);
  const length = engine.exports.thy_update_request(engine.exports.thy_update_input_capacity() + 1, 0);
  const invalid = JSON.parse(new TextDecoder().decode(new Uint8Array(engine.exports.memory.buffer, engine.exports.thy_update_output_ptr(), length)));
  assert.equal(invalid.ok, false);
});

test('late successful WASM phase-ending reports and final data acknowledgement cannot reset deadlines', async () => {
  const decoder = new TextDecoder();
  for (const [phase, token, limit, writeCount] of [
    ['preparation', 'SFIFDONE', 60000, 1],
    ['ready2', 'FWSETRD2', 60000, 12],
    ['checksums', 'FWDLYRDY', 60000, 21],
    ['application', null, 180000, 20],
  ]) {
    const engine = await core(), state = prepared(engine); let triggered = false;
    const result = replay(engine, state.queue, { beforeFeed({ feed, value, phaseStart, writes }) {
      if (value.progress.phase === phase && (token ? feed.kind === 'read' && decoder.decode(Uint8Array.from(feed.data).subarray(0,8)) === token : feed.kind === 'write' && writes.length === writeCount)) {
        triggered = true; return phaseStart + limit;
      }
    }});
    assert.ok(triggered, phase); assert.equal(result.value.ok, false, phase);
    assert.equal(result.value.error.phase, phase); assert.match(result.value.error.message, /timed out/);
    assert.equal(result.value.error.indeterminate, true); assert.equal(result.writes.length, writeCount);
  }
});

test('WASM delayed continuations remain inside old deadlines, including after tab suspension', async () => {
  const decoder = new TextDecoder();
  for (const [phase, token, writeCount] of [
    ['preparation', 'SFIFDONE', 1], ['ready2', 'FWSETRD2', 12], ['checksums', 'FWDLYRDY', 21],
  ]) {
    const engine = await core(), state = prepared(engine); let delayNext = false, triggered = false;
    const result = replay(engine, state.queue, { beforeFeed({ feed, value, phaseStart }) {
      if (delayNext && feed.kind === 'sleep') { triggered = true; return phaseStart + 60000; }
      if (value.progress.phase === phase && feed.kind === 'read' && decoder.decode(Uint8Array.from(feed.data).subarray(0,8)) === token) delayNext = true;
    }});
    assert.ok(triggered, phase); assert.equal(result.value.ok, false);
    assert.equal(result.value.error.phase, phase); assert.match(result.value.error.message, /timed out/);
    assert.equal(result.writes.length, writeCount);
  }
  const engine = await core(), state = prepared(engine);
  const lateActivation = replay(engine, state.queue, { beforeFeed({ feed, value, phaseStart, writes }) {
    if (value.progress.phase === 'activation' && feed.kind === 'sleep' && writes.length === 22) return phaseStart + 10000;
  }});
  assert.equal(lateActivation.value.ok, true); assert.equal(lateActivation.value.action.kind, 'done');
  assert.equal(lateActivation.value.action.outcome.activation_writes, 1);
  assert.match(lateActivation.value.action.outcome.warning, /verify the application/);
  assert.equal(lateActivation.writes.length, 22);
});
