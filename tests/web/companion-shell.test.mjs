import test from 'node:test';
import {performancePacket} from './performance-fixture.mjs';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
const guidanceCore=await createCore({bytes:await readFile(new URL('../../web/generated/core.wasm',import.meta.url))});
import assert from 'node:assert/strict';
import { initializeCompanionShell, renderCompanionScreen, createCockpitContextCache } from '../../web/companion-shell.js';
import { STUDIO_MENU, MENU_ROWS } from '../../web/studio-menu.js';
import { deviceCRC32 } from '../../web/device-client.js';
import { initializeAppSettings, APP_THEME_KEY, APP_POLL_KEY, DEFAULT_POLL_INTERVAL_MS } from '../../web/app-settings.js';

const UID = '0123456789abcdef01234567', BUILD = '90d128d90324c978', SCHEMA = 'ab'.repeat(32);
const updateHealthReports = {
  status: `STATUS THYINSTRUMENT/1 build=${BUILD} ready=1 errors=0 voices=0`,
  io: 'IO din_ready=1 usb_ready=1 usb_mounted=1 din_errors=0 transport_fault=0 led_errors=0 led_fault=0 held=0 warmed=96 blocked=0',
  settings: 'SETTINGS version=13 status=saved errors=0',
  display: 'DISPLAY version=2 faulted=0 failed_chunks=0 initial_complete=1 frames=2',
};
const encoder = new TextEncoder(), rowIndex = id => MENU_ROWS.findIndex(row => row.id === id);
function backup() {
  const bytes = new Uint8Array(5697), view = new DataView(bytes.buffer);
  bytes.set(encoder.encode('THYB')); view.setUint16(4, 1, true); view.setUint16(6, 64, true);
  view.setUint32(8, 5633, true); view.setUint32(12, deviceCRC32(bytes.subarray(64)), true);
  view.setUint16(16, 13, true); view.setUint16(18, 0x413, true); view.setUint32(20, 0x10001, true);
  bytes.set(Uint8Array.from(UID.match(/../g), value => parseInt(value, 16)), 24);
  bytes.set(Uint8Array.from(BUILD.match(/../g), value => parseInt(value, 16)), 36);
  view.setUint32(52, 1, true); view.setUint32(60, deviceCRC32(bytes.subarray(0, 60)), true); return bytes;
}
function snapshot() {
  const bytes = new Uint8Array(1680), view = new DataView(bytes.buffer);
  bytes.set(encoder.encode('THYCMP1')); view.setUint16(8, 1, true); view.setUint16(10, 1680, true);
  view.setUint32(12, 7, true); bytes.set(encoder.encode(BUILD), 32);
  bytes.set(encoder.encode(UID), 48); bytes.set(encoder.encode(SCHEMA), 72);
  bytes.set(encoder.encode('MAJOR C'), 1549); bytes[1632] = 255; bytes[1633] = 255;
  view.setUint32(1624, 120000, true); view.setUint32(1654, 20, true);
  bytes[1598] = 2; bytes[1599] = 1; view.setUint16(1591, rowIndex('performance.pressureMode'), true);
  return bytes;
}

const dataKey = name => name.replace(/-([a-z])/g, (_, value) => value.toUpperCase());
class Node {
  constructor(doc, tag) {
    this.ownerDocument = doc; this.tagName = tag.toUpperCase(); this.children = []; this.parentNode = null;
    this.dataset = {}; this.attributes = {}; this.listeners = new Map(); this.className = ''; this.id = '';
    this._text = ''; this.hidden = false; this.disabled = false; this.value = '';
    this.writes={style:0,attribute:0,canvas:0};
    this.style = { setProperty:(name,value)=>{this.writes.style++;this.style[name]=value;} };
    this.classList = { contains: name => this.className.split(/\s+/).includes(name),
      add: (...names) => { this.className = [...new Set([...this.className.split(/\s+/).filter(Boolean), ...names])].join(' '); },
      remove: (...names) => { this.className = this.className.split(/\s+/).filter(name => !names.includes(name)).join(' '); },
      toggle: (name, force) => { const show = force ?? !this.classList.contains(name); show ? this.classList.add(name) : this.classList.remove(name); return show; } };
  }
  set textContent(value) { this._text = String(value); this.children = []; }
  get textContent() { return this._text + this.children.map(node => node.textContent).join(''); }
  set innerHTML(_) { throw new Error('Untrusted shell data must be text.'); }
  get firstChild() { return this.children[0]; } get childNodes() { return this.children; }
  get offsetHeight() { return 350; } get clientWidth() { return 1280; }
  get isConnected() { return this.ownerDocument.body.contains(this); }
  setAttribute(key, value) { this.writes.attribute++;this.attributes[key] = String(value); if (key === 'id') this.id = String(value);if(key==='class')this.className=String(value);if(key.startsWith('data-'))this.dataset[dataKey(key.slice(5))]=String(value); }
  getAttribute(key){return key.startsWith('data-')?this.dataset[dataKey(key.slice(5))]??null:this.attributes[key]??null;}
  removeAttribute(key){delete this.attributes[key];if(key.startsWith('data-'))delete this.dataset[dataKey(key.slice(5))];}
  append(...nodes) { for (const node of nodes) { if (!node) continue; node.remove(); node.parentNode = this; this.children.push(node); } }
  replaceChildren(...nodes) { for (const node of this.children) node.parentNode = null; this.children = []; this._text = ''; this.append(...nodes); }
  prepend(...nodes){for(const node of [...nodes].reverse()){node.remove();node.parentNode=this;this.children.unshift(node);}}
  insertBefore(node,anchor){if(!anchor){this.append(node);return node;}anchor.before(node);return node;}
  remove() { if (this.parentNode) this.parentNode.children = this.parentNode.children.filter(node => node !== this); this.parentNode = null; }
  replaceWith(...nodes) { this.before(...nodes); this.remove(); }
  before(...nodes) {
    if (!this.parentNode) return; const parent = this.parentNode;
    for (const node of nodes) { node.remove(); node.parentNode = parent; parent.children.splice(parent.children.indexOf(this), 0, node); }
  }
  after(...nodes) {
    if (!this.parentNode) return; const parent = this.parentNode; let at = parent.children.indexOf(this) + 1;
    for (const node of nodes) { node.remove(); node.parentNode = parent; parent.children.splice(at++, 0, node); }
  }
  contains(node) { return node === this || this.children.some(child => child.contains(node)); }
  matches(selector) {
    return selector.split(',').some(branch => {
      const expression = branch.trim();
      if (expression.includes('>')) {
        const pieces = expression.split('>').map(value => value.trim());
        return this.matches(pieces.at(-1)) && Boolean(this.parentNode?.matches(pieces.slice(0, -1).join('>')));
      }
      const parts = expression.split(/\s+/);
      if (parts.length > 1) {
        if (!this.matches(parts.at(-1))) return false;
        for (let parent = this.parentNode; parent; parent = parent.parentNode) if (parent.matches(parts.slice(0, -1).join(' '))) return true;
        return false;
      }
      const tag = /^[a-z]+/i.exec(expression)?.[0]; if (tag && this.tagName !== tag.toUpperCase()) return false;
      const id = /#([\w-]+)/.exec(expression)?.[1]; if (id && this.id !== id) return false;
      for (const match of expression.matchAll(/\.([\w-]+)/g)) if (!this.classList.contains(match[1])) return false;
      for (const match of expression.matchAll(/\[([\w-]+)(?:="([^"]*)")?\]/g)) {
        const value = match[1].startsWith('data-') ? this.dataset[dataKey(match[1].slice(5))] : this.attributes[match[1]];
        if (value === undefined || match[2] !== undefined && String(value) !== match[2]) return false;
      }
      return true;
    });
  }
  closest(selector) { for (let node = this; node; node = node.parentNode) if (node.matches(selector)) return node; return null; }
  querySelectorAll(selector) { const all = []; const walk = node => { for (const child of node.children) { if (child.matches(selector)) all.push(child); walk(child); } }; walk(this); return all; }
  querySelector(selector) { return this.querySelectorAll(selector)[0] || null; }
  addEventListener(type, fn, capture = false) { if (!this.listeners.has(type)) this.listeners.set(type, []); this.listeners.get(type).push({ fn, capture: Boolean(capture) }); }
  removeEventListener(type, fn, capture = false) { this.listeners.set(type, (this.listeners.get(type) || []).filter(item => item.fn !== fn || item.capture !== Boolean(capture))); }
  focus() { this.ownerDocument.activeElement = this; }
  getContext() { return { createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4) }), putImageData:()=>{this.writes.canvas++;} }; }
}

