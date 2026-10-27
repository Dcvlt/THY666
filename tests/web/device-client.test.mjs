import test from 'node:test';
import assert from 'node:assert/strict';
import {
  BACKUP_HEADER_BYTES, BACKUP_MAX_BYTES, deviceCRC32, deviceFields,
  inspectDeviceBackup, createDeviceClient,
} from '../../web/device-client.js';

const UID = '0123456789abcdef01234567', BUILD = '90d128d90324c978', SCHEMA = 'ab'.repeat(32);
const encoder = new TextEncoder(), decoder = new TextDecoder();
const hex = bytes => [...bytes].map(value => value.toString(16).padStart(2, '0')).join('');
const unhex = value => Uint8Array.from(value.match(/../g) || [], word => parseInt(word, 16));
const crc = bytes => deviceCRC32(bytes).toString(16).padStart(8, '0');
const backupIdentity = { uid: UID, build: BUILD, schema: SCHEMA, settings: '13', target: '1043', compatibility: '65537' };

function makeBackup({ payloadBytes = 5633, uid = UID, build = BUILD, schema = SCHEMA } = {}) {
  const bytes = new Uint8Array(BACKUP_HEADER_BYTES + payloadBytes), data = new DataView(bytes.buffer);
  bytes.set(encoder.encode('THYB')); data.setUint16(4, 1, true); data.setUint16(6, 64, true);
  for (let n = 0; n < payloadBytes; n++) bytes[64 + n] = (n * 17 + 3) & 255;
  data.setUint32(8, payloadBytes, true); data.setUint32(12, deviceCRC32(bytes.subarray(64)), true);
  data.setUint16(16, 13, true); data.setUint16(18, 0x413, true); data.setUint32(20, 0x10001, true);
  bytes.set(unhex(uid), 24); bytes.set(unhex(build), 36); bytes.set(unhex(schema.slice(0, 16)), 44);
  data.setUint32(52, 1, true); data.setUint32(60, deviceCRC32(bytes.subarray(0, 60)), true);
  return bytes;
}
function reseal(bytes) {
  const data = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  data.setUint32(12, deviceCRC32(bytes.subarray(64)), true);
  data.setUint32(60, deviceCRC32(bytes.subarray(0, 60)), true);
  return bytes;
}

