import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { webcrypto } from 'node:crypto';
import { createBrowserUpdaterClient, loadBrowserUpdaterCore } from '../../web/browser-updater-client.js';

const BASE = 'https://firmware.example.test/THY666/';
const BUILD = '1234567890abcdef', UID = '0123456789abcdef01234567';
const signature = new TextEncoder().encode('sSJFfddSAD$srAffy84JappEKL3$SALgK@3J4hwd5920xg1!8hs7J6Kg');
const manifest = JSON.parse(readFileSync(new URL('../../web/generated/browser-updater.json', import.meta.url)));
const wasm = readFileSync(new URL('../../web/generated/browser-updater.wasm', import.meta.url));
const marker = name => { const bytes = new Uint8Array(64); bytes.set(new TextEncoder().encode(name)); bytes.set(signature, 8); return bytes; };
const integer = value => { const bytes = new Uint8Array(64); new DataView(bytes.buffer).setUint32(0, value, true); return bytes; };
function firmware() {
  const app = new Uint8Array(512), view = new DataView(app.buffer);
  view.setUint32(0, 0x20020000, true); view.setUint32(4, 0x08040191, true); app.set(new TextEncoder().encode(BUILD), 400);
  const fields = [[6,1],[6,0],[6,1],[6,0],[6,512],[8,0xebf3a7f7],[8,0x429b51d6],[8,0xaa017d8c],[6,4],[6,0],[6,1]];
  const header = new TextEncoder().encode('THY333' + fields.map(([width, value]) => value.toString(16).padStart(width,'0')).join(''));
  const file = new Uint8Array(header.length + app.length); file.set(header); file.set(app, header.length); return file;
}
function replies(crc, { wrongCRC = false, wrongUID = false, status = 1 } = {}) {
  return [marker('FILSTATU'), integer(status), marker('SFIFDONE'), marker('FWMETABG'), integer(4),
    ...[1,1,1,0,1,0,0,1,0,0,0,0x01234567,0x89abcdef,wrongUID ? 0 : 0x01234567].map(integer), marker('FWMETADN'),
    marker('FCMDDONE'), marker('FCMDDONE'), marker('FWSETRD1'), marker('FWSETRD2'), marker('TRACRCVL'), integer(wrongCRC ? 0 : crc),
    marker('EXTCRCVL'), integer(crc), marker('FWDLYRDY')];
}
const fetchCore = async url => new Response(String(url).endsWith('.json') ? JSON.stringify(manifest) : wasm);
const loadCore = options => loadBrowserUpdaterCore({ ...options, baseURL: BASE,
  wasmURL: new URL('browser-updater.wasm', BASE), coreManifestURL: new URL('browser-updater.json', BASE), fetch: fetchCore, subtle: webcrypto.subtle });
const flush = () => new Promise(resolve => setImmediate(resolve));
async function waitDone(client, id) { for (let i=0;i<30;i++) { const op = await client.progress(id); if(op.state!=='running') return op; await flush(); } throw new Error('Synthetic update did not finish'); }
function fixture({ replyOptions = {}, writeError = false, lease = true, holdWrite = false } = {}) {
  const calls = [], id = 'synthetic-explicit-target'; let clock = 0, info, resume, queue = [];
  const usb = { available: true, has: value => value === id, clear() { calls.push('clear'); },
    async requestDevices() { calls.push('chooser'); return [{ id, label: 'Synthetic transport only', mode: 'bootloader', simulation: false }]; },
    async open(selected) {
      assert.equal(selected, id); calls.push('open'); queue = replies(parseInt(info.crc32, 16), replyOptions);
      return {
        async writeReport(bytes) { calls.push(bytes.slice()); if (holdWrite && !resume) await new Promise(resolve => { resume = resolve; }); if (writeError) throw new Error('Synthetic lost write'); return 65; },
        async readReport() { return queue.shift() || new Uint8Array(); },
        async sleep(ms) { clock += ms; }, async close() { calls.push('close'); },
      };
    },
  };
  const locks = { async request(name, options, body) { calls.push('lease'); assert.equal(name, 'thy666-bootloader-update'); assert.equal(options.ifAvailable, true); return body(lease ? {} : null); } };
  const client = createBrowserUpdaterClient({ transport: usb, secureContext: true, loadCore, baseURL: BASE, locks,
    now: () => clock, onProgress(value) { calls.push({ progress: value.phase }); } });
  const request = { operation_id: 'synthetic-operation-001', device_id: id, package_sha256: '', expected_uid: UID,
    backup_disposition: 'downloaded', confirmed: true, preparation_acknowledged: true, physical_target_confirmed: true };
  return { client, calls, request, queue: () => queue, resume: () => resume?.(),
    async prepare() { await client.connect(); info = await client.inspectPackage(firmware(), { name: 'THY666-instrument-alpha-12345678.333' }); request.package_sha256 = info.file_sha256; await client.listDevices(); return info; } };
}

