import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { createUpdateBoardArt, createUpdateLogoArt, createUpdateIcon, createUpdateProgressArt } from '../../web/updater-art.js';

class Element {
  constructor(doc, tag, namespace = null) { this.ownerDocument = doc; this.tagName = tag; this.namespaceURI = namespace; this.attributes = {}; this.dataset = {}; this.children = []; this._text = ''; }
  setAttribute(name, value) { this.attributes[name] = String(value); if (name.startsWith('data-')) this.dataset[name.slice(5).replace(/-([a-z])/g, (_, char) => char.toUpperCase())] = String(value); }
  getAttribute(name) { return this.attributes[name] ?? null; }
  set className(value) { this.attributes.class = String(value); }
  get className() { return this.attributes.class || ''; }
  set textContent(value) { this._text = String(value); this.children = []; }
  get textContent() { return this._text + this.children.map(child => child.textContent).join(''); }
  set innerHTML(_) { throw new Error('Artwork must use DOM nodes rather than HTML injection.'); }
  append(...nodes) { this.children.push(...nodes); }
  addEventListener() { throw new Error('Presentation-only artwork must not register interactions.'); }
}
function document({ startup, header } = {}) {
  const doc = { createElement(tag) { return new Element(doc, tag); }, createElementNS(namespace, tag) { return new Element(doc, tag, namespace); },
    getElementById(id) { assert.equal(id, 'startup-brand'); return startup === undefined ? null : { getAttribute(name) { assert.equal(name, 'href'); return startup; } }; },
    querySelector(selector) { assert.equal(selector, '.companion-brand-logo'); return header === undefined ? null : { getAttribute(name) { assert.equal(name, 'src'); return header; } }; } };
  Object.defineProperty(doc, 'defaultView', { get() { throw new Error('Artwork has no reason to access device APIs or a browser connection.'); } });
  return doc;
}
function all(root) { return [root, ...root.children.flatMap(all)]; }
function classNodes(root, name) { return all(root).filter(node => node.className.split(/\s+/).includes(name)); }
function dataNodes(root, key) { return all(root).filter(node => Object.hasOwn(node.dataset, key)); }