function fixture(t, { storage = new Map(), clock = null, updaterHelper = null, hash = '', runtime=null, projectSite=false,harmonySupport=false,resetSupport=false,padMapSupport=false,performanceSupport=false } = {}) {
  const document = { hidden: false, listeners: new Map(), activeElement: null,
    createElement(tag) { return new Node(this, tag); },
    createElementNS(namespace,tag) { return new Node(this,tag); },
    addEventListener: Node.prototype.addEventListener, removeEventListener: Node.prototype.removeEventListener,
    querySelector(selector) { return this.body.querySelector(selector); }, querySelectorAll(selector) { return this.body.querySelectorAll(selector); } };
  document.documentElement = document.createElement('html');
  document.documentElement.dataset.projectSite=String(projectSite);
  document.body = document.createElement('body');
  const add = (parent, tag, className = '', id = '') => { const node = document.createElement(tag); node.className = className; node.id = id; parent.append(node); return node; };
  const app = add(document.body, 'div', 'app-shell'), header = add(app, 'header', 'masthead');
  const brand = add(header, 'a', 'wordmark');const logo=add(brand,'img','companion-brand-logo');logo.setAttribute('src','./assets/thy666-web-logo.svg');add(brand, 'span', 'brand-sub');
  add(header, 'div', 'header-actions');
  const main = add(app, 'main'), instrument = add(main, 'section', 'instrument'), scroll = add(instrument, 'div', 'controller-scroll');
  const face = add(scroll, 'div', 'controller-face');
  const physical = add(face, 'div', 'physical-screen'), hardware = add(physical, 'div', '', 'hardware-display'); add(hardware, 'canvas', '', 'oled-canvas');
  const pad = add(face, 'button', 'pad'); pad.dataset.pad = '0';
  const rightPad = add(face, 'button', 'pad'); rightPad.dataset.pad = '48';
  const nav = add(face, 'button'); nav.dataset.control = 'nav'; nav.dataset.nav = 'select';
  const func = add(face, 'button'); func.dataset.control = 'function';
  for (const name of ['display-lab-tools', 'controller-routes', 'expression-strip']) add(instrument, 'div', name);
  for (const id of ['engine-studio', 'sequencer-lab', 'library-lab', 'firmware-studio', 'arp-lab', 'mpe-lab', 'rhythm-lab']) add(main, 'section', '', id);
  add(main, 'div', 'diagnostic-grid'); add(main, 'section', 'timeline-panel');
  const window = { listeners: new Map(), addEventListener: Node.prototype.addEventListener, removeEventListener: Node.prototype.removeEventListener };
  const originals = new Map();
  const localStorage = storage instanceof Map ? { getItem: key => storage.get(key) ?? null, setItem: (key, value) => storage.set(key, String(value)) } : storage;
  for (const [key, value] of Object.entries({ document, window, localStorage, location: { hash },
    setInterval: () => 1, clearInterval: () => {},
    setTimeout: clock ? clock.setTimeout : () => 1, clearTimeout: clock ? clock.clearTimeout : () => {} })) {
    originals.set(key, Object.getOwnPropertyDescriptor(globalThis, key)); Object.defineProperty(globalThis, key, { configurable: true, writable: true, value });
  }
  const originalNow=Object.getOwnPropertyDescriptor(Date,'now');
  if(clock)Object.defineProperty(Date,'now',{configurable:true,writable:true,value:clock.now});
  let stateChange = () => {}, bridge, localNotes = 0, localKeys = 0, localControls = 0, previewReleases = 0, keyboardHeld = false;
  let sustainHeld = false; const navigationHeld = new Set();
  const calls = [], applies = [], previewBytes = Uint8Array.of(9, 8, 7);
  const identity = { uid: UID, build: BUILD, schema: SCHEMA };
  const client = { connected: false, compatible: true, busy: false, identity,
    harmonySupported:harmonySupport,harmonyRevision:0,harmonyActive:false,menuLevel:0,resetSupported:resetSupport,resetOpen:false,resetEpoch:0,resetChoice:0,resetApplications:0,
    performanceSupported:performanceSupport,performanceSequence:0,
    padMapSupported:padMapSupport,padMapRevision:17,padMapPendingCount:1,padMapByteReads:0,
    backupIdentity: { ...identity, settings: '13', target: '1043', compatibility: '65537' },
    get status() { return { connected: this.connected, compatible: this.compatible, busy: this.busy, connecting: false, identity: this.connected ? identity : null }; },
    async connect() { calls.push(['connect']); this.connected = true; stateChange(this.status); return identity; },
    async disconnect() { calls.push(['disconnect']); this.connected = false; stateChange(this.status); },
    async downloadBackup() { calls.push(['backup']); return backup(); },
    async telemetry(command) { calls.push(['telemetry', command]); return updateHealthReports[command]; },
    async restoreBackup() { throw new Error('Unexpected restore'); },
    async snapshot() { calls.push(['snapshot']); const bytes=snapshot();const view=new DataView(bytes.buffer);view.setUint32(24,this.instrumentRevision??0,true);view.setUint32(28,this.settingsRevision??0,true);view.setUint32(1650,this.layoutRevision??0,true);view.setUint32(1654,this.savedGeneration??20,true);view.setUint32(1668,this.harmonyRevision,true);
      if(this.performanceSupported)bytes[1604]|=1;bytes[1582]=this.menuLevel;bytes[1588]=Number(Boolean(this.arpQuick));bytes[1662]=this.harmonyActive?6:0;
      bytes[1672]=Number(this.resetOpen);bytes[1673]=this.resetChoice;bytes[1674]=this.resetGroup??0;bytes[1675]=this.resetTrack??255;view.setUint32(1676,this.resetEpoch,true);return { bytes, generation: 7 }; },
    async performanceSnapshot(){calls.push(['performanceSnapshot']);if(this.performanceFailure)throw new Error(this.performanceFailure);return {bytes:performancePacket({voices:3,tracks:2,sequence:++this.performanceSequence}),receivedAt:Date.now()};},
    async harmonySnapshot(){
      calls.push(['harmonySnapshot']);const bytes=new Uint8Array(800),view=new DataView(bytes.buffer);
      bytes.set(encoder.encode('THYHRM1'));view.setUint16(8,4,true);view.setUint16(10,800,true);
      bytes.set(encoder.encode(BUILD),32);bytes.set(encoder.encode(UID),48);bytes.set(encoder.encode(SCHEMA),72);
      view.setUint32(136,this.harmonyRevision,true);bytes[164]=2;bytes[165]=255;bytes[168]=255;bytes[169]=255;bytes[171]=255;
      bytes[156]=Number(this.harmonyActive);
      bytes[523]=127;bytes[571]=255;
      bytes.set(encoder.encode('C Major'),224);bytes.set(encoder.encode('G Major'),256);return {bytes,generation:8};
    },
    async padMapSnapshot(knownRevision=null){
      calls.push(['padMapSnapshot',knownRevision]);
      if(knownRevision===this.padMapRevision)return {unchanged:true,revision:this.padMapRevision};
      if(this.padMapPendingCount>0){this.padMapPendingCount--;return {pending:true,generation:9};}
      const bytes=new Uint8Array(1672),view=new DataView(bytes.buffer);bytes.set(encoder.encode('THYMAP1'));
      view.setUint16(8,1,true);view.setUint16(10,1672,true);view.setUint32(12,9,true);view.setUint32(20,this.padMapRevision,true);
      view.setUint32(24,this.layoutRevision??0,true);view.setUint32(28,this.settingsRevision??0,true);
      bytes.set(encoder.encode(BUILD),32);bytes.set(encoder.encode(UID),48);bytes.set(encoder.encode(SCHEMA),72);
      bytes[136]=2;bytes.set(encoder.encode(this.padMapChord||'Cmaj7'),137);bytes[904]=1;bytes.set(encoder.encode('C#4'),905);
      this.padMapByteReads++;return {pending:false,bytes,generation:9};
    },
    async cancelPadMap(){calls.push(['cancelPadMap']);return true;},
    async harmonyAction(code,value){calls.push(['harmonyAction',code,value]);if(code===0)this.harmonyActive=Boolean(value);this.harmonyRevision++;},
    async harmonyControl(bank,index){calls.push(['harmonyControl',bank,index]);if(this.menuLevel||this.arpQuick)throw new Error('COMPANION/1 ERR harmony-context');this.harmonyRevision++;},
    async confirmReset(epoch,confirmed){calls.push(['confirmReset',epoch,confirmed]);if(!this.resetOpen||this.resetEpoch!==epoch)throw new Error('COMPANION/1 ERR reset-context');
      if(this.resetBlocked&&confirmed)throw new Error('The device could not complete this reset: release controls.');
      this.resetOpen=false;if(confirmed)this.resetApplications++;},
    async inspectPad(n) { calls.push(['inspect', n]); },
    async control(bank, index) { calls.push(['control', bank, index]); },
    async navigate(action) { calls.push(['navigate', action]); },
    async action(row, instance) { calls.push(['action', row, instance]);if(MENU_ROWS[row]?.id.endsWith('.resetOptions')){this.resetOpen=true;this.resetEpoch++;this.resetGroup=STUDIO_MENU.groups.findIndex(group=>group.id===MENU_ROWS[row].group);}},
    async set(row, instance, raw) { if (!this.compatible) throw new Error('read-only'); calls.push(['set', row, instance, raw]); },
    async read(row, instance=255) { calls.push(['read', row, instance]); return this.readOverride?.(row,instance)??{ row, instance: instance===255?1:instance, value: MENU_ROWS[row]?.id==='performance.bendRange'?2:0 }; },
  };
  const counts={
    'arps.clear':2,'arps.enabled':2,'sequencer.channel':21,'sequencer.length':21,
    'midi.pressure':9,'midi.channel':9,'midi.bendSetup':9,
    'views.slot1':9,'views.slot2':9,'sensitivity.amount':98,'sensitivity.reset':98,
    'sensitivity.velocityMode':3,...Object.fromEntries(['velocity','pressureMode','profileEnabled','mpeOutput','pressureBend','bendRange','bendSetup'].map(id=>['performance.'+id,3])),
  };
  const engine = { guidance:guidanceCore.guidance, state: { schemaHash: () => SCHEMA, instanceCount: row => counts[MENU_ROWS[row]?.id]??1 }, settings: { encode: () => previewBytes.slice() } };
  const selected={groupId:'performance',selectedId:'performance.pressureMode'};
  const studio = { model: { groups: STUDIO_MENU.groups, decode: (row, value) => row.type === 'toggle' ? Boolean(value) : row.type === 'select' && !row.dynamic ? row.options[value]?.value : value },
    snapshot: () => ({...selected}), refresh() {}, setDeviceBridge(value) { bridge = value; } };
  // Simulate legacy handlers that must never run from the client surface.
  pad.addEventListener('pointerdown', () => localNotes++); pad.addEventListener('click', () => localNotes++);
  nav.addEventListener('click', () => localControls++); func.addEventListener('click', () => localControls++);
  document.addEventListener('keydown', event => { if (event.key === 'Escape') { localKeys++; keyboardHeld=false; return; } if (!event.target.closest('input,textarea,select') && /^[q ]$/i.test(event.key)){localKeys++;keyboardHeld=true;} });
  document.addEventListener('keyup', event=>{if(/^[q ]$/i.test(event.key))keyboardHeld=false;});
  // These model the legacy instrument's paired sustain/navigation cleanup,
  // independently of the shell's presentation-control keyboard guards.
  document.addEventListener('keydown', event=>{if(event.key===' ')sustainHeld=true;if(event.key==='ArrowUp')navigationHeld.add(event.key);});
  document.addEventListener('keyup', event=>{if(event.key===' ')sustainHeld=false;navigationHeld.delete(event.key);});
  const shell = initializeCompanionShell({ engine, studio, groups: [],runtime,
    createUpdaterHelper: updaterHelper ? () => updaterHelper : undefined,
    onPreviewStart:()=>{previewReleases++;keyboardHeld=false;},
    applyPayload: async payload => applies.push(payload.slice()), createClient(options) { stateChange = options.onState; return client; } });
  let cleaned = false;
  const cleanup = async () => { if (cleaned) return; cleaned = true; shell.destroy(); await flush(); if(clock)Object.defineProperty(Date,'now',originalNow);for (const [key, descriptor] of originals) { if (descriptor) Object.defineProperty(globalThis, key, descriptor); else delete globalThis[key]; } };
  t.after(cleanup);
  const fire = (target, type, extra = {}) => {
    const event = { target, key: '', type, repeat: false, detail: 1, stopped: false, immediate: false,
      preventDefault() { this.defaultPrevented = true; }, stopPropagation() { this.stopped = true; }, stopImmediatePropagation() { this.stopped = true; this.immediate = true; }, ...extra };
    const path = []; for (let node = target; node; node = node.parentNode) path.push(node); path.push(document);
    for (const capture of [true, false]) for (const node of capture ? [...path].reverse() : path) {
      for (const listener of node.listeners?.get(type) || []) {
        if (listener.capture === capture) listener.fn(event); if (event.immediate) return event;
      }
      if (event.stopped) return event;
    }
    return event;
  };
  const byText = text => document.body.querySelectorAll('button').find(node => node.textContent === text);
  return { document, shell, client, engine, studio, calls, applies, previewBytes, pad, rightPad, nav, func, fire, byText, storage, cleanup, bridge: () => bridge,
    refreshConnection:()=>stateChange(client.status),
    selectContext(groupId,selectedId){Object.assign(selected,{groupId,selectedId});},
    notes: () => localNotes, keys: () => localKeys, controls:()=>localControls, previewReleases:()=>previewReleases, keyboardHeld:()=>keyboardHeld,
    sustainHeld:()=>sustainHeld, navigationHeld:()=>new Set(navigationHeld) };
}
const flush = () => new Promise(resolve => setImmediate(resolve));

// Browser timers advance independently of unfinished serial promises. This
// catches overlapping polls that an immediately resolved mock cannot expose.
function manualClock(start=10000){
  let now=start,nextId=0;const jobs=new Map();
  return {
    now:()=>now,
    setTimeout(fn,delay=0){const id=++nextId;jobs.set(id,{fn,at:now+Math.max(0,Number(delay)||0)});return id;},
    clearTimeout(id){jobs.delete(id);},
    pending:()=>jobs.size,
    nextDelay:()=>jobs.size?Math.max(0,Math.min(...[...jobs.values()].map(job=>job.at))-now):null,
    async advance(ms){
      const end=now+ms;let turns=0;
      for(;;){
        const job=[...jobs.entries()].sort((a,b)=>a[1].at-b[1].at)[0];
        if(!job||job[1].at>end)break;
        assert.ok(++turns<1000,'The scheduler must not busy-loop through zero-delay timers.');
        jobs.delete(job[0]);now=job[1].at;job[1].fn();await flush();
      }
      now=end;await flush();
    },
  };
}

function choosePollInterval(f,ms){
  const gear=f.document.querySelector('.app-settings-toggle');f.fire(gear,'click');
  const select=f.document.querySelector('[data-app-setting="poll-interval"]');
  assert.ok(select,'Live refresh is a browser preference in App settings.');
  select.value=String(ms);f.fire(select,'change');f.fire(f.byText('Done'),'click');
}

test('shell starts offline with one visible page and routes Documentation independently from Client', t => {
  const f = fixture(t); assert.deepEqual(f.calls, []);
  assert.equal(f.document.body.querySelectorAll('.companion-page').filter(node => !node.hidden).length, 1);
  assert.equal(f.document.querySelector('#companion-board').hidden, false);
  f.shell.showPage('parameters');
  assert.equal(f.document.querySelector('#companion-board').hidden, false);
  assert.equal(f.document.querySelector('#companion-parameters').hidden, true);
  for(const id of ['parameters','harmony','library','backups','diagnostics']){
    assert.equal(f.document.querySelector(`[data-page="${id}"]`).disabled,true);
    f.shell.showPage(id);assert.equal(f.document.querySelector(`#companion-${id}`).hidden,true);
  }
  const update = f.byText('Update');
  assert.equal(update.disabled, false);assert.equal(f.document.querySelector('[data-page="update"]'),null);
  f.shell.showPage('update'); assert.equal(f.document.querySelector('#companion-update').hidden, false);
  assert.equal(f.document.querySelector('.companion-workspace').hidden,true);assert.equal(location.hash,'update');
  assert.equal(f.document.querySelector('[data-updater-field="file"]').disabled, false);
  f.shell.showArea('documentation');
  assert.equal(f.document.querySelector('.companion-workspace').hidden, true);
  assert.equal(f.document.querySelector('#documentation').hidden, false);
  f.shell.showPage('backups');
  assert.equal(f.document.querySelector('#documentation').hidden, true);
  assert.equal(f.document.body.querySelectorAll('.companion-page').filter(node => !node.hidden).length, 1);
  assert.deepEqual(f.calls, [], 'Changing pages and opening guides must not connect or exchange packets.');
});

test('header uses the original logo alone and groups navigation with connection controls',t=>{
  const f=fixture(t),header=f.document.querySelector('.companion-topbar');
  const brand=header.querySelector('.companion-centered-brand'),actions=header.querySelector('.companion-header-actions');
  assert.ok(brand);assert.equal(brand.textContent,'');assert.equal(brand.querySelectorAll('img').length,1);
  assert.equal(brand.querySelector('img').attributes.src,'./assets/thy666-web-logo.svg');
  assert.equal(header.querySelector('.brand-sub'),null);assert.equal(header.querySelector('.brand-number'),null);
  assert.equal(actions.contains(header.querySelector('.companion-main-nav')),true);
  assert.equal(actions.contains(f.byText('Connect board')),true);
  assert.equal(actions.contains(brand),false);assert.deepEqual(f.calls,[]);
  assert.deepEqual(header.querySelector('.companion-main-nav').children.map(node=>node.textContent),['Update','Guide','Client']);
  assert.equal(f.byText('Client').attributes['aria-pressed'],'true');assert.equal(f.byText('Update').attributes['aria-pressed'],'false');
});

