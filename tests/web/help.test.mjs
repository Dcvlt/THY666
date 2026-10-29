import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { MENU_ROWS } from '../../web/studio-menu.js';
import {
  HELP_GROUPS, HELP_TOPICS, HELP_FIGURES, HELP_SCREEN_FRAMES, resolveHelpTopic, getHelpTopic,
  getHelpContext, getHelpReference, searchHelpTopics,
} from '../../web/help-topics.js';
import { initializeHelp } from '../../web/help.js';

test('the guide has substantial workflows, valid cross-links and a home for each live menu control', () => {
  const ids = new Set(HELP_TOPICS.map(topic => topic.id));
  const groups = new Set(HELP_GROUPS.map(group => group.id));
  assert.equal(ids.size, HELP_TOPICS.length);
  assert.ok(ids.size >= 25);
  for (const topic of HELP_TOPICS) {
    assert.ok(groups.has(topic.group), topic.id);
    assert.ok(topic.summary.length > 45, topic.id);
    assert.ok(topic.sections.length >= 2, topic.id);
    assert.ok(JSON.stringify(topic.sections).length > 500, topic.id);
    for (const related of topic.related) assert.ok(ids.has(related), `${topic.id}: ${related}`);
    for (const section of topic.sections) {
      assert.ok(section.title, topic.id);
      if (section.table) for (const row of section.table.rows)
        assert.equal(row.length, section.table.columns.length, `${topic.id}: ${section.title}`);
    }
  }
  for (const row of MENU_ROWS.filter(row => row.status === 'live' && !row.retired)) {
    assert.notEqual(resolveHelpTopic(row.id), 'getting-started', row.id);
    assert.ok(getHelpReference(row.id).some(item => item.id === row.id), row.id);
  }
  const productText = JSON.stringify(HELP_TOPICS) + JSON.stringify(HELP_TOPICS.flatMap(topic => getHelpReference(topic.id)));
  assert.ok(productText.includes("THY666"));
});

test('Quick start teaches the default physical flow in three visible parts with working guide links', () => {
  const item = getHelpTopic('getting-started');
  assert.equal(item.title, 'Quick start');
  assert.equal(item.sections.length, 3);
  const map = item.sections[0].figures[0];
  assert.equal(map.kind, 'screen');
  assert.equal(map.label, HELP_SCREEN_FRAMES['quickstart-play'].label);
  assert.deepEqual(map.navigation.map(({ control }) => control), ['LEFT', 'UP', 'CENTER', 'DOWN', 'RIGHT']);
  for (const shortcut of map.navigation.filter(item => item.control !== 'CENTER'))
    assert.equal(shortcut.destination.toLowerCase(), MENU_ROWS.find(row => row.id === `controls.${shortcut.control.toLowerCase()}`).default);
  assert.equal(map.navigation.find(item => item.control === 'CENTER').destination, 'Settings');
  assert.match(JSON.stringify(item.sections[0].steps), /RIGHT again applies/);
  assert.match(JSON.stringify(item.sections[0].steps), /discards any unapplied draft/);
  assert.match(JSON.stringify(item.sections[1]), /Arp keeps running/);
  assert.match(JSON.stringify(item.sections[1]), /seven lower diamonds add octave layers/i);
  const ids = new Set(HELP_TOPICS.map(topic => topic.id));
  const domains = item.sections[2].table.rows;
  assert.deepEqual(domains.map(row => row[0]), ['Engines', 'Performance', 'Chords', 'Arps', 'Expression', 'Sensitivity', 'MIDI', 'Lights', 'Views', 'Controls', 'Profiles', 'System']);
  for (const row of domains) assert.ok(ids.has(row[2].topic), row[0]);
  const f = fixture();
  assert.equal(f.all().filter(node => node.className === 'thy-help-section' && node.tagName === 'SECTION').length, 3);
  assert.ok(f.byClass('thy-help-nav-keys'));
  const midi = f.all().find(node => node.tagName === 'A' && node.dataset.helpTopic === 'midi-routing');
  assert.ok(midi); const event = f.fire(midi, 'click');
  assert.equal(event.defaultPrevented, true);
  assert.equal(f.help.selected, 'midi-routing');
  assert.deepEqual(f.navigations, ['midi-routing']);
  assert.equal(f.all().some(node => node.tagName === 'IFRAME' || node.tagName === 'CANVAS'), false);
  f.help.destroy();
});

