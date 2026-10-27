import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createCore } from '../../web/core.js';
import { MENU_ROWS, STUDIO_MENU } from '../../web/studio-menu.js';

const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));
const index = id => {
  const result = MENU_ROWS.findIndex(row => row.id === id);
  assert.notEqual(result, -1, id);
  return result;
};
const ordinal = (id, value) => {
  const result = MENU_ROWS[index(id)].options.findIndex(option => option.value === value);
  assert.notEqual(result, -1, `${id}: ${value}`);
  return result;
};
const set = (state, id, value) => state.set(index(id), value);
const get = (state, id) => state.get(index(id));
const choose = (state, id, value) => set(state, id, ordinal(id, value));
const fresh = () => createCore({ bytes });
const layout = Array.from({ length: 96 }, (_, pad) => ({
  notes: pad < 48 ? [60, 64, 67, 0, 0, 0, 0] : [60, 0, 0, 0, 0, 0, 0],
  mask: pad < 48 ? 7 : 1, color: 1,
}));

test('WASM exposes the full bounded C menu state with typed committed values', async () => {
  const { state } = await fresh();
  assert.equal(state.rowCount, MENU_ROWS.length);
  assert.equal(STUDIO_MENU.groups.length, 13);
  assert.equal(STUDIO_MENU.groups[0].id, 'engines');
  assert.ok(state.info().stateBytes > 0 && state.info().stateBytes <= 8192);
  assert.equal(get(state, 'performance.root'), 0);
  assert.equal(get(state, 'performance.velocity'), 96);
  assert.equal(get(state, 'sequencer.track'), 1);
  assert.equal(get(state, 'sequencer.name'), 'Track 1');
  assert.equal(MENU_ROWS.some(row => row.id === 'chords.noteSlots'), false);
  assert.equal(get(state, 'controls.lowerMode'), ordinal('controls.lowerMode', 'octave-layers'));
  assert.equal(get(state, 'midi.remoteRecord'), 0);
  assert.equal(get(state, 'midi.remotePosition'), 0);
  for (const id of ['performance.velocity', 'midi.remoteLow', 'midi.remoteHigh', 'midi.assignmentNumber']) {
    const before = get(state, id), revision = state.info().revision;
    for (const invalid of [NaN, Infinity, 1.5, null, {}, [], '3']) {
      assert.equal(set(state, id, invalid).ok, false, `${id}: ${String(invalid)}`);
      assert.equal(get(state, id), before);
      assert.equal(state.info().revision, revision);
    }
  }
});

test('the engine hub exposes real workspaces and unavailable engines without changing held owners', async () => {
  const engine = await fresh(), {state,firmware} = engine;
  assert.ok(firmware.layout(layout).ok);
  assert.ok(engine.press({pad:0,notes:[60,64,67],channel:3,mask:5,velocity:96}).ok);
  engine.drain();
  const owner = engine.owner(0);
  assert.equal(MENU_ROWS.some(row => ['engines.scales','engines.chromatic'].includes(row.id)),false);
  assert.equal(MENU_ROWS[index('engines.play')].status,'live');
  for (let layout = 0; layout < MENU_ROWS[index('performance.melodyLayout')].options.length; ++layout) {
    assert.ok(state.set(index('performance.melodyLayout'),layout).ok);
    const before = firmware.state().melodyLayout;
    assert.ok(state.begin(index('engines.play')).ok);
    assert.equal(state.session().mode,1);
    assert.equal(firmware.state().melodyLayout,before,'Selecting Play retains the chosen layout.');
    assert.deepEqual(engine.owner(0),owner);
    assert.deepEqual(engine.drain(),[]);
  }
  assert.equal(MENU_ROWS[index('engines.arp')].type,'action');
  assert.ok(state.begin(index('engines.arp')).ok);
  assert.equal(engine.arp.snapshot(0).enabled,true);
  assert.equal(engine.arp.snapshot(1).enabled,false);
  assert.equal(state.session().mode,1);
  assert.deepEqual(engine.owner(0),owner);
  assert.ok(state.navigate('select').ok);
  assert.equal(MENU_ROWS[index('engines.sequencer')].type,'action');
  assert.ok(state.begin(index('engines.sequencer')).ok);
  assert.equal(state.session().mode,2);
  assert.equal(engine.sequencer.snapshot().open,true);
  assert.deepEqual(engine.owner(0),owner);
  assert.ok(state.navigate('select').ok);
  assert.equal(state.session().mode,1);
  for (const id of ['engines.drums','engines.lightboard','engines.user']) {
    assert.equal(MENU_ROWS[index(id)].type,'info');
    assert.ok(state.begin(index(id)).ok,id);
    assert.equal(state.info().editing,0);
    assert.deepEqual(engine.owner(0),owner);
  }
  assert.ok(engine.release(0).ok);
  assert.equal(engine.drain().filter(event => event.destination===2 && event.status===0x83).length,3);
});

