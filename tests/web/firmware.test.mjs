import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createCore } from '../../web/core.js';
import { MENU_ROWS } from '../../web/studio-menu.js';

const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));
const makeLayout = (root = 60) => Array.from({ length: 96 }, (_, id) => ({
  notes: id < 48 ? [root, root + 4, root + 7, 0, 0, 0, 0] : [root, 0, 0, 0, 0, 0, 0],
  mask: id < 48 ? 7 : 1, color: id % 22,
}));
const response = (changes = {}) => ({ attack: 1200, release: 1000, floor: 800, ceiling: 3200, curve: 0, ...changes });
const compact = events => events.map(({ destination, status, data1, data2 }) => [destination, status, data1, data2]);

async function fresh(pads = makeLayout()) {
  const engine = await createCore({ bytes });
  const firmware = engine.firmware;
  assert.ok(firmware.layout(pads).ok);
  let now = 0;
  return {
    engine, firmware,
    sample(pad, raw, count = 1) {
      for (let i = 0; i < count; i++) assert.ok(firmware.sample(pad, raw, now++).ok);
    },
    warm(pad, raw = 100) { this.sample(pad, raw, 8); },
    get now() { return now; },
  };
}

test('shared detector requires quiet qualification and preserves hysteresis through WASM', async () => {
  const h = await fresh();
  const { engine, firmware } = h;
  assert.ok(firmware.setResponse(-1, response()).ok);
  h.sample(0, 100, 7);
  assert.equal(firmware.sensor(0).warmup, 7);
  assert.deepEqual(engine.drain(), []);
  h.sample(0, 100);
  assert.equal(firmware.sensor(0).warmup, 8);
  h.sample(0, 1199, 2);
  assert.equal(engine.isHeld(0), false);
  h.sample(0, 1200);
  assert.equal(engine.isHeld(0), false);
  h.sample(0, 1200);
  assert.equal(engine.isHeld(0), true);
  assert.deepEqual(engine.drain().map(e => e.data1), [60, 64, 67]);
  h.sample(0, 1100, 3);
  assert.equal(engine.isHeld(0), true);
  h.sample(0, 1000);
  assert.equal(engine.isHeld(0), true);
  h.sample(0, 1000);
  assert.equal(engine.isHeld(0), false);
  assert.deepEqual(compact(engine.drain()), [[0, 0x80, 60, 0], [0, 0x80, 64, 0], [0, 0x80, 67, 0]]);
  assert.equal(engine.stats().activeVoices, 0);
});

test('global response edits preserve per-pad overrides and requalify the affected inputs', async () => {
  const h = await fresh();
  const { firmware, engine } = h;
  const local = response({ attack: 900, release: 700, floor: 500, curve: 1 });
  assert.ok(firmware.setResponse(-1, response()).ok);
  assert.ok(firmware.setResponse(0, local).ok);
  assert.equal(firmware.response(0).overridden, 1);
  h.warm(0);
  const global = response({ attack: 1400, release: 1100, floor: 900, curve: 2 });
  assert.ok(firmware.setResponse(-1, global).ok);
  assert.deepEqual(firmware.response(0), { ...local, overridden: 1 });
  assert.deepEqual(firmware.response(1), { ...global, overridden: 0 });
  assert.equal(firmware.sensor(0).warmup, 8);
  assert.equal(firmware.sensor(1).warmup, 0);
  assert.ok(firmware.clearResponse(0).ok);
  assert.deepEqual(firmware.response(0), { ...global, overridden: 0 });
  assert.equal(firmware.sensor(0).warmup, 0);
  h.warm(0);
  h.sample(0, 1300, 2);
  assert.equal(engine.isHeld(0), false);
  assert.deepEqual(engine.drain(), []);
  h.sample(0, 1400, 2);
  assert.equal(engine.isHeld(0), true);
  h.sample(0, 100, 2);
  engine.drain();
  assert.ok(firmware.factory().ok);
  assert.equal(firmware.response(0).overridden, 0);
  assert.equal(firmware.sensor(0).warmup, 0);
  assert.equal(firmware.sensor(0).profile, 1);
});