function environment(t, { override, writable = '1', schema = SCHEMA, expectedSchema = SCHEMA, timeoutMs = 40,
  fragment = false, statusSequence = ['applied', 'queued', 'saved-verified'], exportBytes,native=false } = {}) {
  let waiter = null, closed = false, time = 0;
  const queued = [], states = [], commands = [], portEvents = new Map();
  const server = { file: makeBackup(), snapshot: Uint8Array.from({ length: 1680 }, (_, n) => n & 255),
    generation: 7, committed: false, upload: null, statusReads: 0, received: 0 };
  const push = text => {
    if (closed) return;
    const bytes = encoder.encode(text);
    if (waiter) { const resolve = waiter; waiter = null; resolve({ value: bytes, done: false }); }
    else queued.push(bytes);
  };
  const reply = text => {
    if (text == null) return;
    const line = text + '\r\n';
    queueMicrotask(() => {
      if (fragment) { push(line.slice(0, 3)); push(line.slice(3, 19)); push(line.slice(19)); }
      else push(line);
    });
  };
  const statusLine = state => `BACKUP version=1 state=${state} bytes=${server.committed ? server.file.length : 0} received=${server.received} crc32=${crc(server.file)} payload_crc32=${crc(server.file.subarray(64))} generation=31`;
  function respond(command) {
    if (command === 'companion caps') return `COMPANION/1 CAPS role=instrument backup=1 song=1 companion=1 uid=${UID} build=${BUILD} schema=${schema} writable=${writable} chunk=128`;
    if (command === 'backup caps') return `BACKUP version=1 settings=13 max=${BACKUP_MAX_BYTES} chunk=32 target=1043 compatibility=65537 uid=${UID} build=${BUILD} schema=${schema}`;
    if (command === 'companion snapshot') return `COMPANION/1 SNAPSHOT seq=${server.generation} bytes=${server.snapshot.length} crc32=${crc(server.snapshot)}`;
    if (command.startsWith('companion get ')) {
      const [, , seq, offset, length] = command.split(' ').map((word, index) => index > 1 ? Number(word) : word);
      return `COMPANION/1 DATA seq=${seq} offset=${offset} hex=${hex(server.snapshot.subarray(offset, offset + length))}`;
    }
    if (command.startsWith('companion release ')) return `COMPANION/1 RELEASED seq=${server.generation}`;
    if (command.startsWith('companion read ')) {
      const [, , , row, instance] = command.split(' ');
      return `COMPANION/1 VALUE row=${row} instance=${instance === '255' ? '1' : instance} value=80`;
    }
    if (command.startsWith('companion set ')) {
      const [, , , row, instance, value] = command.split(' ');
      return `COMPANION/1 SET row=${row} instance=${instance} value=${value} result=0`;
    }
    if (command.startsWith('companion nav ')) return 'COMPANION/1 NAV result=0';
    if (command.startsWith('companion control ')) return 'COMPANION/1 CONTROL result=0';
    if (command.startsWith('companion action ')) return 'COMPANION/1 ACTION result=0';
    if (command.startsWith('companion pad ')) return `COMPANION/1 PAD logical=${command.split(' ').at(-1)} result=0`;
    if (command === 'backup export') {
      const file = exportBytes ? exportBytes(server) : server.file;
      server.download = file;
      return `BACKUP export bytes=${file.length} crc32=${crc(file)} settings=13`;
    }
    if (command.startsWith('backup get ')) {
      const [, , offset, length] = command.split(' ').map((word, index) => index > 1 ? Number(word) : word);
      return `BACKUP data offset=${offset} hex=${hex(server.download.subarray(offset, offset + length))}`;
    }
    if (command === 'backup abort') return 'BACKUP aborted';
    if (command === 'backup status') return statusLine(server.committed ? statusSequence[Math.min(server.statusReads++, statusSequence.length - 1)] : 'idle');
    if (command.startsWith('backup begin ')) {
      const [, , length, checksum] = command.split(' ');
      server.upload = new Uint8Array(Number(length)); server.expectedCRC = Number(checksum); server.received = 0;
      return `BACKUP ready bytes=${length}`;
    }
    if (command.startsWith('backup put ')) {
      const [, , offset, text] = command.split(' '), part = unhex(text);
      assert.equal(Number(offset), server.received); server.upload.set(part, Number(offset)); server.received += part.length;
      return `BACKUP received=${server.received}`;
    }
    if (command === 'backup commit') {
      assert.equal(deviceCRC32(server.upload), server.expectedCRC); server.file = server.upload; server.committed = true;
      return `BACKUP applied bytes=${server.file.length} payload_crc32=${crc(server.file.subarray(64))}`;
    }
    if (/^(status|io|settings|display)$/.test(command)) return command.toUpperCase() + ' ready=1';
    throw new Error(`Unexpected mock command: ${command}`);
  }
  const reader = { async read() {
    if (queued.length) return { value: queued.shift(), done: false };
    if (closed) return { done: true };
    return new Promise(resolve => waiter = resolve);
  }, async cancel() { closed = true; if (waiter) { waiter({ done: true }); waiter = null; } }, releaseLock() {} };
  const port = {
    opens: 0, closes: 0, getInfo: () => ({ usbVendorId: 0xcafe, usbProductId: 0x4009 }),
    async open(options) { this.opens++; this.options = options; }, async close() { this.closes++; },
    readable: { getReader: () => reader },
    writable: { getWriter: () => ({ releaseLock() {}, async write(bytes) {
      const text = decoder.decode(bytes); assert.ok(text.endsWith('\n'));
      const command = text.slice(0, -1); commands.push(command);
      const overridden = override?.(command, { server, push, reply, port, serial, respond, statusLine });
      reply(overridden === undefined ? respond(command) : overridden);
    } }) },
  };
  const serial = { requests: [], async requestPort(options) { this.requests.push(options); return port; },
    addEventListener(type, callback) { portEvents.set(type, callback); },
    disconnect(which = port) { portEvents.get('disconnect')?.({ target: which, port: which }); } };
  if(native){
    delete port.readable;delete port.writable;port.batches=[];
    port.exchange=async command=>{commands.push(command);const value=override?.(command,{server,push,reply,port,serial,respond,statusLine});return value===undefined?respond(command):value;};
    port.exchangeMany=async values=>{port.batches.push(values.slice());const lines=[];for(const command of values)lines.push(await port.exchange(command));return lines;};
  }
  const client = createDeviceClient({ serial, timeoutMs, expectedSchema, onState: state => states.push(state),
    now: () => time, sleep: async ms => { time += ms; } });
  t.after(() => client.disconnect());
  return { client, serial, port, commands, states, server, push, reply };
}