test('offline installation link opens the top-level updater silently and Client returns to Board',async t=>{
  const helperCalls=[],f=fixture(t,{updaterHelper:{connected:false,busy:false,status(){helperCalls.push('status');},async disconnect(){}}});
  const prompt=f.document.querySelector('.companion-onboarding'),link=prompt.querySelector('a');
  assert.equal(prompt.hidden,false);
  assert.equal(prompt.textContent,'Connect a theoryboard with THY666 firmware or install the firmware on your device');
  assert.equal(link.href,'#update');assert.equal(prompt.parentNode.id,'companion-board');
  assert.ok(prompt.parentNode.children.indexOf(prompt)>prompt.parentNode.children.indexOf(f.document.querySelector('.companion-cockpit')));
  const click=f.fire(link,'click');assert.equal(click.defaultPrevented,true);assert.equal(location.hash,'update');
  assert.equal(f.document.querySelector('#companion-update').hidden,false);assert.equal(f.document.querySelector('.companion-workspace').hidden,true);
  assert.equal(f.byText('Update').attributes['aria-pressed'],'true');assert.equal(f.byText('Client').attributes['aria-pressed'],'false');
  f.fire(f.byText('Client'),'click');assert.equal(location.hash,'client/board');
  assert.equal(f.document.querySelector('#companion-update').hidden,true);assert.equal(f.document.querySelector('.companion-workspace').hidden,false);
  assert.deepEqual(f.calls,[]);assert.deepEqual(helperCalls,[]);
  f.fire(f.byText('Connect board'),'click');await flush();assert.equal(prompt.hidden,true);
  f.fire(f.byText('Disconnect'),'click');await flush();assert.equal(prompt.hidden,false);
});

test('updater direct links and legacy routes select Update without losing the last Client page',async t=>{
  let f=fixture(t,{hash:'#update'});assert.equal(f.document.querySelector('#companion-update').hidden,false);
  assert.equal(f.byText('Update').attributes['aria-pressed'],'true');assert.deepEqual(f.calls,[]);await f.cleanup();
  f=fixture(t,{hash:'#client/update'});assert.equal(location.hash,'update');
  assert.equal(f.document.querySelector('#companion-update').hidden,false);assert.deepEqual(f.calls,[]);
  f.fire(f.byText('Client'),'click');f.fire(f.byText('Connect board'),'click');await flush();f.shell.showPage('parameters');
  f.calls.length=0;f.fire(f.byText('Update'),'click');f.fire(f.byText('Client'),'click');
  assert.equal(f.document.querySelector('#companion-parameters').hidden,false);
  assert.equal(f.byText('Client').attributes['aria-pressed'],'true');
  assert.deepEqual(f.calls.filter(([kind])=>!['read','snapshot'].includes(kind)),[]);
});

test('Update remains available offline, owns USB exclusively during native work and checks an explicit application reconnect', async t => {
  const helperCalls = [], id = '00000000-0000-4000-8000-000000000001';
  const updaterHelper = { connected: false, busy: false,
    async connect() { helperCalls.push('connect'); this.connected = true; return { operation: { operation_id: id, sequence: 1, state: 'running' } }; },
    async progress() { helperCalls.push('progress'); return { operation_id: id, sequence: 2, state: 'awaiting-verification', expected_build: BUILD, target_uid: UID }; },
    async disconnect() { helperCalls.push('disconnect'); this.connected = false; } };
  const f = fixture(t, { updaterHelper }); f.shell.showPage('update');
  assert.deepEqual(f.calls, []); assert.deepEqual(helperCalls, []);
  assert.equal(f.document.querySelector('#companion-update').querySelector('[data-help="updates"]').dataset.help, 'updates');
  f.fire(f.byText('Connect helper'), 'click'); await flush();
  assert.deepEqual(helperCalls, ['connect']); assert.equal(f.shell.updater.usbBusy, true);
  const connect = f.byText('Connect board'); assert.equal(connect.disabled, true); assert.match(connect.title, /updater owns USB/);
  f.fire(connect, 'click'); await flush(); assert.deepEqual(f.calls, []);
  f.fire(f.byText('Check update status'), 'click'); await flush(); assert.equal(f.shell.updater.usbBusy, false);
  assert.equal(connect.disabled, false); assert.deepEqual(f.calls, []);
  f.fire(connect, 'click'); await flush();
  assert.equal(f.document.querySelector('#companion-update').hidden, false);
  assert.equal(f.shell.updater.refresh().applicationVerification, null, 'A matching explicit CDC reconnect alone must not imply health verification.');
  f.fire(f.document.querySelector('[data-updater-action="verify"]'), 'click'); await flush();
  const verified = f.shell.updater.refresh().applicationVerification;
  assert.equal(verified.verified, true); assert.equal(verified.scope, 'basic-installation-health');
  assert.deepEqual(f.calls.filter(([name]) => name === 'telemetry').map(([,id]) => id), ['status','io','settings','display']);
  assert.match(f.document.querySelector('[data-updater-readout="verification"]').textContent, /not raw flash readback/);
  assert.equal(f.calls.some(([name]) => /restore|install|write/.test(name)), false);
});

test('direct browser installation warns before unloading only while it owns USB and removes the guard on cleanup', async t => {
  const id='00000000-0000-4000-8000-000000000001';
  const updaterHelper={connected:false,busy:false,
    async connect(){this.connected=true;return{operation:{operation_id:id,sequence:1,state:'running',simulation:false}};},
    async progress(){return{operation_id:id,sequence:2,state:'awaiting-verification',expected_build:BUILD,target_uid:UID};},
    async disconnect(){this.connected=false;}};
  const f=fixture(t,{updaterHelper,runtime:{kind:'browser-direct'}});await flush();
  const window=globalThis.window;
  const unload=()=>{const event={prevented:false,preventDefault(){this.prevented=true;}};
    for(const listener of window.listeners.get('beforeunload')||[])listener.fn(event);return event;};
  assert.equal(f.shell.updater.usbBusy,true);
  assert.equal(unload().prevented,true);
  f.fire(f.byText('Check update status'),'click');await flush();
  assert.equal(f.shell.updater.usbBusy,false);assert.equal(unload().prevented,false);
  await f.cleanup();assert.equal((window.listeners.get('beforeunload')||[]).length,0);
});

test('updater ownership blocks connected Client navigation, controls and parameter writes until terminal status', async t => {
  const id='00000000-0000-4000-8000-000000000001';
  const updaterHelper={connected:false,busy:false,
    async connect(){this.connected=true;return{operation:{operation_id:id,sequence:1,state:'running',simulation:false}};},
    async progress(){return{operation_id:id,sequence:2,state:'awaiting-verification',expected_build:BUILD,target_uid:UID};},
    async disconnect(){this.connected=false;}};
  const f=fixture(t,{updaterHelper});f.fire(f.byText('Connect board'),'click');await flush();
  f.shell.showArea('update');f.fire(f.byText('Connect helper'),'click');await flush();
  assert.equal(f.client.connected,true);assert.equal(f.shell.updater.usbBusy,true);
  assert.equal(f.pad.disabled,true);assert.equal(f.byText('Client').disabled,true);
  const before=f.calls.length;
  f.shell.showPage('parameters');f.fire(f.nav,'click');f.bridge().select('performance.root');
  await assert.rejects(f.bridge().commit('performance.root',0,1),/updater owns USB/);
  await assert.rejects(f.bridge().action('arps.open',0),/updater owns USB/);
  await flush();assert.equal(f.calls.length,before);
  assert.equal(f.document.querySelector('#companion-update').hidden,false);
  f.fire(f.byText('Check update status'),'click');await flush();
  assert.equal(f.shell.updater.usbBusy,false);assert.equal(f.pad.disabled,false);
  assert.equal(f.byText('Client').disabled,false);
});

test('disconnected outline represents the whole board without exposing playable controls',async t=>{
  const f=fixture(t),standby=f.document.querySelector('.companion-board-standby'),instrument=f.document.querySelector('.instrument');
  assert.equal(standby.hidden,false);assert.equal(instrument.hidden,true);
  assert.equal(standby.querySelector('svg').attributes.focusable,'false');
  assert.equal(standby.querySelectorAll('.standby-pad').length,96);
  assert.equal(standby.querySelectorAll('.standby-touch').length,24);
  assert.equal(standby.querySelectorAll('.standby-control').length,19);
  assert.equal(standby.querySelector('button'),null);
  assert.match(standby.querySelector('svg').attributes['aria-label'],/connect a physical board/i);
  assert.equal(f.document.querySelector('#audio-toggle'),null);
  f.fire(f.byText('Connect board'),'click');await flush();
  assert.equal(standby.hidden,true);assert.equal(instrument.hidden,false);
  assert.equal(f.document.querySelector('#audio-toggle'),null);
  f.fire(f.byText('Disconnect'),'click');await flush();
  assert.equal(standby.hidden,false);assert.equal(instrument.hidden,true);
});

test('disconnected board is inert and all physical gestures are blocked without opening a connection', t => {
  const f = fixture(t), face = f.document.querySelector('.controller-face');
  assert.equal(face.inert, true);
  for (const control of [f.pad, f.rightPad, f.nav, f.func]) {
    assert.equal(control.disabled, true);
    for (const type of ['pointerdown', 'pointerup', 'pointercancel', 'click']) {
      const event = f.fire(control, type);
      assert.equal(event.defaultPrevented, true, `${type} must not reach a legacy input handler.`);
    }
  }
  for (const key of ['q', ' ', 'ArrowLeft', 'ArrowUp', 'ArrowRight', 'ArrowDown', 'Enter', 'Escape']) {
    f.fire(f.document.body, 'keydown', { key }); f.fire(f.document.body, 'keyup', { key });
  }
  assert.equal(f.notes(), 0); assert.equal(f.controls(), 0); assert.equal(f.keys(), 0);
  assert.equal(f.keyboardHeld(), false); assert.equal(f.sustainHeld(), false); assert.equal(f.navigationHeld().size, 0);
  assert.deepEqual(f.calls, []);
});

test('custom window control keyboard activation never navigates the connected instrument',async t=>{
  const f=fixture(t);f.fire(f.byText('Connect board'),'click');await flush();
  const chrome=f.document.createElement('div');chrome.className='desktop-window-chrome';const close=f.document.createElement('button');chrome.append(close);f.document.body.append(chrome);
  const before=f.calls.length;
  for(const key of ['Enter',' ','ArrowLeft','Escape']){
    const down=f.fire(close,'keydown',{key}),up=f.fire(close,'keyup',{key});assert.equal(Boolean(down.defaultPrevented),false);assert.equal(Boolean(up.defaultPrevented),false);
  }
  assert.equal(f.calls.length,before);assert.equal(f.notes(),0);
});

test('a failed connection keeps the wireframe inactive and does not unlock board controls',async t=>{
  const f=fixture(t);f.client.connect=async()=>{f.calls.push(['connect']);throw Error('No board selected');};
  f.fire(f.byText('Connect board'),'click');await flush();
  assert.equal(f.client.connected,false);
  assert.equal(f.document.querySelector('.controller-face').inert,true);
  assert.equal(f.document.querySelector('.board-toolbar').hidden,true);
  for(const control of [f.pad,f.nav,f.func]){f.fire(control,'pointerdown');f.fire(control,'click');}
  f.fire(f.document.body,'keydown',{key:'q'});
  assert.equal(f.notes(),0);assert.equal(f.keys(),0);assert.equal(f.controls(),0);
  assert.deepEqual(f.calls,[['connect']]);
});

test('Documentation, Parameters and context help do not play keyboard notes or forward navigation to the board', async t => {
  const f = fixture(t);
  f.fire(f.byText('Connect board'), 'click'); await flush();
  for (const page of ['parameters', 'backups', 'diagnostics']) {
    f.shell.showPage(page); f.fire(f.document.body, 'keydown', { key: 'q' }); f.fire(f.document.body, 'keydown', { key: 'Escape' });
  }
  f.shell.showArea('documentation'); f.fire(f.document.body, 'keydown', { key: 'q' });
  const search = f.document.querySelector('.thy-help-search'); f.fire(search, 'keydown', { key: 'Escape' });
  assert.equal(f.keys(), 0);
  f.shell.showPage('board'); f.shell.help.context('play-chords'); f.fire(f.document.activeElement, 'keydown', { key: 'q' });
  assert.equal(f.keys(), 0); f.shell.help.closeContext();
  await flush(); f.calls.length = 0;
  f.shell.showArea('documentation');
  for (const key of ['ArrowUp', 'ArrowRight', 'Enter', 'q']) f.fire(f.document.body, 'keydown', { key });
  await flush(); assert.deepEqual(f.calls, []);
});

test('connected pad gestures inspect only; physical control clicks use the control namespace', async t => {
  const f = fixture(t); f.fire(f.byText('Connect board'), 'click'); await flush(); f.calls.length = 0;
  for (const type of ['pointerdown', 'pointerup', 'click']) f.fire(f.pad, type); await flush();
  assert.equal(f.notes(), 0); assert.deepEqual(f.calls, [['inspect', 0]]);
  f.calls.length = 0; f.fire(f.nav, 'click'); await flush();
  assert.ok(f.calls.some(call => call[0] === 'control' && call[1] === 0 && call[2] === 0));
  assert.ok(!f.calls.some(call => ['press', 'note-on', 'midi'].includes(call[0])));
});

test('connected bridge edits use explicit row and captured instance and disconnect restores the prior Preview', async t => {
  const f = fixture(t); f.fire(f.byText('Connect board'), 'click'); await flush();
  assert.equal(f.client.connected, true); assert.equal(f.applies.length, 1);
  assert.deepEqual(f.applies[0], backup().subarray(64));
  const bridge = f.bridge(); assert.equal(bridge.connected, true);
  f.calls.length = 0; await bridge.commit('performance.pressureMode', 2, 1);
  assert.deepEqual(f.calls.find(call => call[0] === 'set'), ['set', rowIndex('performance.pressureMode'), 2, 1]);
  f.client.compatible = false;
  await assert.rejects(bridge.commit('performance.pressureMode', 2, 1), /read-only/);
  f.fire(f.byText('Disconnect'), 'click'); await flush();
  assert.equal(f.client.connected, false); assert.deepEqual(f.applies.at(-1), f.previewBytes);
});

