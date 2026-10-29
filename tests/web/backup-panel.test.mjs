import test from 'node:test';
import assert from 'node:assert/strict';
import { initializeBackupPanel } from '../../web/backup-panel.js';
import { BACKUP_MAX_BYTES, deviceCRC32 } from '../../web/device-client.js';

const UID = '0123456789abcdef01234567', BUILD = '90d128d90324c978';
function backup(uid = UID) {
  const bytes = new Uint8Array(5697), view = new DataView(bytes.buffer);
  bytes.set(new TextEncoder().encode('THYB')); view.setUint16(4, 1, true); view.setUint16(6, 64, true);
  for (let n = 64; n < bytes.length; n++) bytes[n] = n & 255;
  view.setUint32(8, bytes.length - 64, true); view.setUint32(12, deviceCRC32(bytes.subarray(64)), true);
  view.setUint16(16, 13, true); view.setUint16(18, 0x413, true); view.setUint32(20, 0x10001, true);
  bytes.set(Uint8Array.from(uid.match(/../g), word => parseInt(word, 16)), 24);
  bytes.set(Uint8Array.from(BUILD.match(/../g), word => parseInt(word, 16)), 36);
  view.setUint32(52, 1, true); view.setUint32(60, deviceCRC32(bytes.subarray(0, 60)), true); return bytes;
}

class Element {
  constructor(doc, tag) { this.ownerDocument = doc; this.tagName = tag.toUpperCase(); this.children = [];
    this.dataset = {}; this.attributes = {}; this.listeners = new Map(); this.parentNode = null; this._text = ''; this.value = ''; this.disabled = false; }
  set textContent(value) { this._text = String(value); this.children = []; }
  get textContent() { return this._text + this.children.map(node => node.textContent).join(''); }
  set innerHTML(_) { throw new Error('Backup strings must be rendered as text.'); }
  setAttribute(key, value) { this.attributes[key] = String(value); }
  append(...children) { for (const child of children) { child.remove(); child.parentNode = this; this.children.push(child); } }
  replaceChildren(...children) { for (const child of this.children) child.parentNode = null; this._text = ''; this.children = []; this.append(...children); }
  remove() { if (this.parentNode) this.parentNode.children = this.parentNode.children.filter(node => node !== this); this.parentNode = null; }
  contains(node) { return node === this || this.children.some(child => child.contains(node)); }
  matches(selector) {
    const match = /^\[data-([a-z-]+)(?:="([^"]*)")?\]$/.exec(selector); if (!match) return false;
    const key = match[1].replace(/-([a-z])/g, (_, char) => char.toUpperCase());
    return Object.hasOwn(this.dataset, key) && (match[2] === undefined || this.dataset[key] === match[2]);
  }
  closest(selector) { for (let node = this; node; node = node.parentNode) if (node.matches(selector)) return node; return null; }
  querySelector(selector) { for (const child of this.children) { if (child.matches(selector)) return child; const nested = child.querySelector(selector); if (nested) return nested; } return null; }
  addEventListener(type, handler, capture = false) { if (!this.listeners.has(type)) this.listeners.set(type, []); this.listeners.get(type).push({ handler, capture: Boolean(capture) }); }
  removeEventListener(type, handler, capture = false) { this.listeners.set(type, (this.listeners.get(type) || []).filter(value => value.handler !== handler || value.capture !== Boolean(capture))); }
  focus() { this.ownerDocument.activeElement = this; }
  get isConnected() { return this.ownerDocument.body.contains(this); }
  click() { if (this.tagName === 'A') this.ownerDocument.downloads.push({ name: this.download, href: this.href }); }
}