test('the actual Rust WASM loads only after integrity/ABI checks and never grants USB itself', async () => {
  const core = await loadCore({});
  const info = core.request({ cmd: 'inspect', name: 'THY666-instrument-alpha-12345678.333' }, firmware(), 0);
  assert.equal(info.ok, true); assert.equal(info.package.build_id, BUILD); assert.equal(info.package.build_identity_verified, true);
  const invalid = core.request({ cmd: 'inspect', expected_build: 'ffffffffffffffff' }, firmware(), 0);
  assert.equal(invalid.ok, false); assert.equal(core.request({ cmd: 'begin', expected_uid: UID }, new Uint8Array(), 0).ok, false);
  await assert.rejects(loadBrowserUpdaterCore({ baseURL: BASE, wasmURL: BASE+'core.wasm', coreManifestURL: BASE+'core.json', subtle: webcrypto.subtle,
    fetch: async url => new Response(String(url).endsWith('.json') ? JSON.stringify({ ...manifest, artifact: { ...manifest.artifact, sha256: '0'.repeat(64) } }) : wasm) }), /differs from its build manifest/);
  await assert.rejects(loadBrowserUpdaterCore({ baseURL: BASE, wasmURL: 'https://other.example/core.wasm' }), /same secure site/);
});

test('connecting the browser updater loads policy only; discovery and transfer stay explicit', async () => {
  const f = fixture(); assert.deepEqual(f.calls, []); assert.equal(f.client.connected, false);
  const result = await f.client.connect(); assert.equal(result.mode, 'browser-direct'); assert.equal(result.simulation, false);
  assert.deepEqual(f.calls, []); assert.equal((await f.client.releases()).configured, false);
  await f.client.disconnect(); assert.equal(f.client.connected, false); assert.deepEqual(f.calls, ['clear']);
});

test('the browser client drives actual shared Rust protocol to CRC completion on a synthetic transport', async () => {
  const f = fixture(); const info = await f.prepare(); assert.equal(info.kind, 'thy666');
  const initial = await f.client.install(f.request); assert.equal(initial.state, 'running');
  const complete = await waitDone(f.client, f.request.operation_id);
  assert.equal(complete.state, 'awaiting-verification'); assert.equal(complete.target_uid, UID); assert.equal(complete.expected_build, BUILD);
  assert.equal(complete.completed, 512); assert.equal(complete.indeterminate, false); assert.equal(f.queue().length, 0);
  assert.equal(f.calls.at(-2), 'close');
  const writes = f.calls.filter(value => value instanceof Uint8Array), command = value => new TextDecoder().decode(value.subarray(1,9));
  assert.equal(command(writes[0]), 'XCFIFCFF'); assert.ok(writes.some(value => command(value) === 'XCSMDIUS'));
  assert.equal(writes.filter(value => command(value) === 'ZCSMCCOZ').length, 5);
});

test('bad checksums and mismatched backup UID prevent activation and never replay data', async () => {
  for (const replyOptions of [{ wrongCRC: true }, { wrongUID: true }, { status: 0 }]) {
    const f = fixture({ replyOptions }); await f.prepare(); await f.client.install(f.request);
    const result = await waitDone(f.client, f.request.operation_id); assert.equal(result.state, 'failed'); assert.equal(result.indeterminate, true);
    const writes = f.calls.filter(value => value instanceof Uint8Array);
    assert.equal(writes.some(value => new TextDecoder().decode(value.subarray(1,9)) === 'ZCSMCCOZ'), false);
    const count = writes.length; const repeat = await f.client.install(f.request); assert.equal(repeat.state, 'failed');
    assert.equal(f.calls.filter(value => value instanceof Uint8Array).length, count);
  }
});