test('the C navigator owns parent cursors, editor drafts and transition direction', async () => {
  const { state } = await fresh();
  assert.equal(state.info().level, 0);
  assert.ok(state.navigate('select').ok);
  assert.equal(state.info().level, 1);
  const openRevision = state.info().transitionRevision;
  assert.ok(state.navigate('down').ok);
  assert.equal(state.info().group, 1);
  assert.equal(state.info().transitionRevision, openRevision, 'Selection movement does not start a side slide.');
  assert.ok(state.navigate('right').ok);
  assert.equal(state.info().level, 2);
  const rowsRevision = state.info().transitionRevision;
  assert.ok(state.navigate('down').ok);
  assert.equal(state.info().cursor, 1);
  assert.equal(state.info().transitionRevision, rowsRevision);
  assert.ok(state.navigate('left').ok);
  assert.ok(state.navigate('right').ok);
  assert.equal(state.info().cursor, 1);
  assert.ok(state.begin(index('chords.inversion')).ok);
  assert.ok(state.stage(2).ok);
  assert.equal(get(state, 'chords.inversion'), 0);
  const draftTransition = state.info().transitionRevision;
  assert.ok(state.adjust(1).ok);
  assert.equal(state.info().editValue, 3);
  assert.equal(state.info().transitionRevision, draftTransition);
  assert.ok(state.cancel().ok);
  assert.equal(get(state, 'chords.inversion'), 0);
  assert.ok(state.begin(index('chords.inversion')).ok);
  assert.ok(state.stage(2).ok);
  assert.ok(state.commit().ok);
  assert.equal(get(state, 'chords.inversion'), 2);
  assert.equal(state.info().editing, 0);
  const revision = state.info().revision;
  assert.equal(state.commit().ok, false);
  assert.equal(state.info().revision, revision);
});