test('response editing cannot silently release held notes or reinterpret a partial attack', async () => {
  const h = await fresh();
  const { firmware, engine } = h;
  h.warm(0);
  h.sample(0, 2000, 2);
  engine.drain();
  const owner = engine.owner(0), global = firmware.response();
  assert.equal(firmware.setResponse(-1, response()).status, 'invalid');
  assert.equal(engine.isHeld(0), true);
  assert.deepEqual(firmware.response(), global);
  assert.deepEqual(engine.owner(0), owner);
  assert.deepEqual(engine.drain(), []);
  assert.ok(firmware.setResponse(1, response()).ok); // An unrelated released pad can be edited.
  assert.equal(firmware.clearResponse(0).status, 'invalid');
  h.sample(0, 100, 2);
  engine.drain();
  h.sample(0, 2000); // Only one attack-confirmation sample, so no owner yet.
  assert.equal(engine.isHeld(0), false);
  assert.equal(firmware.setResponse(0, response()).status, 'invalid');
  assert.deepEqual(engine.drain(), []);
  h.sample(0, 100);
  assert.ok(firmware.setResponse(0, response()).ok);
});

test('pressure curves are independently configurable while attack velocity remains explicit', async () => {
  const pressures = [];
  for (const curve of [0, 1, 2]) {
    const h = await fresh();
    assert.ok(h.firmware.setResponse(0, response({ floor: 1000, ceiling: 3000, curve })).ok);
    h.warm(0);
    h.sample(0, 2000);
    pressures.push(h.firmware.sensor(0).pressure);
    assert.deepEqual(h.engine.drain(), []);
  }
  assert.ok(pressures[1] > pressures[0] && pressures[0] > pressures[2]);
  const h = await fresh();
  const { firmware, engine } = h;
  assert.ok(firmware.expression(87, true).ok);
  assert.ok(firmware.route(0, 3, 4).ok);
  assert.ok(firmware.setResponse(0, response({ floor: 1000, ceiling: 3000 })).ok);
  h.warm(0);
  h.sample(0, 2000, 2);
  assert.deepEqual(compact(engine.drain()), [[2, 0x93, 60, 87], [2, 0x93, 64, 87], [2, 0x93, 67, 87]]);
  h.sample(0, 2200);
  const pressure = engine.drain();
  assert.equal(pressure.length, 3);
  assert.ok(pressure.every(e => e.destination === 2 && e.status === 0xa3 && e.data2 > pressures[0]));
  assert.deepEqual(pressure[0].din, [0xa3, 60, pressure[0].data2]);
  h.sample(0, 3000);
  assert.equal(firmware.sensor(0).pressure, 127);
  engine.drain();
  h.sample(0, 1000, 2);
  assert.equal(firmware.sensor(0).pressure, 0);
  assert.equal(engine.stats().activeVoices, 0);
});

test('layout, routing and note illumination retain the original held owner until release', async () => {
  const h = await fresh();
  const { engine, firmware } = h;
  assert.ok(firmware.route(0, 0, 5).ok);
  assert.ok(firmware.lighting({ enabled: true, idle: 3, held: 8, fade: 0 }).ok);
  h.warm(0);
  h.sample(0, 2000, 2);
  assert.equal(engine.drain().length, 6);
  const originalOwner = engine.owner(0);
  const originalColor = firmware.colors(100)[0];
  assert.ok(originalColor);
  assert.ok(firmware.layout(makeLayout(62), { root: 2 }).ok);
  assert.ok(firmware.route(0, 7, 2, 12).ok);
  assert.deepEqual(engine.owner(0), originalOwner);
  assert.equal(firmware.colors(101)[0], originalColor);
  h.sample(0, 100, 2);
  assert.deepEqual(compact(engine.drain()), [
    [0, 0x80, 60, 0], [0, 0x80, 64, 0], [0, 0x80, 67, 0],
    [2, 0x80, 60, 0], [2, 0x80, 64, 0], [2, 0x80, 67, 0],
  ]);
  h.sample(0, 2000, 2);
  assert.deepEqual(compact(engine.drain()), [[1, 0x97, 74, 96], [1, 0x97, 78, 96], [1, 0x97, 81, 96]]);
  assert.notEqual(firmware.colors(102)[0], originalColor);
  h.sample(0, 100, 2);
  engine.drain();
  assert.equal(engine.stats().activeVoices, 0);
});