test('context links distinguish scopes, views, track settings and expression applications', () => {
  for (const [context, expected] of [
    ['performance.routeTarget', 'aftertouch'], ['performance.pressureMode', 'aftertouch'],
    ['performance.velocityTarget', 'velocity'], ['performance.mpe', 'mpe'],
    ['sensitivity.velocityMode', 'velocity'], ['sensitivity.profileSplit', 'aftertouch'],
    ['sensitivity.calibrate', 'calibration'], ['sequencer.channel', 'track-settings'],
    ['sequencer.source', 'sequencer-recording'], ['steps', 'sequencer-editor'],
    ['lights.view', 'views'], ['system.storage', 'storage'],
    ['chords.editChords', 'custom-library'], ['storage.exportProfile', 'backup'],
  ]) assert.equal(resolveHelpTopic(context), expected, context);
  assert.equal(getHelpTopic('not-a-setting').id, 'getting-started');
  for (const id of ['constructor', '__proto__', 'toString', 'hasOwnProperty', '<img onerror=alert(1)>'])
    assert.equal(getHelpTopic(id).id, 'getting-started', id);
  const pressure = getHelpContext('performance.pressureMode');
  assert.equal(pressure.title, MENU_ROWS.find(row => row.id === 'performance.pressureMode').label);
  assert.equal(pressure.options.length, 3);
  assert.equal(pressure.unavailable, false);
  assert.equal(getHelpContext('engines.drums').unavailable, true);
});

test('search finds authored workflows and canonical setting references without interpreting markup', () => {
  for (const [query, id] of [
    ['track channel', 'track-settings'], ['soft firm', 'calibration'],
    ['CC74', 'aftertouch'], ['512', 'storage'], ['M4', 'sequencer-editor'],
    ['Osmose', 'mpe'], ['Response for', 'aftertouch'], ['rollback', 'updates'],
  ]) assert.ok(searchHelpTopics(query).some(topic => topic.id === id), query);
  assert.deepEqual(searchHelpTopics('  '), HELP_TOPICS);
  assert.deepEqual(searchHelpTopics('<img src=x onerror=alert(1)>'), []);
  assert.deepEqual(searchHelpTopics('nonexistent-phrase-739'), []);
  assert.equal(searchHelpTopics('CALIBRATION')[0].id, 'calibration');
});