test('all visible information rows require explicit entry in the shared C framebuffer without changing music', async () => {
  const information = MENU_ROWS.filter(row => row.type === 'info');
  const counts = [];
  for (const design of [false, true]) {
    const engine = await fresh(), { state, firmware } = engine;
    assert.ok(firmware.layout(layout).ok);
    assert.ok(engine.press({ pad: 0, notes: [60, 64, 67], channel: 3, mask: 5, velocity: 96 }).ok);
    engine.drain();
    const owner = engine.owner(0);
    assert.ok(state.designMode(design).ok);
    let checked = 0;
    for (const definition of information) {
      const row = index(definition.id);
      const group = STUDIO_MENU.groups.findIndex(item => item.id === definition.group);
      assert.notEqual(group, -1, definition.id);
      const visible = design || definition.status === 'live' || definition.group === 'engines';
      assert.equal(state.visible(row), visible, definition.id);
      if (!visible) {
        assert.equal(state.focus(group, row).ok, false, definition.id);
        assert.equal(state.begin(row).ok, false, definition.id);
        continue;
      }
      assert.ok(state.focus(group, row).ok, definition.id);
      const cursor = state.cursor(group), focused = state.info();
      const events = state.events();
      const list = state.frame({ animations: false });
      assert.ok(state.navigate('down').ok);
      assert.ok(state.navigate('up').ok);
      assert.equal(state.cursor(group), cursor, definition.id);
      assert.equal(state.info().transitionRevision, focused.transitionRevision, definition.id);
      assert.deepEqual(state.frame({ animations: false }), list, `Highlighting ${definition.id} stays in the list.`);

      assert.ok(state.navigate('right').ok, definition.id);
      const opened = state.info();
      assert.equal(opened.level, 2);
      assert.equal(opened.editing, 0);
      assert.equal(opened.direction, -1);
      assert.notEqual(opened.transitionRevision, focused.transitionRevision);
      const description = state.frame({ animations: false });
      assert.notDeepEqual(description, list, definition.id);
      for (const action of ['up', 'down', 'right']) {
        assert.ok(state.navigate(action).ok);
        assert.equal(state.cursor(group), cursor, definition.id);
        assert.equal(state.info().transitionRevision, opened.transitionRevision, definition.id);
        assert.deepEqual(state.frame({ animations: false }), description, definition.id);
      }
      assert.ok(state.navigate('left').ok);
      assert.equal(state.info().level, 2);
      assert.equal(state.cursor(group), cursor, definition.id);
      assert.equal(state.info().direction, 1);
      assert.notEqual(state.info().transitionRevision, opened.transitionRevision, definition.id);
      assert.deepEqual(state.frame({ animations: false }), list, `LEFT restores the exact list for ${definition.id}.`);
      assert.ok(state.navigate('right').ok);
      assert.ok(state.navigate('select').ok);
      assert.equal(state.info().level, 0);
      assert.ok(state.navigate('select').ok);
      assert.ok(state.navigate('right').ok);
      assert.equal(state.cursor(group), cursor, definition.id);
      assert.deepEqual(state.frame({ animations: false }), list, definition.id);
      assert.equal(state.info().revision, focused.revision, definition.id);
      assert.deepEqual(state.events(), events, definition.id);
      assert.deepEqual(engine.owner(0), owner, definition.id);
      assert.deepEqual(engine.drain(), [], definition.id);
      ++checked;
    }
    counts.push(checked);
    assert.ok(engine.release(0).ok);
    assert.equal(engine.drain().filter(event => event.destination === 2 && event.status === 0x83).length, 3);
  }
  const liveInformationIds = ['engines.drums','engines.lightboard','engines.user','arps.trigger','expression.help','sensitivity.scope','sensitivity.customCurve','system.storage'];
  assert.deepEqual(information.filter(row => row.status === 'live' || row.group === 'engines').map(row => row.id).sort(), [...liveInformationIds].sort());
  assert.equal(counts[0], liveInformationIds.length);
  assert.equal(counts[1], information.length);
});

test('catalog replacement validates the proposed group and rejects incompatible track scales atomically', async () => {
  const { state } = await fresh();
  assert.ok(state.catalog(0, 2).ok, 'A valid new group must replace the previous group before catalog bounds are checked.');
  assert.equal(get(state, 'performance.scale'), 0);
  assert.ok(set(state, 'sequencer.scale', 1).ok);
  const before = state.info().revision;
  assert.equal(state.catalog(0, 1).ok, false, 'A narrower catalog cannot silently clip a stored track scale.');
  assert.equal(get(state, 'performance.scale'), 0);
  assert.equal(get(state, 'sequencer.scale'), 1);
  assert.equal(state.info().revision, before);
  assert.equal(state.allowed(index('performance.scale'), 1), true, 'Rejected replacement retains the previous catalog bounds.');
});