test('note colors follow actual pitches, generated melody and empty/range states', async () => {
  const pads = makeLayout();
  pads[0] = { notes: [60, 0, 0, 0, 0, 0, 0], mask: 1, color: 255 };
  pads[1] = { notes: [72, 0, 0, 0, 0, 0, 0], mask: 1, color: 1 };
  pads[2] = { notes: [62, 0, 0, 0, 0, 0, 0], mask: 1, color: 1 };
  pads[3] = { notes: [0, 0, 0, 0, 0, 0, 0], mask: 0, color: 1 };
  pads[4] = { notes: [127, 0, 0, 0, 0, 0, 0], mask: 1, color: 1 };
  pads[48] = { notes: [0, 0, 0, 0, 0, 0, 0], mask: 0, color: 11 };
  const original = structuredClone(pads);
  const { firmware, engine } = await fresh(pads);
  assert.ok(firmware.lighting({ enabled: true, idle: 3, held: 8, fade: 0 }).ok);
  let colors = firmware.colors(0);
  assert.ok(colors[0]);
  assert.equal(colors[0], colors[1]); // Absolute pitch class; archive IDs are not a CSS palette index.
  assert.notEqual(colors[0], colors[2]);
  assert.equal(colors[3], 0);
  assert.equal(colors[48], 0);
  assert.ok(firmware.performance({ melodyLayout: 1, upper: 1 }).ok);
  assert.ok(engine.state.set(MENU_ROWS.findIndex(row => row.id === 'controls.lowerMode'), 0).ok);
  colors = firmware.colors(1);
  assert.equal(colors[4], 0); // MIDI127 + an octave cannot play and cannot be advertised as playable.
  assert.equal(colors[48], colors[0]); // Generated top-left melody is C4.
  assert.notEqual(colors[52], colors[48]); // The next chromatic column is C#.
  assert.deepEqual(pads, original);
  assert.deepEqual(engine.drain(), []);
  assert.ok(firmware.lighting({ enabled: false, idle: 3, held: 8, fade: 500 }).ok);
  assert.ok(firmware.colors(2).every(color => color === 0)); // OFF bypasses a fade.
});

test('pointer/keyboard musical owners brighten without fabricating ADC sensor presses', async () => {
  const { engine, firmware } = await fresh();
  assert.ok(firmware.lighting({ enabled: true, idle: 3, held: 8, fade: 0 }).ok);
  const idle = firmware.colors(0)[0];
  assert.ok(engine.press({ pad: 0, notes: [60, 64, 67], mask: 1, channel: 0, velocity: 96 }).ok);
  assert.equal(firmware.sensor(0).held, 0);
  const held = firmware.colors(1)[0];
  assert.notEqual(held, idle);
  assert.equal(Math.max(held >>> 16, (held >>> 8) & 255, held & 255), 8);
  assert.equal(Math.max(idle >>> 16, (idle >>> 8) & 255, idle & 255), 3);
  assert.ok(engine.release(0).ok);
  assert.equal(firmware.colors(2)[0], idle);
  assert.equal(firmware.sensor(0).held, 0);
  assert.equal(engine.stats().activeVoices, 0);
});

test('the firmware menu is the real 23-row controller and its values change the shared OLED', async () => {
  const { firmware } = await fresh();
  assert.ok(firmware.focus(0).ok);
  for (let row = 0; row < 23; row++) {
    assert.equal(firmware.state().menuRow, row);
    assert.ok(firmware.navigate('down').ok);
  }
  assert.equal(firmware.state().menuRow, 0);
  assert.ok(firmware.navigate('up').ok);
  assert.equal(firmware.state().menuRow, 22);
  assert.ok(firmware.focus(14).ok);
  const before = firmware.frame({ animations: false });
  const attack = firmware.response().attack;
  assert.ok(firmware.navigate('right').ok);
  assert.ok(firmware.navigate('down').ok);
  assert.ok(firmware.response().attack > attack);
  assert.equal(firmware.state().menuEditing, 1);
  assert.notDeepEqual(firmware.frame({ animations: false }), before);
  assert.ok(firmware.navigate('left').ok);
  assert.equal(firmware.state().menuEditing, 0);
  assert.ok(firmware.navigate('left').ok);
  assert.equal(firmware.state().menuOpen, 0);
  const play = firmware.frame({ scaleName: 'C', animations: false });
  const c = [0x0e, 0x11, 0x11, 0x11];
  for (let x = 0; x < 4; x++) for (let y = 0; y < 5; y++) {
    assert.equal(Boolean(play[x + 2] & (1 << (y + 1))), Boolean(c[x] & (1 << y)));
  }
  assert.equal(play.length, 1024);
});