test('an unexpected physical disconnect restores the pre-connect Preview rather than leaving the device mirror behind', async t => {
  const f = fixture(t); f.fire(f.byText('Connect board'), 'click'); await flush();
  f.shell.showPage('parameters');assert.equal(f.document.querySelector('#companion-parameters').hidden,false);
  assert.deepEqual(f.applies.at(-1), backup().subarray(64));
  await f.client.disconnect(); await flush();
  assert.equal(f.shell.connected, false);
  assert.deepEqual(f.applies.at(-1), f.previewBytes);
  assert.equal(f.document.querySelector('#companion-board').hidden,false);
  assert.equal(f.document.querySelector('[data-page="parameters"]').disabled,true);
});

test('Harmony requires a connection, including direct links and an unexpected disconnect', async t => {
  const f=fixture(t,{hash:'#client/harmony'}),nav=f.document.querySelector('[data-page="harmony"]');
  assert.equal(nav.disabled,true);
  assert.equal(f.document.querySelector('#companion-harmony').hidden,true);
  assert.equal(f.document.querySelector('#companion-board').hidden,false);
  assert.deepEqual(f.calls,[],'An offline Harmony link never opens a device.');
  f.fire(f.byText('Connect board'),'click');await flush();
  assert.equal(nav.disabled,false);
  f.shell.showPage('harmony');
  assert.equal(f.document.querySelector('#companion-harmony').hidden,false);
  assert.equal(f.calls.some(call=>['set','action'].includes(call[0])),false,'Harmony advice sends no setting or musical actions.');
  await f.client.disconnect();await flush();
  assert.equal(nav.disabled,true);
  assert.equal(f.document.querySelector('#companion-harmony').hidden,true);
  assert.equal(f.document.querySelector('#companion-board').hidden,false);
});

test('live Harmony reads only while visible and changed; actions refresh authoritative state',async t=>{
  const clock=manualClock(),f=fixture(t,{clock,harmonySupport:true});
  f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  assert.equal(f.calls.some(c=>c[0]==='harmonySnapshot'),false);
  f.shell.showPage('harmony');await flush();
  assert.equal(f.calls.filter(c=>c[0]==='harmonySnapshot').length,1);
  await clock.advance(300);assert.equal(f.calls.filter(c=>c[0]==='harmonySnapshot').length,1);
  f.client.harmonyRevision=2;await clock.advance(100);
  assert.equal(f.calls.filter(c=>c[0]==='harmonySnapshot').length,2);
  f.fire(f.byText('Enter Harmony'),'click');await flush();
  assert.ok(f.calls.some(c=>c[0]==='harmonyAction'&&c[1]===0&&c[2]===1));
  assert.equal(f.calls.filter(c=>c[0]==='harmonySnapshot').length,3);
  f.shell.showPage('board');f.client.harmonyRevision=4;await clock.advance(200);
  assert.equal(f.calls.filter(c=>c[0]==='harmonySnapshot').length,4,'Visible Board Harmony uses the same cached extension.');
  assert.equal(f.shell.snapshot.context.harmony.revision,4);
  await clock.advance(200);assert.equal(f.calls.filter(c=>c[0]==='harmonySnapshot').length,4);
  f.shell.showArea('documentation');f.client.harmonyRevision=5;await clock.advance(1000);
  assert.equal(f.calls.filter(c=>c[0]==='harmonySnapshot').length,4,'Hidden Harmony adds no extension traffic.');
  f.shell.showPage('harmony');await flush();assert.equal(f.calls.filter(c=>c[0]==='harmonySnapshot').length,5);
});

test('cluster swap metadata never swaps physical RGB, held pads or inspection a second time',async t=>{
  const f=fixture(t);
  f.client.snapshot=async()=>{
    const bytes=snapshot();bytes[1663]=1;bytes.set([8,0,0],1160);bytes.set([0,0,8],1160+48*3);bytes[1467]=1;bytes[1632]=0;
    return {bytes,generation:7};
  };
  f.fire(f.byText('Connect board'),'click');await flush();
  assert.equal(f.shell.snapshot.clusterSwapped,true);
  assert.equal(f.pad.style['--note-light'],'255 0 0');assert.equal(f.rightPad.style['--note-light'],'0 0 255');
  assert.equal(f.pad.getAttribute('aria-pressed'),'true');assert.equal(f.rightPad.getAttribute('aria-pressed'),'false');
  f.fire(f.pad,'click');await flush();assert.ok(f.calls.some(call=>call[0]==='inspect'&&call[1]===0));
});

test('Harmony virtual shortcuts track physical UI context even without a Harmony revision change',async t=>{
  const clock=manualClock(),f=fixture(t,{clock,harmonySupport:true});f.client.harmonyActive=true;
  f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);f.shell.showPage('harmony');await flush();
  const pane=f.document.querySelector('#companion-harmony'),root=pane.querySelectorAll('button').find(node=>node.textContent==='D');
  assert.equal(root.disabled,false);f.fire(root,'click');await flush();
  assert.ok(f.calls.some(c=>c[0]==='harmonyControl'&&c[1]===2&&c[2]===2));
  assert.equal(f.calls.some(c=>c[0]==='control'),false);
  const reads=f.calls.filter(c=>c[0]==='harmonySnapshot').length;
  f.client.menuLevel=2;await clock.advance(100);assert.equal(root.disabled,true);
  assert.equal(f.calls.filter(c=>c[0]==='harmonySnapshot').length,reads,'Base context updates do not need another analysis transfer.');
  f.client.menuLevel=0;await clock.advance(100);assert.equal(root.disabled,false);
  // The device can change after the last displayed frame. Its scoped command,
  // not an ordinary root/scale fallback, owns that last-moment decision.
  f.client.menuLevel=2;f.fire(root,'click');await flush();
  assert.match(pane.querySelector('[role="alert"]').textContent,/harmony-context/);
  assert.equal(f.calls.some(c=>c[0]==='control'),false);
});

test('Reset opens a C-sourced global Client modal and physical choice never becomes Client consent',async t=>{
  const clock=manualClock(),f=fixture(t,{clock,resetSupport:true});
  f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  f.selectContext('lights','lights.resetOptions');await f.bridge().action('lights.resetOptions',0);await flush();
  const overlay=f.document.querySelector('.companion-reset-overlay'),dialog=f.document.querySelector('.companion-reset-dialog');
  const buttons=dialog.querySelectorAll('button'),cancel=buttons.find(n=>n.textContent==='Cancel'),apply=buttons.find(n=>n.textContent==='Reset options');
  assert.equal(overlay.hidden,false);assert.equal(f.document.activeElement,cancel);assert.ok(dialog.querySelector('svg'));
  assert.equal(f.client.resetApplications,0);const epoch=f.client.resetEpoch;
  f.client.resetChoice=1;await clock.advance(100);
  assert.equal(f.document.activeElement,cancel);assert.equal(f.calls.some(c=>c[0]==='confirmReset'),false);
  f.fire(f.document.body,'keydown',{key:'ArrowRight'});await flush();assert.equal(f.calls.some(c=>c[0]==='navigate'),false);
  f.fire(apply,'click');await flush();
  assert.ok(f.calls.some(c=>c[0]==='confirmReset'&&c[1]===epoch&&c[2]===true));
  assert.equal(f.client.resetApplications,1);assert.equal(overlay.hidden,true);
});

test('stale and blocked reset choices remain visible without retrying or affecting a replacement request',async t=>{
  const clock=manualClock(),f=fixture(t,{clock,resetSupport:true});f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  f.client.resetOpen=true;f.client.resetEpoch=1;await clock.advance(100);
  const dialog=f.document.querySelector('.companion-reset-dialog'),apply=dialog.querySelectorAll('button').find(n=>n.textContent==='Reset options');
  f.client.resetEpoch=2;f.fire(apply,'click');await flush();
  assert.equal(f.client.resetApplications,0);assert.match(dialog.querySelector('[role="alert"]').textContent,/reset-context/);
  await clock.advance(100);f.client.resetBlocked=true;f.fire(apply,'click');await flush();
  assert.match(dialog.querySelector('[role="alert"]').textContent,/release controls/);assert.equal(f.client.resetApplications,0);
  const attempts=f.calls.filter(c=>c[0]==='confirmReset').length;f.client.resetBlocked=false;await clock.advance(300);
  assert.equal(f.calls.filter(c=>c[0]==='confirmReset').length,attempts,'Becoming quiet never supplies a missing confirmation.');
  f.fire(dialog.querySelectorAll('button').find(n=>n.textContent==='Cancel'),'click');await flush();
  assert.equal(f.client.resetApplications,0);assert.equal(f.document.querySelector('.companion-reset-overlay').hidden,true);
});

test('physical reset confirmation appears on Backups without triggering a background configuration export',async t=>{
  const clock=manualClock(),f=fixture(t,{clock,resetSupport:true});f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  f.shell.showPage('backups');const exports=f.calls.filter(c=>c[0]==='backup').length;
  f.client.resetOpen=true;f.client.resetEpoch=3;await clock.advance(1000);
  assert.equal(f.document.querySelector('.companion-reset-overlay').hidden,false);
  assert.equal(f.calls.filter(c=>c[0]==='backup').length,exports);
  await f.client.disconnect();await flush();assert.equal(f.document.querySelector('.companion-reset-overlay').hidden,true);
});

test('app settings open independently offline, change the whole-app theme and never touch the instrument',t=>{
  const f=fixture(t),gear=f.document.querySelector('.app-settings-toggle');
  assert.ok(gear.classList.contains('hud-icon-button'));
  assert.equal(f.document.body.dataset.theme,'red');
  f.fire(gear,'click');const picker=f.document.querySelector('.app-accent-trigger');
  assert.ok(picker);f.fire(picker,'click');f.fire(f.document.querySelector('.app-accent-option[data-accent="cyan"]'),'click');assert.equal(f.document.body.dataset.theme,'cyan');
  f.fire(picker,'keydown',{key:'q'});assert.equal(f.keys(),0);
  f.fire(picker,'keydown',{key:'ArrowDown'});f.fire(f.document.activeElement,'keydown',{key:'End'});f.fire(f.document.activeElement,'keydown',{key:'Enter'});
  assert.equal(f.document.body.dataset.theme,'mono');assert.equal(f.keys(),0);assert.equal(f.controls(),0);assert.deepEqual(f.calls,[]);
  f.fire(picker,'keydown',{key:'ArrowUp'});f.fire(f.document.activeElement,'keydown',{key:'Escape'});
  assert.ok(f.document.querySelector('.app-settings-overlay'),'First Escape dismisses only the accent options.');
  f.fire(picker,'keydown',{key:'Escape'});assert.equal(f.document.querySelector('.app-settings-overlay'),null);
  assert.equal(f.document.activeElement,gear);assert.deepEqual(f.calls,[]);
});

test('theme preferences restore only recognized values and remain usable without browser storage',t=>{
  const f=fixture(t),saved=new Map([[APP_THEME_KEY,'amber']]);
  const storage={getItem:key=>saved.get(key),setItem:(key,value)=>saved.set(key,value)};
  const settings=initializeAppSettings({mount:f.document.body,storage});
  assert.equal(settings.theme,'amber');settings.open();
  const picker=f.document.querySelector('.app-accent-trigger');f.fire(picker,'click');f.fire(f.document.querySelector('.app-accent-option[data-accent="mono"]'),'click');
  assert.equal(saved.get(APP_THEME_KEY),'mono');settings.destroy();
  saved.set(APP_THEME_KEY,'unknown');const fallback=initializeAppSettings({mount:f.document.body,storage});assert.equal(fallback.theme,'red');fallback.destroy();
  const unavailable=initializeAppSettings({mount:f.document.body,storage:{getItem(){throw Error('blocked');},setItem(){throw Error('blocked');}}});
  assert.equal(unavailable.theme,'red');unavailable.open();unavailable.close();unavailable.destroy();assert.deepEqual(f.calls,[]);
});

test('app settings cannot reveal or unlock an offline playable board', async t=>{
  const f=fixture(t),face=f.document.querySelector('.controller-face'),toolbar=f.document.querySelector('.board-toolbar');
  assert.equal(toolbar.hidden,true);assert.equal(face.inert,true);
  f.fire(f.document.querySelector('.app-settings-toggle'),'click');
  assert.equal(f.document.querySelector('[data-app-setting="cockpit-preview"]'),null);
  assert.equal(f.document.body.classList.contains('cockpit-preview'),false);
  f.fire(f.byText('Done'),'click');f.fire(f.byText('Connect board'),'click');await flush();
  assert.equal(face.inert,false);assert.equal(toolbar.hidden,false);
  for(const control of [f.pad,f.rightPad,f.nav,f.func])assert.equal(control.disabled,false);
  f.fire(f.byText('Disconnect'),'click');await flush();
  assert.equal(face.inert,true);assert.equal(toolbar.hidden,true);
  for(const control of [f.pad,f.rightPad,f.nav,f.func])assert.equal(control.disabled,true);
  assert.equal(f.document.querySelector('.cockpit-hud').hidden,true);
});