test('backup framing validates both CRCs, byte offsets, identity and provenance without tying restoration to the old build', () => {
  assert.equal(deviceCRC32(encoder.encode('123456789')), 0xcbf43926);
  const file = makeBackup(), padded = new Uint8Array(file.length + 11); padded.set(file, 5);
  const info = inspectDeviceBackup(padded.subarray(5, 5 + file.length), backupIdentity);
  assert.equal(info.fileVersion, 1); assert.equal(info.payloadBytes, 5633); assert.equal(info.bytes, 5697);
  assert.equal(info.uid, UID); assert.equal(info.build, BUILD); assert.equal(info.schemaPrefix, SCHEMA.slice(0, 16));
  assert.equal(info.payloadCRC, deviceCRC32(file.subarray(64)));
  assert.doesNotThrow(() => inspectDeviceBackup(makeBackup({ build: '01'.repeat(8), schema: 'cd'.repeat(32) }), backupIdentity), 'Build/schema are provenance; supported settings compatibility and board identity govern this container.');
  assert.throws(() => inspectDeviceBackup(file, { ...backupIdentity, uid: 'ff'.repeat(12) }), /belong/);
  for (const bytes of [new Uint8Array(64), new Uint8Array(BACKUP_MAX_BYTES + 1), file.subarray(1), [...file]])
    assert.throws(() => inspectDeviceBackup(bytes));
  for (const change of [
    b => b[0] ^= 1, b => b[4] = 2, b => b[6] = 63,
    b => b[8] ^= 1, b => b[12] ^= 1, b => b[60] ^= 1, b => b[70] ^= 1,
  ]) { const changed = file.slice(); change(changed); assert.throws(() => inspectDeviceBackup(changed)); }
  for (const change of [b => b[16] = 12, b => b[18] = 0, b => b[20] = 2, b => b[52] = 2, b => b[56] = 1]) {
    const changed = file.slice(); change(changed); reseal(changed); assert.throws(() => inspectDeviceBackup(changed), /unsupported/);
  }
  assert.throws(() => deviceFields('BACKUP bytes=1 bytes=2', 'BACKUP'), /Duplicate/);
  assert.throws(() => deviceFields('OTHER bytes=1', 'BACKUP'), /Unexpected/);
});

test('native snapshots batch fourteen chunks into one bridge call and retain the exact CRC and generation checks',async t=>{
  const f=environment(t,{native:true});assert.equal(f.commands.length,0);await f.client.connect();
  assert.deepEqual((await f.client.snapshot()).bytes,f.server.snapshot);
  assert.equal(f.port.batches.length,1);assert.equal(f.port.batches[0].length,14);
  assert.equal(f.commands.at(-1),'companion release 7');
  assert.equal(f.port.opens,1);assert.equal(f.client.connected,true);
});