test('OLED menu transitions finish after 120ms through clock wrap and respect reduced motion', async () => {
  const { firmware } = await fresh();
  const initial = firmware.frame({ now: 0xffffffe0 });
  firmware.navigate('select');
  assert.deepEqual(firmware.frame({ now: 0xfffffff0 }), initial);
  const halfway = firmware.frame({ now: 0x2c });
  assert.notDeepEqual(halfway, initial);
  const complete = firmware.frame({ now: 0x68 });
  assert.deepEqual(complete, firmware.frame({ now: 0x68, animations: false }));
  firmware.navigate('left');
  assert.deepEqual(firmware.frame({ now: 0x69, animations: true }), complete);
  const reduced = firmware.frame({ now: 0x6a, animations: false });
  assert.notDeepEqual(reduced, complete);
  assert.deepEqual(reduced, firmware.frame({ now: 0x6a, animations: false }));
});

test('invalid ABI inputs leave settings, notes and output queues intact', async () => {
  const { firmware, engine } = await fresh();
  const state = firmware.state(), current = firmware.response(), stats = engine.stats();
  const badLayout = makeLayout();
  badLayout[95].notes[0] = 128;
  const invalid = [
    () => firmware.layout(makeLayout().slice(1)),
    () => firmware.layout(badLayout),
    () => firmware.layout(makeLayout(), { degrees: [0, 4, 2] }),
    () => firmware.performance({ spread: 4 }),
    () => firmware.performance({ melodyLayout: 256 }),
    () => firmware.route(0, 16, 1),
    () => firmware.route(0, 0, 8),
    () => firmware.slot(48, 1),
    () => firmware.sample(256, 0, 0),
    () => firmware.sample(0, Number.NaN, 0),
    () => firmware.sample(0, 0, -1),
    () => firmware.setResponse(-1, response({ attack: 1000, release: 1000 })),
    () => firmware.setResponse(256, response()),
    () => firmware.setResponse(0, response({ curve: 3 })),
    () => firmware.clearResponse(-1),
    () => firmware.expression(0, true),
    () => firmware.expression(96, 1),
    () => firmware.lighting({ enabled: true, idle: 9, held: 8, fade: 0 }),
    () => firmware.lighting({ enabled: true, idle: 3, held: 8, fade: 501 }),
    () => firmware.focus(23),
    () => firmware.navigate('unknown'),
  ];
  for (const operation of invalid) assert.equal(operation().status, 'invalid');
  assert.deepEqual(firmware.state(), state);
  assert.deepEqual(firmware.response(), current);
  assert.deepEqual(engine.stats(), stats);
  assert.deepEqual(engine.drain(), []);
  assert.equal(firmware.sensor(96), null);
  assert.equal(firmware.response(-3), null);
});


const menuIndex = id => {
  const index = MENU_ROWS.findIndex(row => row.id === id);
  assert.notEqual(index, -1, id);
  return index;
};
const chooseMenu = (state, id, option) => {
  const row = menuIndex(id);
  const index = MENU_ROWS[row].options.findIndex(value => value.value === option);
  assert.notEqual(index, -1, `${id}: ${option}`);
  assert.ok(state.set(row, index).ok, `${id}: ${option}`);
};