test('new or re-enabled playground controls stay inert while connected despite local lab refreshes',async t=>{
  const f=fixture(t);f.fire(f.byText('Connect board'),'click');await flush();f.calls.length=0;
  const hosts=[f.document.querySelector('#sequencer-lab').parentNode,
    f.document.querySelector('#library-lab').parentNode,f.document.querySelector('.debug-panels')];
  let mutations=0;
  for(const host of hosts){
    assert.equal(host.inert,true);
    const button=f.document.createElement('button');button.disabled=false;
    for(const type of ['pointerdown','pointerup','click','beforeinput','input','change'])button.addEventListener(type,()=>mutations++);
    host.append(button);
    for(const type of ['pointerdown','pointerup','click','beforeinput','input','change']){
      const event=f.fire(button,type);assert.equal(event.defaultPrevented,true,type);
    }
  }
  assert.equal(mutations,0);assert.deepEqual(f.calls,[]);
  await f.client.disconnect();await flush();for(const host of hosts)assert.equal(host.inert,false);
});

test('opening documentation and app settings never starts local input or issues device cleanup',async t=>{
  const f=fixture(t);
  assert.equal(f.document.querySelector('.help-mode'),null);
  const entries=[
    [()=>f.fire(f.document.querySelector('.app-settings-toggle'),'click'),()=>f.fire(f.byText('Done'),'click')],
    [()=>f.shell.help.context('play-chords'),()=>f.shell.help.closeContext()],
  ];
  for(const [open,close] of entries){
    f.fire(f.document.body,'keydown',{key:'q'});assert.equal(f.keyboardHeld(),false);
    open();f.fire(f.document.activeElement||f.document.body,'keydown',{key:'q'});close();
    assert.equal(f.keys(),0);assert.equal(f.keyboardHeld(),false);
  }
  f.fire(f.byText('Connect board'),'click');await flush();f.calls.length=0;
  for(const [open,close] of entries){open();close();}
  assert.deepEqual(f.calls,[]);
});

test('connected display tools resize the existing center screen in place without sending commands', async t => {
  const f = fixture(t);f.fire(f.byText('Connect board'),'click');await flush();f.calls.length=0;
  const doc = f.document, tools = doc.querySelector('.companion-display-tools');
  const physical = doc.querySelector('.physical-screen'), hardware = doc.querySelector('#hardware-display');
  const oled = doc.querySelector('#oled-canvas'), web = doc.querySelector('.companion-web-screen');
  const cockpit = doc.querySelector('.companion-cockpit'), expand = tools.querySelector('.screen-expand');
  const parents = [physical.parentNode, hardware.parentNode, oled.parentNode, web.parentNode];
  assert.equal(tools.parentNode, physical.parentNode, 'Display tools belong directly above the center display.');
  assert.ok(tools.parentNode.children.indexOf(tools) < tools.parentNode.children.indexOf(physical));
  assert.equal(doc.querySelector('.board-toolbar').contains(tools), false);
  assert.ok(expand.querySelector('svg'));
  assert.ok(expand.classList.contains('hud-icon-button'));
  assert.equal(expand.attributes['aria-label'], 'Expand screen');
  assert.equal(expand.attributes['aria-expanded'], 'false');
  assert.equal(expand.attributes['aria-controls'], physical.id);
  f.fire(expand, 'click');
  assert.equal(cockpit.classList.contains('screen-expanded'), true);
  assert.equal(expand.attributes['aria-expanded'], 'true');
  assert.equal(expand.attributes['aria-label'], 'Collapse screen');
  const mode = label => tools.querySelectorAll('button').find(node => node.textContent === label);
  f.fire(mode('OLED'), 'click'); assert.equal(hardware.hidden, false); assert.equal(web.hidden, true);
  f.fire(mode('Web'), 'click'); assert.equal(hardware.hidden, true); assert.equal(web.hidden, false);
  f.fire(expand, 'click'); assert.equal(cockpit.classList.contains('screen-expanded'), false);
  assert.equal(expand.attributes['aria-expanded'], 'false');
  for (const [selector, node, parent] of [
    ['.physical-screen', physical, parents[0]], ['#hardware-display', hardware, parents[1]],
    ['#oled-canvas', oled, parents[2]], ['.companion-web-screen', web, parents[3]],
  ]) {
    assert.deepEqual(doc.querySelectorAll(selector), [node], selector);
    assert.equal(node.parentNode, parent);
  }
  assert.deepEqual(doc.querySelectorAll('[role="dialog"]'), [doc.querySelector('.compact-nav-panel')]);
  assert.equal(doc.querySelector('.compact-nav-overlay').hidden, true);
  await flush();assert.deepEqual(f.calls, []);
});

test('connected display and sidebar presentation controls never forward navigation or musical commands', async t => {
  const f = fixture(t); f.fire(f.byText('Connect board'), 'click'); await flush(); f.calls.length = 0;
  const tools = f.document.querySelector('.companion-display-tools');
  const buttons = [...tools.querySelectorAll('button'), f.document.querySelector('.rail-toggle')];
  for (const node of buttons) {
    for (const key of ['ArrowLeft', 'ArrowUp', 'ArrowRight', 'ArrowDown', 'Enter', ' ']) {
      f.fire(node, 'keydown', { key }); f.fire(node, 'keyup', { key });
    }
    f.fire(node, 'click');
  }
  await flush(); assert.deepEqual(f.calls, []);
  assert.equal(f.notes(), 0); assert.equal(f.keys(), 0);
});

test('the Board always fits its available width without Fit or one-to-one controls',async t=>{
  const f=fixture(t),scroll=f.document.querySelector('.controller-scroll'),face=f.document.querySelector('.controller-face');
  const scale=f.document.querySelector('.companion-board-scale');
  assert.equal(f.byText('Fit'),undefined);assert.equal(f.byText('1:1'),undefined);
  Object.defineProperty(scroll,'clientWidth',{configurable:true,value:240});
  f.fire(f.document.querySelector('.rail-toggle'),'click');
  assert.equal(face.style.transform,`scale(${236/1280})`);
  assert.equal(scale.style.width,'236px','Even a narrow viewport must not be forced wider by a minimum scale.');
  f.fire(f.byText('Connect board'),'click');await flush();f.calls.length=0;
  f.fire(f.document.querySelector('.screen-expand'),'click');
  assert.equal(face.style.transform,`scale(${236/1536})`);
  assert.equal(scale.style.width,'236px','Expanding the center screen preserves responsive fit.');
  assert.deepEqual(f.calls,[],'Fitting is presentation only.');
});

test('Labels projects device-generated musical names only on demand and caches unchanged maps',async t=>{
  const clock=manualClock(),f=fixture(t,{clock,padMapSupport:true});
  f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  assert.equal(f.calls.some(call=>call[0]==='padMapSnapshot'),false);
  const labels=f.byText('Labels');f.fire(labels,'click');await flush();
  assert.equal(labels.getAttribute('aria-pressed'),'true');assert.equal(labels.getAttribute('aria-busy'),'true');
  assert.equal(f.pad.querySelector('.pad-name').textContent,'','No local-core approximation fills the loading state.');
  f.calls.length=0;await clock.advance(100);
  assert.equal(f.calls[0][0],'padMapSnapshot','A pending label lease finishes before the next visual snapshot.');
  assert.ok(f.calls.findIndex(call=>call[0]==='snapshot')>0);
  assert.equal(f.pad.querySelector('.pad-name').textContent,'Cmaj7');
  assert.equal(f.rightPad.querySelector('.pad-name').textContent,'C#4');
  assert.equal(labels.getAttribute('aria-busy'),'false');assert.equal(f.client.padMapByteReads,1);
  const reads=f.calls.filter(call=>call[0]==='padMapSnapshot').length;
  for(let n=0;n<9;n++){f.client.instrumentRevision=n+1;await clock.advance(100);}
  assert.equal(f.calls.filter(call=>call[0]==='padMapSnapshot').length,reads,'Ordinary playing cannot force full pad-map reads each frame.');
  f.client.instrumentRevision=10;await clock.advance(100);
  assert.equal(f.calls.filter(call=>call[0]==='padMapSnapshot').length,reads+1);
  assert.equal(f.calls.filter(call=>call[0]==='padMapSnapshot').at(-1)[1],17,'Only a cheap known-key query follows playing activity.');
  assert.equal(f.client.padMapByteReads,1);
  f.client.layoutRevision=1;f.client.padMapRevision=18;f.client.padMapChord='Dmin9';await clock.advance(100);
  assert.equal(f.pad.querySelector('.pad-name').textContent,'Dmin9');assert.equal(f.client.padMapByteReads,2);
  f.fire(labels,'click');f.calls.length=0;await clock.advance(1000);
  assert.equal(f.pad.querySelector('.pad-name').textContent,'');assert.equal(labels.getAttribute('aria-pressed'),'false');
  assert.equal(f.calls.some(call=>call[0]==='padMapSnapshot'),false,'Labels OFF adds no map requests.');
  assert.equal(f.calls.some(call=>['set','control','action','navigate'].includes(call[0])),false);
});

test('unsupported labels explain the firmware requirement without displaying invented names',async t=>{
  const f=fixture(t);f.fire(f.byText('Connect board'),'click');await flush();f.calls.length=0;
  f.fire(f.byText('Labels'),'click');await flush();
  assert.equal(f.document.querySelector('.companion-notice-text').textContent,'Labels require newer firmware.');
  assert.equal(f.byText('Labels').getAttribute('aria-pressed'),'false');
  assert.equal(f.pad.querySelector('.pad-name').textContent,'');assert.deepEqual(f.calls,[]);
});

test('hiding Labels cancels pending staging and late completions cannot publish names',async t=>{
  const clock=manualClock(),f=fixture(t,{clock,padMapSupport:true});f.client.padMapPendingCount=10;
  f.fire(f.byText('Connect board'),'click');await flush();f.fire(f.byText('Labels'),'click');await flush();
  f.calls.length=0;f.shell.showArea('documentation');await flush();
  assert.equal(f.calls.filter(call=>call[0]==='cancelPadMap').length,1);
  await clock.advance(1000);assert.equal(f.calls.some(call=>call[0]==='padMapSnapshot'),false);
  f.shell.showPage('board');f.fire(f.byText('Labels'),'click');await flush();
  let finish;f.client.padMapSnapshot=()=>new Promise(resolve=>{finish=()=>resolve({pending:true,generation:9});});
  f.fire(f.byText('Labels'),'click');await flush();assert.equal(typeof finish,'function');
  f.fire(f.byText('Labels'),'click');f.calls.length=0;finish();await flush();
  assert.equal(f.calls.filter(call=>call[0]==='cancelPadMap').length,1);
  assert.equal(f.byText('Labels').getAttribute('aria-pressed'),'false');assert.equal(f.pad.querySelector('.pad-name').textContent,'');
});

test('an unstable label map yields staging so live board snapshots cannot starve',async t=>{
  const clock=manualClock(),f=fixture(t,{clock,padMapSupport:true});f.client.padMapPendingCount=99;
  f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);f.calls.length=0;
  f.fire(f.byText('Labels'),'click');await flush();await clock.advance(500);
  assert.equal(f.calls.filter(call=>call[0]==='padMapSnapshot').length,3,'Initial request and two pending checks are bounded.');
  assert.equal(f.calls.filter(call=>call[0]==='cancelPadMap').length,1);
  assert.ok(f.calls.filter(call=>call[0]==='snapshot').length>=4,'Base feedback resumes while label retry is deferred.');
  assert.equal(f.pad.querySelector('.pad-name').textContent,'');assert.equal(clock.pending(),1);
});

test('disconnected presentation controls cannot start keyboard sustain or navigation', t => {
  const f = fixture(t), tools = f.document.querySelector('.companion-display-tools');
  for (const control of [...tools.querySelectorAll('button'), f.document.querySelector('.rail-toggle')]) {
    f.fire(f.document.body, 'keydown', { key: ' ' });
    f.fire(f.document.body, 'keydown', { key: 'ArrowUp' });
    assert.equal(f.sustainHeld(), false); assert.equal(f.navigationHeld().size, 0);
    control.focus();
    f.fire(control, 'keyup', { key: ' ' }); f.fire(control, 'keyup', { key: 'ArrowUp' });
    f.fire(control, 'keydown', { key: ' ' }); f.fire(control, 'keydown', { key: 'ArrowUp' });
    assert.equal(f.sustainHeld(), false); assert.equal(f.navigationHeld().size, 0);
  }
  assert.deepEqual(f.calls, []);
});

test('connected header tabs retain native keyboard activation without navigating the board', async t => {
  const f=fixture(t);f.fire(f.byText('Connect board'),'click');await flush();f.calls.length=0;
  const chrome=[...f.document.querySelector('.companion-main-nav').querySelectorAll('button'),f.byText('Disconnect')];
  for(const node of chrome)for(const key of ['Enter',' ','ArrowLeft','ArrowRight','ArrowUp','ArrowDown']){
    assert.ok(node);const event=f.fire(node,'keydown',{key});f.fire(node,'keyup',{key});
    assert.equal(Boolean(event.defaultPrevented),false,'Native header activation remains available.');
  }
  await flush();assert.deepEqual(f.calls,[]);
  f.fire(f.byText('Guide'),'click');assert.equal(f.document.querySelector('#documentation').hidden,false);
});