test('native backup batches stay bounded and normal device errors preserve the connection',async t=>{
  const f=environment(t,{native:true,override:command=>command.startsWith('companion set ')?'COMPANION/1 ERR range':undefined});
  await f.client.connect();assert.deepEqual(await f.client.downloadBackup(),f.server.file);
  assert.ok(f.port.batches.length>1);assert.ok(f.port.batches.every(batch=>batch.length<=32));
  await assert.rejects(()=>f.client.set(1,0,999),/range/);assert.equal(f.client.connected,true);
  assert.match(await f.client.telemetry('status'),/^STATUS /);
});

test('a malformed native response closes the session and never resends an edit',async t=>{
  const f=environment(t,{native:true,override:command=>command.startsWith('companion set ')?'unexpected stale response':undefined});
  await f.client.connect();await assert.rejects(()=>f.client.set(1,0,50),/Unexpected native/);
  assert.equal(f.client.connected,false);assert.equal(f.commands.filter(command=>command.startsWith('companion set ')).length,1);
  assert.ok(f.port.closes>0);
});

test('construction is inert; explicit connect selects only the application identity and opens a single port', async t => {
  const f = environment(t);
  assert.equal(f.client.connected, false); assert.equal(f.client.available, true);
  assert.deepEqual(f.serial.requests, []); assert.deepEqual(f.commands, []);
  await assert.rejects(f.client.snapshot(), /Connect/);
  const caps = await f.client.connect();
  assert.equal(caps.uid, UID); assert.equal(f.client.compatible, true);
  assert.deepEqual(f.serial.requests, [{ filters: [{ usbVendorId: 0xcafe, usbProductId: 0x4009 }] }]);
  assert.equal(f.port.opens, 1); assert.deepEqual(f.port.options, { baudRate: 115200 });
  assert.deepEqual(f.commands, ['companion caps', 'backup caps']);
  await f.client.connect(); assert.equal(f.serial.requests.length, 1);
  await f.client.disconnect(); assert.equal(f.client.connected, false); assert.equal(f.client.identity, null);
});

test('wrong USB ports and inconsistent companion/backup identities are rejected before editing', async t => {
  const wrong = environment(t); wrong.port.getInfo = () => ({ usbVendorId: 0x333, usbProductId: 0x333 });
  await assert.rejects(wrong.client.connect(), /not a THY666/); assert.equal(wrong.port.opens, 0);
  const mismatch = environment(t, { override: command => command === 'backup caps' ?
    `BACKUP version=1 settings=13 target=1043 compatibility=65537 uid=${'ff'.repeat(12)} build=${BUILD} schema=${SCHEMA}` : undefined });
  await assert.rejects(mismatch.client.connect(), /identities disagree/);
  assert.equal(mismatch.client.connected, false); assert.equal(mismatch.port.closes, 1);
});

test('an interface without the instrument role or complete service capabilities cannot connect or mutate', async t => {
  for (const missing of ['role', 'backup', 'song', 'companion']) {
    const f = environment(t, { override: command => command === 'companion caps' ?
      `COMPANION/1 CAPS ${['role=instrument', 'backup=1', 'song=1', 'companion=1'].filter(field => !field.startsWith(missing + '=')).join(' ')} uid=${UID} build=${BUILD} schema=${SCHEMA} writable=1 chunk=128` : undefined });
    await assert.rejects(f.client.connect(), /support|capabilit|role|instrument|identity|complete|services/i, missing);
    assert.equal(f.client.connected, false);
    assert.ok(!f.commands.some(command => /^(?:backup begin|companion (?:set|nav|control|action)) /.test(command)));
  }
});

test('schema mismatch remains read-only while snapshots and read-only telemetry still work', async t => {
  const f = environment(t, { schema: 'cd'.repeat(32) }); await f.client.connect();
  assert.equal(f.client.compatible, false); assert.equal(f.client.connected, true);
  await f.client.snapshot(); assert.match(await f.client.telemetry('status'), /^STATUS /);
  for (const request of [() => f.client.set(12, 1, 80), () => f.client.navigate('right'),
    () => f.client.action(12), () => f.client.control(0, 1)]) await assert.rejects(request(), /read-only/);
  assert.ok(!f.commands.some(command => /^companion (set|nav|control|action) /.test(command)));
});