test('each stable view ID keeps its own C information slots and captured edit instance', async () => {
  const {state} = await fresh();
  for (const id of ['views.slot1','views.slot2','views.header','views.motion',...Array.from({length:6},(_,n)=>`views.detail${n+1}`)])
    assert.equal(state.instanceCount(index(id)),9,id);
  for (const [view,id,first,second] of [
    ['piano',1,'notes','outputs'],['simple',4,'channels','off'],
    ['chladni',5,'clock','arps'],['orbit',6,'pressure','counts'],['waves',7,'octaves','scale'],['fifths',8,'clock','notes'],
  ]) {
    assert.ok(choose(state,'lights.view',view).ok);
    assert.ok(choose(state,'views.slot1',first).ok);
    assert.ok(choose(state,'views.slot2',second).ok);
    assert.equal(state.instance(index('views.slot1'),id),ordinal('views.slot1',first));
    assert.equal(state.instance(index('views.slot2'),id),ordinal('views.slot2',second));
  }
  assert.ok(choose(state,'lights.view','piano').ok);
  assert.ok(state.begin(index('views.slot1')).ok);
  assert.ok(state.stage(ordinal('views.slot1','arps')).ok);
  assert.ok(choose(state,'lights.view','chladni').ok);
  assert.ok(state.commit().ok);
  assert.equal(get(state,'views.slot1'),ordinal('views.slot1','clock'));
  assert.equal(state.instance(index('views.slot1'),1),ordinal('views.slot1','arps'));
  for (const retiredOrdinal of [2,3]) assert.equal(set(state,'lights.view',retiredOrdinal).ok,false);
});

test('live Arps and planned strummer/track configurations remain independent', async () => {
  const engine = await fresh(), { state } = engine;
  assert.ok(choose(state, 'arps.target', 1).ok);
  assert.ok(set(state, 'arps.enabled', 1).ok);
  assert.ok(set(state, 'arps.channel', 3).ok);
  assert.ok(choose(state, 'arps.target', 2).ok);
  assert.equal(get(state, 'arps.enabled'), 0);
  assert.equal(get(state, 'arps.channel'), 3);
  assert.ok(set(state, 'arps.channel', 9).ok);
  assert.ok(choose(state, 'arps.target', 1).ok);
  assert.equal(get(state, 'arps.enabled'), 1);
  assert.equal(get(state, 'arps.channel'), 3);
  assert.ok(choose(state, 'chords.strummerTarget', 1).ok);
  assert.ok(set(state, 'chords.continuous', 1).ok);
  assert.ok(set(state, 'chords.channel', 4).ok);
  assert.ok(choose(state, 'chords.strummerTarget', 2).ok);
  assert.equal(get(state, 'chords.continuous'), 0);
  assert.equal(get(state, 'chords.channel'), 1);
  assert.ok(set(state, 'chords.channel', 8).ok);
  assert.ok(choose(state, 'chords.strummerTarget', 1).ok);
  assert.equal(get(state, 'chords.continuous'), 1);
  assert.equal(get(state, 'chords.channel'), 4);
  for (let track = 1; track <= 21; track++) {
    assert.ok(set(state, 'sequencer.track', track).ok);
    assert.ok(set(state, 'sequencer.name', `Voice ${track}`).ok);
    assert.ok(set(state, 'sequencer.channel', track % 16 + 1).ok);
  }
  for (let track = 21; track >= 1; track--) {
    assert.ok(set(state, 'sequencer.track', track).ok);
    assert.equal(get(state, 'sequencer.name'), `Voice ${track}`);
    assert.equal(get(state, 'sequencer.channel'), track % 16 + 1);
  }
  assert.equal(engine.arp.snapshot(0).enabled, true);
  assert.equal(engine.arp.snapshot(1).enabled, false);
  assert.deepEqual(engine.drain(), [], 'Enabling an Arp without held/admitted input emits no notes.');
  assert.equal(engine.stats().heldPads, 0);
});

test('editor commits retain the original instance when a selected target changes externally', async () => {
  const { state } = await fresh();
  assert.ok(choose(state, 'arps.target', 1).ok);
  assert.ok(state.begin(index('arps.enabled')).ok);
  assert.ok(state.stage(1).ok);
  assert.ok(choose(state, 'arps.target', 2).ok);
  assert.ok(state.commit().ok);
  assert.equal(get(state, 'arps.enabled'), 0);
  assert.ok(choose(state, 'arps.target', 1).ok);
  assert.equal(get(state, 'arps.enabled'), 1);
  assert.ok(state.begin(index('sequencer.name')).ok);
  assert.ok(state.stage('Original target').ok);
  assert.ok(set(state, 'sequencer.track', 21).ok);
  assert.ok(state.commit().ok);
  assert.notEqual(get(state, 'sequencer.name'), 'Original target');
  assert.ok(set(state, 'sequencer.track', 1).ok);
  assert.equal(get(state, 'sequencer.name'), 'Original target');
});