class FakeElement {
  constructor(document, tag) {
    this.ownerDocument = document; this.tagName = tag.toUpperCase(); this.children = [];
    this.dataset = {}; this.attributes = {}; this.listeners = new Map();
    this.value = ''; this.parentNode = null; this._text = ''; this.disabled = false;
  }
  set textContent(value) { this._text = String(value); this.children = []; }
  get textContent() { return this._text + this.children.map(node => node.textContent).join(''); }
  set innerHTML(_) { throw new Error('Help must never use innerHTML.'); }
  setAttribute(key, value) { this.attributes[key] = String(value); }
  getAttribute(key) { return this.attributes[key] ?? null; }
  get classList() { return {
    remove: name => { this.className = this.className.split(/\s+/).filter(value => value !== name).join(' '); },
    toggle: (name, enabled) => { const names = new Set(this.className.split(/\s+/).filter(Boolean)); if(enabled) names.add(name); else names.delete(name); this.className = [...names].join(' '); },
  }; }
  append(...children) {
    for (const child of children) {
      child.remove(); child.parentNode = this; this.children.push(child);
    }
  }
  replaceChildren(...children) {
    for (const child of this.children) child.parentNode = null;
    this.children = []; this._text = ''; this.append(...children);
  }
  remove() {
    if (this.parentNode) this.parentNode.children = this.parentNode.children.filter(node => node !== this);
    this.parentNode = null;
  }
  contains(node) { return node === this || this.children.some(child => child.contains(node)); }
  querySelector(selector) {
    const visit=node=>node.children.flatMap(child=>[child,...visit(child)]);
    return visit(this).find(node=>selector===`#${node.id}`)||null;
  }
  closest(selector) {
    const match = /^\[data-([a-z-]+)\]$/.exec(selector);
    if (!match) return null;
    const key = match[1].replace(/-([a-z])/g, (_, value) => value.toUpperCase());
    for (let node = this; node; node = node.parentNode) if (Object.hasOwn(node.dataset || {}, key)) return node;
    return null;
  }
  addEventListener(type, handler, capture = false) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push({ handler, capture: Boolean(capture) });
  }
  removeEventListener(type, handler, capture = false) {
    this.listeners.set(type, (this.listeners.get(type) || []).filter(value => value.handler !== handler || value.capture !== Boolean(capture)));
  }
  focus() { this.ownerDocument.activeElement = this; }
  get isConnected() { return this.ownerDocument.body.contains(this); }
}

function fixture() {
  const document = {
    activeElement: null, listeners: new Map(),
    createElement(tag) { return new FakeElement(this, tag); },
    addEventListener: FakeElement.prototype.addEventListener,
    removeEventListener: FakeElement.prototype.removeEventListener,
  };
  document.body = document.createElement('body');
  const mount = document.createElement('section'); document.body.append(mount);
  const navigations = [], contexts = [];
  const help = initializeHelp({ mount, onNavigate: id => navigations.push(id), onContextChange: id => contexts.push(id) });
  const all = () => {
    const result = [];
    const walk = node => { result.push(node); node.children.forEach(walk); };
    walk(document.body); return result;
  };
  const find = (key, value) => all().find(node => node.dataset[key] === value);
  const byClass = name => all().find(node => (node.className || '').split(' ').includes(name));
  const fire = (target, type, extra = {}) => {
    const event = { target, defaultPrevented: false, stopped: false,
      preventDefault() { this.defaultPrevented = true; }, stopPropagation() { this.stopped = true; }, ...extra };
    const path = []; for (let node = target; node; node = node.parentNode) path.push(node); path.push(document);
    for (const capture of [true, false]) {
      for (const node of capture ? [...path].reverse() : path) {
        for (const listener of node.listeners?.get(type) || []) if (listener.capture === capture) listener.handler(event);
        if (event.stopped) return event;
      }
    }
    return event;
  };
  return { document, mount, help, all, find, byClass, fire, navigations, contexts };
}

test('documentation navigation and search render real articles using text nodes', () => {
  const f = fixture();
  assert.equal(f.help.selected, 'getting-started');
  assert.deepEqual(f.navigations, [], 'Mounting documentation does not force shell navigation.');
  assert.equal(f.byClass('thy-help-article').tagName, 'ARTICLE');
  f.fire(f.find('helpTopic', 'sequencer-editor'), 'click');
  assert.equal(f.help.selected, 'sequencer-editor');
  assert.deepEqual(f.navigations, ['sequencer-editor']);
  assert.match(f.byClass('thy-help-article').textContent, /Tap a left step/);
  assert.match(f.byClass('thy-help-article').textContent, /M4/);
  assert.equal(f.document.activeElement, f.byClass('thy-help-article'));
  assert.ok(f.all().some(node => node.tagName === 'TABLE'));
  assert.ok(f.help.search('aftertouch').some(topic => topic.id === 'aftertouch'));
  f.help.search('<script>alert(1)</script>');
  assert.match(f.byClass('thy-help-result-count').textContent, /0 guides/);
  assert.match(f.byClass('thy-help-topic-list').textContent, /No matching guide/);
  assert.equal(f.all().some(node => node.tagName === 'SCRIPT'), false);
  assert.ok(f.all().filter(node => node.tagName === 'IMG').every(node =>
    node.src.startsWith('data:image/svg+xml;charset=utf-8,') &&
    (node.alt.includes('Illustrative') || node.alt.includes('Board diagram'))));
  assert.equal(f.help.open('performance.pressureMode').id, 'aftertouch');
  assert.match(f.byClass('thy-help-article').textContent, /not a fourth Aftertouch message mode/);
});