test('board illustration preserves both physical 4 by 12 pad clusters and all 43 distinct small controls', () => {
  const root = createUpdateBoardArt(document()), pads = dataNodes(root, 'pad'), controls = dataNodes(root, 'control');
  assert.equal(pads.length, 96); assert.equal(new Set(pads.map(node => node.dataset.pad)).size, 96);
  assert.deepEqual(pads.map(node => Number(node.dataset.pad)).sort((a, b) => a - b), Array.from({ length: 96 }, (_, i) => i));
  assert.equal(new Set(pads.map(node => node.dataset.visualPad)).size, 96);
  for (const [side, name] of [[0, 'left'], [1, 'right']]) {
    const cluster = pads.filter(node => node.dataset.side === name); assert.equal(cluster.length, 48);
    assert.deepEqual([...new Set(cluster.map(node => Number(node.dataset.row)))].sort(), [0, 1, 2, 3]);
    assert.equal(new Set(cluster.map(node => node.dataset.column)).size, 12);
    assert.equal(new Set(cluster.map(node => node.getAttribute('x'))).size, 12);
    assert.equal(new Set(cluster.map(node => node.getAttribute('y'))).size, 4);
    assert.equal(new Set(cluster.map(node => `${node.getAttribute('x')},${node.getAttribute('y')}`)).size, 48);
    for (const pad of cluster) {
      const row = Number(pad.dataset.row), column = Number(pad.dataset.column);
      assert.equal(Number(pad.dataset.pad), side * 48 + column * 4 + row, 'Pad IDs follow the maintained column-major physical map.');
      assert.equal(Number(pad.dataset.visualPad), side * 48 + row * 12 + column);
    }
  }
  assert.equal(controls.length, 43); assert.equal(new Set(controls.map(node => node.dataset.control)).size, 43);
  assert.equal(controls.filter(node => node.dataset.control.startsWith('upper-')).length, 24);
  assert.equal(controls.filter(node => node.dataset.control.startsWith('navigation-')).length, 5);
  assert.equal(controls.filter(node => node.dataset.control.startsWith('function-')).length, 14);
});
test('only the top-left and bottom-left large LEFT chord pads are marked for the physical update gesture', () => {
  const root = createUpdateBoardArt(document()), pads = dataNodes(root, 'pad'), held = pads.filter(node => node.dataset.held === 'true');
  assert.deepEqual(held.map(node => Number(node.dataset.pad)).sort(), [0, 3]);
  assert.deepEqual(held.map(node => Number(node.dataset.visualPad)).sort((a, b) => a - b), [0, 36]);
  assert.ok(held.every(node => node.dataset.side === 'left' && node.dataset.column === '0'));
  assert.deepEqual(held.map(node => Number(node.dataset.row)).sort(), [0, 3]);
  const left = pads.filter(node => node.dataset.side === 'left');
  const leftmost = Math.min(...left.map(node => Number(node.getAttribute('x'))));
  const top = Math.min(...left.map(node => Number(node.getAttribute('y')))), bottom = Math.max(...left.map(node => Number(node.getAttribute('y'))));
  assert.ok(held.every(node => Number(node.getAttribute('x')) === leftmost));
  assert.deepEqual(held.map(node => Number(node.getAttribute('y'))).sort((a, b) => a - b), [top, bottom]);
  assert.equal(classNodes(root, 'update-board-pad-held').length, 2);
  assert.equal(classNodes(root, 'update-board-hold-mark').length, 2);
  assert.match(root.textContent, /top-left and bottom-left large pads of the LEFT chord cluster/i);
  assert.match(root.textContent, /not live inputs|not sensed pad telemetry/i);
  assert.equal(root.dataset.state, 'waiting');
});
test('board and tiled reveal reuse the existing logo URL including an embedded standalone-bundle source', () => {
  for (const settings of [{ startup: 'data:image/svg+xml;base64,c3ludGhldGlj', header: './header-fallback.svg' }, { header: './current-project-logo.svg' }, {}]) {
    const doc = document(settings), expected = settings.startup || settings.header || './assets/thy666-web-logo.svg';
    for (const art of [createUpdateBoardArt(doc), createUpdateLogoArt(doc)]) {
      const images = all(art).filter(node => node.tagName === 'image'); assert.equal(images.length, 1);
      assert.equal(images[0].getAttribute('href'), expected);
    }
  }
});
test('logo reveal uses fourteen clipped uses plus the original center and has unique local references', () => {
  const first = createUpdateLogoArt(document()), second = createUpdateLogoArt(document());
  for (const root of [first, second]) {
    const tiles = classNodes(root, 'update-logo-tile'), center = classNodes(root, 'update-logo-center');
    assert.equal(tiles.length, 14); assert.equal(center.length, 1);
    const sources = all(root).filter(node => node.tagName === 'image'); assert.equal(sources.length, 1);
    const sourceID = sources[0].getAttribute('id');
    const clips = all(root).filter(node => node.tagName === 'clipPath'); assert.equal(clips.length, 14);
    const clipIDs = new Set(clips.map(node => node.getAttribute('id')));
    for (const tile of tiles) {
      assert.equal(tile.children.length, 1); const use = tile.children[0]; assert.equal(use.tagName, 'use');
      assert.equal(use.getAttribute('href'), `#${sourceID}`);
      const referenced = /^url\(#(.+)\)$/.exec(use.getAttribute('clip-path')); assert.ok(referenced && clipIDs.has(referenced[1]));
    }
    assert.equal(center[0].children[0].getAttribute('href'), `#${sourceID}`);
    const maskID = /^url\(#(.+)\)$/.exec(center[0].children[0].getAttribute('mask'))[1];
    assert.ok(all(root).some(node => node.tagName === 'mask' && node.getAttribute('id') === maskID));
    assert.equal(root.dataset.state, 'waiting');
  }
  const ids = [...all(first), ...all(second)].map(node => node.getAttribute('id')).filter(Boolean);
  assert.equal(new Set(ids).size, ids.length, 'Multiple art instances cannot alias each other’s SVG source or clip IDs.');
});
test('all SVG art is labelled or decorative and registers no interaction or hardware behavior', () => {
  const doc = document(), roots = [createUpdateBoardArt(doc), createUpdateLogoArt(doc), createUpdateProgressArt(doc), createUpdateIcon(doc, 'verify')];
  for (const root of roots) for (const node of all(root)) {
    assert.ok(!['button', 'input', 'a', 'form'].includes(node.tagName));
    assert.equal(Object.keys(node.attributes).some(name => /^on/i.test(name)), false);
    assert.equal(node.getAttribute('tabindex'), null);
    if (node.tagName === 'svg') {
      assert.equal(node.getAttribute('focusable'), 'false');
      assert.ok(node.getAttribute('aria-hidden') === 'true' || node.getAttribute('aria-label') || node.getAttribute('aria-labelledby'));
    }
  }
  const source = fs.readFileSync(new URL('../../web/updater-art.js', import.meta.url), 'utf8');
  assert.doesNotMatch(source, /\b(?:fetch|WebSocket|XMLHttpRequest)\s*\(|navigator\s*\.\s*(?:serial|hid|usb)|\bimport\s.*(?:device-client|transport|firmware)/);
});
test('progress begins with exactly thirty pending segments and unsupported icon/document requests fail clearly', () => {
  const progress = createUpdateProgressArt(document()), segments = dataNodes(progress, 'segment');
  assert.equal(segments.length, 30); assert.deepEqual(segments.map(node => Number(node.dataset.segment)), Array.from({ length: 30 }, (_, i) => i));
  assert.ok(segments.every(node => node.dataset.state === 'pending'));
  assert.throws(() => createUpdateIcon(document(), 'unknown'), RangeError);
  for (const create of [createUpdateBoardArt, createUpdateLogoArt, createUpdateProgressArt]) assert.throws(() => create({}), TypeError);
});