test('quick Arp state is the same shared C page used by menu edits and physical M controls', async () => {
  const engine = await fresh(), { state, firmware, arp } = engine;
  assert.ok(arp.openQuick(0).ok);
  assert.equal(state.info().quickOpen, 1);
  assert.equal(state.info().arpTarget, 0);
  assert.equal(state.info().quickCursor, 0);
  assert.equal(state.info().quickKeys, 1);
  assert.equal(arp.quickRows().length, 15);
  assert.ok(arp.quickRows().every(row => row >= 0 && row < state.rowCount));
  const transition = state.info().transitionRevision;
  assert.ok(state.navigate('down').ok);
  assert.equal(state.info().quickCursor, 1);
  assert.equal(state.info().transitionRevision, transition);
  assert.ok(state.navigate('right').ok);
  assert.ok(state.navigate('up').ok);
  assert.equal(arp.snapshot(0).enabled, false);
  assert.ok(state.navigate('right').ok);
  assert.equal(arp.snapshot(0).enabled, true);
  assert.equal(state.info().transitionRevision, transition);
  const shortcut = firmware.state().shortcut;
  for (const [control, expected] of [[2, false], [3, true]]) {
    assert.ok(firmware.control(3, control, true).ok);
    assert.ok(firmware.control(3, control, false).ok);
    assert.equal(arp.snapshot(control - 2).enabled, expected);
    assert.equal(firmware.state().shortcut, shortcut);
  }
  assert.ok(firmware.control(3, 4, true).ok);
  assert.ok(firmware.control(3, 4, false).ok);
  assert.match(state.info().message, /Hold only.*Latch/);
  assert.ok(firmware.control(3, 8, true).ok);
  assert.ok(firmware.control(3, 8, false).ok);
  assert.equal(state.info().arpTarget, 1);
  assert.ok(state.navigate('select').ok);
  assert.equal(state.info().quickOpen, 0);
  assert.equal(state.info().level, 0);
  assert.ok(firmware.control(3, 2, true).ok);
  assert.ok(firmware.control(3, 2, false).ok);
  assert.equal(firmware.state().shortcut, 2);
  assert.equal(arp.snapshot(0).enabled, false);
});

test('routing profiles admit only their own targets and preserve each target configuration', async () => {
  const { state } = await fresh();
  const target = index('midi.routeTarget');
  assert.ok(choose(state, 'midi.routeTarget', 'left').ok);
  assert.ok(set(state, 'midi.channel', 3).ok);
  assert.ok(choose(state, 'midi.routeTarget', 'right').ok);
  assert.ok(set(state, 'midi.channel', 5).ok);
  assert.ok(choose(state, 'midi.routingProfile', 'six-zones').ok);
  const zones = ['left-1', 'left-2', 'left-3', 'right-1', 'right-2', 'right-3'];
  for (const [i, id] of zones.entries()) {
    assert.equal(state.allowed(target, ordinal('midi.routeTarget', id)), true);
    assert.ok(choose(state, 'midi.routeTarget', id).ok);
    assert.ok(set(state, 'midi.channel', i + 7).ok);
  }
  const before = state.info().revision;
  assert.equal(choose(state, 'midi.routeTarget', 'left').ok, false);
  assert.equal(state.info().revision, before);
  assert.ok(choose(state, 'midi.routingProfile', 'split-sides').ok);
  assert.ok(choose(state, 'midi.routeTarget', 'left').ok);
  assert.equal(get(state, 'midi.channel'), 3);
  assert.ok(choose(state, 'midi.routeTarget', 'right').ok);
  assert.equal(get(state, 'midi.channel'), 5);
  assert.ok(choose(state, 'midi.routingProfile', 'six-zones').ok);
  for (const [i, id] of zones.entries()) {
    assert.ok(choose(state, 'midi.routeTarget', id).ok);
    assert.equal(get(state, 'midi.channel'), i + 7);
  }
});