test('lost writes remain uncertain and an operation ID cannot be changed or replaced while running', async () => {
  const f = fixture({ holdWrite: true, writeError: true }); await f.prepare(); await f.client.install(f.request); await flush();
  assert.equal(f.client.busy, true); assert.equal((await f.client.install({ ...f.request })).state, 'running');
  await assert.rejects(f.client.install({ ...f.request, expected_uid: 'f'.repeat(24) }), /different request/);
  await assert.rejects(f.client.install({ ...f.request, operation_id: 'another-operation-001' }), /already owns/);
  await assert.rejects(f.client.inspectPackage(firmware(), { expected_build: BUILD }), /already owns/);
  await assert.rejects(f.client.disconnect(), /update is active/);
  f.resume(); const result = await waitDone(f.client, f.request.operation_id);
  assert.equal(result.state, 'failed'); assert.equal(result.indeterminate, true);
  assert.equal(f.calls.filter(value => value instanceof Uint8Array).length, 1);
  await assert.rejects(f.client.progress('unknown-operation-001'), /Unknown update operation/);
});

test('missing confirmation, unbound packages and a competing tab cannot start an update write', async () => {
  const f = fixture(); await f.prepare();
  for (const field of ['confirmed', 'preparation_acknowledged', 'physical_target_confirmed'])
    await assert.rejects(f.client.install({ ...f.request, [field]: false }), /confirmation is required/);
  await assert.rejects(f.client.install({ ...f.request, expected_uid: null }), /requires its valid/);
  await f.client.inspectPackage(firmware(), { name: 'renamed.333' });
  await assert.rejects(f.client.install(f.request), /Inspect and bind/); assert.equal(f.calls.includes('open'), false);
  const busy = fixture({ lease: false }); await busy.prepare(); await busy.client.install(busy.request);
  const result = await waitDone(busy.client, busy.request.operation_id); assert.equal(result.state, 'failed'); assert.equal(result.mutation_started, false);
  assert.equal(busy.calls.includes('open'), false);
});

test('a rejected replacement file invalidates previously checked firmware', async () => {
  const f = fixture(); await f.prepare(); const broken = firmware(); broken[0] = 0;
  await assert.rejects(f.client.inspectPackage(broken, { expected_build: BUILD }), /header/);
  await assert.rejects(f.client.install(f.request), /Inspect and bind/); assert.equal(f.calls.includes('open'), false);
});

test('published catalogs stay same-origin, bounded and SHA/build bound through the real Rust parser', async () => {
  const bytes = firmware(), checked = (await loadCore({})).request({ cmd: 'inspect', expected_build: BUILD }, bytes, 0).package;
  const catalog = { format: 'thy666.release-catalog', version: 1, releases: [{ id: 'latest', label: 'Synthetic release', kind: 'thy666', file: 'release.333', build_id: BUILD, sha256: checked.file_sha256 }] };
  let activeCatalog = catalog; const fetched = [];
  const client = createBrowserUpdaterClient({ transport: { available: true, clear() {} }, loadCore, baseURL: BASE, releasesURL: BASE+'releases.json',
    fetch: async url => { fetched.push(String(url)); return new Response(String(url).endsWith('.json') ? JSON.stringify(activeCatalog) : bytes); } });
  await client.connect(); assert.equal((await client.releases()).releases[0].available, true);
  assert.equal((await client.selectRelease('latest')).build_id, BUILD);
  assert.equal(fetched[1], BASE+'release.333');
  activeCatalog = { ...catalog, releases: [{ ...catalog.releases[0], file: '../escape.333' }] };
  const bad = createBrowserUpdaterClient({ transport: { available: true }, loadCore, baseURL: BASE, releasesURL: BASE+'bad.json', fetch: async () => new Response(JSON.stringify(activeCatalog)) });
  await bad.connect(); await assert.rejects(bad.releases(), /entry is invalid/);
});