test('actual namespaced companion errors reject without timing out or losing the connection', async t => {
  for(const error of ['busy','rate','snapshot-active']){
    let blocked=true;
    const f=environment(t,{override:command=>command==='companion snapshot'&&blocked?
      `COMPANION/1 ERR ${error}`:undefined});
    await f.client.connect();
    await assert.rejects(f.client.snapshot(),new RegExp(`COMPANION/1 ERR ${error}`));
    assert.equal(f.client.connected,true);assert.equal(f.client.compatible,true);assert.equal(f.client.busy,false);
    assert.equal(f.states.some(state=>/timed out/.test(state.message)),false);
    blocked=false;assert.deepEqual((await f.client.snapshot()).bytes,f.server.snapshot);
    assert.equal(f.port.opens,1);await f.client.disconnect();
  }
});

test('temporary writable zero at connect does not turn a matching schema permanently read-only', async t => {
  let saving=true;
  const f=environment(t,{writable:'0',override:command=>command.startsWith('companion set ')&&saving?
    'COMPANION/1 ERR busy':undefined});
  await f.client.connect();
  assert.equal(f.client.identity.writable,'0');assert.equal(f.client.compatible,true);
  await assert.rejects(f.client.set(12,1,80),/COMPANION\/1 ERR busy/);
  assert.equal(f.client.connected,true);
  saving=false;assert.equal((await f.client.set(12,1,80)).result,'0');
  assert.equal(f.port.opens,1);assert.equal(f.client.compatible,true);
});

test('fragmented snapshot lines reassemble exactly and a completed snapshot is released', async t => {
  const f = environment(t, { fragment: true }); await f.client.connect();
  f.push('STATUS irrelevant=1\r\n');
  const result = await f.client.snapshot();
  assert.equal(result.generation, 7); assert.deepEqual(result.bytes, f.server.snapshot);
  assert.equal(f.commands.at(-1), 'companion release 7'); assert.equal(f.client.busy, false);
});

test('snapshot generation, chunk offset/length and CRC errors release the frozen generation', async t => {
  for (const failure of ['generation', 'offset', 'length', 'crc']) {
    const f = environment(t, { override(command, { server }) {
      if (command === 'companion snapshot' && failure === 'crc') return `COMPANION/1 SNAPSHOT seq=7 bytes=1680 crc32=00000000`;
      if (command === 'companion get 7 0 128') return `COMPANION/1 DATA seq=${failure === 'generation' ? 8 : 7} offset=${failure === 'offset' ? 1 : 0} hex=${hex(server.snapshot.subarray(0, failure === 'length' ? 127 : 128))}`;
    } });
    await f.client.connect(); await assert.rejects(f.client.snapshot(), /generation|offset|checksum/);
    assert.equal(f.commands.at(-1), 'companion release 7'); await f.client.disconnect();
  }
});

test('backup download is exclusive, checksum-bound and aborted after export without overlapping snapshot commands', async t => {
  const f = environment(t); await f.client.connect();
  const download = f.client.downloadBackup(), snapshot = f.client.snapshot();
  assert.equal(f.client.busy, true);
  assert.deepEqual(await download, f.server.file); await snapshot;
  assert.ok(f.commands.indexOf('companion snapshot') > f.commands.indexOf('backup abort'));
  assert.equal(f.client.busy, false);
  assert.ok(f.commands.filter(command => command.startsWith('backup get ')).every(command => Number(command.split(' ').at(-1)) <= 32));
});