test('C cross-field validation rejects inverted filters and non-press Program Change', async () => {
  const { state } = await fresh();
  let revision = state.info().revision;
  assert.equal(set(state, 'midi.remoteLow', 100).ok, false);
  assert.equal(get(state, 'midi.remoteLow'), 36);
  assert.equal(state.info().revision, revision);
  assert.ok(set(state, 'midi.remoteHigh', 110).ok);
  assert.ok(set(state, 'midi.remoteLow', 100).ok);
  revision = state.info().revision;
  assert.equal(set(state, 'midi.remoteHigh', 99).ok, false);
  assert.equal(state.info().revision, revision);
  assert.ok(choose(state, 'midi.assignmentTrigger', 'toggle').ok);
  assert.equal(choose(state, 'midi.assignmentKind', 'program-change').ok, false);
  assert.ok(choose(state, 'midi.assignmentTrigger', 'press').ok);
  assert.ok(choose(state, 'midi.assignmentKind', 'program-change').ok);
  revision = state.info().revision;
  assert.equal(choose(state, 'midi.assignmentTrigger', 'press-release').ok, false);
  assert.equal(state.info().revision, revision);
});

test('lower buttons use the live C mode selector with drafts, bounds and immutable held owners', async () => {
  const engine = await fresh(), { state, firmware } = engine;
  assert.ok(firmware.layout(layout).ok);
  const mode = index('controls.lowerMode');
  assert.equal(MENU_ROWS[mode].status, 'live');
  assert.equal(MENU_ROWS[mode].type, 'select');
  assert.deepEqual(MENU_ROWS[mode].options.map(option => option.value), ['stored-notes', 'octave-layers']);
  assert.equal(get(state, 'controls.lowerMode'), 1);
  assert.equal(firmware.state().lowerMode, 1);
  assert.equal(firmware.state().lowerMask, 8);
  assert.equal(firmware.state().lowerAvailable, 127);
  assert.ok(engine.press({ pad: 0, notes: [60, 64], channel: 3, mask: 5, velocity: 96 }).ok);
  engine.drain();
  const owner = engine.owner(0), revision = state.info().revision;
  for (const invalid of [-1, 2, NaN, 0.5, null, '1111111', 'stored-notes']) {
    assert.equal(set(state, 'controls.lowerMode', invalid).ok, false, String(invalid));
    assert.equal(state.info().revision, revision);
    assert.equal(firmware.state().lowerMode, 1);
    assert.deepEqual(engine.owner(0), owner);
  }
  assert.ok(state.begin(mode).ok);
  assert.ok(state.stage(0).ok);
  assert.equal(get(state, 'controls.lowerMode'), 1);
  assert.equal(firmware.state().lowerMode, 1);
  assert.ok(state.cancel().ok);
  assert.equal(firmware.state().lowerMode, 1);
  assert.ok(state.begin(mode).ok);
  assert.ok(state.stage(0).ok);
  assert.ok(state.commit().ok);
  assert.equal(get(state, 'controls.lowerMode'), 0);
  assert.equal(firmware.state().lowerMode, 0);
  assert.equal(firmware.state().lowerAvailable, 7, 'Empty stored slots are unavailable, rather than harmonic extensions.');
  assert.deepEqual(engine.owner(0), owner);
  assert.deepEqual(engine.drain(), []);
  assert.ok(choose(state, 'controls.lowerMode', 'octave-layers').ok);
  assert.equal(firmware.state().lowerMode, 1);
  assert.equal(firmware.state().lowerMask, 8);
  assert.deepEqual(engine.owner(0), owner);
  assert.deepEqual(engine.drain(), []);
  assert.ok(engine.release(0).ok);
  const releases = engine.drain();
  assert.equal(releases.length, 4);
  assert.ok(releases.every(event => event.status === 0x83 && [60, 64].includes(event.data1) && [0, 2].includes(event.destination)));
});