function fixture({ connected = false, read, restore } = {}) {
  const blobs = new Map(), revoked = [], downloads = [], notifications = [], applied = [];
  const document = { activeElement: null, listeners: new Map(), downloads,
    createElement(tag) { return new Element(this, tag); },
    addEventListener: Element.prototype.addEventListener, removeEventListener: Element.prototype.removeEventListener,
    defaultView: { Blob, URL: { createObjectURL(blob) { const url = `blob:test-${blobs.size}`; blobs.set(url, blob); return url; }, revokeObjectURL(url) { revoked.push(url); } } } };
  document.body = document.createElement('body'); const mount = document.createElement('section'); document.body.append(mount);
  const calls = [], bytes = backup();
  const client = { connected, busy: false, identity: { uid: UID, build: BUILD },
    backupIdentity: { uid: UID, build: BUILD, settings: '13', target: '1043', compatibility: '65537' },
    status: { message: 'Connected' },
    async downloadBackup() { calls.push(['read']); return read ? read() : bytes; },
    async restoreBackup(value) { calls.push(['restore', value.slice()]); return restore ? restore(value) : { verified: true, generation: 20 }; } };
  let engineReads = 0;
  const panel = initializeBackupPanel({ mount, client, getEngine: () => { engineReads++; throw new Error('Physical backups must not touch the preview.'); },
    notify: (...args) => notifications.push(args), onApplied: result => applied.push(result) });
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
  const select = (value = bytes, name = 'saved.thyb', options = {}) => {
    const input = find('backupField', 'file');
    input.files = [{ size: value.length, name, arrayBuffer: async () => value.slice().buffer, ...options }]; input.value = name; fire(input, 'change');
  };
  return { panel, client, document, mount, calls, bytes, find, all, fire, select, downloads, blobs, revoked, notifications, applied, engineReads: () => engineReads };
}
const flush = () => new Promise(resolve => setImmediate(resolve));

test('Backups mounts without connecting or reading, clearly separates preview and gates physical operations', () => {
  const f = fixture();
  assert.deepEqual(f.calls, []); assert.equal(f.engineReads(), 0);
  assert.equal(f.find('backupAction', 'read').disabled, true); assert.equal(f.find('backupAction', 'restore').disabled, true);
  assert.match(f.mount.textContent, /Connect your board to save or restore a backup/);
  assert.match(f.mount.textContent, /Restore requires the same board/);
  assert.match(f.mount.textContent, /not firmware, the bootloader or unsaved edits/);
  assert.equal(f.find('backupReadout', 'notice').hidden, true);
  assert.equal(f.find('backupAction', 'help').dataset.help, 'backup');
  f.client.connected = true; f.panel.refresh(); assert.equal(f.find('backupAction', 'read').disabled, false);
  f.client.busy = true; f.panel.refresh(); assert.equal(f.find('backupAction', 'read').disabled, true);
  assert.equal(f.find('backupField', 'file').disabled, true);
});

test('Read backup delegates once, downloads exact bounded THYB bytes and does not select an implicit restore', async () => {
  const f = fixture({ connected: true }); f.fire(f.find('backupAction', 'read')); await flush();
  assert.deepEqual(f.calls, [['read']]); assert.equal(f.downloads.length, 1);
  assert.equal(f.downloads[0].name, `THY666-${UID}-${BUILD}.thyb`);
  assert.deepEqual(new Uint8Array(await f.blobs.get(f.downloads[0].href).arrayBuffer()), f.bytes);
  assert.deepEqual(f.revoked, [f.downloads[0].href]); assert.equal(f.find('backupAction', 'restore').disabled, true);
  assert.match(f.find('backupReadout', 'notice').textContent, /Backup downloaded/);
  assert.equal(f.engineReads(), 0);
});