test('compact sidebar retains accessible page names, disabled reasons and browser-only preference', async t => {
  const storage = new Map(), key = 'thy666.companion.sidebar.v1';
  let f = fixture(t, { storage }), workspace = f.document.querySelector('.companion-workspace');
  let toggle = f.document.querySelector('.rail-toggle');
  assert.equal(toggle.classList.contains('hud-rail-toggle'), true, 'Sidebar collapse has a divider tab, separate from a page choice.');
  assert.equal(toggle.attributes['aria-expanded'], 'true');
  assert.equal(toggle.attributes['aria-controls'], f.document.querySelector('.companion-client-nav').id);
  f.fire(toggle, 'click'); assert.equal(toggle.attributes['aria-expanded'], 'false');
  assert.equal(workspace.classList.contains('rail-collapsed'), true); assert.equal(storage.get(key), 'collapsed');
  assert.equal(f.document.querySelector('.companion-client-nav').querySelectorAll('button').length, 6);
  for (const [id, label] of [['board', 'Board'], ['parameters', 'Parameters'], ['harmony', 'Harmony'], ['library', 'Songs & library'], ['backups', 'Backups'], ['diagnostics', 'Diagnostics']]) {
    const nav = f.document.querySelector(`[data-page="${id}"]`);
    assert.equal(nav.attributes['aria-label'], label); assert.equal(nav.querySelector('.rail-text').textContent, label);
    assert.ok(nav.title.startsWith(label)); assert.equal(nav.disabled, id!=='board');
    if (nav.disabled) assert.match(nav.title, /Connect your board/);
  }
  assert.equal(toggle.attributes['aria-label'], 'Expand sidebar'); assert.equal(toggle.title, 'Expand sidebar');
  assert.deepEqual(f.calls, []);
  await f.cleanup();
  f = fixture(t, { storage }); workspace = f.document.querySelector('.companion-workspace'); toggle = f.document.querySelector('.rail-toggle');
  assert.equal(workspace.classList.contains('rail-collapsed'), true, 'A fresh shell restores the browser preference.');
  assert.equal(toggle.attributes['aria-expanded'], 'false');
  f.fire(toggle, 'click'); assert.equal(storage.get(key), 'expanded'); assert.equal(toggle.attributes['aria-expanded'], 'true');
  assert.deepEqual(f.calls, []);
});

test('sidebar defaults safely when storage is invalid or inaccessible', async t => {
  let f = fixture(t, { storage: new Map([['thy666.companion.sidebar.v1', 'unexpected']]) });
  assert.equal(f.document.querySelector('.rail-toggle').attributes['aria-expanded'], 'true');
  await f.cleanup();
  f = fixture(t, { storage: { getItem() { throw Error('blocked'); }, setItem() { throw Error('blocked'); } } });
  const toggle = f.document.querySelector('.rail-toggle');
  assert.equal(toggle.attributes['aria-expanded'], 'true');
  f.fire(toggle, 'click'); assert.equal(toggle.attributes['aria-expanded'], 'false');
  assert.deepEqual(f.calls, []);
});

test('component library navigation cannot abandon a connected or busy Client', async t => {
  const f=fixture(t),docs=f.document.querySelector('#documentation');
  const link=f.document.createElement('a'),label=f.document.createElement('span');
  link.className='thy-help-inline-link';link.href='./design-system.html';link.append(label);docs.append(link);
  assert.equal(Boolean(f.fire(label,'click').defaultPrevented),false,'A disconnected Client can open its bundled reference.');
  f.client.busy=true;assert.equal(f.fire(label,'click').defaultPrevented,true);f.client.busy=false;
  f.fire(f.byText('Connect board'),'click');await flush();f.calls.length=0;
  assert.equal(f.fire(label,'click').defaultPrevented,true);
  assert.match(f.document.querySelector('.companion-notice').textContent,/Disconnect your board/);
  assert.deepEqual(f.calls,[],'Opening documentation must not drop or change a live device.');
});

test('Board heading shows firmware identity without redundant status or pad-help panels', async t => {
  const f = fixture(t), eyebrow = f.document.querySelector('#companion-board .eyebrow');
  assert.equal(f.document.querySelector('.client-identity'),null);
  assert.equal(f.document.querySelector('.companion-pad-summary'),null);
  assert.equal(f.document.querySelector('.companion-board-footer'),null);
  assert.equal(f.document.querySelector('.help-mode'),null);
  assert.equal(eyebrow.textContent, 'THY666');
  f.fire(f.byText('Connect board'), 'click'); await flush();
  assert.equal(eyebrow.textContent, `THY666 / ${BUILD.slice(0, 8)}`);
  assert.equal(f.document.querySelector('#oled-canvas').attributes['aria-label'], 'Live instrument OLED: 128 by 64 pixels');
  assert.doesNotMatch(f.document.querySelector('#companion-board').textContent,/source of truth|pad help|board connected/i);
  f.fire(f.byText('Disconnect'), 'click'); await flush();
  assert.equal(eyebrow.textContent, 'THY666');
});

test('Diagnostics report remains clickable during background polling and locks only its own pending read', async t => {
  const f = fixture(t); f.fire(f.byText('Connect board'), 'click'); await flush(); f.shell.showPage('diagnostics');
  const read = f.byText('Read report'), pick = f.document.querySelector('.telemetry-controls select');
  const output = f.document.querySelector('.telemetry-output'); f.calls.length = 0;
  f.client.busy = true; f.refreshConnection();
  assert.equal(read.disabled, false); assert.equal(pick.disabled, false);
  let resolveReport, rejectReport;
  f.client.telemetry = command => {
    f.calls.push(['telemetry', command]);
    return new Promise((resolve, reject) => { resolveReport = resolve; rejectReport = reject; });
  };
  pick.value = 'settings'; f.fire(read, 'pointerdown');
  f.client.busy = false; f.refreshConnection(); f.client.busy = true; f.refreshConnection();
  assert.equal(read.disabled, false, 'A background busy transition must not drop the pointer mouse-up.');
  f.fire(read, 'pointerup'); f.fire(read, 'click'); f.fire(read, 'click');
  assert.deepEqual(f.calls, [['telemetry', 'settings']], 'Repeated activation queues only one explicit report.');
  assert.equal(read.disabled, true); assert.equal(pick.disabled, true); assert.equal(output.textContent, 'Reading…');
  f.refreshConnection(); assert.equal(read.disabled, true, 'Connection updates cannot unlock the pending read.');
  resolveReport('Settings V13\nSaved generation 23'); await flush();
  assert.equal(output.textContent, 'Settings V13\nSaved generation 23');
  assert.equal(read.disabled, false); assert.equal(pick.disabled, false); assert.equal(f.client.busy, true);
  pick.value = 'display'; f.fire(read, 'click');
  assert.deepEqual(f.calls.at(-1), ['telemetry', 'display']);
  rejectReport(Error('Report transport failed')); await flush();
  assert.equal(output.textContent, 'Read failed. Try again.');
  assert.equal(f.document.querySelector('.companion-notice-text').textContent, 'Report transport failed');
  assert.equal(f.document.querySelector('.companion-notice-icon').getAttribute('aria-hidden'),'true');
  assert.ok(f.document.querySelector('.companion-notice').classList.contains('hud-notice--banner'));
  assert.ok(f.document.querySelector('.companion-topbar').classList.contains('has-notice'));
  const dismiss=f.document.querySelector('.companion-notice-close');
  assert.ok(dismiss.classList.contains('hud-icon-button'));
  assert.equal(dismiss.getAttribute('aria-label'),'Dismiss message');
  f.fire(dismiss,'click');
  assert.equal(f.document.querySelector('.companion-notice').hidden,true);
  assert.equal(f.document.querySelector('.companion-topbar').classList.contains('has-notice'),false);
  assert.equal(read.disabled, false); assert.equal(pick.disabled, false);
  await f.client.disconnect(); await flush();
  assert.equal(read.disabled, true); assert.equal(pick.disabled, true);
  f.fire(read, 'click'); assert.equal(f.calls.filter(call => call[0] === 'telemetry').length, 2);
});

test('scoped device actions stay unavailable until an authoritative live probe resolves their target', async t => {
  const f=fixture(t);f.fire(f.byText('Connect board'),'click');await flush();
  const bridge=f.bridge();assert.equal(bridge.instance('arps.clear'),null);
  await assert.rejects(bridge.action('arps.clear',0),/current device action target/);
  let finish;
  f.client.readOverride=(row)=>row===rowIndex('arps.enabled')?new Promise(resolve=>finish=resolve):undefined;
  f.selectContext('arps','arps.clear');bridge.select('arps.clear');await flush();
  assert.equal(bridge.instance('arps.clear'),null,'No Arp 1 fallback while Arp target read is pending.');
  finish({row:rowIndex('arps.enabled'),instance:1,value:1});await flush();
  assert.equal(bridge.instance('arps.clear'),1);
  f.client.readOverride=row=>row===rowIndex('arps.enabled')?{row,instance:1,value:1}:undefined;
  await bridge.action('arps.clear',1);
  assert.ok(f.calls.some(call=>call[0]==='action'&&call[1]===rowIndex('arps.clear')&&call[2]===1));
});

test('route and pad actions use resolved device targets instead of stale snapshot selector guesses', async t => {
  const f=fixture(t);f.fire(f.byText('Connect board'),'click');await flush();
  f.client.readOverride=row=>row===rowIndex('midi.channel')?{row,instance:8,value:2}:
    row===rowIndex('sensitivity.amount')?{row,instance:95,value:80}:undefined;
  const bridge=f.bridge();bridge.select('midi.bendSetup');await flush();
  assert.equal(bridge.instance('midi.bendSetup'),8,'Snapshot route is 1; authoritative read resolves route 8.');
  bridge.select('sensitivity.reset');await flush();assert.equal(bridge.instance('sensitivity.reset'),95);
  await assert.rejects(bridge.action('sensitivity.reset',96),/current device action target/);
  assert.ok(!f.calls.some(call=>call[0]==='action'));
  await bridge.action('sensitivity.reset',95);
  assert.ok(f.calls.some(call=>call[0]==='action'&&call[1]===rowIndex('sensitivity.reset')&&call[2]===95));
});

test('track and view scalar reads keep the instance resolved by C and an unread scalar has no guessed instance', async t => {
  const f=fixture(t);f.fire(f.byText('Connect board'),'click');await flush();
  const bridge=f.bridge();assert.equal(bridge.instance('views.slot1'),null);
  f.client.readOverride=row=>row===rowIndex('sequencer.channel')?{row,instance:17,value:2}:
    row===rowIndex('views.slot1')?{row,instance:8,value:12}:undefined;
  bridge.select('sequencer.channel');bridge.select('views.slot1');await flush();
  assert.equal(bridge.instance('sequencer.channel'),17);assert.equal(bridge.instance('views.slot1'),8);
  assert.deepEqual(bridge.get('sequencer.channel'),{row:rowIndex('sequencer.channel'),instance:17,value:2,raw:2});
});

test('the first snapshot establishes a saved-generation baseline and later physical saves refresh the mirror', async t => {
  const f=fixture(t);f.fire(f.byText('Connect board'),'click');await flush();
  assert.equal(f.applies.length,1);
  f.client.savedGeneration=21;
  f.fire(f.nav,'click');await flush();
  assert.equal(f.applies.length,2,'A changed hardware save generation reloads applied device configuration.');
  assert.equal(f.calls.filter(call=>call[0]==='backup').length,2);
  f.fire(f.nav,'click');await flush();assert.equal(f.applies.length,2,'An unchanged generation does not repeatedly reload settings.');
});

test('a busy automatic configuration refresh keeps its generation pending while snapshots continue',async t=>{
  const clock=manualClock(),f=fixture(t,{clock});f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  f.calls.length=0;let reads=0;
  f.client.downloadBackup=async()=>{f.calls.push(['backup']);if(++reads===1)throw Error('ERR backup-busy');return backup();};
  f.client.savedGeneration=21;
  await clock.advance(100);
  assert.equal(reads,1);assert.equal(f.applies.length,1);
  assert.doesNotMatch(f.document.body.textContent,/ERR backup-busy/);
  await clock.advance(800);assert.equal(reads,1,'backoff prevents a full export on every display poll');
  assert.ok(f.calls.filter(call=>call[0]==='snapshot').length>=8,'busy staging does not pause screen sampling');
  f.client.savedGeneration=22;
  await clock.advance(200);
  assert.equal(reads,2);assert.equal(f.applies.length,2,'a later quiet poll applies the pending newest generation');
  await clock.advance(300);assert.equal(reads,2,'successful mirror refresh consumes the generation exactly once');
});

test('temporary backup staging at connect defers the initial mirror without losing live snapshots',async t=>{
  const clock=manualClock(),f=fixture(t,{clock});let reads=0;
  f.client.downloadBackup=async()=>{f.calls.push(['backup']);if(++reads===1)throw Error('ERR backup-busy');return backup();};
  f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  assert.equal(f.client.connected,true);assert.equal(f.applies.length,0);
  assert.ok(f.calls.some(call=>call[0]==='snapshot'));assert.doesNotMatch(f.document.body.textContent,/ERR backup-busy/);
  await clock.advance(1000);
  assert.equal(reads,2);assert.equal(f.applies.length,1);
  await clock.advance(200);assert.equal(reads,2);
});

test('explicit backup requests still surface a busy device instead of claiming a saved file',async t=>{
  const f=fixture(t);f.fire(f.byText('Connect board'),'click');await flush();
  f.shell.showPage('backups');
  f.client.downloadBackup=async()=>{throw Error('ERR backup-busy');};
  f.fire(f.byText('Read backup'),'click');await flush();
  assert.match(f.document.querySelector('.backup-notice').textContent,/Backup was not downloaded: ERR backup-busy/);
});

