import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {initializeReferencePicker} from '../../web/design-system.js';
import {UI_ICON_NAMES} from '../../web/ui-system.js';

const read = path => readFileSync(new URL(`../../${path}`, import.meta.url), 'utf8');
const html = read('web/design-system.html'), reference = read('web/design-system.js');
const contractText = html.match(/<script type="application\/json" id="component-contract">([\s\S]*?)<\/script>/)?.[1];
assert.ok(contractText, 'The component reference must publish its inventory.');
const contract = JSON.parse(contractText), examples = html.replace(contractText, '') + reference;

test('every shared component and public helper belongs to the documented production inventory', () => {
  assert.equal(contract.version, 1);
  const css = read('web/ui-system.css'), helper = read('web/ui-system.js');
  const registered = new Set([...contract.families.flatMap(family => family.selectors), ...contract.internalSelectors]);
  const styled = new Set([...css.matchAll(/\.(hud-[\w-]+)/g)].map(match => `.${match[1]}`));
  for (const selector of styled) assert.ok(registered.has(selector), `${selector}: add the family or declare its generated decoration owner.`);
  const exported = [...helper.matchAll(/^export\s+(?:function|const|let|class)\s+(\w+)/gm)].map(match => match[1]);
  assert.deepEqual([...contract.exports].sort(), exported.sort(), 'Public helper changes require an inventory update.');
  assert.equal(new Set(contract.families.map(family => family.id)).size, contract.families.length);
  const gaps = [];
  for (const family of contract.families) {
    assert.ok(html.includes(`id="${family.example}"`), `${family.id}: missing linked example.`);
    assert.ok(family.production.length, `${family.id}: missing production owner.`);
    const production = family.production.map(read).join('\n');
    for (const factory of family.factories || []) {
      if (!reference.includes(`${factory}(`)) gaps.push(`${family.id}: ${factory} is not exercised by the reference.`);
      if (!production.includes(`${factory}(`)) gaps.push(`${family.id}: ${factory} is not used by its production owners.`);
    }
    for (const selector of family.selectors) {
      const name = selector.slice(1);
      if (!examples.includes(name)) gaps.push(`${family.id}: ${selector} is listed but not demonstrated.`);
      if (!production.includes(name)) gaps.push(`${family.id}: ${selector} has no use in its production owners.`);
      if (!css.includes(selector)) gaps.push(`${family.id}: ${selector} has no shared style.`);
    }
  }
  for (const exception of contract.specialized) {
    assert.ok(exception.reason.length > 30);
    const production = exception.production.map(read).join('\n');
    for (const selector of exception.selectors) assert.ok(production.includes(selector.slice(1)), `${exception.title}: stale production selector ${selector}`);
  }
  assert.deepEqual(gaps, [], 'Library coverage gaps:');
});