test('documentation examples reproduce the current C renderer with synthetic musical input', () => {
  assert.deepEqual(Object.keys(HELP_SCREEN_FRAMES), [
    'quickstart-play', 'detailed', 'chladni', 'orbit', 'waves',
    'simple', 'piano', 'fifths', 'engines', 'arp', 'steps', 'track',
    'performance', 'chords', 'chord-advice', 'scale-discovery', 'modulation-advice',
    'harmony-fifths', 'harmony-settings', 'reset-options', 'library', 'midi', 'aftertouch', 'mpe',
    'sensitivity', 'pick', 'calibration', 'at-stage', 'draft', 'applied',
    'tracks', 'recording', 'lights', 'storage',
  ]);
  const checked = spawnSync(process.execPath,
    [fileURLToPath(new URL('../../tools/render_help_examples.mjs', import.meta.url)), '--check'],
    { cwd: fileURLToPath(new URL('../../', import.meta.url)), encoding: 'utf8' });
  assert.equal(checked.status, 0, checked.stderr);
  assert.match(checked.stdout, /34 shared C documentation screenshots verified/);
});

test('instruction diagrams preserve physical counts and distinguish step selection from input', () => {
  for (const figure of Object.values(HELP_FIGURES)) {
    const svg = decodeURIComponent(figure.src.slice(figure.src.indexOf(',') + 1));
    assert.equal((svg.match(/data-pad=/g) || []).length, 96);
    assert.equal((svg.match(/data-top-control=/g) || []).length, 24);
    assert.equal((svg.match(/data-navigation=/g) || []).length, 5);
    assert.equal((svg.match(/data-function=/g) || []).length, 14);
    assert.ok(!/<script|\bon\w+=|href=/.test(svg));
    assert.match(figure.caption, /Illustration/);
  }
  const svg = decodeURIComponent(HELP_FIGURES.steps.src.split(',')[1]);
  assert.match(svg, /Tap and release step 3/);
  assert.match(svg, /M4/);
  assert.match(svg, /data-pad="2"[^>]+fill="#ff2846"/);
  assert.match(svg, /data-pad="48"[^>]+fill="#ff2846"/);
});

test('physical diagrams mark the intended route blocks, silent picker, update pads and Piano gaps', () => {
  const source = id => decodeURIComponent(HELP_FIGURES[id].src.split(',')[1]);
  const selected = id => Array.from(source(id).matchAll(/data-pad="(\d+)"[^>]+fill="#ff2846"/g), match => Number(match[1]));
  assert.deepEqual(selected('pick'), [48], 'Silent picker highlights only the right top-left physical pad.');
  assert.deepEqual(selected('update'), [0, 36], 'Update entry holds the left top-left and bottom-left pads.');
  assert.match(source('pick'), /Selection is silent/);
  const zones = source('routing');
  assert.deepEqual(Array.from(zones.matchAll(/data-route-zone="(\d+)"/g), match => Number(match[1])), [0,1,2,3,4,5]);
  for (const label of ['L1','L2','L3','R1','R2','R3']) assert.ok(zones.includes(`>${label}</text>`));
  const piano = source('piano');
  const cells = Array.from(piano.matchAll(/data-pad="(\d+)"[^>]+data-piano="(note|gap)"/g), match => [Number(match[1]), match[2]]);
  assert.equal(cells.length, 48);
  assert.equal(cells.filter(([, value]) => value === 'note').length, 24);
  for (const [pad, kind] of cells) {
    const row = Math.floor((pad - 48) / 12), column = (pad - 48) % 12;
    const black = [1,3,6,8,10].includes(column);
    assert.equal(kind === 'note', row % 2 === 0 ? black : !black, `Physical piano row ${row}, column ${column}`);
  }
});