test('enhanced display uses the exact C frame and never invents text from untrusted snapshot metadata', t => {
  const f = fixture(t), mount = f.document.createElement('div');
  const frame = new Uint8Array(1024); frame[0] = 1; frame[1023] = 128;
  const snapshot = { frame, panelVisible: true, level: 0, scale: '<img onerror=alert(1)>', harmony: '<script>x</script>',
    pool: [60, 64, 67], sounding: [64], heldPads: 1, noteCount: 1, tempo: 120, mpe: true };
  const model = renderCompanionScreen(mount, snapshot, { source: 'DEVICE' });
  assert.equal(mount.querySelector('img,script'), null);
  assert.deepEqual(model.frame, frame); assert.deepEqual(model.residual, frame); assert.equal(model.texts.length, 0);
  assert.equal(mount.querySelector('path').attributes.d, 'M0 0h1v1h-1zM127 63h1v1h-1z');
  assert.doesNotMatch(mount.textContent, /script|img|E4|MPE|DEVICE/);
  const originalRoot = mount.firstChild; renderCompanionScreen(mount, snapshot);
  assert.equal(mount.firstChild, originalRoot, 'An unchanged frame keeps its existing SVG tree.');
  renderCompanionScreen(mount, { ...snapshot, panelVisible: false });
  assert.equal(mount.querySelector('.companion-display').dataset.panel, 'off'); assert.equal(mount.querySelector('path'), null);
  renderCompanionScreen(mount, { ...snapshot, frame: undefined });
  assert.equal(mount.querySelector('.companion-display').dataset.panel, 'unavailable'); assert.equal(mount.querySelector('path'), null);
});

test('adaptive polling starts at the healthy default, stays read-only offline and restores its browser interval',async t=>{
  let clock=manualClock(),f=fixture(t,{clock});
  assert.equal(DEFAULT_POLL_INTERVAL_MS,100);
  assert.equal(clock.pending(),0,'Offline has no device polling timer.');
  await clock.advance(550);assert.deepEqual(f.calls,[],'An offline refresh timer never opens or polls a device.');
  f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  assert.equal(clock.pending(),1);assert.equal(clock.nextDelay(),100);
  await f.cleanup();
  clock=manualClock();f=fixture(t,{clock,storage:new Map([[APP_POLL_KEY,'500']])});
  assert.equal(clock.pending(),0);
  f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);f.calls.length=0;
  assert.equal(clock.pending(),1);assert.equal(clock.nextDelay(),500,'The saved browser interval configures the scheduler.');
  await clock.advance(499);assert.equal(f.calls.filter(call=>call[0]==='snapshot').length,0);
  await clock.advance(1);assert.equal(f.calls.filter(call=>call[0]==='snapshot').length,1);
  assert.equal(clock.pending(),1);
});

test('a slow snapshot never overlaps another poll and the next deadline subtracts actual transfer time',async t=>{
  const clock=manualClock(),f=fixture(t,{clock});f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  f.calls.length=0;let active=0,maximum=0,finish;
  const snapshots=()=>f.calls.filter(call=>call[0]==='snapshot').length;
  f.client.snapshot=()=>{
    f.calls.push(['snapshot']);active++;maximum=Math.max(maximum,active);
    return new Promise(resolve=>{finish=()=>{active--;resolve({bytes:snapshot(),generation:7});};});
  };
  await clock.advance(100);assert.equal(active,1);
  await clock.advance(40);assert.equal(snapshots(),1);
  finish();await flush();
  assert.equal(clock.pending(),1);assert.equal(clock.nextDelay(),60,'A 40ms transfer leaves 60ms of the 100ms target cadence.');
  await clock.advance(59);assert.equal(snapshots(),1);
  await clock.advance(1);assert.equal(active,1);assert.equal(snapshots(),2);
  await clock.advance(350);assert.equal(snapshots(),2,'Elapsed timer boundaries cannot queue concurrent serial snapshots.');
  finish();await flush();
  assert.equal(maximum,1);assert.equal(clock.pending(),1);assert.equal(clock.nextDelay(),5,'A slow transfer yields briefly rather than queuing missed intervals.');
});

test('changing refresh while a snapshot is pending keeps one request and one future timer',async t=>{
  const clock=manualClock(),f=fixture(t,{clock});f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  f.calls.length=0;let finish,active=0,maximum=0;
  const snapshots=()=>f.calls.filter(call=>call[0]==='snapshot').length;
  f.client.snapshot=()=>{
    f.calls.push(['snapshot']);active++;maximum=Math.max(maximum,active);
    return new Promise(resolve=>{finish=()=>{active--;resolve({bytes:snapshot(),generation:7});};});
  };
  await clock.advance(100);assert.equal(active,1);
  choosePollInterval(f,50);assert.equal(f.storage.get(APP_POLL_KEY),'50');
  await clock.advance(60);assert.equal(snapshots(),1,'Even a new interval deadline must wait for the outstanding request.');
  choosePollInterval(f,250);assert.equal(f.storage.get(APP_POLL_KEY),'250');
  await clock.advance(40);assert.equal(snapshots(),1,'Preference changes do not start a second request beside the first.');
  finish();await flush();
  assert.equal(clock.pending(),1,'Only the newest interval owns the completion timer.');
  assert.ok(clock.nextDelay()>=5&&clock.nextDelay()<=250);
  await clock.advance(clock.nextDelay());assert.equal(snapshots(),2);assert.equal(active,1);assert.equal(maximum,1);
  finish();await flush();assert.equal(clock.pending(),1);
});

test('a live refresh preference replaces a pending timer and hidden, busy or disconnected states send no polls',async t=>{
  const clock=manualClock(),f=fixture(t,{clock});f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);f.calls.length=0;
  choosePollInterval(f,50);assert.equal(clock.pending(),1);assert.ok(clock.nextDelay()<=50);
  await clock.advance(clock.nextDelay());assert.equal(f.calls.filter(call=>call[0]==='snapshot').length,1);
  f.calls.length=0;f.document.hidden=true;await clock.advance(300);assert.deepEqual(f.calls,[],'Hidden browser tabs do not exchange snapshots.');
  f.document.hidden=false;f.client.busy=true;await clock.advance(300);assert.deepEqual(f.calls,[],'An explicit device operation takes priority over background polling.');
  f.client.busy=false;await clock.advance(50);assert.equal(f.calls.filter(call=>call[0]==='snapshot').length,1);
  f.fire(f.byText('Disconnect'),'click');await flush();f.calls.length=0;
  assert.equal(clock.pending(),0,'Disconnect cancels the background timer.');
  await clock.advance(300);assert.deepEqual(f.calls,[],'Disconnect never causes automatic reconnect or queued device reads.');
});

test('destroy cancels pending refresh and a late snapshot completion cannot revive its scheduler',async t=>{
  const clock=manualClock(),f=fixture(t,{clock});f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);f.calls.length=0;
  let finish;f.client.snapshot=()=>{f.calls.push(['snapshot']);return new Promise(resolve=>{finish=()=>resolve({bytes:snapshot(),generation:7});});};
  await clock.advance(100);assert.equal(f.calls.length,1);
  f.shell.destroy();await flush();assert.equal(clock.pending(),0);
  finish();await flush();assert.equal(clock.pending(),0,'Settling an old callback after disposal cannot install another timeout.');
  const count=f.calls.length;await clock.advance(1000);assert.equal(f.calls.length,count);
});

test('repeated snapshot and busy notifications do not rewrite unchanged board LEDs, controls or canvas',async t=>{
  const clock=manualClock(),f=fixture(t,{clock});f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  const canvas=f.document.querySelector('#oled-canvas'),web=f.document.querySelector('.companion-web-screen');
  const originalWeb=web.firstChild;
  const baseline=[{...f.pad.writes},{...f.rightPad.writes},{...f.nav.writes},{...f.func.writes}];
  for(let n=0;n<20;n++){f.client.busy=Boolean(n%2);f.refreshConnection();}
  f.client.busy=false;await clock.advance(500);
  assert.deepEqual([f.pad.writes,f.rightPad.writes,f.nav.writes,f.func.writes],baseline,'Unchanged 20 Hz snapshots and transaction busy edges require zero physical-surface DOM writes.');
  assert.equal(web.firstChild,originalWeb);assert.equal(canvas.writes.canvas,0,'The hidden OLED view is not rasterized while Web is selected.');
  const oledButton=f.document.querySelector('.companion-display-tools').querySelectorAll('button').find(node=>node.textContent==='OLED');
  f.fire(oledButton,'click');assert.equal(canvas.writes.canvas,1,'Changing display paints the newest frame once.');
  await clock.advance(500);assert.equal(canvas.writes.canvas,1,'An unchanged visible OLED frame is not repainted.');
});

test('changed pads repaint individually and inactive screen modes catch up only when shown',async t=>{
  const clock=manualClock(),f=fixture(t,{clock});f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  const baseline={left:{...f.pad.writes},right:{...f.rightPad.writes}},canvas=f.document.querySelector('#oled-canvas');
  let changed=snapshot();changed[1160]=8;changed[1467]=1;changed[136]=1;changed[1658]=1;
  f.client.snapshot=async()=>({bytes:changed.slice(),generation:8});await clock.advance(100);
  assert.equal(f.pad.writes.style,baseline.left.style+2);assert.equal(f.pad.writes.attribute,baseline.left.attribute+1);
  assert.deepEqual(f.rightPad.writes,baseline.right,'An unrelated pad receives no style or aria writes.');
  assert.equal(canvas.writes.canvas,0);
  const controls=f.document.querySelector('.companion-display-tools').querySelectorAll('button');
  f.fire(controls.find(node=>node.textContent==='OLED'),'click');assert.equal(canvas.writes.canvas,1);
  const web=f.document.querySelector('.companion-web-screen'),before=web.firstChild;
  changed=changed.slice();changed[137]=2;await clock.advance(100);
  assert.equal(canvas.writes.canvas,2);assert.equal(web.firstChild,before,'Hidden Web glyph analysis and SVG construction are skipped.');
  f.fire(controls.find(node=>node.textContent==='Web'),'click');assert.notEqual(web.firstChild,before,'Web catches up to the current device frame on selection.');
});

test('native host suspension cancels polling and late snapshots cannot repaint or restart it',async t=>{
  const clock=manualClock(),f=fixture(t,{clock,runtime:{kind:'desktop'}});f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  let finish;f.client.snapshot=()=>{f.calls.push(['snapshot']);return new Promise(resolve=>{finish=()=>{const bytes=snapshot();bytes[1160]=8;resolve({bytes,generation:8});};});};
  await clock.advance(100);const before={...f.pad.writes};f.shell.setHostActive(false);assert.equal(clock.pending(),0);
  finish();await flush();assert.deepEqual(f.pad.writes,before);assert.equal(clock.pending(),0);
  const count=f.calls.length;await clock.advance(2000);assert.equal(f.calls.length,count,'A minimized/background native window performs no polling.');
  f.shell.setHostActive(true);assert.equal(f.pad.writes.style,before.style+2,'Restoring the window renders the latest complete snapshot.');
  assert.equal(clock.nextDelay(),0);await clock.advance(0);finish();await flush();assert.equal(clock.pending(),1);
});

test('documentation uses a low-rate connection refresh and browser visibility pauses and resumes immediately',async t=>{
  const clock=manualClock(),f=fixture(t,{clock});f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  f.shell.showArea('documentation');f.calls.length=0;assert.equal(clock.nextDelay(),1000);
  await clock.advance(999);assert.deepEqual(f.calls,[]);await clock.advance(1);assert.equal(f.calls.filter(call=>call[0]==='snapshot').length,1);
  f.document.hidden=true;f.fire(f.document.body,'visibilitychange');assert.equal(clock.pending(),0);
  const count=f.calls.length;await clock.advance(2000);assert.equal(f.calls.length,count);
  f.document.hidden=false;f.fire(f.document.body,'visibilitychange');assert.equal(clock.nextDelay(),0);
  await clock.advance(0);f.shell.showPage('board');assert.equal(clock.nextDelay(),100);
  f.shell.showArea('update');f.calls.length=0;await clock.advance(2000);assert.deepEqual(f.calls,[],'The update wizard owns application transactions; its hidden Board poll cannot compete.');
});

test('public site Client exposes Install, Guide, Client and project navigation without connecting',t=>{
  const f=fixture(t,{projectSite:true});
  const nav=f.document.querySelector('.companion-main-nav');
  assert.deepEqual(nav.children.map(node=>node.textContent),['Install','Guide','Client','Project']);
  assert.equal(nav.children[3].href,'../index.html');
  assert.deepEqual(f.calls,[]);
});

test('compact navigation moves existing actions, traps focus and restores desktop layout', async t => {
  const media={matches:true,addEventListener(type,fn){this.change=fn;},removeEventListener(){this.change=null;}};
  const original=Object.getOwnPropertyDescriptor(globalThis,'matchMedia');
  Object.defineProperty(globalThis,'matchMedia',{configurable:true,value:query=>
    query==='(max-width: 900px)'?media:{matches:false,addEventListener(){},removeEventListener(){}}});
  const f=fixture(t),doc=f.document;
  try {
    const toggle=doc.querySelector('.compact-nav-toggle'),overlay=doc.querySelector('.compact-nav-overlay');
    assert.ok(toggle.classList.contains('hud-icon-button'));
    const nav=doc.querySelector('.companion-client-nav'),panel=doc.querySelector('.compact-nav-panel');
    const stripes=toggle.querySelectorAll('svg path');
    assert.equal(stripes.length,3);
    assert.equal(new Set(stripes.map(path=>path.getAttribute('d'))).size,1,'The three rising stripes are identical.');
    assert.deepEqual(stripes.map(path=>path.getAttribute('transform')),['translate(0 0)','translate(0 5)','translate(0 10)']);
    assert.ok(stripes.every(path=>path.getAttribute('stroke')==='currentColor'));
    assert.equal(overlay.hidden,true);assert.ok(panel.contains(nav));
    f.fire(toggle,'click');assert.equal(overlay.hidden,false);assert.equal(doc.body.children[0].inert,true);
    const close=doc.querySelector('.compact-nav-heading button');assert.equal(doc.activeElement,close);
    assert.ok(close.classList.contains('hud-icon-button'));
    f.fire(close,'keydown',{key:'Escape'});assert.equal(overlay.hidden,true);assert.equal(doc.activeElement,toggle);
    assert.notEqual(doc.body.children[0].inert,true);
    f.fire(toggle,'click');f.shell.showArea('documentation');assert.equal(overlay.hidden,true);
    media.matches=false;media.change();assert.equal(toggle.hidden,true);assert.ok(!panel.contains(nav));
    assert.deepEqual(f.calls,[]);
  } finally {await f.cleanup();if(original)Object.defineProperty(globalThis,'matchMedia',original);else delete globalThis.matchMedia;}
});