test('all shared instrument layouts preview the exact routed notes played through the WASM pad ABI', async () => {
  const { engine, firmware } = await fresh();
  assert.ok(firmware.route(1, 2, 5).ok);
  // Piano uses semitone columns with complementary black/white rows. Top-row
  // column2 is C#4; top-row column1 is its inactive black-key gap.
  const expected = [60, 61, 64, 65, 62, 41, 61, 61, 61];
  const ordinal = [0, 3, 6, 7, 1, 5, 8, 2, 4];
  for (let melodyLayout = 0; melodyLayout < 9; melodyLayout++) {
    assert.ok(firmware.performance({ melodyLayout }).ok);
    assert.equal(engine.state.session().mode,1);
    assert.equal(engine.state.get(menuIndex('performance.melodyLayout')), ordinal[melodyLayout]);
    assert.deepEqual(firmware.previewPad(52).notes, [expected[melodyLayout]]);
    if (melodyLayout === 6) assert.deepEqual(firmware.previewPad(48).notes, []);
    if (melodyLayout === 1) assert.deepEqual(firmware.previewPad(48).notes, [60]);
    assert.ok(firmware.pressPad(52).ok);
    assert.equal(firmware.sensor(52).held, 0); // Pointer input never fabricates ADC state.
    assert.deepEqual(compact(engine.drain()), [[0, 0x92, expected[melodyLayout], 96], [2, 0x92, expected[melodyLayout], 96]]);
    assert.ok(engine.release(52).ok);
    assert.deepEqual(compact(engine.drain()), [[0, 0x82, expected[melodyLayout], 0], [2, 0x82, expected[melodyLayout], 0]]);
  }
  assert.equal(firmware.performance({ melodyLayout: 9 }).ok, false);
  assert.equal(firmware.pressPad(96).ok, false);
  assert.equal(firmware.previewPad(-1).ok, false);
  assert.equal(engine.stats().activeVoices, 0);
});

test('guitar windows use standard strings independently of scale root and retain the original held route', async () => {
  const { engine, firmware } = await fresh();
  assert.ok(engine.state.set(menuIndex('lights.view'), 7).ok); // Explicit Waves preference.
  assert.ok(engine.state.begin(menuIndex('performance.guitarShortcut')).ok);
  assert.equal(firmware.state().melodyLayout, 5);
  assert.equal(firmware.state().displayView, 7); // A layout shortcut preserves the chosen view.
  for (const [window, notes] of [[0, [40, 45, 50, 55]], [1, [45, 50, 55, 59]], [2, [50, 55, 59, 64]]]) {
    assert.ok(engine.state.set(menuIndex('performance.guitarWindow'), window).ok);
    assert.deepEqual([48, 49, 50, 51].map(pad => firmware.previewPad(pad).notes[0]), notes);
  }
  assert.ok(firmware.pressPad(48).ok);
  const held = engine.owner(48);
  engine.drain();
  assert.ok(firmware.layout(makeLayout(62), { anchor: 62, root: 2 }).ok);
  assert.deepEqual(firmware.previewPad(48).notes, [50]);
  assert.deepEqual(engine.owner(48), held);
  assert.ok(firmware.performance({ melodyLayout: 5, melodyOctave: 1 }).ok);
  assert.ok(firmware.route(1, 7, 4, 12).ok);
  assert.deepEqual(firmware.previewPad(48).notes, [74]);
  assert.ok(engine.release(48).ok);
  assert.deepEqual(compact(engine.drain()), [[0, 0x81, 50, 0]]);
  assert.ok(firmware.pressPad(48).ok);
  assert.deepEqual(compact(engine.drain()), [[2, 0x97, 74, 96]]);
  assert.ok(engine.release(48).ok);
  engine.drain();
});

test('context controls reserve exactly six labeled pads, act once per press and produce no MIDI', async () => {
  const { engine, firmware } = await fresh();
  assert.ok(engine.state.set(menuIndex('performance.padOverlays'), 1).ok);
  assert.equal(firmware.state().contextControls, 1);
  const contexts = new Map([[32, 1], [36, 2], [40, 3], [44, 4], [88, 5], [92, 6]]);
  for (let pad = 0; pad < 96; pad++) assert.equal(firmware.contextPad(pad), contexts.get(pad) ?? 0);
  for (const [pad] of contexts) {
    assert.deepEqual(firmware.previewPad(pad).notes, []);
    assert.ok(firmware.pressPad(pad).ok);
    assert.equal(firmware.contextHeld(pad), true);
    assert.equal(engine.isHeld(pad), false);
    const snapshot = firmware.state();
    assert.ok(firmware.pressPad(pad).ok);
    assert.deepEqual(firmware.state(), snapshot);
    assert.equal(engine.state.set(menuIndex('performance.padOverlays'), 0).ok, false);
    assert.deepEqual(engine.drain(), []);
    assert.ok(engine.release(pad).ok);
    assert.equal(firmware.contextHeld(pad), false);
  }
  assert.equal(firmware.state().inversion, 1);
  assert.equal(firmware.state().spread, 0);
  assert.equal(firmware.state().bassOctave, -1);
  assert.equal(firmware.state().chordOctave, 0);
  assert.equal(firmware.state().melodyOctave, 0);
  assert.ok(engine.state.set(menuIndex('performance.padOverlays'), 0).ok);
  assert.ok(firmware.previewPad(3).notes.length > 0);
  assert.equal(engine.stats().heldPads, 0);
  assert.deepEqual(engine.drain(), []);
});