test('a held sensor rejects a live response commit without changing its owner or the open draft', async () => {
  const engine = await fresh(), { state, firmware } = engine;
  assert.ok(firmware.layout(layout).ok);
  let now = 0;
  for (let n = 0; n < 8; n++) assert.ok(firmware.sample(0, 100, now++).ok);
  for (let n = 0; n < 2; n++) assert.ok(firmware.sample(0, 2000, now++).ok);
  assert.equal(engine.isHeld(0), true);
  engine.drain();
  const owner = engine.owner(0), response = firmware.response(), before = state.info().revision;
  const trigger = get(state, 'sensitivity.attack');
  assert.ok(state.begin(index('sensitivity.attack')).ok);
  assert.ok(state.stage(trigger + 1).ok);
  assert.equal(state.commit().ok, false);
  assert.equal(state.info().revision, before);
  assert.equal(state.info().editing, 1);
  assert.equal(state.info().editValue, trigger + 1);
  assert.deepEqual(firmware.response(), response);
  assert.deepEqual(engine.owner(0), owner);
  assert.deepEqual(engine.drain(), []);
  for (let n = 0; n < 2; n++) assert.ok(firmware.sample(0, 100, now++).ok);
  assert.equal(engine.isHeld(0), false);
  assert.ok(state.commit().ok);
  assert.equal(get(state, 'sensitivity.attack'), trigger + 1);
  assert.equal(firmware.response().attack, trigger + 1);
});

test('live menu edits use the firmware adapter and preserve press-time ownership', async () => {
  const engine = await fresh(), { state, firmware } = engine;
  assert.ok(engine.press({ pad: 0, notes: [60, 64], channel: 3, mask: 5, velocity: 96 }).ok);
  engine.drain();
  const owner = engine.owner(0);
  assert.ok(set(state, 'chords.inversion', 2).ok);
  assert.equal(firmware.state().inversion, 2);
  assert.ok(set(state, 'performance.velocity', 88).ok);
  assert.equal(firmware.state().velocity, 88);
  assert.ok(set(state, 'lights.idle', 4).ok);
  assert.equal(firmware.state().lightIdle, 4);
  assert.deepEqual(engine.owner(0), owner);
  assert.deepEqual(engine.drain(), []);
  assert.ok(engine.release(0).ok);
  const releases = engine.drain();
  assert.equal(releases.length, 4);
  assert.ok(releases.every(event => event.status === 0x83 && [60, 64].includes(event.data1) && [0, 2].includes(event.destination)));
});

test('unimplemented actions cannot produce simulated success, storage or MIDI events', async () => {
  const engine = await fresh(), { state } = engine;
  const before = state.info().revision;
  const unavailable = ['storage.saveProfile','storage.loadProfile','storage.exportProfile','storage.importProfile','storage.deleteProfile','storage.resetSettings','storage.library','storage.usbDisk','system.sleep','system.shutdown'];
  assert.deepEqual(MENU_ROWS.filter(row => row.type === 'action' && row.status !== 'live').map(row => row.id), unavailable);
  for (const id of unavailable) {
    assert.equal(state.begin(index(id)).ok, false, id);
    assert.equal(set(state, id, 1).ok, false, id);
  }
  assert.ok(state.begin(index('system.installation')).ok, 'Information opens read-only without starting a service.');
  assert.equal(state.info().editing, 0);
  assert.equal(state.info().revision, before);
  assert.deepEqual(engine.drain(), []);
});

test('the C text/history bounds remain intact through WebAssembly calls', async () => {
  const { state } = await fresh();
  assert.ok(set(state, 'storage.profileName', 'N'.repeat(48)).ok);
  const before = state.info().revision;
  for (const value of ['X'.repeat(49), 'é'.repeat(25), '\u0000hidden']) {
    assert.equal(set(state, 'storage.profileName', value).ok, false);
    assert.equal(get(state, 'storage.profileName'), 'N'.repeat(48));
    assert.equal(state.info().revision, before);
  }
  for (let n = 0; n < 1000; n++) assert.ok(set(state, 'arps.enabled', n & 1).ok);
  const events = state.events();
  assert.equal(events.length, 64);
  assert.equal(events.at(-1).sequence, state.info().revision);
  assert.equal(events.at(-1).sequence - events[0].sequence, 63);
  assert.equal(state.frame({ scaleName: 'MAJOR C', animations: false }).length, 1024);
});