test('major workflows have restrained visual coverage and every C example is actually referenced', () => {
  const figures = HELP_TOPICS.flatMap(topic => topic.sections.flatMap(section => section.figures || []));
  for (const frame of Object.values(HELP_SCREEN_FRAMES)) {
    assert.ok(figures.some(figure => figure.kind === 'screen' && figure.label === frame.label), frame.label);
    assert.ok(frame.pixels.length > 100, frame.label);
  }
  for (const id of ['play-chords', 'scales-layouts', 'custom-library', 'velocity', 'sensitivity',
    'calibration', 'aftertouch', 'mpe', 'midi-routing', 'sequencer-tracks', 'sequencer-recording',
    'lights', 'settings', 'storage', 'updates']) {
    const topic = getHelpTopic(id);
    const examples = topic.sections.flatMap(section => section.figures || []);
    assert.ok(examples.length >= 1 && examples.length <= 2, id);
    assert.ok(examples.every(example => /Example|Illustration/.test(example.caption)), id);
  }
  for (const id of ['calibration', 'mpe', 'sequencer-recording', 'storage'])
    assert.ok(getHelpTopic(id).sections.some(section => section.table), id);
  assert.match(JSON.stringify(getHelpTopic('calibration').sections), /first LEFT pad/);
  assert.match(JSON.stringify(getHelpTopic('calibration').sections), /synthetic/);
  assert.match(JSON.stringify(getHelpTopic('calibration').sections), /broader hardware qualification/);
});

test('illustrations render as labeled read-only figures and option sets use tables', () => {
  const f = fixture();
  f.help.open('engines');
  let images = f.all().filter(node => node.tagName === 'IMG');
  assert.equal(images.length, 1); assert.equal(images[0].width, 128); assert.equal(images[0].height, 64);
  assert.ok(f.all().some(node => node.tagName === 'FIGCAPTION' && /Example/.test(node.textContent)));
  const engines = getHelpTopic('engines').sections.find(part => part.table);
  assert.deepEqual(engines.table.rows.map(row => row[0]), ['Play', 'Harmony', 'Sequencer', 'Arp', 'Drums']);
  assert.ok(engines.table.rows.some(row => row[0] === 'Harmony'));
  assert.ok(engines.table.rows.find(row => row[0] === 'Drums').includes('Planned'));
  for (const id of ['scales-layouts', 'views', 'aftertouch']) assert.ok(getHelpTopic(id).sections.some(part => part.table), id);
  f.help.open('sequencer-editor');
  images = f.all().filter(node => node.tagName === 'IMG');
  assert.equal(images.length, 2);
  assert.ok(f.all().some(node => node.tagName === 'FIGCAPTION' && /Illustration/.test(node.textContent)));
  assert.equal(f.all().some(node => node.tagName === 'CANVAS' || node.tagName === 'IFRAME'), false);
  assert.deepEqual(searchHelpTopics('data:image/svg+xml'), []);
});