test('file selection inspects header, payload and UID without sending anything or rendering a filename as HTML', async () => {
  const f = fixture(); f.select(f.bytes, '<img src=x onerror=alert(1)>.thyb'); await flush();
  assert.deepEqual(f.calls, []); assert.equal(f.find('backupAction', 'restore').disabled, true);
  assert.match(f.find('backupReadout', 'file').textContent, /<img src=x onerror=alert\(1\)>/);
  assert.equal(f.all().some(node => node.tagName === 'IMG'), false);
  assert.match(f.find('backupReadout', 'file').textContent, /settings V13/);
  f.client.connected = true; f.panel.refresh(); assert.equal(f.find('backupAction', 'restore').disabled, false);
  f.client.backupIdentity.uid = 'ff'.repeat(12); f.panel.refresh();
  assert.equal(f.find('backupAction', 'restore').disabled, true); assert.match(f.find('backupReadout', 'file').textContent, /does not belong/);
});

test('invalid and oversized files clear old restore state and reject before reading oversized data', async () => {
  const f = fixture({ connected: true }); f.select(); await flush(); assert.equal(f.panel.refresh().canRestore, true);
  let reads = 0; f.select(f.bytes, 'huge.thyb', { size: BACKUP_MAX_BYTES + 1, arrayBuffer: async () => { reads++; return f.bytes.buffer; } }); await flush();
  assert.equal(reads, 0); assert.equal(f.panel.refresh().canRestore, false); assert.deepEqual(f.calls, []);
  const bad = f.bytes.slice(); bad[100] ^= 1; f.select(bad); await flush();
  assert.equal(f.panel.refresh().canRestore, false); assert.match(f.find('backupReadout', 'notice').textContent, /File rejected.*checksum/);
});

test('Restore requires an explicit modal confirmation; Cancel and Escape send no data and restore focus', async () => {
  const f = fixture({ connected: true }); f.select(); await flush();
  const restore = f.find('backupAction', 'restore'); restore.focus(); f.fire(restore);
  assert.deepEqual(f.calls, []); assert.equal(f.panel.refresh().confirmationOpen, true);
  const dialog = f.all().find(node => node.attributes.role === 'dialog');
  assert.equal(dialog.attributes['aria-modal'], 'true'); assert.match(dialog.textContent, /Stop playback and release/);
  assert.match(dialog.textContent, /whole applied configuration/);
  f.fire(f.find('backupAction', 'cancel')); assert.equal(f.panel.refresh().confirmationOpen, false);
  assert.equal(f.document.activeElement, restore);
  f.fire(restore); const cancel = f.find('backupAction', 'cancel'), confirm = f.find('backupAction', 'confirm');
  assert.equal(f.document.activeElement, cancel);
  f.fire(cancel, 'keydown', { key: 'Tab', shiftKey: true }); assert.equal(f.document.activeElement, confirm);
  f.fire(confirm, 'keydown', { key: 'Escape' }); assert.equal(f.panel.refresh().confirmationOpen, false);
  assert.deepEqual(f.calls, []);
});

test('confirmed restore supplies exact bytes and calls onApplied only after saved readback verification', async () => {
  const f = fixture({ connected: true }); f.select(); await flush();
  f.fire(f.find('backupAction', 'restore')); f.fire(f.find('backupAction', 'confirm')); await flush();
  assert.equal(f.calls.length, 1); assert.equal(f.calls[0][0], 'restore'); assert.deepEqual(f.calls[0][1], f.bytes);
  assert.deepEqual(f.applied, [{ verified: true, generation: 20 }]);
  assert.equal(f.find('backupReadout', 'notice').textContent, 'Restore complete. Settings saved and verified.');
});

test('applied-but-unverified or save-error results preserve the selected file and never claim success', async () => {
  for (const restore of [async () => ({ verified: false }), async () => { throw new Error('Settings were applied but saving failed.'); }]) {
    const f = fixture({ connected: true, restore }); f.select(); await flush();
    f.fire(f.find('backupAction', 'restore')); f.fire(f.find('backupAction', 'confirm')); await flush();
    assert.deepEqual(f.applied, []); assert.equal(f.panel.refresh().selected, 'saved.thyb');
    assert.match(f.find('backupReadout', 'notice').textContent, /Restore not verified/);
    assert.match(f.find('backupReadout', 'notice').textContent, /Keep the backup file/);
    assert.equal(f.notifications.some(([message]) => message.startsWith('Restore complete')), false);
  }
});

