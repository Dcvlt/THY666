import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { initializeCockpit, initializeDiagnostics } from '../../web/cockpit.js';
import {performancePacket} from './performance-fixture.mjs';
import {decodePerformanceSnapshot} from '../../web/companion-protocol.js';
import { decodeCompanionSnapshot } from '../../web/companion-protocol.js';

const dataKey = key => key.replace(/-([a-z])/g, (_, letter) => letter.toUpperCase());
class Element {
  constructor(document, tag) {
    this.ownerDocument = document; this.tagName = tag.toUpperCase(); this.children = [];
    this.parentNode = null; this.dataset = {}; this.attributes = {}; this.className = '';
    this.hidden = false; this.listeners = []; this._text = '';
    this.style = { setProperty(key, value) { this[key] = String(value); },
      getPropertyValue(key) { return this[key] || ''; }, removeProperty(key) { delete this[key]; } };
    this.classList = {
      contains: name => this.className.split(/\s+/).includes(name),
      add: (...names) => { this.className = [...new Set([...this.className.split(/\s+/).filter(Boolean), ...names])].join(' '); },
      remove: (...names) => { this.className = this.className.split(/\s+/).filter(name => !names.includes(name)).join(' '); },
      toggle: (name, force) => { const on = force ?? !this.classList.contains(name); on ? this.classList.add(name) : this.classList.remove(name); return on; },
    };
  }
  set textContent(value) { this.ownerDocument.writes = (this.ownerDocument.writes || 0) + 1; this._text = String(value); this.replaceChildren(); }
  get textContent() { return this._text + this.children.map(node => node.textContent).join(''); }
  set innerHTML(_) { throw new Error('Cockpit device values must never become HTML.'); }
  get parentElement() { return this.parentNode; }
  get firstChild() { return this.children[0] || null; }
  setAttribute(key, value) {
    this.ownerDocument.writes = (this.ownerDocument.writes || 0) + 1;
    this.attributes[key] = String(value);
    if (key === 'class') this.className = String(value);
    if (key.startsWith('data-')) this.dataset[dataKey(key.slice(5))] = String(value);
  }
  getAttribute(key) { return key.startsWith('data-') ? this.dataset[dataKey(key.slice(5))] ?? null : this.attributes[key] ?? null; }
  removeAttribute(key) { delete this.attributes[key]; if (key.startsWith('data-')) delete this.dataset[dataKey(key.slice(5))]; }
  append(...nodes) {
    for (let node of nodes) {
      if (typeof node === 'string') { const text = this.ownerDocument.createElement('#text'); text.textContent = node; node = text; }
      node.remove(); node.parentNode = this; this.children.push(node);
    }
  }
  appendChild(node) { this.append(node); return node; }
  prepend(...nodes) { for (const node of [...nodes].reverse()) { node.remove(); node.parentNode = this; this.children.unshift(node); } }
  insertBefore(node, reference) {
    if (!reference) return this.appendChild(node);
    node.remove(); node.parentNode = this; this.children.splice(this.children.indexOf(reference), 0, node); return node;
  }
  before(...nodes) { if (this.parentNode) for (const node of nodes) this.parentNode.insertBefore(node, this); }
  replaceChildren(...nodes) { for (const child of this.children) child.parentNode = null; this.children = []; this.append(...nodes); }
  remove() { if (this.parentNode) this.parentNode.children = this.parentNode.children.filter(child => child !== this); this.parentNode = null; }
  contains(node) { return node === this || this.children.some(child => child.contains(node)); }
  addEventListener(type, listener) { this.listeners.push([type, listener]); }
  removeEventListener(type, listener) { this.listeners = this.listeners.filter(item => item[0] !== type || item[1] !== listener); }
  matches(selector) {
    return selector.split(',').some(branch => {
      const tag = /^[a-z][\w-]*/i.exec(branch.trim())?.[0];
      if (tag && this.tagName !== tag.toUpperCase()) return false;
      for (const [, name] of branch.matchAll(/\.([\w-]+)/g)) if (!this.classList.contains(name)) return false;
      for (const [, name, value] of branch.matchAll(/\[([\w-]+)(?:="([^"]*)")?\]/g)) {
        const actual = this.getAttribute(name);
        if (actual === null || value !== undefined && actual !== value) return false;
      }
      return true;
    });
  }
  querySelectorAll(selector) {
    const found = []; const walk = node => { for (const child of node.children) { if (child.matches(selector)) found.push(child); walk(child); } };
    walk(this); return found;
  }
  querySelector(selector) { return this.querySelectorAll(selector)[0] || null; }
}

function snapshot() {
  const packet = new Uint8Array(1680), view = new DataView(packet.buffer), encoder = new TextEncoder();
  const string = (offset, text) => packet.set(encoder.encode(text), offset);
  const bit = (offset, index) => { packet[offset + (index >>> 3)] |= 1 << (index & 7); };
  string(0, 'THYCMP1'); view.setUint16(8, 1, true); view.setUint16(10, 1680, true);
  view.setUint32(12, 8, true); view.setUint32(16, 1111, true); view.setUint32(20, 1100, true);
  string(32, 'b2240f946ad045db'); string(48, '0123456789abcdef01234567'); string(72, 'ab'.repeat(32));
  string(1517, 'A MIN7'); string(1549, 'DORIAN A');
  for (const pad of [0, 52, 95]) bit(1467, pad);
  for (const note of [57, 60, 64, 67]) bit(1485, note);
  for (const note of [57, 60, 62, 64, 67]) bit(1501, note);
  packet[1604] = 31; packet[1605] = 9; packet[1608] = 39; packet[1609] = 108;
  packet[1612] = 94; packet[1613] = 98; packet[1615] = 1;
  packet[1616] = 44; packet[1617] = 101; packet[1618] = 3; packet[1619] = 4;
  view.setUint16(1620, 8, true); view.setUint16(1622, 4, true); view.setUint32(1624, 123500, true);
  packet[1628] = 1; packet[1629] = 2; packet[1630] = 1; packet[1631] = 3;
  packet[1632] = 52; packet[1633] = 7; view.setUint16(1634, 2123, true);
  view.setInt16(1636, -9, true); packet[1638] = 63; packet[1639] = 5; packet[1640] = 104;
  view.setUint16(1642, 1300, true); view.setUint16(1644, 1277, true); packet[1658] = 1;
  return decodeCompanionSnapshot(packet);
}

function fixture(t) {
  let hardwareCalls = 0;
  const document = { hidden: false, writes: 0, listeners: new Map(),
    addEventListener(type, callback) { this.listeners.set(type, callback); },
    removeEventListener(type, callback) { if (this.listeners.get(type) === callback) this.listeners.delete(type); },
    createElement(tag) { return new Element(this, tag); },
    createElementNS(_namespace, tag) { return new Element(this, tag); } };
  document.body = document.createElement('body');
  const mount = document.createElement('section'), instrument = document.createElement('div');
  instrument.className = 'instrument'; mount.append(instrument); document.body.append(mount);
  const descriptors = new Map();
  for (const [key, value] of Object.entries({ document, navigator: {
    get serial() { hardwareCalls++; throw new Error('No serial access in presentation code.'); },
    get usb() { hardwareCalls++; throw new Error('No USB access in presentation code.'); },
    requestMIDIAccess() { hardwareCalls++; throw new Error('No MIDI access in presentation code.'); },
  } })) {
    descriptors.set(key, Object.getOwnPropertyDescriptor(globalThis, key));
    Object.defineProperty(globalThis, key, { configurable: true, value });
  }
  const cockpit = initializeCockpit({ mount });
  t.after(() => {
    cockpit.destroy();
    for (const [key, descriptor] of descriptors) {
      if (descriptor) Object.defineProperty(globalThis, key, descriptor); else delete globalThis[key];
    }
  });
  const value = name => {
    const node = mount.querySelector(`[data-cockpit-value="${name}"]`);
    assert.ok(node, `Missing cockpit telemetry field ${name}`);
    return node.textContent;
  };
  const nodes = () => { let count = 0; const walk = node => { count++; node.children.forEach(walk); }; walk(mount); return count; };
  return { cockpit, mount, instrument, document, value, nodes, hardwareCalls: () => hardwareCalls };
}

test('the cockpit stays hidden offline and labels its optional local design preview explicitly', t => {
  const f = fixture(t);
  f.cockpit.update(null);
  assert.equal(f.mount.dataset.cockpitState, 'offline');
  const huds = f.mount.querySelectorAll('.cockpit-hud');
  assert.ok(huds.length > 0); assert.ok(huds.every(node => node.hidden));
  assert.equal(f.value('source'), 'OFFLINE');
  f.cockpit.update(snapshot(), { preview: true });
  assert.equal(f.mount.dataset.cockpitState, 'preview');
  assert.match(f.value('source'), /DESIGN MODE/);
  assert.doesNotMatch(f.value('source'), /LIVE DEVICE|CONNECTED/i);
  assert.ok(huds.every(node => !node.hidden));
  f.cockpit.update(snapshot(), { connected: true, preview: true });
  assert.equal(f.mount.dataset.cockpitState, 'device');
  assert.equal(f.value('source'), 'LIVE DEVICE');
  assert.equal(f.hardwareCalls(), 0);
});

test('connected gauges and labels report actual decoded packet fields without touching the snapshot', t => {
  const f = fixture(t), actual = snapshot(), before = structuredClone(actual);
  f.cockpit.update(actual, { connected: true });
  assert.equal(f.value('pressure-left'), '39 / 127');
  assert.equal(f.value('pressure-right'), '108 / 127');
  assert.equal(f.value('notes'), '4'); assert.equal(f.value('pads'), '3');
  assert.equal(f.value('tempo'), '123.5');
  assert.equal(f.value('clock-source'), 'USB'); assert.equal(f.value('clock-status'), 'LOCKED');
  assert.equal(f.value('oled-age'), '11 ms');
  assert.equal(f.value('inspect'), 'RIGHT R1 C2');
  assert.equal(f.value('harmony'), 'A MIN7'); assert.equal(f.value('scale'), 'DORIAN A');
  const contacts = f.mount.querySelectorAll('[data-cockpit-pad]');
  assert.equal(contacts.length, 96);
  assert.deepEqual(contacts.filter(node => node.classList.contains('is-held')).map(node => Number(node.dataset.cockpitPad)), [0, 52, 95]);
  const pitches = f.mount.querySelectorAll('[data-cockpit-pitch]');
  assert.equal(pitches.length, 12);
  assert.deepEqual(pitches.filter(node => node.classList.contains('is-sounding')).map(node => Number(node.dataset.cockpitPitch)).sort((a, b) => a - b), [0, 4, 7, 9]);
  assert.deepEqual(pitches.filter(node => node.classList.contains('is-pool')).map(node => Number(node.dataset.cockpitPitch)).sort((a, b) => a - b), [0, 2, 4, 7, 9]);
  assert.equal(pitches.find(node => node.classList.contains('is-root')).dataset.cockpitPitch, '9');
  for (const flag of ['mpe', 'arp1', 'arp2', 'running', 'recording'])
    assert.equal(f.mount.querySelector(`[data-cockpit-flag="${flag}"]`).classList.contains('is-on'), true, flag);
  assert.deepEqual(actual, before);
  assert.equal(f.hardwareCalls(), 0);
});

test('clock wrap and expression stage changes remain direct telemetry rather than a local timer', t => {
  const f = fixture(t), actual = snapshot();
  f.cockpit.update({ ...actual, now: 5, frameMs: 4294967293, mpe: false, aftertouchStage: [false, true] }, { connected: true });
  assert.equal(f.value('oled-age'), '8 ms');
  assert.equal(f.value('expression'), 'MIDI / UPPER STAGE');
  f.cockpit.update({ ...actual, now: 100, frameMs: 200 }, { connected: true });
  assert.equal(f.value('oled-age'), '—', 'A future timestamp cannot become a fabricated multi-day age.');
  f.cockpit.update({ ...actual, now: 100, frameMs: 0, pressure: [-1, 128], tempo: NaN, noteCount: 129, heldPads: 97, inspect: { logical: 255 } }, { connected: true });
  for (const key of ['oled-age', 'pressure-left', 'pressure-right', 'tempo', 'notes', 'pads', 'inspect'])
    assert.equal(f.value(key), '—', key);
  assert.equal(f.hardwareCalls(), 0);
});

test('missing live measurements remain unknown instead of fabricated defaults', t => {
  const f = fixture(t);
  f.cockpit.update({}, { connected: true });
  for (const key of ['pressure-left', 'pressure-right', 'notes', 'pads', 'tempo', 'oled-age', 'inspect'])
    assert.equal(f.value(key), '—', key);
  assert.equal(f.mount.querySelectorAll('[data-cockpit-pad].is-held').length, 0);
  assert.equal(f.value('source'), 'LIVE DEVICE');
  assert.equal(f.hardwareCalls(), 0);
});

test('telemetry updates keep one connected session and bounded DOM rather than restarting its entry motion', t => {
  const f = fixture(t), actual = snapshot();
  f.cockpit.update(actual, { connected: true });
  const session = Number(f.mount.dataset.cockpitSession), count = f.nodes();
  const source = f.mount.querySelector('[data-cockpit-value="source"]');
  const hud = f.mount.querySelector('.cockpit-hud');
  assert.ok(session > 0);
  for (let index = 0; index < 250; index++) {
    f.cockpit.update({ ...actual, now: actual.now + index, pressure: [index % 128, (index * 3) % 128] }, { connected: true });
    assert.equal(Number(f.mount.dataset.cockpitSession), session);
    assert.equal(f.nodes(), count);
    assert.equal(f.mount.querySelector('[data-cockpit-value="source"]'), source);
    assert.equal(f.mount.querySelector('.cockpit-hud'), hud);
  }
  f.cockpit.update(actual, { connected: true, preview: true });
  assert.equal(Number(f.mount.dataset.cockpitSession), session, 'Preview cannot replace a real connected session.');
  f.cockpit.update(null, { connected: false });
  assert.ok(f.mount.querySelectorAll('.cockpit-hud').every(node => node.hidden));
  f.cockpit.update(actual, { connected: true });
  assert.equal(Number(f.mount.dataset.cockpitSession), session + 1);
  assert.equal(f.nodes(), count);
  assert.equal(f.hardwareCalls(), 0);
});

test('untrusted musical labels stay text and teardown preserves the playable board', t => {
  const f = fixture(t), actual = snapshot();
  actual.harmony = '<img src=x>'; actual.scale = '<svg/onload=x>';
  f.cockpit.update(actual, { connected: true });
  assert.equal(f.value('harmony'), '<img src=x>'); assert.equal(f.value('scale'), '<svg/onload=x>');
  assert.equal(f.mount.querySelector('img,script'), null);
  assert.ok(f.mount.contains(f.instrument));
  f.cockpit.destroy();
  assert.equal(f.mount.querySelector('.cockpit-hud'), null);
  assert.equal(f.mount.classList.contains('cockpit-active'), false);
  assert.equal(f.mount.dataset.cockpitState, undefined);
  assert.equal(f.mount.dataset.cockpitSession, undefined);
  assert.ok(f.mount.contains(f.instrument));
  const count = f.nodes();
  f.cockpit.update(actual, { connected: true }); f.cockpit.destroy();
  assert.equal(f.nodes(), count);
  assert.equal(f.mount.querySelector('.cockpit-hud'), null);
  assert.equal(f.hardwareCalls(), 0);
});

test('cockpit presentation provides a static reduced-motion policy for its entry and ongoing animation', async () => {
  const css = await readFile(new URL('../../web/cockpit.css', import.meta.url), 'utf8');
  const policies = [...css.matchAll(/@media\s*\(prefers-reduced-motion:\s*reduce\)\s*\{/g)]
    .map(match => css.slice(match.index, match.index + 3500)).join('\n');
  assert.match(policies, /\.cockpit/);
  assert.match(policies, /animation(?:-duration)?\s*:\s*(?:none|0(?:s|ms)|0?\.0*1ms)/);
  assert.match(policies, /transition(?:-duration)?\s*:\s*(?:none|0(?:s|ms)|0?\.0*1ms)/);
});


test('contextual panels follow active modes and do not invent track numbers', t => {
  const f=fixture(t), data=snapshot();
  f.cockpit.update({...data,mpe:false,arp1:false,arp2:false,running:false,recording:false,engine:1},{connected:true});
  assert.ok(f.mount.querySelectorAll('.cockpit-context').every(n=>n.hidden));
  f.cockpit.update({...data,engine:6},{connected:true});
  assert.equal(f.mount.querySelectorAll('.cockpit-context').filter(n=>!n.hidden).length,3);
  assert.equal(f.mount.querySelector('.cockpit-harmony-detail').hidden,false,'Harmony expands its existing harmonic instrument instead of duplicating a text card.');
  assert.equal(f.value('harmony-home'),'DORIAN A');
  f.cockpit.update({...data,engine:2,selectedTrack:-1},{connected:true});
  assert.equal(f.value('seq-track'),'—');assert.equal(f.hardwareCalls(),0);
});

test('MPE routing shows the configured axes and physical input peaks without guessing an upper-stage value', t => {
  const f=fixture(t),data=snapshot();
  const context={expression:{mpeOutput:[0,2],aftertouchStage:[true,false]}};
  f.cockpit.update({...data,context},{connected:true});
  assert.equal(f.mount.querySelector('.cockpit-pressure').hidden,true);
  const overview=f.mount.querySelector('.cockpit-panels');
  assert.ok(overview.contains(f.mount.querySelector('.cockpit-mpe-context')));
  assert.equal(overview.children.filter(node=>!node.hidden).length,4,'MPE replaces the pressure slot rather than adding a fifth overview card.');
  assert.equal(f.value('mpe-primary-0'),'Pressure → Pressure');
  assert.equal(f.value('mpe-primary-1'),'Pressure → CC1');
  assert.equal(f.value('mpe-upper-0'),'On → CC74');
  assert.equal(f.value('mpe-upper-1'),'Off');
  assert.equal(f.value('velocity-left'),'44');assert.equal(f.value('velocity-right'),'101');
  const lanes=f.mount.querySelectorAll('.cockpit-expression-lane');
  assert.match(lanes[0].getAttribute('aria-label'),/pressure 39, velocity 44/);
  const bars=lanes[0].querySelectorAll('rect');
  assert.equal(Number(bars[0].getAttribute('width')),39*220/127);
  assert.equal(Number(bars[1].getAttribute('width')),44*220/127);
  f.cockpit.update({...data,context:{expression:{mpeOutput:[1,null],aftertouchStage:[true,-1]}}},{connected:true});
  assert.equal(f.value('mpe-upper-0'),'On → Pressure');assert.equal(f.value('mpe-primary-1'),'Pressure → —');
  assert.equal(f.value('mpe-upper-1'),'Mixed');
  f.cockpit.update({...data,context:undefined},{connected:true});
  assert.equal(f.value('mpe-primary-0'),'Pressure → —','No configuration must not silently imply the default mapping.');
  f.cockpit.update({...data,mpe:false},{connected:true});
  assert.equal(f.mount.querySelector('.cockpit-pressure').hidden,false);
  assert.equal(f.mount.querySelector('.cockpit-mpe-context').hidden,true);
  assert.equal(f.hardwareCalls(),0);
});

test('the Arp ribbon projects the exact combined MIDI pitch sets, not inferred per-Arp phases', t => {
  const f=fixture(t),data=snapshot();
  f.cockpit.update({...data,pool:[0,60,60,64,127,-1,128],sounding:[64,127]},{connected:true});
  const present=kind=>f.mount.querySelectorAll('.cockpit-ribbon-note.is-present')
    .filter(n=>n.dataset.cockpitRibbon.startsWith(`${kind}-`)).map(n=>Number(n.dataset.cockpitRibbon.split('-')[1]));
  assert.deepEqual(present('pool'),[0,60,64,127]);assert.deepEqual(present('output'),[64,127]);
  assert.equal(f.value('arp-pool'),'4 pooled');assert.equal(f.value('arp-sounding'),'2 sounding');
  assert.match(f.mount.querySelector('.cockpit-note-ribbon').getAttribute('aria-label'),/Combined Arp pool: 4.*all sounding output: 2/);
  f.cockpit.update({...data,pool:[],sounding:[]},{connected:true});
  assert.deepEqual(present('pool'),[]);assert.deepEqual(present('output'),[]);
});

test('the Arp pitch window fits sustained inputs and stays stable across sounding gates', t => {
  const f=fixture(t),data={...snapshot(),pool:[60,64,67],heldPitches:[]};
  f.cockpit.update({...data,sounding:[60]},{connected:true});
  assert.equal(f.value('arp-range'),'C3–B4');
  const cell=note=>f.mount.querySelector(`[data-cockpit-ribbon="pool-${note}"]`);
  assert.equal(cell(48).getAttribute('display'),'inline');assert.equal(cell(47).getAttribute('display'),'none');
  assert.ok(Number(cell(60).getAttribute('width'))>16,'Ordinary chord pitches are readable cells rather than two-pixel full-range slivers.');
  const x=cell(60).getAttribute('x'),width=cell(60).getAttribute('width');
  for(const sounding of [[64],[],[67],[60,64,67],[127]]) {
    f.cockpit.update({...data,sounding},{connected:true});
    assert.equal(f.value('arp-range'),'C3–B4');assert.equal(cell(60).getAttribute('x'),x);assert.equal(cell(60).getAttribute('width'),width);
  }
  assert.match(f.value('arp-sounding'),/1 outside range/);
  const labels=f.mount.querySelectorAll('.cockpit-ribbon-label').filter(node=>node.getAttribute('display')==='inline').map(node=>node.textContent);
  assert.ok(['C4','E4','G4'].every(name=>labels.includes(name)));
  f.cockpit.update({...data,heldPitches:[84],sounding:[84]},{connected:true});assert.equal(f.value('arp-range'),'C3–B6');
  f.cockpit.update({...data,pool:[0,127],sounding:[0,127]},{connected:true});assert.equal(f.value('arp-range'),'C−1–G9'.replace('−','-'));
  assert.ok(f.mount.querySelectorAll('.cockpit-ribbon-note').every(node=>node.getAttribute('display')==='inline'));
  f.cockpit.update({...data,pool:[],sounding:[]},{connected:true});assert.equal(f.value('arp-range'),'C-1–G9');
});

test('sequencer instruments distinguish queued, armed, recording and paused without a fabricated playhead', t => {
  const f=fixture(t),data={...snapshot(),engine:2,selectedTrack:20};
  const transport=f.mount.querySelector('.cockpit-transport-visual');
  f.cockpit.update({...data,transport:2,capture:2},{connected:true});
  assert.equal(f.value('seq-track'),'T21');assert.equal(f.value('seq-state'),'Paused');assert.equal(f.value('seq-capture'),'Queued');
  assert.equal(f.mount.querySelector('.cockpit-transport-pause').getAttribute('display'),'inline');
  assert.equal(f.mount.querySelector('.cockpit-transport-play').getAttribute('display'),'none');
  assert.equal(f.mount.querySelector('.cockpit-capture-dot').classList.contains('is-armed'),true);
  f.cockpit.update({...data,transport:1,capture:3},{connected:true});
  assert.equal(f.value('seq-capture'),'Recording');assert.equal(f.mount.querySelector('.cockpit-capture-dot').classList.contains('is-recording'),true);
  assert.match(transport.getAttribute('aria-label'),/T21.*Playing.*Recording/);
  f.cockpit.update({...data,transport:3,capture:99,selectedTrack:255,running:undefined},{connected:true});
  assert.equal(f.value('seq-track'),'—');assert.equal(f.value('seq-state'),'—');assert.equal(f.value('seq-capture'),'—');
  assert.doesNotMatch(f.mount.textContent,/playhead|step position/i);
});

test('Harmony compares the C-selected chord against captured context and retains only bounded supplied history', t => {
  const f=fixture(t),data=snapshot(),harmony={page:0,inputMask:145,inputLabel:'C MAJ',keyLabel:'C MAJOR',cursor:0,
    selectedLabel:'F MAJ',reason:'Predominant',rows:[{mask:545}],common:4095,added:4095,cacheReady:true,
    history:[{mask:145,root:0},{mask:580,root:2},{mask:2192,root:4},{mask:290,root:1},{mask:4095,root:11}]};
  const input={...data,engine:6,context:{harmony}};f.cockpit.update(input,{connected:true});
  assert.equal(f.mount.querySelector('.cockpit-contacts').hidden,true,'Harmony uses the redundant contact-map slot for its wider explanation.');
  assert.equal(f.value('harmony-played'),'C MAJ');assert.equal(f.value('harmony-next'),'F MAJ');assert.equal(f.value('harmony-reason'),'Predominant');
  assert.equal(f.value('harmony-home'),'C MAJOR');
  assert.equal(f.mount.querySelector('.cockpit-tone-map').getAttribute('aria-label'),'Shared: C. New: F A. Leaving: E G.');
  assert.equal(f.mount.querySelectorAll('.cockpit-history-item').filter(n=>!n.hidden).length,4);
  const historyDots=f.mount.querySelectorAll('.cockpit-history-item')[0].querySelectorAll('.cockpit-history-tone');
  assert.ok(Number(historyDots[7].getAttribute('cx'))>28&&Number(historyDots[7].getAttribute('cy'))<28,'G follows C clockwise in the fifths geometry.');
  assert.equal(f.mount.querySelectorAll('.cockpit-pitch-node.is-current').length,3);
  assert.equal(f.mount.querySelectorAll('.cockpit-pitch-node.is-next').length,3);
  const count=f.nodes();for(let n=0;n<250;n++)f.cockpit.update({...input,context:{harmony:{...harmony,revision:n}}},{connected:true});
  assert.equal(f.nodes(),count);
  f.cockpit.update({...data,engine:6,context:undefined},{connected:true});
  assert.equal(f.value('harmony-next'),'—');assert.equal(f.value('harmony-status'),'Context unavailable');
  assert.equal(f.mount.querySelectorAll('.cockpit-history-item').filter(n=>!n.hidden).length,0);
  assert.equal(f.mount.querySelector('.cockpit-tone-map').getAttribute('aria-label'),'Harmony tones unavailable');
  f.cockpit.update(data,{connected:true});
  assert.equal(f.mount.querySelector('.cockpit-contacts').hidden,false);
});

test('entry waits for a snapshot, motion pauses without restarting the session, and unchanged data performs no writes', t => {
  const f=fixture(t),data=snapshot();f.cockpit.update(null,{connected:true});
  assert.equal(f.mount.classList.contains('cockpit-active'),false);assert.equal(f.mount.dataset.cockpitSession,'0');
  f.cockpit.update(data,{connected:true});
  assert.equal(f.mount.classList.contains('cockpit-assembling'),true);
  f.mount.listeners.find(([type])=>type==='animationend')[1]({target:f.mount,animationName:'cockpit-assembly-lifetime'});
  assert.equal(f.mount.classList.contains('cockpit-assembling'),false);
  const session=f.mount.dataset.cockpitSession,writes=f.document.writes;
  for(let n=0;n<50;n++)f.cockpit.update(data,{connected:true});
  assert.equal(f.document.writes,writes,'Stable values do not rewrite text or SVG attributes.');
  assert.equal(f.mount.classList.contains('cockpit-assembling'),false,'Ordinary updates never replay the connection sequence.');
  f.cockpit.setActive(false);assert.equal(f.mount.dataset.cockpitMotion,'paused');assert.equal(f.mount.classList.contains('cockpit-motion-paused'),true);
  assert.equal(f.mount.classList.contains('cockpit-active'),true);f.cockpit.setActive(true);
  assert.equal(f.mount.classList.contains('cockpit-assembling'),false,'Returning to a visible page does not replay entry.');
  f.document.hidden=true;f.document.listeners.get('visibilitychange')();assert.equal(f.mount.dataset.cockpitMotion,'paused');
  f.document.hidden=false;f.document.listeners.get('visibilitychange')();assert.equal(f.mount.dataset.cockpitMotion,'running');
  assert.equal(f.mount.dataset.cockpitSession,session);f.cockpit.destroy();assert.equal(f.document.listeners.has('visibilitychange'),false);
});

test('diagnostic charts use real report fields, bound history and clear stale values', t => {
  const f=fixture(t), health=initializeDiagnostics(f.mount);
  assert.equal(health.report('status','STATUS voices=4'),false);
  health.snapshot({heldPads:2,generated:9},true);
  const meters=f.mount.querySelectorAll('meter');
  assert.equal(meters[0].value,2);assert.equal(meters[1].value,9);
  for(let i=0;i<100;i++)health.report('status',`STATUS uptime_ms=${i*1000} voices=${i%9} errors=0`);
  const chart=f.mount.querySelector('.diagnostics-trend'),line=chart.querySelector('polyline');
  assert.equal(line.getAttribute('points').split(' ').length,60);
  assert.match(chart.getAttribute('aria-label'),/60 samples/);
  assert.match(f.mount.querySelector('.diagnostics-unavailable').textContent,/does not expose/);
  assert.equal(health.report('io','STATUS voices=1'),false);
  health.report('display','DISPLAY last_render_us=450 last_duration_ms=12');
  assert.match(chart.getAttribute('aria-label'),/450 µs; 1 samples/);
  health.report('display','DISPLAY version=2');
  assert.equal(line.getAttribute('points'),'');
  assert.match(f.mount.querySelector('.diagnostics-metrics').textContent,/Unavailable/);
  health.snapshot({heldPads:100,generated:-1},true);assert.ok(meters.every(m=>m.hidden));
  health.reset();assert.equal(chart.getAttribute('aria-label'),'No report samples');
  health.snapshot(null,false);assert.equal(health.report('io','IO din_pending=9'),false);
  assert.equal(line.getAttribute('points'),'');
  health.destroy();assert.equal(f.mount.querySelector('.diagnostics-dashboard'),null);
});

test('restore diagnostics distinguish applied-but-unsaved settings from scanner failures',t=>{
  const f=fixture(t),health=initializeDiagnostics(f.mount);health.snapshot({heldPads:0,generated:0},true);
  assert.equal(health.report('backup status','BACKUP version=1 state=queued bytes=5726 received=5726 crc32=12345678 payload_crc32=8fd45367 generation=1'),true);
  assert.match(f.mount.querySelector('.diagnostics-metrics').textContent,/Transfer statequeued.*Payload checksum8fd45367/);
  assert.match(f.mount.querySelector('.diagnostics-chart-caption').textContent,/Settings were applied.*not verified/);
  health.report('settings','SETTINGS version=19 status=pending generation=1 saves=1 errors=0');
  assert.match(f.mount.querySelector('.diagnostics-metrics').textContent,/Save statepending/);
  health.report('pads','PADS frames=47 scans=752 errors=1');
  assert.match(f.mount.querySelector('.diagnostics-metrics').textContent,/Acquisition errors1/);
  assert.equal(health.report('backup status','STATUS state=queued'),false);
});


test('DIN output measures sent bytes even when queues stay empty and resets after reboot', t => {
  const f=fixture(t), health=initializeDiagnostics(f.mount);
  health.snapshot({heldPads:1,generated:0},true);
  const chart=f.mount.querySelector('.diagnostics-trend');
  health.report('io','IO uptime_ms=1000 din_tx=100 din_pending=0 usb_pending=0');
  assert.match(chart.getAttribute('aria-label'),/unavailable/);
  health.report('io','IO uptime_ms=2000 din_tx=160 din_pending=0 usb_pending=0');
  assert.match(chart.getAttribute('aria-label'),/DIN output: 60 bytes\/s/);
  assert.match(f.mount.querySelector('.diagnostics-metrics').textContent,/DIN transmitted160 bytes/);
  assert.match(f.mount.querySelector('.diagnostics-metrics').textContent,/DIN backlog0 events/);
  health.report('io','IO uptime_ms=500 din_tx=6 din_pending=0');
  assert.match(chart.getAttribute('aria-label'),/unavailable/);
  health.report('io','IO uptime_ms=1500 din_tx=18 din_pending=0');
  assert.match(chart.getAttribute('aria-label'),/DIN output: 12 bytes\/s/);
  health.snapshot(null,false);
  health.snapshot({heldPads:0,generated:0},true);
  health.report('io','IO uptime_ms=2500 din_tx=24');
  assert.match(chart.getAttribute('aria-label'),/unavailable/);
});

test('resource gauges require target values and valid timing or stack samples', t => {
  const f=fixture(t),health=initializeDiagnostics(f.mount);
  health.snapshot({heldPads:0,generated:0},true);
  const report='STATUS resources=1 resource_ms=1000 cpu_active=100 timing_valid=1 stack_valid=1 ram_used=102400 ram_total=131072 ccm_used=49152 ccm_total=65536 stack_used=3072 stack_total=8192 flash_used=409600 flash_total=524288';
  health.report('status',report);
  const meter=label=>f.mount.querySelector(`meter[aria-label="${label}"]`);
  assert.equal(meter('CPU active').value,100);assert.equal(meter('CPU active').hidden,false);
  assert.equal(meter('Observed stack peak').value,3072);
  assert.equal(meter('Firmware flash').max,524288);
  assert.match(f.mount.textContent,/20.0 KiB free/,'RAM free excludes the reserved 8 KiB stack');
  health.report('status',report.replace('timing_valid=1','timing_valid=0').replace('stack_valid=1','stack_valid=0'));
  assert.equal(meter('CPU active').hidden,true);assert.equal(meter('Observed stack peak').hidden,true);
  health.report('status','STATUS uptime_ms=3000 voices=0');
  assert.match(f.mount.textContent,/This firmware does not expose resources/);
  assert.equal(meter('Firmware flash').hidden,true);
  health.snapshot(null,false);assert.equal(health.report('status',report),false);
});


test('MPE member meters use queued per-voice data, captured physical mapping and fixed reusable rows',t=>{
  const f=fixture(t),sample=decodePerformanceSnapshot(performancePacket({voices:15,tracks:21}));
  sample.voices[0].pad=0;sample.voices[0].logicalPad=60;
  const data={...snapshot(),context:{performance:{supported:true,available:true,snapshot:sample}}};
  f.cockpit.update(data,{connected:true});
  assert.equal(f.mount.querySelectorAll('.cockpit-voice-row').length,15);assert.equal(f.mount.querySelectorAll('.cockpit-track-row').length,21);
  assert.equal(f.mount.querySelectorAll('.cockpit-voice-row').filter(node=>!node.hidden).length,15);
  assert.match(f.value('voice-state-0'),/Reserved/);assert.match(f.value('voice-state-3'),/Sustained/);
  assert.match(f.value('voice-route-0'),/^R13/,'Physical labels follow mapped position, not the electrical index.');
  assert.match(f.value('voice-values-14'),/CC1 112/);assert.match(f.mount.textContent,/accepted into the board’s MIDI queue/);
  assert.match(f.mount.querySelector('[data-cockpit-voice="0"]').getAttribute('aria-label'),/active references none.*Queued Pressure 0/);
  const count=f.nodes(),writes=f.document.writes;f.cockpit.update(data,{connected:true});assert.equal(f.nodes(),count);assert.equal(f.document.writes,writes);
  assert.equal(f.hardwareCalls(),0);
});

test('sequencer playheads follow only reported positions across loop jumps, pauses, mute and clock loss',t=>{
  const f=fixture(t),sample=decodePerformanceSnapshot(performancePacket({tracks:21})),data={...snapshot(),context:{performance:{supported:true,available:true,snapshot:sample}}};
  f.cockpit.update(data,{connected:true});assert.equal(f.mount.querySelectorAll('.cockpit-track-row').filter(node=>!node.hidden).length,21);
  assert.equal(f.value('track-step-20'),'Step 11 / 16');assert.match(f.value('track-state-1'),/Muted/);
  const row=f.mount.querySelector('[data-cockpit-track="1"]'),needle=row.querySelector('.cockpit-playhead-needle'),before=needle.getAttribute('d');
  sample.transport=2;f.cockpit.update(data,{connected:true});assert.match(f.value('track-state-1'),/^Paused/);assert.equal(needle.getAttribute('d'),before);
  sample.transport=1;sample.suspended=true;sample.clockReady=false;sample.clockStatus=3;f.cockpit.update(data,{connected:true});
  assert.match(f.value('track-state-1'),/^Clock lost/);assert.equal(needle.getAttribute('d'),before);
  sample.suspended=false;sample.clockReady=true;sample.tracks[1].playhead=sample.tracks[1].loopStart;sample.tracks[1].generation++;
  f.cockpit.update(data,{connected:true});assert.equal(needle.getAttribute('d'),'M1.00 1V15');assert.equal(f.value('track-step-1'),'Step 1 / 16');
  assert.match(f.value('track-state-0'),/^Stopped/,'A stopped individual track does not become playing with the global transport.');
});

test('unsupported, stale and disconnected performance detail never leaves old meters presented as live',t=>{
  const f=fixture(t),sample=decodePerformanceSnapshot(performancePacket({voices:1,tracks:1})),base=snapshot();
  f.cockpit.update({...base,context:{performance:{supported:true,available:true,snapshot:sample}}},{connected:true});
  assert.equal(f.mount.querySelector('[data-cockpit-voice="0"]').hidden,false);
  for(const context of [{supported:true,available:false,stale:true,snapshot:sample},{supported:false,available:false}]){
    f.cockpit.update({...base,context:{performance:context}},{connected:true});
    assert.equal(f.mount.querySelector('[data-cockpit-voice="0"]').hidden,true);assert.equal(f.mount.querySelector('[data-cockpit-track="0"]').hidden,true);
    assert.match(f.value('voice-detail-status'),context.stale?/fresh data/:/Update firmware/);
  }
  f.cockpit.update(null);assert.equal(f.mount.querySelector('[data-cockpit-voice="0"]').hidden,true);
});

test('reusing a member channel replaces its gesture, pitch, owner and queued values without an old voice trail',t=>{
  const f=fixture(t),sample=decodePerformanceSnapshot(performancePacket({voices:1})),data={...snapshot(),context:{performance:{supported:true,available:true,snapshot:sample}}};
  f.cockpit.update(data,{connected:true});const row=f.mount.querySelector('[data-cockpit-voice="0"]'),count=f.nodes();
  Object.assign(sample.voices[0],{gesture:999,note:72,logicalPad:95,pad:20,pressure:80,upper:63,bend:7000,pendingAttack:false});
  f.cockpit.update(data,{connected:true});assert.equal(f.nodes(),count);assert.equal(f.value('voice-name-0'),'C5 · Ch 2');assert.match(f.value('voice-route-0'),/^R48/);
  assert.match(f.value('voice-values-0'),/Pressure 80 · Upper 63 · Bend 7000/);assert.match(row.getAttribute('aria-label'),/Gesture 999/);assert.doesNotMatch(row.getAttribute('aria-label'),/C4|Gesture 100\./);
});