test('context triggers show focused help, trap Tab, dismiss with Escape and restore focus', () => {
  const f = fixture(), trigger = f.document.createElement('button');
  trigger.dataset.help = 'performance.pressureMode'; trigger.textContent = '?'; f.document.body.append(trigger); trigger.focus();
  const click = f.fire(trigger, 'click');
  assert.equal(click.defaultPrevented, true); assert.equal(f.help.contextOpen, true);
  const dialog = f.byClass('thy-help-dialog'), close = f.find('helpAction', 'close'), read = f.find('helpAction', 'read');
  assert.equal(dialog.attributes.role, 'dialog'); assert.equal(dialog.attributes['aria-modal'], 'true');
  assert.ok(dialog.attributes['aria-labelledby']); assert.ok(dialog.attributes['aria-describedby']);
  assert.equal(f.document.activeElement, close);
  assert.match(dialog.textContent, /Poly AT/);
  f.fire(close, 'keydown', { key: 'Tab', shiftKey: true });
  assert.equal(f.document.activeElement, read);
  f.fire(read, 'keydown', { key: 'Tab', shiftKey: false });
  assert.equal(f.document.activeElement, close);
  const escape = f.fire(close, 'keydown', { key: 'Escape' });
  assert.equal(escape.defaultPrevented, true); assert.equal(f.help.contextOpen, false);
  assert.equal(f.document.activeElement, trigger);
  assert.deepEqual(f.contexts, ['aftertouch', null]);
  assert.deepEqual(f.navigations, [], 'A short hint does not switch the shell to Documentation.');
});

test('Open full guide leaves the overlay, navigates to the actual article and moves focus there', () => {
  const f = fixture();
  f.help.context('sequencer.channel');
  f.fire(f.find('helpAction', 'read'), 'click');
  assert.equal(f.help.contextOpen, false);
  assert.equal(f.help.selected, 'track-settings');
  assert.deepEqual(f.navigations, ['track-settings']);
  assert.equal(f.document.activeElement, f.byClass('thy-help-article'));
  assert.match(f.byClass('thy-help-article').textContent, /A new T1 uses channel 1/);
});

test('help cleanup removes delegated listeners and cannot act on a device or stale UI', () => {
  const f = fixture(), trigger = f.document.createElement('button');
  trigger.dataset.help = 'mpe'; f.document.body.append(trigger);
  f.help.context('mpe', { anchor: trigger });
  f.help.destroy();
  assert.equal(f.mount.children.length, 0); assert.equal(f.help.contextOpen, false);
  f.fire(trigger, 'click');
  assert.equal(f.help.contextOpen, false); assert.equal(f.help.open('mpe'), null);
  assert.deepEqual(f.help.search('mpe'), []); assert.equal(f.help.context('mpe'), null);
  assert.equal(f.document.listeners.get('click').length, 0);
  assert.equal(f.document.listeners.get('keydown').length, 0);
  f.help.destroy();
});

test('guide index opens on request and closes after choosing an article', () => {
  const f=fixture(),toggle=f.byClass('thy-help-index-toggle');
  assert.equal(toggle.getAttribute('aria-expanded'),'false');
  f.fire(toggle,'click');assert.equal(toggle.getAttribute('aria-expanded'),'true');
  f.fire(f.find('helpTopic','sequencer-editor'),'click');
  assert.equal(toggle.getAttribute('aria-expanded'),'false');
  assert.ok(f.byClass('thy-help-contents'));f.help.destroy();
});


test('article links expand deeper sections and option tables retain mobile labels', () => {
  const f=fixture();f.help.open('sequencer-editor');
  const sections=f.all().filter(n=>n.className==='thy-help-section');
  assert.ok(sections.length>2);assert.equal(sections[0].tagName,'SECTION');
  assert.equal(sections[2].tagName,'DETAILS');assert.ok(!sections[2].open);
  const link=f.all().find(n=>n.tagName==='A'&&n.href===`#${sections[2].id}`);
  assert.ok(link);f.fire(link,'click');assert.equal(sections[2].open,true);
  const cells=f.all().filter(n=>n.tagName==='TD');assert.ok(cells.length);
  assert.ok(cells.every(n=>n.dataset.label));f.help.destroy();
});