test('disconnect during confirmation blocks restoration and an older file read cannot replace a newer selection', async () => {
  const f = fixture({ connected: true }); f.select(); await flush(); f.fire(f.find('backupAction', 'restore'));
  f.client.connected = false; f.panel.refresh(); assert.equal(f.panel.refresh().confirmationOpen, false); assert.deepEqual(f.calls, []);
  let finish;
  f.select(f.bytes, 'older.thyb', { arrayBuffer: () => new Promise(resolve => finish = resolve) });
  f.select(f.bytes, 'newer.thyb'); await flush(); finish(f.bytes.buffer); await flush();
  assert.equal(f.panel.refresh().selected, 'newer.thyb'); assert.deepEqual(f.calls, []);
});

test('destroy removes listeners and ignores an already pending read without downloading or calling callbacks', async () => {
  let finish; const f = fixture({ connected: true, read: () => new Promise(resolve => finish = resolve) });
  f.fire(f.find('backupAction', 'read')); f.panel.destroy(); finish(f.bytes); await flush();
  assert.equal(f.mount.children.length, 0); assert.deepEqual(f.downloads, []); assert.deepEqual(f.applied, []);
  assert.equal(f.document.listeners.get('click').length, 0); assert.equal(f.document.listeners.get('keydown').length, 0);
  assert.equal(f.panel.refresh(), null); f.panel.destroy();
});

test('Backup and Restore choices reveal only their own flow without device access', () => {
  const f=fixture({connected:true});
  const save=f.find('backupAction','choose-save'),restore=f.find('backupAction','choose-restore');
  assert.equal(save.attributes['aria-expanded'],'false');assert.equal(restore.attributes['aria-expanded'],'false');
  f.fire(save);assert.equal(save.attributes['aria-expanded'],'true');assert.equal(restore.attributes['aria-expanded'],'false');
  f.fire(restore);assert.equal(save.attributes['aria-expanded'],'false');assert.equal(restore.attributes['aria-expanded'],'true');
  assert.deepEqual(f.calls,[]);f.panel.destroy();
});


test('choosing another restore file clears the previous completed journey', async () => {
  const f=fixture({connected:true});f.fire(f.find('backupAction','choose-restore'));
  f.select();await flush();f.fire(f.find('backupAction','restore'));
  f.fire(f.find('backupAction','confirm'));await flush();
  const current=()=>f.all().find(n=>n.attributes['aria-current']==='step')?.textContent;
  assert.equal(current(),'3 · Verified');
  f.select(f.bytes,'next.thyb');await flush();assert.equal(current(),'2 · Review & restore');
  f.panel.destroy();
});


test('flow choices stay enabled through background polls but lock during a backup', async () => {
  let finish;
  const f = fixture({ connected: true, read: () => new Promise(resolve => { finish = resolve; }) });
  const save = f.find('backupAction', 'choose-save');
  const restore = f.find('backupAction', 'choose-restore');
  for (const busy of [true, false, true, false]) {
    f.client.busy = busy; f.panel.refresh();
    assert.equal(save.disabled, false);
    assert.equal(restore.disabled, false);
    f.fire(restore);
    assert.equal(restore.attributes['aria-expanded'], 'true');
    f.fire(save);
    assert.equal(save.attributes['aria-expanded'], 'true');
  }
  assert.deepEqual(f.calls, []);
  f.fire(f.find('backupAction', 'read'));
  assert.equal(save.disabled, true);
  assert.equal(restore.disabled, true);
  f.fire(restore);
  assert.equal(save.attributes['aria-expanded'], 'true');
  finish(f.bytes); await flush();
  assert.equal(save.disabled, false);
  assert.equal(restore.disabled, false);
  f.panel.destroy();
});