test('live telemetry uses bounded polling and stops on leaving Diagnostics or disconnecting', async t => {
  const clock=manualClock(),f=fixture(t,{clock});
  f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  f.shell.showPage('diagnostics');f.calls.length=0;
  f.fire(f.byText('Start live session'),'click');await flush();await clock.advance(1100);
  const count=()=>f.calls.filter(c=>c[0]==='telemetry').length;
  assert.ok(count()>=1);const first=count();
  await clock.advance(100);assert.equal(count(),first);
  f.document.hidden=true;await clock.advance(2100);assert.equal(count(),first);
  f.document.hidden=false;f.shell.showPage('board');await clock.advance(2100);assert.equal(count(),first);
  assert.ok(f.byText('Start live session'));
  f.shell.showPage('diagnostics');f.fire(f.byText('Start live session'),'click');await flush();
  await f.client.disconnect();await flush();const stopped=count();await clock.advance(2000);assert.equal(count(),stopped);
});


test('a late failed report from a stopped session cannot stop its replacement',async t=>{
  const clock=manualClock(),f=fixture(t,{clock});
  f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  f.shell.showPage('diagnostics');let reject;
  f.client.telemetry=()=>new Promise((resolve,fail)=>{reject=fail;});
  f.fire(f.byText('Start live session'),'click');await flush();await clock.advance(1100);
  assert.equal(typeof reject,'function');
  f.fire(f.byText('Stop live session'),'click');f.fire(f.byText('Start live session'),'click');
  reject(new Error('Old session failure'));await flush();
  assert.ok(f.byText('Stop live session'));
  assert.doesNotMatch(f.document.querySelector('.telemetry-session').textContent,/Session stopped/);
});

test('cockpit settings cache reads explicit physical halves in bounded batches and reuses unchanged settings',async()=>{
  const calls=[],live={uid:UID,build:BUILD,schema:SCHEMA,stateRevision:1,savedGeneration:20};
  const values={mpeOutput:[1,2],profileEnabled:[1,-1],pressureMode:[1,2],pressureBend:[0,1],bendRange:[2,12]};
  const cache=createCockpitContextCache({read:async(row,instance)=>{
    calls.push([MENU_ROWS[row].id,instance]);
    return {row,instance,value:values[MENU_ROWS[row].id.split('.')[1]][instance-1]};
  }});
  assert.equal(cache.current,null);
  await cache.refresh(live,{eligible:false});assert.deepEqual(calls,[],'Hidden/read-only/disconnected callers do no optional work.');
  for(let n=0;n<5;n++){
    const before=calls.length;await cache.refresh(live);
    assert.equal(calls.length-before,2,'One snapshot poll permits at most two additional scalar reads.');
  }
  assert.equal(cache.current.pending,false);
  assert.deepEqual(cache.current.expression,{mpeOutput:[1,2],aftertouchStage:[true,-1],atMode:[1,2],bend:[0,1],bendRange:[2,12]});
  assert.ok(calls.every(([,instance])=>instance===1||instance===2),'No temporary Settings for selector is written or guessed.');
  const revision=cache.current.revision;
  await cache.refresh({...live,instrumentRevision:99,pressure:[120,80]});
  assert.equal(calls.length,10);assert.equal(cache.current.revision,revision,'Pad/pressure changes do not re-read settings.');
  await cache.refresh({...live,stateRevision:2});
  assert.equal(calls.length,12);assert.ok(cache.current.revision>revision);assert.equal(cache.current.pending,true);
  assert.deepEqual(cache.current.expression.aftertouchStage,[null,null],'A new revision never presents old stages as current.');
});

test('cockpit cache discards stale scalar completions and never overlaps its read batch',async()=>{
  const live={uid:UID,build:BUILD,schema:SCHEMA,stateRevision:1,savedGeneration:20};
  let finish,reads=0;
  const cache=createCockpitContextCache({read:(row,instance)=>{
    reads++;return new Promise(resolve=>{finish=()=>resolve({row,instance,value:2});});
  }});
  const pending=cache.refresh(live);await Promise.resolve();assert.equal(reads,1);
  await cache.refresh(live);assert.equal(reads,1);
  cache.reset();finish();await pending;
  assert.equal(cache.current,null,'Disconnect/reset invalidates the old read, even if it succeeds later.');
});

test('busy or malformed cockpit scalar replies cannot fabricate expression values',async()=>{
  const live={uid:UID,build:BUILD,schema:SCHEMA,stateRevision:1,savedGeneration:20};
  let busy=true;
  const cache=createCockpitContextCache({read:async(row,instance)=>{
    if(busy)throw Error('COMPANION/1 ERR busy');
    return {row,instance:instance===1?2:1,value:2};
  }});
  await cache.refresh(live);assert.deepEqual(cache.current.expression.mpeOutput,[null,null]);assert.equal(cache.deferred,true);
  busy=false;await assert.rejects(cache.refresh(live),/invalid expression settings/);
  assert.deepEqual(cache.current.expression.mpeOutput,[null,null],'Another physical half is not an acceptable replacement.');
});

test('cockpit activates only after a validated snapshot and pauses when hidden or host-inactive',async t=>{
  const clock=manualClock(),f=fixture(t,{clock});let finish;
  f.client.snapshot=()=>new Promise(resolve=>{finish=()=>resolve({bytes:snapshot(),generation:7});});
  f.fire(f.byText('Connect board'),'click');await flush();
  const cockpit=f.document.querySelector('.companion-cockpit');
  assert.equal(f.client.connected,true);assert.equal(cockpit.dataset.cockpitState,'offline');
  assert.equal(cockpit.classList.contains('cockpit-motion-paused'),true);
  assert.equal(f.calls.some(call=>call[0]==='read'&&call[2]!==255),false);
  finish();await flush();
  assert.equal(cockpit.dataset.cockpitState,'device');assert.equal(cockpit.dataset.cockpitMotion,'running');
  f.shell.setHostActive(false);assert.equal(cockpit.dataset.cockpitMotion,'paused');
  f.shell.setHostActive(true);assert.equal(cockpit.dataset.cockpitMotion,'running');
  f.document.hidden=true;f.fire(f.document.body,'visibilitychange');assert.equal(cockpit.dataset.cockpitMotion,'paused');
  f.document.hidden=false;f.fire(f.document.body,'visibilitychange');assert.equal(cockpit.dataset.cockpitMotion,'running');
  f.shell.showArea('documentation');assert.equal(cockpit.dataset.cockpitMotion,'paused');
  f.shell.showPage('board');assert.equal(cockpit.dataset.cockpitMotion,'running');
  await f.client.disconnect();await flush();assert.equal(cockpit.dataset.cockpitState,'offline');assert.equal(f.shell.snapshot,null);
});

test('disconnected late context reads cannot reactivate the cockpit or fetch the other half',async t=>{
  const clock=manualClock(),f=fixture(t,{clock});let finish;
  f.client.readOverride=(row,instance)=>instance===1&&MENU_ROWS[row].id==='performance.mpeOutput'?
    new Promise(resolve=>{finish=()=>resolve({row,instance,value:2});}):undefined;
  f.fire(f.byText('Connect board'),'click');await flush();assert.equal(typeof finish,'function');
  await f.client.disconnect();await flush();const before=f.calls.length;
  finish();await flush();
  assert.equal(f.calls.length,before,'A completed old read cannot continue its batch after disconnect.');
  assert.equal(f.document.querySelector('.companion-cockpit').dataset.cockpitState,'offline');
  assert.equal(f.shell.snapshot,null);assert.equal(clock.pending(),0);
});

test('held velocity changes refresh cockpit feedback without waiting for a settings revision',async t=>{
  const clock=manualClock(),f=fixture(t,{clock});let bytes=snapshot();bytes[1612]=96;bytes[1613]=96;
  f.client.snapshot=async()=>({bytes:bytes.slice(),generation:7});
  f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(500);
  const reads=f.calls.filter(call=>call[0]==='read').length,revision=f.shell.snapshot.context.revision;
  const left=f.document.querySelector('[data-cockpit-value="velocity-left"]');assert.equal(left.textContent,'—');
  bytes=bytes.slice();bytes[1616]=91;bytes[1614]=1;await clock.advance(100);
  assert.equal(left.textContent,'91');assert.equal(f.shell.snapshot.context.revision,revision);
  assert.equal(f.calls.filter(call=>call[0]==='read').length,reads,'Playing adds no settings reads.');
});

test('invalid first snapshots cannot activate the cockpit or start contextual reads',async t=>{
  const f=fixture(t);f.client.snapshot=async()=>({bytes:snapshot().subarray(0,1679),generation:7});
  f.fire(f.byText('Connect board'),'click');await flush();
  assert.equal(f.document.querySelector('.companion-cockpit').dataset.cockpitState,'offline');
  assert.equal(f.shell.snapshot,null);
  assert.equal(f.calls.some(call=>call[0]==='read'&&call[2]!==255),false);
});

test('context backpressure defers optional Harmony work without dropping the validated musical frame',async t=>{
  const clock=manualClock(),f=fixture(t,{clock,harmonySupport:true});f.client.harmonyActive=true;
  let busy=true;
  f.client.readOverride=(row,instance)=>{
    if(instance===1&&MENU_ROWS[row].id==='performance.mpeOutput'&&busy)throw Error('COMPANION/1 ERR busy');
  };
  f.fire(f.byText('Connect board'),'click');await flush();
  assert.equal(f.document.querySelector('.companion-cockpit').dataset.cockpitState,'device');
  assert.equal(f.calls.some(call=>call[0]==='harmonySnapshot'),false,'Busy scalar work stops optional reads for this poll.');
  assert.equal(f.shell.snapshot.context.expression.mpeOutput[0],null);
  busy=false;await clock.advance(100);
  assert.equal(f.calls.filter(call=>call[0]==='harmonySnapshot').length,1);
  assert.equal(f.shell.snapshot.context.harmony.active,true);
  assert.equal(clock.pending(),1,'Context work shares the existing scheduler.');
});


test('performance feedback is paced within the existing poll and clears on pause, busy, disconnect and older firmware',async t=>{
  const clock=manualClock(),f=fixture(t,{clock,performanceSupport:true});
  f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  const count=()=>f.calls.filter(call=>call[0]==='performanceSnapshot').length;
  assert.equal(count(),1);assert.equal(f.document.querySelector('[data-cockpit-voice="0"]').hidden,false);
  await clock.advance(100);assert.equal(count(),1);await clock.advance(100);assert.equal(count(),2);
  const initial=count();f.shell.setHostActive(false);await clock.advance(500);assert.equal(count(),initial);
  f.shell.setHostActive(true);assert.equal(f.document.querySelector('[data-cockpit-voice="0"]').hidden,true,'Old detail clears before resuming.');
  await clock.advance(0);assert.equal(count(),initial+1);
  f.client.busy=true;f.shell.render();assert.equal(f.document.querySelector('[data-cockpit-voice="0"]').hidden,true);
  await clock.advance(500);assert.equal(count(),initial+1);f.client.busy=false;await clock.advance(100);
  f.client.performanceFailure='COMPANION/1 ERR busy';await clock.advance(200);
  assert.equal(f.document.querySelector('[data-cockpit-voice="0"]').hidden,true);assert.match(f.document.querySelector('[data-cockpit-value="voice-detail-status"]').textContent,/fresh data/);
  f.fire(f.byText('Disconnect'),'click');await flush();assert.equal(f.document.querySelector('.cockpit-hud').hidden,true);
});

test('performance pacing expires old detail across wall-clock jumps and respects slow refresh preferences',async t=>{
  const clock=manualClock(),f=fixture(t,{clock,performanceSupport:true});let wallOffset=0;
  Object.defineProperty(Date,'now',{configurable:true,writable:true,value:()=>clock.now()+wallOffset});
  f.fire(f.byText('Connect board'),'click');await flush();await clock.advance(0);
  const count=()=>f.calls.filter(call=>call[0]==='performanceSnapshot').length;
  assert.equal(count(),1);
  wallOffset=-60000;f.shell.render();assert.equal(f.document.querySelector('[data-cockpit-voice="0"]').hidden,true);
  await clock.advance(100);assert.equal(count(),2,'Backward adjustment admits a fresh capture instead of suppressing it for a minute.');
  wallOffset=60000;f.shell.render();assert.equal(f.document.querySelector('[data-cockpit-voice="0"]').hidden,true);
  await clock.advance(100);assert.equal(count(),3);
  choosePollInterval(f,1000);await clock.advance(1000);
  assert.equal(count(),4);assert.equal(f.document.querySelector('[data-cockpit-voice="0"]').hidden,false);
  assert.equal(clock.pending(),1,'Detail telemetry adds no second scheduler.');
});