test('backup corruption and transfer timeout trigger cleanup and never return a successful file', async t => {
  for (const failure of ['offset', 'crc', 'timeout']) {
    const f = environment(t, { timeoutMs: 15, override(command, { server }) {
      if (command === 'backup get 0 32') {
        if (failure === 'timeout') return null;
        const bytes = server.download.slice(0, 32); if (failure === 'crc') bytes[0] ^= 1;
        return `BACKUP data offset=${failure === 'offset' ? 1 : 0} hex=${hex(bytes)}`;
      }
    } });
    await f.client.connect(); await assert.rejects(f.client.downloadBackup(), /offset|checksum|timed out/);
    assert.equal(f.commands.at(-1), 'backup abort'); assert.equal(f.client.busy, false);
    await f.client.disconnect();
  }
});

test('restore verifies upload bytes, waits beyond applied for saved-verified and re-exports the exact payload', async t => {
  const f = environment(t); await f.client.connect(); const file = makeBackup({ build: '01'.repeat(8) });
  const result = await f.client.restoreBackup(file);
  assert.deepEqual(result, { generation: 31, payloadCRC: deviceCRC32(file.subarray(64)), verified: true });
  assert.deepEqual(f.server.upload, file); assert.equal(f.server.statusReads, 3);
  assert.ok(f.states.some(state => /waiting for verified save/.test(state.message)));
  assert.match(f.client.status.message, /saved and read back exactly/);
  assert.ok(f.commands.indexOf('backup export') > f.commands.indexOf('backup commit'));
  assert.equal(f.commands.at(-1), 'backup abort');
});

test('restore can retry a prior ERROR state while the device still enforces journal availability', async t => {
  const recovered=environment(t,{override:(command,{server,statusLine})=>
    command==='backup status'&&!server.committed?statusLine('error'):undefined});
  await recovered.client.connect();
  const result=await recovered.client.restoreBackup(makeBackup());
  assert.equal(result.verified,true);assert.ok(recovered.commands.some(command=>command.startsWith('backup begin ')));
  assert.ok(recovered.commands.includes('backup commit'));assert.equal(recovered.commands.at(-1),'backup abort');

  const failed=environment(t,{override:(command,{statusLine})=>command==='backup status'?statusLine('error'):
    command.startsWith('backup begin ')?'ERR backup-busy':undefined});
  await failed.client.connect();
  await assert.rejects(failed.client.restoreBackup(makeBackup()),/ERR backup-busy/);
  assert.equal(failed.client.connected,true);assert.equal(failed.client.busy,false);
  assert.equal(failed.commands.some(command=>command.startsWith('backup put ')),false);
  assert.equal(failed.commands.includes('backup commit'),false);
});

test('restore does not confuse applied with saved, saving errors, or different readback with verified restoration', async t => {
  for (const failure of ['never-saved', 'save-error', 'readback']) {
    const f = environment(t, { statusSequence: failure === 'never-saved' ? ['applied'] : failure === 'save-error' ? ['error'] : ['saved-verified'],
      exportBytes: failure === 'readback' ? server => { const changed = server.file.slice(); changed[100] ^= 1; return reseal(changed); } : undefined });
    await f.client.connect(); await assert.rejects(f.client.restoreBackup(makeBackup()), /could not be verified|saving failed|re-export differs/);
    assert.equal(f.states.some(state => state.message === 'Restored · saved and read back exactly'), false);
    if (failure !== 'readback') assert.equal(f.commands.includes('backup export'), false);
    await f.client.disconnect();
  }
});

test('upload acknowledgement failure aborts without commit and a wrong-board file sends nothing', async t => {
  const f = environment(t, { override: command => command.startsWith('backup put 0 ') ? 'BACKUP received=31' : undefined });
  await f.client.connect(); const before = f.commands.length;
  await assert.rejects(f.client.restoreBackup(makeBackup({ uid: 'ff'.repeat(12) })), /belong/);
  assert.equal(f.commands.length, before);
  await assert.rejects(f.client.restoreBackup(makeBackup()), /acknowledgement/);
  assert.equal(f.commands.includes('backup commit'), false); assert.equal(f.commands.at(-1), 'backup abort');
});