test('reference examples include accessible combined states, trace origins and all current glyphs', () => {
  for (const origin of ['top','bottom','corner','left']) assert.ok(html.includes(`data-trace-origin="${origin}"`));
  assert.match(html, /<button[^>]*aria-pressed="true"[^>]*disabled>/);
  assert.match(html, /<button[^>]*aria-busy="true"[^>]*disabled>/);
  assert.match(html, /<button[^>]*aria-pressed="true"[^>]*data-trace-origin="left"[^>]*data-state-probe/);
  assert.equal([...html.matchAll(/class="hud-notice hud-notice--banner(?: reference-banner-wrapped)?"/g)].length, 2, 'The compact banner has single-line and wrapped examples.');
  for (const [, attributes] of html.matchAll(/<button([^>]*data-nav-choice="[^"]+"[^>]*)>/g)) assert.ok(attributes.includes('data-trace-origin="bottom"'), 'Main navigation mirrors the production bottom-origin choice.');
  for (const [, attributes] of html.matchAll(/<button([^>]*data-(?:sidebar-choice|task)="[^"]+"[^>]*)>/g)) assert.ok(attributes.includes('data-trace-origin="left"'), 'Sidebar and backup choices mirror the production leading edge.');
  assert.match(html, /<input class="hud-toggle" type="checkbox" role="switch"/);
  assert.match(html, /<input class="hud-checkbox" type="checkbox"/);
  assert.match(html, /class="hud-scroll-region[^>]*role="region"[^>]*aria-label="[^"]+"[^>]*tabindex="0"/);
  assert.match(reference, /for\s*\(const name of UI_ICON_NAMES\)/, 'The glyph catalog follows the actual registry.');
  for (const [, name] of html.matchAll(/data-(?:demo-)?icon="([^"]+)"/g)) assert.ok(UI_ICON_NAMES.includes(name), name);
  assert.doesNotMatch(reference, /\b(?:setInterval|setTimeout|fetch|WebSocket)\s*\(/, 'The reference must not simulate telemetry or start background traffic.');
});

class Node {
  constructor(tag = 'div', doc = null) { this.tagName = tag;this.ownerDocument = doc;this.children = [];this.attrs = new Map();this.listeners = new Map();this.dataset = {};this.className = '';this.hidden = false; }
  append(...nodes) { for (const node of nodes) { node.parentNode = this;this.children.push(node); } }
  setAttribute(name, value) { this.attrs.set(name, String(value)); }
  getAttribute(name) { return this.attrs.get(name) ?? null; }
  addEventListener(type, callback) { if (!this.listeners.has(type)) this.listeners.set(type, new Set());this.listeners.get(type).add(callback); }
  removeEventListener(type, callback) { this.listeners.get(type)?.delete(callback); }
  focus() { this.ownerDocument.activeElement = this; }
  contains(node) { return node === this || this.children.some(child => child.contains(node)); }
  querySelector(selector) {
    const match = node => selector === 'button' ? node.tagName === 'button' : selector === '[role="listbox"]' ? node.getAttribute('role') === 'listbox' : selector.startsWith('.') ? node.className.split(/\s+/).includes(selector.slice(1)) : selector === '[data-picker-label]' && node.attrs.has('data-picker-label');
    for (const child of this.children) { if (match(child)) return child;const found = child.querySelector(selector);if (found) return found; }return null;
  }
  fire(type, extra = {}) { const event = {type, target: this, prevented: false, preventDefault() { this.prevented = true; }, ...extra};for (const callback of this.listeners.get(type) || []) callback(event);return event; }
}
function pickerFixture() {
  const doc = new Node('document');doc.createElement = tag => new Node(tag, doc);
  const root = doc.createElement('div'), trigger = doc.createElement('button'), list = doc.createElement('div'), label = doc.createElement('span'), swatch = doc.createElement('span');
  list.setAttribute('role', 'listbox');list.hidden = true;label.setAttribute('data-picker-label', '');swatch.className = 'theme-swatch';trigger.append(swatch, label);root.append(trigger, list);doc.append(root);
  const applied = [], picker = initializeReferencePicker({root, choices: [{value: 'red', label: 'Red'}, {value: 'cyan', label: 'Cyan'}, {value: 'pink', label: 'Pink'}], onChange: value => applied.push(value)});
  return {doc, root, trigger, list, label, swatch, picker, applied};
}

test('reference accent picker supports keyboard commit, cancellation and outside dismissal without device access', () => {
  const f = pickerFixture();
  assert.deepEqual(f.list.children.map(option => option.querySelector('.app-accent-check').hidden), [false, true, true]);
  f.trigger.fire('click');assert.equal(f.trigger.getAttribute('aria-expanded'), 'true');assert.equal(f.doc.activeElement, f.list.children[0]);
  f.root.fire('keydown', {key: 'End'});assert.equal(f.doc.activeElement, f.list.children[2]);
  f.root.fire('keydown', {key: 'Enter'});assert.equal(f.picker.value, 'pink');assert.deepEqual(f.applied, ['pink']);assert.equal(f.list.hidden, true);assert.equal(f.doc.activeElement, f.trigger);assert.equal(f.label.textContent, 'Pink');assert.equal(f.swatch.dataset.accent, 'pink');
  assert.deepEqual(f.list.children.map(option => option.querySelector('.app-accent-check').hidden), [true, true, false]);
  f.trigger.fire('click');f.root.fire('keydown', {key: 'Home'});f.root.fire('keydown', {key: 'Escape'});assert.equal(f.picker.value, 'pink');assert.deepEqual(f.applied, ['pink']);
  f.trigger.fire('click');const tab = f.root.fire('keydown', {key: 'Tab'});assert.equal(tab.prevented, false);assert.equal(f.list.hidden, true);
  f.trigger.fire('click');const outside = f.doc.createElement('button');outside.focus();f.doc.fire('click', {target: outside});assert.equal(f.doc.activeElement, outside);assert.equal(f.list.hidden, true);
  assert.equal(f.picker.setValue('unknown'), false);assert.equal(f.picker.setValue('cyan'), true);assert.equal(f.picker.value, 'cyan');assert.deepEqual(f.applied, ['pink']);
  f.picker.destroy();assert.equal([...f.doc.listeners.values()].reduce((count, listeners) => count + listeners.size, 0), 0);
  f.trigger.fire('click');assert.equal(f.list.hidden, true);
});