test('shared C startup waits for readiness then hands off exactly to Play', async () => {
  const engine = await fresh(), { state } = engine;
  const play = state.frame({ now: 0, animations: false });
  const revision = state.info().revision;
  assert.ok(state.boot.begin(42).ok);
  assert.ok(state.boot.progress(2).ok);
  const first = state.frame({ now: 42 });
  assert.notDeepEqual(first, play);
  for (let now = 84; now <= 1512; now += 42) state.frame({ now });
  assert.equal(state.boot.info().active, 1, 'Presentation cannot announce Play before real readiness.');
  assert.equal(state.boot.info().ready, 0);
  assert.equal(state.boot.info().stage, 2);
  assert.ok(state.boot.progress(4).ok);
  state.boot.ready();
  let last;
  for (let now = 1554; now <= 2310; now += 42) last = state.frame({ now });
  assert.equal(state.boot.info().active, 0);
  assert.deepEqual(last, play, 'The handoff endpoint is the current shared Play framebuffer.');
  assert.equal(state.info().revision, revision, 'Startup presentation never edits configuration.');
  assert.deepEqual(engine.drain(), []);
});

test('startup reduced motion is static until ready and then immediately shows Play', async () => {
  const { state } = await fresh();
  const play = state.frame({ now: 0, animations: false });
  state.boot.begin(1);
  const first = state.frame({ now: 1, animations: false });
  assert.deepEqual(state.frame({ now: 2000, animations: false }), first);
  assert.equal(state.boot.info().active, 1);
  state.boot.ready();
  assert.deepEqual(state.frame({ now: 2001, animations: false }), play);
  assert.equal(state.boot.info().active, 0);
});

test('startup replay preserves musical owners and yields immediately to playing', async () => {
  const engine = await fresh(), { state } = engine;
  assert.ok(engine.press({ pad: 0, notes: [60, 64], channel: 3, mask: 5, velocity: 94 }).ok);
  engine.drain();
  const owner = engine.owner(0), stats = engine.stats();
  state.boot.begin(0);
  state.boot.progress(4);
  state.boot.ready();
  state.frame({ now: 42 });
  assert.equal(state.boot.info().active, 0);
  assert.deepEqual(engine.owner(0), owner);
  assert.deepEqual(engine.stats(), stats);
  assert.deepEqual(engine.drain(), []);
  assert.ok(engine.release(0).ok);
  const releases = engine.drain();
  assert.equal(releases.length, 4);
  assert.ok(releases.every(event => event.status === 0x83 && [60, 64].includes(event.data1) && [0, 2].includes(event.destination)));
});

test('startup animation remains deterministic across millisecond wrap and yields to navigation', async () => {
  const a = await fresh(), b = await fresh();
  a.state.boot.begin(0); b.state.boot.begin(0xffffff00);
  a.state.boot.progress(4); b.state.boot.progress(4);
  a.state.boot.ready(); b.state.boot.ready();
  for (let delta = 0; delta <= 1512; delta += 42) {
    assert.deepEqual(a.state.frame({ now: delta }), b.state.frame({ now: (0xffffff00 + delta) >>> 0 }));
    assert.deepEqual(a.state.boot.info(), b.state.boot.info());
  }
  a.state.boot.begin(1554);
  assert.equal(a.state.boot.info().active, 1);
  assert.ok(a.state.navigate('select').ok);
  assert.equal(a.state.boot.info().active, 0);
  assert.equal(a.state.info().level, 1, 'The first control press still opens Settings.');
});

test('startup bridge rejects invalid clocks and milestone ordinals without mutation', async () => {
  const { state } = await fresh();
  state.boot.begin(5);
  const before = state.boot.info();
  for (const value of [-1, 4294967296, 1.5, NaN, Infinity, null, '3']) {
    assert.equal(state.boot.begin(value).ok, false);
    assert.deepEqual(state.boot.info(), before);
  }
  for (const value of [-1, 5, 1.5, NaN, null, '2']) {
    assert.equal(state.boot.progress(value).ok, false);
    assert.deepEqual(state.boot.info(), before);
  }
});
