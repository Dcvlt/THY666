import test from 'node:test';
import assert from 'node:assert/strict';
import { createUpdaterClient, initializeUpdaterPanel, UPDATE_HELPER_PROTOCOL, UPDATE_PACKAGE_MAX_BYTES } from '../../web/updater-panel.js';
import { consumeUpdaterHandoff } from '../../web/updater-client.js';
import { deviceCRC32 } from '../../web/device-client.js';

const UID = '0123456789abcdef01234567', BUILD = '90d128d90324c978', SHA = 'a'.repeat(64), ID = '00000000-0000-4000-8000-000000000001';
const packageBytes = () => new Uint8Array(78 + 64);
const inspection = () => ({ kind: 'thy666', installable: true, file_bytes: 142, file_sha256: SHA, application_bytes: 64, build_id: BUILD, build_identity_verified: true });
const status = (state = 'running', sequence = 1) => ({ operation_id: ID, state, sequence, firmware_kind: 'thy666', expected_build: BUILD, target_uid: UID, message: `Phase ${sequence}` });
const healthReports = () => ({
  status: `STATUS THYINSTRUMENT/1 build=${BUILD} ready=1 errors=0 voices=0`,
  io: 'IO din_ready=1 usb_ready=1 usb_mounted=1 din_errors=0 transport_fault=0 led_errors=0 led_fault=0 held=0 warmed=96 blocked=0',
  settings: 'SETTINGS version=13 status=saved errors=0',
  display: 'DISPLAY version=2 faulted=0 failed_chunks=0 initial_complete=1 frames=2',
});
function backupBytes() {
  const bytes = new Uint8Array(5697), view = new DataView(bytes.buffer);
  bytes.set(new TextEncoder().encode('THYB')); view.setUint16(4, 1, true); view.setUint16(6, 64, true);
  view.setUint32(8, 5633, true); view.setUint32(12, deviceCRC32(bytes.subarray(64)), true);
  view.setUint16(16, 13, true); view.setUint16(18, 0x413, true); view.setUint32(20, 0x10001, true);
  bytes.set(Uint8Array.from(UID.match(/../g), value => parseInt(value, 16)), 24);
  bytes.set(Uint8Array.from(BUILD.match(/../g), value => parseInt(value, 16)), 36);
  view.setUint32(52, 1, true); view.setUint32(60, deviceCRC32(bytes.subarray(0, 60)), true); return bytes;
}
class Element {
  constructor(doc, tag) { this.ownerDocument = doc; this.tagName = tag.toUpperCase(); this.children = [];
    this.dataset = {}; this.attributes = {}; this.listeners = new Map(); this.parentNode = null; this._text = ''; this.value = ''; this.disabled = false; this.hidden = false; this.className = '';
    this.style = { setProperty(name, value) { this[name] = value; } }; }
  set textContent(value) { this._text = String(value); this.children = []; }
  get textContent() { return this._text + this.children.map(node => node.textContent).join(''); }
  set innerHTML(_) { throw new Error('Updater values must be rendered as text.'); }
  setAttribute(key, value) { this.attributes[key] = String(value); if (key === 'class') this.className = String(value); if (key.startsWith('data-')) this.dataset[key.slice(5).replace(/-([a-z])/g, (_, char) => char.toUpperCase())] = String(value); }
  getAttribute(key) { if (key.startsWith('data-')) return this.dataset[key.slice(5).replace(/-([a-z])/g, (_, char) => char.toUpperCase())] ?? null; return this.attributes[key] ?? null; }
  removeAttribute(key) { delete this.attributes[key]; if (key.startsWith('data-')) delete this.dataset[key.slice(5).replace(/-([a-z])/g, (_, char) => char.toUpperCase())]; }
  append(...children) { for (const child of children) { child.remove(); child.parentNode = this; this.children.push(child); } }
  replaceChildren(...children) { for (const child of this.children) child.parentNode = null; this._text = ''; this.children = []; this.append(...children); }
  prepend(...children) { for (const child of [...children].reverse()) { child.remove(); child.parentNode = this; this.children.unshift(child); } }
  remove() { if (this.parentNode) this.parentNode.children = this.parentNode.children.filter(node => node !== this); this.parentNode = null; }
  contains(node) { return node === this || this.children.some(child => child.contains(node)); }
  matches(selector) {
    return selector.split(',').some(branch => {
      const expression = branch.trim(), tag = /^[a-z]+/i.exec(expression)?.[0];
      if (tag && this.tagName !== tag.toUpperCase()) return false;
      for (const match of expression.matchAll(/\.([\w-]+)/g)) if (!this.className.split(/\s+/).includes(match[1])) return false;
      for (const match of expression.matchAll(/\[([\w-]+)(?:="([^"]*)")?\]/g)) {
        const key = match[1].startsWith('data-') ? match[1].slice(5).replace(/-([a-z])/g, (_, char) => char.toUpperCase()) : null;
        const value = key === null ? this.attributes[match[1]] : this.dataset[key];
        if (value === undefined || match[2] !== undefined && String(value) !== match[2]) return false;
      }
      return Boolean(tag || expression.startsWith('.') || expression.startsWith('['));
    });
  }
  closest(selector) { for (let node = this; node; node = node.parentNode) if (node.matches(selector)) return node; return null; }
  querySelector(selector) { for (const child of this.children) { if (child.matches(selector)) return child; const nested = child.querySelector(selector); if (nested) return nested; } return null; }
  querySelectorAll(selector) { const result = []; for (const child of this.children) { if (child.matches(selector)) result.push(child); result.push(...child.querySelectorAll(selector)); } return result; }
  addEventListener(type, handler, capture = false) { if (!this.listeners.has(type)) this.listeners.set(type, []); this.listeners.get(type).push({ handler, capture: Boolean(capture) }); }
  removeEventListener(type, handler, capture = false) { this.listeners.set(type, (this.listeners.get(type) || []).filter(value => value.handler !== handler || value.capture !== Boolean(capture))); }
  focus() { this.ownerDocument.activeElement = this; }
  get isConnected() { return this.ownerDocument.body.contains(this); }
  click() { if (this.tagName === 'A') this.ownerDocument.downloads.push({ name: this.download, href: this.href }); }
}
function fixture({ connected = false, install, progress, read, restore, afterRestore, connect, inspectResult, statusRead, releaseCatalog, selectRelease, telemetry, handoff,runtime } = {}) {
  const blobs = new Map(), downloads = [], notifications = [], ownership = [], calls = [];
  const timers = new Map(); let timerId = 0;
  const document = { activeElement: null, listeners: new Map(), downloads,
    createElement(tag) { return new Element(this, tag); },
    createElementNS(namespace, tag) { return new Element(this, tag); },
    addEventListener: Element.prototype.addEventListener, removeEventListener: Element.prototype.removeEventListener,
    defaultView: { Blob, URL: { createObjectURL(blob) { const url = `blob:updater-${blobs.size}`; blobs.set(url, blob); return url; }, revokeObjectURL() {} } } };
  document.body = document.createElement('body'); const mount = document.createElement('section'); document.body.append(mount);
  const helper = { connected: false, busy: false,
    async connect(value) { calls.push(['connect', value]); this.connected = true; return connect ? connect(value) : {}; },
    async disconnect() { calls.push(['disconnect']); this.connected = false; },
    async inspectPackage(bytes, options) { calls.push(['inspect', bytes.slice(), options]); return inspectResult ? inspectResult(bytes, options) : inspection(); },
    async listDevices() { calls.push(['devices']); return [{ id: 'local-device-1', label: 'Update board', mode: 'bootloader' }]; },
    async install(value) { calls.push(['install', value]); return install ? install(value) : status(); },
    async progress(...args) { calls.push(['progress', ...args]); return progress ? progress(...args) : status('awaiting-verification', 2); } };
  helper.status = async () => { calls.push(['status']); return statusRead ? statusRead() : { operation: null }; };
  if (releaseCatalog) helper.releases = async () => { calls.push(['releases']); return releaseCatalog; };
  if (selectRelease) helper.selectRelease = async id => { calls.push(['release', id]); return selectRelease(id); };
  const client = { connected, busy: false, available: true, status: { connecting: false }, identity: { uid: UID, build: BUILD },
    backupIdentity: { uid: UID, build: BUILD, settings: '13', target: '1043', compatibility: '65537' },
    async connect() { calls.push(['application-connect']); this.connected = true; return this.identity; },
    async disconnect() { calls.push(['application-disconnect']); this.connected = false; },
    async telemetry(id) { calls.push(['telemetry', id]); return telemetry ? telemetry(id) : healthReports()[id]; },
    async downloadBackup() { calls.push(['backup']); return read ? read() : backupBytes(); },
    async restoreBackup(bytes) { calls.push(['restore',bytes.slice()]);return restore?restore(bytes):{verified:true,payloadCRC:deviceCRC32(bytes.subarray(64)),generation:2}; } };
  let companionVisits = 0;
  const panel = initializeUpdaterPanel({ mount, client, createHelper: () => helper, operationId: () => ID, handoff,runtime,
    notify: (...args) => notifications.push(args), onOwnershipChange: value => ownership.push(value), onCompanion: () => companionVisits++,
    onRestored:async()=>{calls.push(['restore-refresh']);await afterRestore?.();},
    setTimer(callback, delay) { timers.set(++timerId, { callback, delay }); return timerId; }, clearTimer(id) { timers.delete(id); } });
  const all = () => { const values = []; const walk = node => { values.push(node); node.children.forEach(walk); }; walk(document.body); return values; };
  const find = (key, value) => all().find(node => node.dataset[key] === value);
  const fire = (target, type = 'click', extra = {}) => {
    const event = { target, stopped: false, defaultPrevented: false, preventDefault() { this.defaultPrevented = true; }, stopPropagation() { this.stopped = true; }, ...extra };
    if (type === 'click' && target.disabled) return event;
    const path = []; for (let node = target; node; node = node.parentNode) path.push(node); path.push(document);
    for (const capture of [true, false]) for (const node of capture ? [...path].reverse() : path) {
      for (const value of node.listeners?.get(type) || []) if (value.capture === capture) value.handler(event);
      if (event.stopped) return event;
    }
    return event;
  };
  const visible = node => { for (let current = node; current; current = current.parentNode) if (current.hidden) return false; return true; };
  const act = action => { const target = all().find(node => node.dataset.updaterAction === action && visible(node)); assert.ok(target, `Visible ${action} action is available at step ${panel.refresh()?.step}`); return fire(target); };
  const source = id => { const target = find('firmwareSource', id); assert.ok(visible(target)); return fire(target); };
  const select = (name = 'THY666-90d128d9.333', options = {}) => {
    const input = find('updaterField', 'file'), bytes = packageBytes();
    input.files = [{ name, size: bytes.length, arrayBuffer: async () => bytes.slice().buffer, ...options }]; fire(input, 'change');
  };
  const tick = () => { const next = timers.entries().next().value; if (next) { timers.delete(next[0]); next[1].callback(); } };
  return { panel, client, helper, document, mount, calls, find, all, fire, act, source, select, downloads, blobs, notifications, ownership, timers, tick, visible, companionVisits: () => companionVisits };
}
const flush = () => new Promise(resolve => setImmediate(resolve));
async function prepare(f, { skip = true } = {}) {
  if (!f.helper.connected) { f.act('connect'); await flush(); }
  if (f.panel.refresh().step === 0) { f.act('find'); await flush(); f.act('next'); await flush(); }
  if (f.panel.refresh().step === 1) {
    if (skip) f.act('skip-backup');
    else if (f.panel.refresh().backupState === 'done') f.act('next');
    else return;
    await flush();
  }
  f.source('custom'); await flush(); f.select(); await flush(); f.act('inspect'); await flush();
}
async function installWithRetainedBackup(f,{restoreWanted=true}={}) {
  f.act('next');f.act('backup');await flush();
  f.find('updaterField','restore-wanted').checked=restoreWanted;
  if(!f.helper.connected){f.act('connect');await flush();}
  f.act('next');await flush();f.act('prepare');await flush();f.act('find');await flush();
  await prepare(f,{skip:false});f.act('install');f.act('confirm');await flush();
  f.act('progress');await flush();f.act('verify');await flush();
}

test('updater mounts offline with no helper requests, physical writes or implicit connection', () => {
  const f = fixture(); assert.deepEqual(f.calls, []); assert.equal(f.panel.usbBusy, false);
  assert.equal(f.find('updaterAction', 'backup').disabled, true); assert.equal(f.find('updaterAction', 'install').disabled, true);
  assert.equal(f.find('updaterField', 'file').disabled, false);
  assert.equal(f.panel.refresh().step, 0); assert.equal(f.panel.refresh().source, 'thy666');
  assert.equal(f.visible(f.find('updaterAction', 'install')), false);
  assert.match(f.mount.textContent, /Nothing is installed until you choose Apply and confirm/);
  assert.match(f.mount.textContent, /No automatic restore or rollback/);
  assert.equal(f.mount.querySelectorAll('.update-board-pad').length, 96); assert.equal(f.mount.querySelectorAll('.update-board-control').length, 43);
});

test('desktop startup connects only the in-process updater status and never discovers USB or offers an external helper',async()=>{
  const f=fixture({runtime:{kind:'desktop'}});await flush();
  assert.deepEqual(f.calls.map(([name])=>name),['connect']);
  assert.equal(f.panel.refresh().helperConnected,true);assert.equal(f.find('updaterReadout','setup').hidden,true);
  assert.equal(f.notifications.length,0,'The silent native startup does not add a redundant updater notice to Companion.');
  assert.equal(f.calls.some(([name])=>['devices','install','application-connect'].includes(name)),false);
});

test('direct browser startup prepares only its updater core and catalog, without helper UI or device access',async()=>{
  const f=fixture({runtime:{kind:'browser-direct',companionLabel:'Companion guide'}});await flush();
  assert.deepEqual(f.calls.map(([name])=>name),['connect']);
  assert.equal(f.find('updaterReadout','setup').hidden,true);
  assert.equal(f.find('updaterReadout','browser').hidden,false);
  assert.match(f.find('updaterReadout','browser').textContent,/Browser updater ready/);
  assert.equal(f.find('updaterAction','companion').textContent,'Companion guide');
  assert.equal(f.calls.some(([name])=>['devices','install','application-connect'].includes(name)),false);
  assert.equal(f.notifications.length,0);
});

test('retained backup gates green completion until a separately reviewed restoration verifies saved readback',async()=>{
  let finish;const f=fixture({connected:true,restore:()=>new Promise(resolve=>finish=resolve),afterRestore:()=>assert.equal(f.panel.usbBusy,false)});
  await installWithRetainedBackup(f);
  assert.equal(f.panel.refresh().applicationVerification.verified,true);
  assert.deepEqual(f.panel.refresh().preservation,{requested:true,state:'pending',resolved:false,error:null});
  assert.equal(f.mount.querySelector('.update-completion').dataset.state,'finished');
  assert.equal(f.find('updaterAction','companion').hidden,true);
  assert.equal(f.calls.some(([name])=>name==='restore'),false);
  f.act('restore');
  const dialog=f.all().find(node=>node.attributes.role==='dialog');
  assert.match(dialog.textContent,/replaces.*applied settings, calibration overrides, custom library and current song/);
  assert.match(dialog.textContent,/re-export and compare the complete payload/);
  f.act('cancel');assert.equal(f.calls.some(([name])=>name==='restore'),false);
  f.act('restore');f.act('confirm');await flush();
  assert.equal(f.panel.usbBusy,true);assert.equal(f.panel.refresh().preservation.state,'running');
  assert.equal(f.find('updaterAction','restore').disabled,true);
  assert.equal(f.find('updaterAction','keep-settings').disabled,true);
  assert.equal(f.panel.refresh().confirmationOpen,false);
  assert.deepEqual(f.calls.find(([name])=>name==='restore')[1],backupBytes());
  finish({verified:true,payloadCRC:deviceCRC32(backupBytes().subarray(64)),generation:2});await flush();
  assert.equal(f.panel.usbBusy,false);assert.equal(f.panel.refresh().preservation.state,'verified');
  assert.equal(f.mount.querySelector('.update-completion').dataset.state,'verified');
  assert.match(f.find('updaterReadout','restoration-status').textContent,/saved payload matches/);
  f.act('companion');assert.equal(f.companionVisits(),1);
  assert.equal(f.calls.filter(([name])=>name==='restore').length,1);
  assert.equal(f.calls.filter(([name])=>name==='install').length,1);
  assert.equal(f.calls.filter(([name])=>name==='restore-refresh').length,1);
});

test('restoration failure, an unverified result and mismatched saved CRC never become full success or trigger retries',async()=>{
  for(const restore of [async()=>{throw new Error('Saved settings re-export differs from the restored payload.');},
    async()=>({verified:false,payloadCRC:deviceCRC32(backupBytes().subarray(64))}),
    async()=>({verified:true,payloadCRC:0})]){
    const f=fixture({connected:true,restore});await installWithRetainedBackup(f);
    f.act('restore');f.act('confirm');await flush();
    assert.equal(f.panel.refresh().preservation.state,'failed');assert.equal(f.panel.usbBusy,false);
    assert.equal(f.find('updaterAction','companion').hidden,true);
    assert.notEqual(f.mount.querySelector('.update-logo-art').dataset.state,'verified');
    assert.match(f.find('updaterReadout','restoration-status').textContent,/No restore retry was sent/);
    assert.equal(f.calls.filter(([name])=>name==='restore').length,1);
    assert.equal(f.calls.filter(([name])=>name==='install').length,1);
  }
});

test('changing board identity closes a pending restore review and does not write the retained configuration',async()=>{
  const f=fixture({connected:true});await installWithRetainedBackup(f);f.act('restore');
  const confirm=f.find('updaterAction','confirm');
  f.client.identity.uid='ff'.repeat(12);f.panel.refresh();
  assert.equal(f.panel.refresh().confirmationOpen,false);assert.equal(f.find('updaterAction','restore').disabled,true);
  f.fire(confirm);await flush();assert.equal(f.calls.some(([name])=>name==='restore'),false);
  assert.equal(f.find('updaterAction','companion').hidden,true);
});

test('keeping current settings is an explicit cancelable decision that never sends a restore',async()=>{
  const f=fixture({connected:true});await installWithRetainedBackup(f);
  f.act('keep-settings');assert.equal(f.panel.refresh().preservation.state,'pending');f.act('cancel');
  assert.equal(f.panel.refresh().preservation.resolved,false);
  f.act('keep-settings');f.act('confirm');
  assert.deepEqual(f.panel.refresh().preservation,{requested:true,state:'declined',resolved:true,error:null});
  assert.equal(f.find('updaterAction','companion').hidden,false);
  assert.match(f.mount.querySelector('.update-completion').textContent,/backup was not restored/);
  assert.equal(f.calls.some(([name])=>name==='restore'),false);
});

test('an explicitly unchecked restore choice keeps the backup but only verifies firmware',async()=>{
  const f=fixture({connected:true});await installWithRetainedBackup(f,{restoreWanted:false});
  assert.deepEqual(f.panel.refresh().preservation,{requested:false,state:'idle',resolved:true,error:null});
  assert.equal(f.find('updaterReadout','restoration').hidden,true);
  assert.equal(f.find('updaterAction','companion').hidden,false);
  assert.equal(f.calls.some(([name])=>name==='restore'),false);
});

test('desktop backup is marked saved only after the native file save succeeds',async()=>{
  let finish;const f=fixture({connected:true,runtime:{kind:'desktop',saveFile:()=>new Promise(resolve=>finish=resolve)}});await flush();
  f.act('next');f.act('backup');await flush();assert.equal(f.panel.refresh().backupState,'running');assert.equal(f.downloads.length,0);
  finish();await flush();assert.equal(f.panel.refresh().backupState,'done');
});

test('native background suspension stops UI progress polling without releasing the updater or restarting installation',async()=>{
  const f=fixture();await prepare(f);f.act('install');f.act('confirm');await flush();assert.equal(f.panel.usbBusy,true);assert.ok(f.timers.size);
  f.panel.setHostActive(false);assert.equal(f.timers.size,0);assert.equal(f.panel.usbBusy,true);
  f.panel.setHostActive(true);await flush();assert.equal(f.calls.filter(([name])=>name==='install').length,1);
  assert.equal(f.panel.refresh().operation.state,'awaiting-verification');
});
test('file selection only reads local bytes; bad sizes are rejected before allocation and stale reads cannot replace a selection', async () => {
  const f = fixture(); await prepare(f); const before = f.calls.length;
  f.select('<img src=x>.333'); await flush(); assert.equal(f.calls.length, before, 'Selecting a replacement file never contacts the helper.');
  assert.match(f.mount.textContent, /<img src=x>/); assert.equal(f.all().some(node => node.tagName === 'IMG'), false);
  let reads = 0; f.select('huge.333', { size: UPDATE_PACKAGE_MAX_BYTES + 1, arrayBuffer: async () => { reads++; } }); await flush();
  assert.equal(reads, 0); assert.equal(f.panel.refresh().selected, null);
  let finish; f.select('old.333', { arrayBuffer: () => new Promise(resolve => finish = resolve) });
  f.select('new.333'); await flush(); finish(packageBytes().buffer); await flush(); assert.equal(f.panel.refresh().selected, 'new.333');
});
test('THY666 detection, exact configuration backup, update-mode preparation and review preserve the captured board identity', async () => {
  const f = fixture({ connected: true }); f.act('next'); assert.equal(f.panel.refresh().step, 1); f.act('backup'); await flush();
  assert.deepEqual(f.calls, [['backup']]); assert.equal(f.downloads.length, 1);
  assert.deepEqual(new Uint8Array(await f.blobs.get(f.downloads[0].href).arrayBuffer()), backupBytes());
  assert.match(f.downloads[0].name, new RegExp(`${UID}-${BUILD}`));
  assert.equal(f.panel.refresh().backupState, 'done'); f.act('connect'); await flush(); assert.equal(f.find('updaterAction', 'find').disabled, true);
  assert.equal(f.find('updaterField', 'backup-unavailable').disabled, true);
  f.act('next'); await flush(); assert.equal(f.panel.refresh().step, 2); assert.equal(f.panel.refresh().canInstall, false);
  f.act('prepare'); await flush(); assert.equal(f.client.connected, false);
  f.act('find'); await flush(); await prepare(f, { skip: false });
  assert.equal(f.panel.refresh().canInstall, true);
  f.act('install'); f.act('confirm'); await flush();
  const request = f.calls.find(([name]) => name === 'install')[1];
  assert.equal(request.expected_uid, UID); assert.equal(request.backup_disposition, 'downloaded');
  assert.equal(request.confirmed, true); assert.equal(request.preparation_acknowledged, true); assert.equal(request.physical_target_confirmed, true);
});
test('install requires reviewed package, explicit board choice, backup disposition and a separate cancelable modal', async () => {
  const f = fixture(); await prepare(f, { skip: false }); assert.equal(f.panel.refresh().canInstall, false);
  assert.equal(f.panel.refresh().step, 1); assert.equal(f.calls.some(([name]) => name === 'inspect'), false);
  f.act('skip-backup'); await flush(); f.source('custom'); await flush(); f.select(); await flush(); f.act('inspect'); await flush();
  assert.equal(f.panel.refresh().canInstall, true); f.act('install');
  assert.equal(f.calls.some(([name]) => name === 'install'), false); assert.equal(f.panel.refresh().confirmationOpen, true);
  const dialog = f.all().find(node => node.attributes.role === 'dialog'); assert.equal(dialog.attributes['aria-modal'], 'true');
  assert.match(dialog.textContent, /No fresh configuration backup/); assert.match(dialog.textContent, /application flash/);
  assert.match(dialog.textContent, /preparation runs before hardware\/UID verification/); assert.match(dialog.textContent, /No automatic restore/);
  const cancel = f.find('updaterAction', 'cancel'), confirm = f.find('updaterAction', 'confirm');
  f.fire(cancel, 'keydown', { key: 'Tab', shiftKey: true }); assert.equal(f.document.activeElement, confirm);
  f.fire(confirm, 'keydown', { key: 'Escape' }); assert.equal(f.panel.refresh().confirmationOpen, false);
  assert.equal(f.calls.some(([name]) => name === 'install'), false);
});
test('four steps require explicit detection and backup disposition, then stage the configured Latest THY666 source by default', async () => {
  const latest = { id: 'latest-thy666', kind: 'thy666', available: true, sha256: SHA, label: 'Latest THY666' };
  const f = fixture({ releaseCatalog: { configured: true, releases: [latest] }, selectRelease: async id => { assert.equal(id, latest.id); return inspection(); } });
  assert.equal(f.panel.refresh().step, 0); assert.equal(f.panel.refresh().source, 'thy666');
  assert.equal(f.find('updaterAction', 'next').disabled, true);
  f.act('connect'); await flush(); assert.equal(f.panel.refresh().step, 0); assert.equal(f.calls.some(([name]) => name === 'devices'), false);
  f.act('find'); await flush(); assert.equal(f.panel.refresh().step, 0, 'Discovery does not silently advance the wizard.');
  assert.match(f.find('updaterReadout', 'board').textContent, /Installed firmware cannot be identified/);
  f.act('next'); assert.equal(f.panel.refresh().step, 1); assert.equal(f.calls.some(([name]) => name === 'release'), false);
  assert.equal(f.visible(f.find('updaterAction', 'install')), false);
  f.act('skip-backup'); await flush(); assert.equal(f.panel.refresh().step, 2);
  assert.equal(f.panel.refresh().backupState, 'skipped'); assert.equal(f.panel.refresh().source, 'thy666');
  assert.equal(f.panel.refresh().selected, 'Latest THY666'); assert.equal(f.panel.refresh().canInstall, true);
  assert.deepEqual(f.calls.filter(([name]) => name === 'release'), [['release', latest.id]]);
  assert.equal(f.calls.some(([name]) => ['backup','inspect','install'].includes(name)), false);
});
test('Original v5 completion stays neutral, exposes no THY666 verification and never requests application telemetry', async () => {
  const stockSHA = 'b'.repeat(64), stockInfo = { ...inspection(), kind: 'stock', file_sha256: stockSHA, build_id: null, build_identity_verified: false };
  const f = fixture({ releaseCatalog: { configured: true, releases: [
    { id: 'latest-thy666', kind: 'thy666', available: true, sha256: SHA, label: 'Latest THY666' },
    { id: 'original-v5', kind: 'stock', available: true, sha256: stockSHA, label: 'Original v5' }] },
    selectRelease: async id => id === 'original-v5' ? stockInfo : inspection(),
    install: async () => ({ ...status(), firmware_kind: 'stock', expected_build: null }),
    progress: async () => ({ ...status('awaiting-verification', 2), firmware_kind: 'stock', expected_build: null }) });
  f.act('connect'); await flush(); f.act('find'); await flush(); f.act('next'); f.act('skip-backup'); await flush();
  f.source('stock'); await flush(); assert.equal(f.panel.refresh().canInstall, true);
  assert.match(f.find('updaterReadout', 'package').textContent, /Original THY333 v5.*exact known package/);
  f.act('install'); f.act('confirm'); await flush(); f.act('progress'); await flush();
  assert.equal(f.panel.refresh().step, 3); const completion = f.mount.querySelector('.update-completion');
  assert.equal(completion.dataset.state, 'finished'); assert.equal(completion.dataset.identity, 'other');
  assert.match(completion.textContent, /Update finished/); assert.doesNotMatch(completion.textContent, /Update successful/);
  assert.equal(f.find('updaterAction', 'verify').hidden, true); assert.equal(f.find('updaterAction', 'companion').hidden, true);
  f.client.connected = true; f.panel.refresh(); f.fire(f.find('updaterAction', 'verify')); await flush();
  assert.equal(f.calls.some(([name]) => name === 'telemetry' || name === 'application-connect'), false);
  assert.notEqual(f.panel.refresh().applicationVerification?.verified, true);
});
test('a delayed custom file read cannot replace a subsequently selected curated firmware identity', async () => {
  const latest = { id: 'latest-thy666', kind: 'thy666', available: true, sha256: SHA, label: 'Latest THY666' };
  const f = fixture({ releaseCatalog: { configured: true, releases: [latest] }, selectRelease: async () => inspection() });
  f.act('connect'); await flush(); f.act('find'); await flush(); f.act('next'); f.act('skip-backup'); await flush();
  f.source('custom'); await flush(); let finish;
  f.select('old-custom.333', { arrayBuffer: () => new Promise(resolve => finish = resolve) });
  f.source('thy666'); await flush(); assert.equal(f.panel.refresh().selected, 'Latest THY666');
  finish(packageBytes().buffer); await flush();
  assert.equal(f.panel.refresh().source, 'thy666'); assert.equal(f.panel.refresh().selected, 'Latest THY666');
  assert.equal(f.panel.refresh().inspected, true); assert.equal(f.calls.some(([name]) => name === 'install'), false);
});
test('double confirmation starts one correlated write; explicit progress unlocks only on a terminal helper result', async () => {
  const f = fixture(); await prepare(f); f.act('install'); const confirm = f.find('updaterAction', 'confirm');
  f.fire(confirm); f.fire(confirm); await flush();
  assert.equal(f.calls.filter(([name]) => name === 'install').length, 1); assert.deepEqual(f.ownership, [true]);
  assert.equal(f.panel.usbBusy, true); assert.equal(f.find('updaterAction', 'disconnect').disabled, true);
  assert.equal(f.timers.size, 1); assert.equal([...f.timers.values()][0].delay, 500);
  f.act('progress'); await flush(); assert.deepEqual(f.calls.at(-1), ['progress', ID, 1]);
  assert.equal(f.timers.size, 0);
  assert.deepEqual(f.ownership, [true, false]); assert.match(f.panel.refresh().notice, /Transfer completed.*verify the running build/);
  assert.equal(f.find('updaterAction', 'install').disabled, true);
});
test('automatic progress reads are serialized, stop on errors and terminal states, and never repeat an installation', async () => {
  let finish, reads = 0;
  const f = fixture({ progress: async () => { reads++; return new Promise(resolve => finish = resolve); } });
  await prepare(f); f.act('install'); f.act('confirm'); await flush(); f.tick();
  assert.equal(reads, 1); assert.equal(f.timers.size, 0); f.act('progress'); assert.equal(reads, 1);
  finish(status('running', 2)); await flush(); assert.equal(f.timers.size, 1);
  f.tick(); finish(status('awaiting-verification', 3)); await flush(); assert.equal(f.timers.size, 0); assert.equal(f.panel.usbBusy, false);
  assert.equal(f.calls.filter(([name]) => name === 'install').length, 1);
  const failed = fixture({ progress: async () => { throw new Error('Read timed out'); } });
  await prepare(failed); failed.act('install'); failed.act('confirm'); await flush(); failed.tick(); await flush();
  assert.equal(failed.timers.size, 0); assert.equal(failed.panel.usbBusy, true); assert.match(failed.panel.refresh().notice, /Read timed out/);
  const destroyed = fixture(); await prepare(destroyed); destroyed.act('install'); destroyed.act('confirm'); await flush();
  destroyed.panel.destroy(); assert.equal(destroyed.timers.size, 0);
});
test('uncertain write outcome retains ownership and never resends or reports firmware verification', async () => {
  const f = fixture({ install: async () => { throw new Error('Connection dropped'); }, progress: async () => ({ ...status('awaiting-verification', 2), operation_id: 'another-operation' }) });
  await prepare(f); f.act('install'); f.act('confirm'); await flush();
  assert.match(f.panel.refresh().notice, /outcome is unknown.*No automatic retry/); assert.equal(f.panel.usbBusy, true);
  f.act('progress'); await flush(); assert.match(f.panel.refresh().notice, /uncorrelated/); assert.equal(f.panel.usbBusy, true);
  assert.equal(f.calls.filter(([name]) => name === 'install').length, 1);
  assert.equal(f.visible(f.find('updaterAction', 'disconnect')), false);
  assert.equal(f.find('updaterAction', 'connect').disabled, true); assert.equal(f.find('updaterAction', 'disconnect').disabled, true);
  f.fire(f.find('updaterAction', 'disconnect')); f.fire(f.find('updaterAction', 'connect')); await flush();
  assert.equal(f.calls.filter(([name]) => name === 'connect').length, 1);
  assert.equal(f.calls.some(([name]) => name === 'status'), false, 'An uncertain transport failure must not be cleared by an unrelated idle status.');
  f.panel.destroy(); assert.equal(f.calls.some(([name]) => name === 'disconnect'), false);
  assert.equal(f.mount.children.length, 0);
});
test('progress sequences belong to operation IDs; a new helper running operation acquires native ownership despite a lower sequence', async () => {
  const nextId = '00000000-0000-4000-8000-000000000002'; let connections = 0;
  const f = fixture({ connect: async () => ({ operation: ++connections === 1 ? status('awaiting-verification', 20) :
    { ...status('running', 1), operation_id: nextId } }) });
  f.act('connect'); await flush(); assert.equal(f.panel.usbBusy, false);
  // Defensive dispatch still handles reconnects if completed-session controls
  // are exposed by a future presentation; no operation is abandoned here.
  f.fire(f.find('updaterAction', 'disconnect')); await flush(); f.fire(f.find('updaterAction', 'connect')); await flush();
  assert.equal(f.panel.refresh().operation.operation_id, nextId); assert.equal(f.panel.usbBusy, true);
  assert.doesNotMatch(f.panel.refresh().notice, /older update status/);
});
test('an explicit new idle native helper session clears completed simulation context and requires fresh review', async () => {
  let connections = 0;
  const f = fixture({ connect: async () => ++connections === 1 ? { mode: 'simulation', simulation: true,
    operation: { ...status('awaiting-verification', 20), simulation: true } } : { mode: 'native', simulation: false, operation: null } });
  f.act('connect'); await flush(); assert.equal(f.panel.refresh().simulation, true);
  f.fire(f.find('updaterAction', 'disconnect')); await flush(); f.fire(f.find('updaterAction', 'connect')); await flush();
  assert.equal(f.panel.refresh().simulation, false); assert.equal(f.panel.refresh().operation, null);
  assert.equal(f.find('updaterReadout', 'simulation').hidden, true);
  assert.doesNotMatch(f.find('updaterReadout', 'helper').textContent, /SIMULATION/);
  assert.equal(f.panel.refresh().canInstall, false); assert.equal(f.panel.refresh().step, 0);
  await prepare(f); assert.equal(f.panel.refresh().canInstall, true);
  assert.equal(f.calls.some(([name]) => name === 'install'), false);
});
test('an inspected package with an unbound full build cannot start installation', async () => {
  const f = fixture({ inspectResult: async () => ({ ...inspection(), installable: false, build_identity_verified: false }) });
  await prepare(f); assert.equal(f.panel.refresh().canInstall, false); f.act('install');
  assert.equal(f.panel.modalOpen, false); assert.equal(f.panel.usbBusy, false);
  assert.equal(f.calls.some(([name]) => name === 'install'), false);
  assert.match(f.find('updaterReadout', 'package').textContent, /identity needs attention.*not approved/);
});
test('a definite helper rejection unlocks only after an explicit status proves no active operation, then requires fresh inspection and discovery', async () => {
  const rejected = Object.assign(new Error('Package identity changed'), { helperRejected: true });
  const f = fixture({ install: async () => { throw rejected; } });
  await prepare(f); f.act('install'); f.act('confirm'); await flush();
  assert.equal(f.panel.usbBusy, true); assert.match(f.panel.refresh().notice, /helper rejected the installation request/);
  assert.equal(f.calls.some(([name]) => name === 'status'), false); f.act('progress'); await flush();
  assert.deepEqual(f.calls.at(-1), ['status']); assert.equal(f.panel.usbBusy, false);
  assert.equal(f.panel.refresh().operation, null); assert.equal(f.panel.refresh().inspected, false);
  assert.equal(f.panel.refresh().canInstall, false); assert.match(f.panel.refresh().notice, /reports no active update.*No automatic retry/);
  assert.equal(f.calls.filter(([name]) => name === 'install').length, 1);
});
test('a rejected request cannot release ownership when explicit helper status reports another active operation or remains unknown', async () => {
  const rejected = Object.assign(new Error('Another update owns USB'), { helperRejected: true });
  const otherId = '00000000-0000-4000-8000-000000000002';
  const active = fixture({ install: async () => { throw rejected; }, statusRead: async () => ({ operation: { ...status('running', 1), operation_id: otherId } }) });
  await prepare(active); active.act('install'); active.act('confirm'); await flush(); active.act('progress'); await flush();
  assert.equal(active.panel.usbBusy, true); assert.equal(active.panel.refresh().operation.operation_id, otherId);
  const unknown = fixture({ install: async () => { throw rejected; }, statusRead: async () => ({}) });
  await prepare(unknown); unknown.act('install'); unknown.act('confirm'); await flush(); unknown.act('progress'); await flush();
  assert.equal(unknown.panel.usbBusy, true); assert.match(unknown.panel.refresh().notice, /Ownership remains unresolved/);
  assert.equal(unknown.calls.filter(([name]) => name === 'install').length, 1);
});
test('new application connection invalidates a pending install confirmation', async () => {
  const f = fixture(); await prepare(f); f.act('install'); assert.equal(f.panel.refresh().confirmationOpen, true);
  f.client.status = { connecting: true }; f.panel.refresh(); assert.equal(f.panel.refresh().confirmationOpen, false);
  assert.equal(f.find('updaterAction', 'find').disabled, true);
  f.client.status.connecting = false; f.client.connected = true; f.panel.refresh(); assert.equal(f.panel.refresh().confirmationOpen, false);
  assert.equal(f.calls.some(([name]) => name === 'install'), false);
});
test('matching THY666 build and UID stay pending until explicit verification completes every read-only health report', async () => {
  const operation = { ...status('awaiting-verification', 2), expected_build: BUILD, target_uid: UID };
  let finishDisplay;
  const f = fixture({ connected: true, connect: async () => ({ operation }), telemetry: async id => id === 'display' ? new Promise(resolve => finishDisplay = resolve) : healthReports()[id] });
  f.act('connect'); await flush(); assert.deepEqual(f.calls.map(([name]) => name), ['connect']);
  assert.equal(f.panel.refresh().step, 3); assert.equal(f.panel.refresh().applicationVerification, null);
  assert.equal(f.mount.querySelector('.update-completion').dataset.state, 'finished');
  f.act('verify'); await flush(); assert.equal(f.panel.refresh().applicationVerification, null);
  assert.equal(f.mount.querySelector('.update-logo-art').dataset.state, 'finished');
  for (const id of ['identity','status','io','settings']) assert.equal(f.mount.querySelector(`[data-check="${id}"]`).dataset.state, 'pass');
  assert.equal(f.mount.querySelector('[data-check="display"]').dataset.state, 'busy');
  assert.equal(f.find('updaterAction', 'companion').hidden, true);
  assert.deepEqual(f.calls.filter(([name]) => name === 'telemetry').map(([,id]) => id), ['status','io','settings','display']);
  finishDisplay(healthReports().display); await flush();
  assert.equal(f.panel.refresh().applicationVerification.verified, true); assert.equal(f.panel.refresh().applicationVerification.scope, 'basic-installation-health');
  assert.equal(f.mount.querySelector('.update-completion').dataset.state, 'verified'); assert.match(f.mount.querySelector('.update-completion').textContent, /Update successful/);
  assert.equal(f.mount.querySelector('.update-logo-art').dataset.state, 'verified');
  assert.equal(f.mount.querySelectorAll('.update-health-row').every(node => node.dataset.state === 'pass'), true);
  f.act('companion'); assert.equal(f.companionVisits(), 1);
  assert.match(f.find('updaterReadout', 'verification').textContent, /not raw flash readback/);
  assert.equal(f.calls.some(([name]) => /restore|write|install/.test(name)), false);
  f.client.identity.uid = 'ff'.repeat(12); assert.equal(f.panel.refresh().applicationVerification.verified, false);
  assert.equal(f.mount.querySelector('.update-completion').dataset.state, 'finished');
  assert.equal(f.mount.querySelectorAll('.update-health-row').some(node => node.dataset.state === 'pass'), false, 'Rows from a previously verified board must not describe a different connected identity.');
  const unknown = fixture({ connected: true, connect: async () => ({ operation: { ...operation, target_uid: null } }) });
  unknown.act('connect'); await flush(); unknown.act('verify'); await flush();
  assert.equal(unknown.panel.refresh().applicationVerification.verified, false);
  assert.equal(unknown.calls.some(([name]) => name === 'telemetry'), false);
});
test('a different completed helper operation cannot inherit the previous operation health passes', async () => {
  const nextId = '00000000-0000-4000-8000-000000000002'; let connections = 0;
  const f = fixture({ connected: true, connect: async () => ({ operation: ++connections === 1 ? status('awaiting-verification', 20) :
    { ...status('awaiting-verification', 1), operation_id: nextId } }) });
  f.act('connect'); await flush(); f.act('verify'); await flush();
  assert.equal(f.panel.refresh().applicationVerification.verified, true);
  f.act('disconnect'); await flush(); f.act('connect'); await flush();
  assert.equal(f.panel.refresh().operation.operation_id, nextId); assert.equal(f.panel.refresh().applicationVerification, null);
  assert.equal(f.mount.querySelectorAll('.update-health-row').some(node => node.dataset.state === 'pass'), false);
  assert.equal(f.mount.querySelector('.update-logo-art').dataset.state, 'finished');
  assert.equal(f.calls.filter(([name]) => name === 'telemetry').length, 4, 'The new operation must wait for another explicit verification request.');
});
test('verification fails before green success for a mismatched board or failed telemetry and can be explicitly checked again', async () => {
  const operation = status('awaiting-verification', 2);
  const mismatch = fixture({ connected: true, connect: async () => ({ operation }) });
  mismatch.client.identity.build = 'ff'.repeat(8); mismatch.act('connect'); await flush(); mismatch.act('verify'); await flush();
  assert.equal(mismatch.panel.refresh().applicationVerification.verified, false); assert.equal(mismatch.calls.some(([name]) => name === 'telemetry'), false);
  let faulty = true;
  const failed = fixture({ connected: true, connect: async () => ({ operation }), telemetry: async id => id === 'io' && faulty ? healthReports().io.replace('led_fault=0','led_fault=1') : healthReports()[id] });
  failed.act('connect'); await flush(); failed.act('verify'); await flush();
  assert.equal(failed.panel.refresh().applicationVerification.verified, false); assert.equal(failed.mount.querySelector('.update-completion').dataset.state, 'finished');
  assert.equal(failed.mount.querySelector('[data-check="io"]').dataset.state, 'failure');
  assert.equal(failed.calls.some(([name,id]) => name === 'telemetry' && id === 'display'), false);
  faulty = false; failed.act('verify'); await flush(); assert.equal(failed.panel.refresh().applicationVerification.verified, true);
  assert.equal(failed.calls.some(([name]) => name === 'install'), false);
});
test('simulation stays visibly marked, never claims physical USB ownership and cannot verify a separately connected board', async () => {
  const f = fixture({ connect: async () => ({ mode: 'simulation', simulation: true }),
    install: async () => ({ ...status(), simulation: true }),
    progress: async () => ({ ...status('awaiting-verification', 2), simulation: true, expected_build: BUILD, target_uid: UID }) });
  await prepare(f); assert.equal(f.find('updaterReadout', 'simulation').hidden, false);
  assert.match(f.find('updaterReadout', 'helper').textContent, /SIMULATION.*does not open/);
  f.act('install'); const dialog = f.all().find(node => node.attributes.role === 'dialog');
  assert.match(dialog.textContent, /never opens physical USB/); assert.match(dialog.textContent, /No physical board is selected/);
  f.act('confirm'); await flush(); assert.equal(f.panel.usbBusy, false); assert.equal(f.ownership.includes(true), false);
  f.act('progress'); await flush(); assert.match(f.panel.refresh().notice, /SIMULATION completed/);
  f.client.connected = true; const verified = f.panel.refresh().applicationVerification;
  assert.equal(verified.verified, false); assert.equal(verified.state, 'simulation');
  assert.match(f.find('updaterReadout', 'verification').textContent, /does not verify a physical board/);
  assert.equal(f.find('updaterAction', 'verify').hidden, true); assert.equal(f.find('updaterAction', 'companion').hidden, true);
  assert.equal(f.calls.some(([name]) => name === 'telemetry'), false); assert.notEqual(f.mount.querySelector('.update-completion').dataset.state, 'verified');
  f.fire(f.find('updaterAction', 'disconnect')); await flush(); assert.equal(f.find('updaterReadout', 'simulation').hidden, false);
});

test('helper client rejects remote endpoints before sending the session token and only connects explicitly', async () => {
  const calls = [], helper = createUpdaterClient({ fetch: async (...args) => { calls.push(args); return { ok: true, text: async () => JSON.stringify({ protocol: UPDATE_HELPER_PROTOCOL }) }; } });
  assert.equal(helper.connected, false); assert.deepEqual(calls, []);
  for (const endpoint of ['https://127.0.0.1:3331', 'http://localhost:3331', 'http://127.0.0.1.evil.test:3331', 'http://user@127.0.0.1:3331', 'http://127.0.0.1:3331/other'])
    await assert.rejects(helper.connect({ endpoint, token: 'a'.repeat(64) }), /127\.0\.0\.1/);
  assert.deepEqual(calls, []);
  await helper.connect({ endpoint: 'http://127.0.0.1:3331', token: 'a'.repeat(64) }); assert.equal(helper.connected, true);
  assert.equal(calls[0][0], 'http://127.0.0.1:3331/v1/status');
  assert.equal(calls[0][1].headers.Authorization, `Bearer ${'a'.repeat(64)}`);
  assert.equal(calls[0][1].redirect, 'error'); assert.equal(calls[0][1].credentials, 'omit');
});
test('helper client sends the exact binary package, basename and explicit build while preserving the agreed request schema', async () => {
  const calls = [], helper = createUpdaterClient({ fetch: async (url, options) => { calls.push([url, options]);
    const value = { protocol: UPDATE_HELPER_PROTOCOL, package: inspection(), devices: [], operation: status() };
    return { ok: true, text: async () => JSON.stringify(value) }; } });
  await helper.connect({ endpoint: 'http://127.0.0.1:3331', token: 'a'.repeat(64) });
  const bytes = packageBytes(); assert.deepEqual(await helper.inspectPackage(bytes, { name: 'local/private/THY666 test.333', expected_build: BUILD }), inspection());
  assert.equal(calls[1][1].body, bytes); assert.equal(calls[1][1].headers['Content-Type'], 'application/octet-stream');
  assert.equal(calls[1][1].headers['X-THY666-Filename'], 'THY666%20test.333'); assert.equal(calls[1][1].headers['X-THY666-Build'], BUILD);
  await helper.install({ operation_id: ID, package_sha256: SHA, device_id: 'local-device-1', backup_disposition: 'unavailable' });
  assert.equal(JSON.parse(calls[2][1].body).operation_id, ID);
  await helper.progress(ID, 1); assert.deepEqual(JSON.parse(calls[3][1].body), { operation_id: ID, after_sequence: 1 });
  await helper.disconnect(); await assert.rejects(helper.listDevices(), /Connect/);
});
test('helper rejects incompatible protocols and installation transport errors without retrying', async () => {
  let calls = 0;
  const incompatible = createUpdaterClient({ fetch: async () => ({ ok: true, text: async () => '{"protocol":"wrong"}' }) });
  await assert.rejects(incompatible.connect({ endpoint: 'http://127.0.0.1:3331', token: 'a'.repeat(64) }), /unsupported protocol/);
  assert.equal(incompatible.connected, false);
  const helper = createUpdaterClient({ fetch: async () => {
    if (++calls > 1) throw new Error('Transport lost');
    return { ok: true, text: async () => JSON.stringify({ protocol: UPDATE_HELPER_PROTOCOL }) };
  } });
  await helper.connect({ endpoint: 'http://127.0.0.1:3331', token: 'a'.repeat(64) });
  await assert.rejects(helper.install({ operation_id: ID }), /Transport lost/); assert.equal(calls, 2);
});
test('only a parsed matching-protocol HTTP 4xx error is marked as a definite helper rejection', async () => {
  for (const [protocol, httpStatus, definite] of [[UPDATE_HELPER_PROTOCOL, 400, true], [UPDATE_HELPER_PROTOCOL, 500, false], ['wrong', 400, false]]) {
    let calls = 0;
    const helper = createUpdaterClient({ fetch: async () => ++calls === 1 ?
      { ok: true, text: async () => JSON.stringify({ protocol: UPDATE_HELPER_PROTOCOL }) } :
      { ok: false, status: httpStatus, text: async () => JSON.stringify({ protocol, error: 'Rejected before operation' }) } });
    await helper.connect({ endpoint: 'http://127.0.0.1:3331', token: 'a'.repeat(64) });
    await assert.rejects(helper.install({ operation_id: ID }), error => Boolean(error.helperRejected) === definite);
    assert.equal(calls, 2);
  }
});

test('launcher handoff accepts one exact loopback endpoint and lowercase 64-hex token, scrubbing history before any helper request', async t => {
  const secret = 'a'.repeat(64), fragment = `#update?helper=${encodeURIComponent('http://127.0.0.1:3331')}&session=${secret}`;
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'window'), events = [];
  const location = { hash: '#update', pathname: '/index.html', search: '?view=local' };
  const launchWindow = { __thyUpdateLaunchFragment: fragment };
  Object.defineProperty(launchWindow, 'localStorage', { get() { throw new Error('A launch token must not be persisted.'); } });
  Object.defineProperty(launchWindow, 'sessionStorage', { get() { throw new Error('A launch token must not be persisted.'); } });
  Object.defineProperty(globalThis, 'window', { configurable: true, value: launchWindow });
  t.after(() => previous ? Object.defineProperty(globalThis, 'window', previous) : delete globalThis.window);
  const history = { state: { keep: 'local' }, replaceState(state, title, url) { events.push(['scrub', state, url]); location.hash = '#update'; } };
  const launch = consumeUpdaterHandoff({ location, history });
  assert.deepEqual(launch, { endpoint: 'http://127.0.0.1:3331', token: secret });
  assert.equal(Object.hasOwn(launchWindow, '__thyUpdateLaunchFragment'), false);
  assert.deepEqual(events, [['scrub', history.state, '/index.html?view=local#update']]);
  assert.equal(JSON.stringify(events).includes(secret), false);
  const helper = createUpdaterClient({ fetch: async (url, options) => {
    assert.equal(events[0][0], 'scrub'); assert.equal(location.hash, '#update');
    assert.equal(Object.hasOwn(launchWindow, '__thyUpdateLaunchFragment'), false);
    events.push(['post', url]); assert.equal(options.headers.Authorization, `Bearer ${secret}`);
    return { ok: true, text: async () => JSON.stringify({ protocol: UPDATE_HELPER_PROTOCOL, operation: null }) };
  } });
  await helper.connect(launch); assert.deepEqual(events.map(([name]) => name), ['scrub','post']);
  await helper.disconnect(); assert.equal(helper.connected, false);
  await assert.rejects(helper.status(), /Connect/); assert.equal(events.filter(([name]) => name === 'post').length, 1);
  assert.equal(consumeUpdaterHandoff({ location, history }), null);
});
test('launcher rejects duplicate parameters, unexpected fields, remote endpoints, paths and invalid tokens after scrubbing the fragment', () => {
  const token = 'b'.repeat(64), encode = encodeURIComponent;
  const fragments = [
    `helper=${encode('http://127.0.0.1:3331')}&session=${token}&session=${token}`,
    `helper=${encode('http://127.0.0.1:3331')}&helper=${encode('http://127.0.0.1:3332')}&session=${token}`,
    `helper=${encode('http://127.0.0.1:3331')}&session=${token}&unknown=yes`,
    ...['https://127.0.0.1:3331','http://localhost:3331','http://127.0.0.1.evil.test:3331','http://user@127.0.0.1:3331','http://127.0.0.1:3331/other','http://127.0.0.1:3331/?extra=yes','http://127.0.0.1:3331/#extra','http://127.0.0.1'].map(url => `helper=${encode(url)}&session=${token}`),
    ...['b'.repeat(63), 'b'.repeat(65), 'B'.repeat(64), 'x'.repeat(64)].map(secret => `helper=${encode('http://127.0.0.1:3331')}&session=${secret}`),
  ];
  for (const query of fragments) {
    const scrubs = [], location = { hash: `#update?${query}`, pathname: '/', search: '' };
    const result = consumeUpdaterHandoff({ location, fragment: location.hash, history: { replaceState(state, title, url) { scrubs.push(url); } } });
    assert.equal(result, null, query); assert.deepEqual(scrubs, ['/#update']);
  }
});
test('a handoff that cannot scrub history never becomes a helper connection and a validated launch opens no application or USB device', async () => {
  const token = 'c'.repeat(64), location = { hash: `#update?helper=${encodeURIComponent('http://127.0.0.1:3331')}&session=${token}` };
  assert.equal(consumeUpdaterHandoff({ location, fragment: location.hash, history: { replaceState() { throw new Error('History unavailable'); } } }), null);
  assert.equal(consumeUpdaterHandoff({ location, fragment: location.hash, history: {} }), null); assert.equal(location.hash, 'update');
  const f = fixture({ handoff: { endpoint: 'http://127.0.0.1:3331', token } }); await flush();
  assert.deepEqual(f.calls.map(([name]) => name), ['connect']);
  assert.equal(f.panel.refresh().helperConnected, true); assert.equal(f.panel.refresh().step, 0);
  assert.equal(f.find('updaterField', 'token').value, ''); assert.equal(f.panel.usbBusy, false);
});