test('lighting policies are live, preserve harmonic-root ownership and offer twelve distinct note colors', async () => {
  const pads = makeLayout();
  pads[0] = { notes: [57, 62, 65, 0, 0, 0, 0], mask: 7, color: 2 }; // D minor / A.
  pads[52] = { notes: [62, 0, 0, 0, 0, 0, 0], mask: 1, color: 2 };
  const { engine, firmware } = await fresh(pads);
  assert.ok(firmware.lighting({ enabled: true, idle: 3, held: 8, fade: 0 }).ok);
  const harmonic = firmware.colors(0)[0];
  chooseMenu(engine.state, 'lights.chordColor', 'bass');
  const bass = firmware.colors(1)[0];
  assert.notEqual(harmonic, bass);
  chooseMenu(engine.state, 'lights.chordColor', 'root');
  assert.equal(firmware.colors(2)[0], harmonic);
  assert.ok(firmware.pressPad(0).ok);
  engine.drain();
  const heldColor = firmware.colors(3)[0];
  assert.ok(firmware.layout(makeLayout(64), { root: 4 }).ok);
  assert.ok(engine.press({ pad: 0, notes: [64], mask: 1, channel: 0 }).ok); // Idempotent owner: do not recapture its color.
  assert.equal(firmware.colors(4)[0], heldColor);
  chooseMenu(engine.state, 'lights.heldStyle', 'white');
  assert.equal(firmware.colors(5)[0], 0x080808);
  assert.ok(engine.release(0).ok);
  engine.drain();
  assert.notEqual(firmware.colors(6)[0], harmonic);
  chooseMenu(engine.state, 'lights.noteColors', 'distinct');
  assert.ok(engine.state.set(menuIndex('lights.keyContrast'), 0).ok);
  assert.ok(firmware.performance({ melodyLayout: 1 }).ok);
  const colors = firmware.colors(7);
  assert.equal(new Set(Array.from({ length: 12 }, (_, column) => colors[48 + 4 * column])).size, 12);
  chooseMenu(engine.state, 'lights.heldStyle', 'note-color');
  assert.ok(engine.state.set(menuIndex('lights.heldFeedback'), 0).ok);
  const idle = firmware.colors(8)[48];
  assert.ok(firmware.pressPad(48).ok);
  engine.drain();
  assert.equal(firmware.colors(9)[48], idle);
  assert.ok(engine.release(48).ok);
  engine.drain();
});

test('live display and center response settings bind to the instrument while future editing is opt-in', async () => {
  const { engine, firmware } = await fresh();
  const { state } = engine;
  assert.equal(firmware.state().centerPoll, 8);
  assert.ok(state.set(menuIndex('sensitivity.centerResponse'), 20).ok);
  assert.equal(firmware.state().centerPoll, 20);
  assert.equal(state.set(menuIndex('sensitivity.centerResponse'), 7).ok, false);
  assert.equal(firmware.state().centerPoll, 20);
  assert.ok(state.set(menuIndex('lights.animations'), 0).ok);
  assert.equal(firmware.state().animationsEnabled, 0);
  for (const [view,id] of [['notes',0],['piano',1],['simple',4],['chladni',5],['orbit',6],['waves',7]]) {
    chooseMenu(state, 'lights.view', view);
    assert.equal(firmware.state().displayView, id);
    assert.equal(state.frame({ animations: false }).length, 1024);
  }
  const future = MENU_ROWS.findIndex(row => row.status === 'design' && row.type === 'number');
  assert.ok(future >= 0);
  assert.ok(state.designMode(false).ok);
  assert.equal(state.begin(future).ok, false);
  assert.ok(state.begin(menuIndex('performance.velocity')).ok);
  assert.ok(state.cancel().ok);
  assert.ok(state.designMode(true).ok);
  assert.ok(state.begin(future).ok);
  assert.ok(state.designMode(false).ok);
  assert.equal(state.info().editing, 0);
});