test('device SET errors are not false successes and connected control commands cannot inject Note Ons', async t => {
  const f = environment(t, { override: command => command.startsWith('companion set ') ? 'COMPANION/1 SET row=12 instance=1 value=80 result=-2' : undefined });
  await f.client.connect(); await assert.rejects(f.client.set(12, 1, 80), /did not apply/);
  assert.equal(f.states.some(state => /Applied to device/.test(state.message)), false);
  await f.client.navigate('right'); await f.client.control(0, 2); await f.client.action(22, 0); await f.client.inspectPad(95);
  const count = f.commands.length;
  for (const request of [() => f.client.telemetry('noteon 60 127'), () => f.client.telemetry('status\nbackup commit'),
    () => f.client.navigate('note-on'), () => f.client.inspectPad(96), () => f.client.control(1, 14),
    () => f.client.set(12, 255, 80), () => f.client.set(12, 1, 2147483648)]) await assert.rejects(request());
  assert.equal(f.commands.length, count);
  assert.ok(f.commands.every(command => /^(companion|backup|status)/.test(command)));
  assert.ok(!f.commands.some(command => /note.?on|midi send|press pad/i.test(command)));
});

test('stale VALUE and successful SET replies cannot be attributed to a different row or explicit instance', async t => {
  for (const [operation, wrong] of [['read', 'row'], ['read', 'instance'], ['set', 'row'], ['set', 'instance']]) {
    const f = environment(t, { override: command => {
      if (!command.startsWith(`companion ${operation} `)) return undefined;
      return `COMPANION/1 ${operation === 'read' ? 'VALUE' : 'SET'} row=${wrong === 'row' ? 13 : 12} instance=${wrong === 'instance' ? 2 : 1} value=80 result=0`;
    } });
    await f.client.connect();
    await assert.rejects(operation === 'read' ? f.client.read(12, 1) : f.client.set(12, 1, 80),
      /different|unexpected|match|stale|setting|reply|response|target|row/i, `${operation}: ${wrong}`);
    assert.equal(f.states.some(state => /Applied to device/.test(state.message)), false);
    await f.client.disconnect();
  }
});

test('VALUE replies are bounded signed 32-bit settings rather than arbitrary decimal strings', async t => {
  for (const value of ['2147483648', '-2147483649', '99999999999999999999999999999', '1.5', 'NaN']) {
    const f = environment(t, { override: command => command.startsWith('companion read ') ?
      `COMPANION/1 VALUE row=12 instance=1 value=${value}` : undefined });
    await f.client.connect(); await assert.rejects(f.client.read(12, 1), /value|setting|range|invalid/i, value);
    await f.client.disconnect();
  }
});

test('a physical disconnect rejects in-flight work, ignores unrelated ports and closes the chosen port', async t => {
  const f = environment(t, { override: command => command === 'companion snapshot' ? null : undefined });
  await f.client.connect(); f.serial.disconnect({ unrelated: true }); assert.equal(f.client.connected, true);
  const waiting = f.client.snapshot();
  await new Promise(resolve => setImmediate(resolve)); f.serial.disconnect();
  await assert.rejects(waiting, /disconnect|closed/);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(f.client.connected, false); assert.equal(f.client.compatible, false); assert.equal(f.client.identity, null);
  assert.equal(f.port.closes, 1); assert.equal(f.client.busy, false);
});

test('stale snapshot data cannot satisfy a new generation and overlong device lines close the session', async t => {
  const stale = environment(t, { override: command => command === 'companion get 7 0 128' ?
    `COMPANION/1 DATA seq=6 offset=0 hex=${'00'.repeat(128)}` : undefined });
  await stale.client.connect(); await assert.rejects(stale.client.snapshot(), /generation changed/);
  const long = environment(t, { timeoutMs: 15, override(command, { push }) {
    if (command === 'companion snapshot') { queueMicrotask(() => push('x'.repeat(4097))); return null; }
  } });
  await long.client.connect(); await assert.rejects(long.client.snapshot(), /line limit/);
  assert.equal(long.client.connected, false);
});
