import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {initializeDesktopReference} from '../../web/desktop-reference.js';
import {DESKTOP_PROTOCOL} from '../../web/desktop-runtime.js';

class Element {
  constructor(tag) { this.tagName = tag.toUpperCase();this.className = '';this.children = [];this.parentNode = null;this.classList = {contains: name => this.className.split(/\s+/).includes(name)}; }
  append(...nodes) { for (const node of nodes) { node.remove();node.parentNode = this;this.children.push(node); } }
  remove() { if (this.parentNode) this.parentNode.children = this.parentNode.children.filter(node => node !== this);this.parentNode = null; }
  before(...nodes) { const parent = this.parentNode;for (const node of nodes) { node.remove();node.parentNode = parent;parent.children.splice(parent.children.indexOf(this), 0, node); } }
  querySelector(selector) { for (const child of this.children) { if (child.classList.contains(selector.slice(1))) return child;const found = child.querySelector(selector);if (found) return found; }return null; }
}
function fixture() {
  const doc = {head: new Element('head'), body: new Element('body'), createElement: tag => new Element(tag)};
  doc.body.className = 'design-reference';doc.querySelector = selector => doc.body.querySelector(selector);
  const header = new Element('header'), main = new Element('main'), footer = new Element('footer'), script = new Element('script');doc.body.append(header, main, footer, script);
  const calls = [], scope = {document: doc, __TAURI__: {core: {async invoke(command) { calls.push(command);if (command === 'desktop_info') return {protocol: DESKTOP_PROTOCOL,version: '0.2.1',simulation: false,platform: 'macos'};throw new Error(`Unexpected native action: ${command}`); }}}};
  return {doc, scope, calls, content: [header, main, footer], script};
}

test('browser reference never initializes native presentation', async () => {
  const f = fixture();let called = false;
  assert.equal(await initializeDesktopReference({scope: {document: f.doc},connect() { called = true; }}), null);
  assert.equal(called, false);assert.equal(f.doc.head.children.length, 0);assert.deepEqual(f.doc.body.children, [...f.content, f.script]);
  f.doc.body.className = 'client';
  assert.equal(await initializeDesktopReference({scope: f.scope,connect() { called = true; }}), null);
  assert.equal(called, false);
});

test('native reference mounts shared window controls without discovering or opening hardware', async () => {
  const f = fixture(), chrome = {destroy() {}};let mounted;
  const result = await initializeDesktopReference({scope: f.scope,mountChrome(options) { mounted = options;return chrome; }});
  assert.equal(result, chrome);assert.equal(mounted.runtime.kind, 'desktop');assert.equal(mounted.runtime.platform, 'macos');assert.equal(mounted.document, f.doc);
  assert.deepEqual(f.calls, ['desktop_info']);
  const shell = f.doc.querySelector('.app-shell');assert.deepEqual(shell.children, f.content);assert.equal(f.script.parentNode, f.doc.body);
  assert.deepEqual(f.doc.head.children.map(link => [link.rel,link.href]), [['stylesheet','./desktop-chrome.css'],['stylesheet','./desktop-reference.css']]);
});

test('native frame setup failure leaves the read-only content usable', async () => {
  const f = fixture();
  await assert.rejects(initializeDesktopReference({scope: f.scope,mountChrome() { throw new Error('frame unavailable'); }}), /frame unavailable/);
  assert.equal(f.doc.querySelector('.app-shell'), null);assert.deepEqual(f.doc.body.children, [f.script, ...f.content]);assert.equal(f.doc.head.children.length, 0);
  assert.deepEqual(f.calls, ['desktop_info']);
});

test('shared browser reference has no native bootstrap and the staged adapter gates initialization', () => {
  const entry = readFileSync(new URL('../../web/design-system.js', import.meta.url), 'utf8');
  assert.doesNotMatch(entry, /desktop-reference|desktop-runtime|__TAURI__/);
  const native = readFileSync(new URL('../../web/desktop-reference.js', import.meta.url), 'utf8');
  assert.match(native, /if \(globalThis\.window\?\.__TAURI__\?\.core\?\.invoke\) \{\s*void initializeDesktopReference\(\)/);
});

test('native reference frame inherits the selected HUD palette without a second color scheme', () => {
  const css = readFileSync(new URL('../../web/desktop-reference.css', import.meta.url), 'utf8');
  assert.match(css, /html\.desktop-window \.design-reference > :is\(\.desktop-window-background,\.desktop-window-chrome,\.desktop-content-viewport\)/);
  for (const [native,hud] of [['ui-paper','hud-paper'],['ui-ink','hud-ink'],['ui-muted','hud-muted'],['theme-bright','hud-accent'],['theme-edge','hud-edge']]) {
    assert.ok(css.includes(`--${native}:var(--${hud})`));
  }
  assert.doesNotMatch(css, /#[0-9a-f]{3,8}\b|\b(?:rgb|hsl|color-mix)\s*\(/i);
  const chrome = readFileSync(new URL('../../web/desktop-chrome.js', import.meta.url), 'utf8');
  assert.match(chrome, /make\('div','desktop-content-viewport'\)/);
  assert.match(chrome, /shell=doc\?\.querySelector\?\.\('\.app-shell'\)/);
});
