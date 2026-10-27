import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { StudioModel } from '../../web/studio-model.js';
import { STUDIO_MENU, MENU_ROWS } from '../../web/studio-menu.js';
import { createCore } from '../../web/core.js';

const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));
const rows = MENU_ROWS;
const editable = row => !['action', 'info'].includes(row.type);
const index = id => rows.findIndex(row => row.id === id);
async function harness() {
  const engine = await createCore({ bytes });
  assert.ok(engine.state.catalog(0, 2).ok);
  const model = new StudioModel({ engine, groups: [{ index: 0, name: 'Major' }, { index: 1, name: 'Minor' }] });
  return { model, engine, state: engine.state };
}

test('the web model projects C navigation and remembers each category cursor', async () => {
  const { model, state } = await harness();
  assert.equal(model.snapshot().level, 'play');
  model.navigate('select');
  assert.equal(state.info().level, 1);
  assert.equal(model.snapshot().level, 'groups');
  const group = model.snapshot().selectedId;
  model.navigate('right');
  model.navigate('down');
  const selected = model.snapshot().selectedId;
  const revision = state.info().transitionRevision;
  model.navigate('down');
  assert.equal(state.info().transitionRevision, revision);
  model.navigate('up');
  model.navigate('left');
  model.navigate('right');
  assert.equal(model.snapshot().groupId, group);
  assert.equal(model.snapshot().selectedId, selected);
  model.navigate('left');
  model.navigate('left');
  assert.equal(model.snapshot().level, 'play');
  assert.equal(state.info().level, 0);
  assert.equal(model.navigate('unknown'), false);
});

test('track settings are a scoped C menu with a direct return and no global rows', async () => {
  const { model, engine, state } = await harness();
  const sequencer = model.groups.find(group => group.id === 'sequencer');
  assert.ok(model.focus('sequencer', 'sequencer.open'));
  const globalRows = model.visibleRows(sequencer).map(row => row.id);
  assert.ok(globalRows.includes('sequencer.tempo'));
  assert.ok(!globalRows.includes('sequencer.channel'));
  assert.ok(engine.sequencer.createTrack().ok);
  assert.ok(engine.sequencer.configureView({selectedStep:7}).ok);
  assert.ok(engine.sequencer.action('trackOptions').ok);
  const snapshot = model.snapshot();
  assert.equal(snapshot.level, 'rows');
  assert.equal(snapshot.trackOptionsOpen, true);
  assert.equal(snapshot.trackTarget, 0);
  assert.equal(snapshot.groupLabel, 'T1 settings');
  assert.equal(state.info().trackOptionsOpen, 1);
  const trackRows = snapshot.rows.map(row => row.id);
  assert.ok(trackRows.includes('sequencer.channel'));
  assert.ok(!trackRows.includes('sequencer.tempo'));
  assert.ok(!trackRows.includes('sequencer.clearSong'));
  assert.ok(!trackRows.includes('sequencer.open'));
  assert.equal(snapshot.rows[snapshot.cursor].id, snapshot.selectedId);
  assert.equal(model.focus('midi', 'midi.channel'), false);
  assert.equal(model.focus('sequencer', 'sequencer.clearSong'), false);
  assert.ok(model.focus('sequencer', 'sequencer.channel'));
  assert.ok(model.beginEdit('sequencer.channel'));
  assert.ok(model.setEditValue(2));
  assert.ok(model.commitEdit().ok);
  assert.equal(engine.sequencer.track(0).channel, 1);
  assert.ok(model.navigate('left'));
  assert.equal(model.snapshot().trackOptionsOpen, false);
  assert.equal(engine.sequencer.snapshot().view, 'steps');
  assert.equal(engine.sequencer.snapshot().selectedStep, 7);
  assert.equal(state.info().level, 0);
  assert.ok(model.navigate('select'));
  assert.equal(engine.sequencer.snapshot().open, false);
  assert.ok(model.focus('sequencer', 'sequencer.open'));
  assert.deepEqual(model.visibleRows(sequencer).map(row => row.id), globalRows);
  assert.ok(engine.sequencer.open('steps').ok);
  assert.ok(engine.sequencer.action('trackOptions').ok);
  model.play();
  assert.equal(model.snapshot().level, 'play');
  assert.equal(engine.sequencer.snapshot().open, false);
  assert.equal(model.snapshot().trackOptionsOpen, false);
  assert.ok(engine.sequencer.open('steps').ok);
  assert.ok(engine.sequencer.action('trackOptions').ok);
  model.open();
  assert.equal(model.snapshot().level, 'groups');
  assert.equal(engine.sequencer.snapshot().open, false);
  assert.equal(model.snapshot().trackOptionsOpen, false);
});

test('Center returns to Play from categories, rows and drafts; Right applies before leaving', async () => {
  const { model, state, engine } = await harness();
  const id = 'performance.velocity';
  const original = model.get(id);
  model.navigate('select');
  assert.equal(model.snapshot().level, 'groups');
  model.navigate('select');
  assert.equal(model.snapshot().level, 'play');
  model.navigate('select');
  model.navigate('right');
  assert.equal(model.snapshot().level, 'rows');
  model.navigate('select');
  assert.equal(model.snapshot().level, 'play');
  model.focus('performance', id);
  model.beginEdit(id);
  model.adjust(1);
  assert.equal(state.info().editValue, original + 1);
  model.navigate('select');
  assert.equal(model.snapshot().level, 'play');
  assert.equal(model.snapshot().editing, false);
  assert.equal(model.get(id), original);
  assert.equal(engine.firmware.state().velocity, original);
  model.focus('performance', id);
  model.beginEdit(id);
  model.adjust(1);
  assert.equal(model.navigate('right'), true);
  assert.equal(model.snapshot().editing, false);
  model.navigate('select');
  assert.equal(model.snapshot().level, 'play');
  assert.equal(model.get(id), original + 1);
  assert.equal(engine.firmware.state().velocity, original + 1);
});

test('web edits use C drafts; cancellation never applies a value and commit applies once', async () => {
  const { model, state, engine } = await harness();
  const id = 'performance.velocity';
  const original = model.get(id);
  assert.equal(model.beginEdit(id), true);
  assert.equal(model.adjust(1), true);
  assert.equal(state.info().editRow, index(id));
  assert.equal(state.info().editValue, original + 1);
  assert.equal(model.get(id), original);
  assert.equal(engine.firmware.state().velocity, original);
  assert.equal(model.cancelEdit(), true);
  assert.equal(model.get(id), original);
  model.beginEdit(id);
  model.setEditValue(original + 2);
  const before = state.info().revision;
  assert.equal(model.commitEdit().ok, true);
  assert.equal(engine.firmware.state().velocity, original + 2);
  assert.equal(model.get(id), original + 2);
  assert.equal(state.info().revision, before + 1);
  assert.equal(model.snapshot().editing, false);
  assert.equal(model.commitEdit().ok, false);
  assert.equal(state.info().revision, before + 1);
});

test('external C settings remain authoritative in the web projection', async () => {
  const { model, state, engine } = await harness();
  assert.ok(state.set(index('performance.velocity'), 77).ok);
  assert.equal(model.get('performance.velocity'), 77);
  assert.equal(engine.firmware.state().velocity, 77);
  assert.equal(model.beginEdit('performance.velocity'), true);
  assert.equal(model.snapshot().edit.value, 77);
  model.cancelEdit();
  assert.ok(engine.firmware.expression(84, true).ok);
  assert.equal(model.get('performance.velocity'), 84);
  assert.equal(model.get('performance.aftertouch'), true);
});

test('Sensitivity picks a pad silently and exposes 96 saved overrides plus separate half defaults',async()=>{
  const {model,engine,state}=await harness();
  const global=engine.firmware.response(),globalVelocity=engine.firmware.velocity();
  assert.ok(model.focus('sensitivity','sensitivity.pick'));
  assert.ok(model.beginEdit('sensitivity.pick'));
  assert.equal(state.pickingPad(),true);
  assert.ok(engine.firmware.pressPad(95).ok);
  assert.equal(state.info().sensitivityTarget,96);
  assert.equal(engine.isHeld(95),false);assert.deepEqual(engine.drain(),[]);
  assert.ok(engine.release(95).ok);assert.equal(state.pickingPad(),false);
  assert.equal(model.get('sensitivity.target'),96);
  assert.match(model.format(model.definitions.get('sensitivity.target')),/R4 C12/);
  assert.ok(model.set('sensitivity.attack',1500).ok);
  assert.ok(engine.firmware.setVelocity(95,{...globalVelocity,mode:1}).ok);
  assert.ok(model.set('sensitivity.velocityCurve',2).ok);
  assert.equal(engine.firmware.response(95).attack,1500);
  assert.equal(engine.firmware.response(95).overridden,1);
  assert.equal(engine.firmware.velocity(95).mode,1);
  assert.equal(engine.firmware.velocity(95).curve,2);
  assert.equal(engine.firmware.velocity(95).overridden,1);
  const attack=index('sensitivity.attack');
  assert.equal(state.instanceCount(attack),98);
  assert.equal(state.instance(attack,95),1500);
  assert.equal(state.instance(attack,96),global.attack);
  assert.equal(state.instance(attack,97),engine.firmware.response(-2).attack);
  assert.equal(state.instance(attack,98),undefined);
  const draft=model.exportDraft();
  assert.equal(draft.instances.pads.length,96);
  assert.equal(draft.instances.pads[95]['sensitivity.attack'],1500);
  assert.equal(draft.version,2);
  assert.equal(draft.instances.padClusters.left['sensitivity.attack'],global.attack);
  assert.equal(draft.instances.padClusters.right['sensitivity.attack'],engine.firmware.response(-2).attack);
  assert.ok(model.focus('sensitivity','sensitivity.customCurve'));
  assert.equal(state.info().infoOpen,0);assert.ok(model.beginEdit('sensitivity.customCurve'));
  assert.equal(state.info().infoOpen,1);
  const graph=state.frame({now:100,animations:false});assert.ok(graph.some(byte=>byte));
  assert.ok(model.navigate('left'));assert.equal(state.info().infoOpen,0);
  assert.ok(model.focus('sensitivity','sensitivity.inherit'));assert.ok(model.beginEdit('sensitivity.inherit'));
  assert.deepEqual(engine.firmware.response(95),{...global,overridden:0});
  assert.deepEqual(engine.firmware.velocity(95),{...globalVelocity,overridden:0});
  assert.deepEqual(engine.drain(),[]);
});

test('Performance velocity scope, Mixed drafts and MPE aliases share C state',async()=>{
  const {model,engine,state}=await harness();
  assert.ok(model.set('sensitivity.target',12).ok);
  assert.ok(model.set('midi.routeTarget','right').ok);
  assert.ok(model.set('performance.velocityTarget',1).ok);
  assert.ok(model.set('sensitivity.velocityMode',1).ok);
  assert.equal(engine.firmware.velocity(0).mode,1);
  assert.equal(engine.firmware.velocity(48).mode,0);
  assert.ok(model.set('performance.velocityTarget',0).ok);
  assert.equal(model.get('sensitivity.velocityMode'),2);
  assert.deepEqual(model.options(model.definitions.get('sensitivity.velocityMode')).map(option=>option.value),[0,1]);
  assert.equal(state.instanceCount(index('sensitivity.velocityMode')),3);
  assert.ok(model.beginEdit('sensitivity.velocityMode'));
  assert.equal(model.snapshot().edit.value,2);
  assert.equal(model.commitEdit().ok,false);
  assert.ok(model.adjust(1));assert.equal(model.snapshot().edit.value,0);
  assert.ok(model.commitEdit().ok);
  assert.equal(model.get('sensitivity.target'),12);
  assert.equal(model.get('midi.routeTarget'),'right');
  const draft=model.exportDraft();
  assert.equal(draft.instances.velocitySides.both['sensitivity.velocityMode'],0);
  assert.equal(draft.instances.velocitySides.left['sensitivity.velocityMode'],0);
  assert.equal(draft.instances.velocitySides.right['sensitivity.velocityMode'],0);
  assert.equal(draft.instances.routes['whole-board']['sensitivity.velocityMode'],undefined);
  assert.ok(model.set('performance.mpe',true).ok);
  assert.equal(model.get('expression.enabled'),true);
  assert.deepEqual(engine.drain(),[]);
  assert.ok(model.set('expression.enabled',false).ok);
  assert.equal(model.get('performance.mpe'),false);
  assert.deepEqual(engine.drain(),[]);
});

test('Performance response controls share MIDI route state in both directions without changing other scopes', async () => {
  const { model, engine, state } = await harness();
  assert.ok(model.set('sensitivity.target', 12).ok);
  assert.ok(model.set('performance.velocityTarget', 2).ok);
  assert.ok(model.set('performance.routeTarget', 'left').ok);
  assert.equal(model.get('midi.routeTarget'), 'left');
  for (const [alias, canonical, value] of [
    ['performance.pressureMode', 'midi.pressure', 'channel'],
    ['performance.pressureBend', 'midi.pressureBend', 'up'],
    ['performance.bendRange', 'midi.bendRange', 12],
  ]) {
    assert.equal(state.instanceCount(index(alias)), 9, alias);
    assert.ok(model.set(alias, value).ok, alias);
    assert.equal(model.get(canonical), value, canonical);
  }
  assert.ok(model.set('midi.routeTarget', 'right').ok);
  assert.equal(model.get('performance.routeTarget'), 'right');
  assert.equal(model.get('performance.pressureMode'), 'off');
  assert.equal(model.get('performance.pressureBend'), 'off');
  assert.equal(model.get('performance.bendRange'), 2);
  assert.ok(model.set('midi.pressure', 'poly').ok);
  assert.ok(model.set('midi.pressureBend', 'down').ok);
  assert.ok(model.set('midi.bendRange', 7).ok);
  assert.equal(model.get('performance.pressureMode'), 'poly');
  assert.equal(model.get('performance.pressureBend'), 'down');
  assert.equal(model.get('performance.bendRange'), 7);
  assert.ok(model.set('performance.routeTarget', 'left').ok);
  assert.equal(model.get('performance.pressureMode'), 'channel');
  assert.equal(model.get('performance.pressureBend'), 'up');
  assert.equal(model.get('performance.bendRange'), 12);
  assert.equal(model.get('sensitivity.target'), 12);
  assert.equal(model.get('performance.velocityTarget'), 2);
  const draft = model.exportDraft();
  for (const route of ['left', 'right']) {
    const values = draft.instances.routes[route];
    assert.equal(values['performance.pressureMode'], values['midi.pressure'], route);
    assert.equal(values['performance.pressureBend'], values['midi.pressureBend'], route);
    assert.equal(values['performance.bendRange'], values['midi.bendRange'], route);
  }
  assert.equal(draft.instances.routes.left['performance.pressureMode'], 'channel');
  assert.equal(draft.instances.routes.right['performance.pressureMode'], 'poly');
  assert.deepEqual(engine.drain(), [], 'Editing response policies never sends receiver setup or notes.');
});

test('Performance Response for follows all routing-profile filters and retains independent route policies', async () => {
  const { model, engine } = await harness();
  const alias = model.definitions.get('performance.routeTarget');
  const canonical = model.definitions.get('midi.routeTarget');
  for (const [profile, allowed] of [
    ['whole-board', ['whole-board']],
    ['split-sides', ['left', 'right']],
    ['six-zones', ['left-1', 'left-2', 'left-3', 'right-1', 'right-2', 'right-3']],
  ]) {
    assert.ok(model.set('midi.routingProfile', profile).ok);
    assert.deepEqual(model.options(alias).map(option => option.value), allowed, profile);
    assert.deepEqual(model.options(alias).map(option => option.value), model.options(canonical).map(option => option.value), profile);
    for (const option of alias.options) {
      const before = model.get(alias.id);
      const result = model.set(alias.id, option.value);
      assert.equal(result.ok, allowed.includes(option.value), `${profile}: ${option.value}`);
      assert.equal(model.get(canonical.id), result.ok ? option.value : before);
    }
    for (const [n, target] of allowed.entries()) {
      assert.ok(model.set(alias.id, target).ok);
      assert.ok(model.set('performance.pressureMode', ['off', 'poly', 'channel'][n % 3]).ok);
      assert.ok(model.set('performance.pressureBend', ['off', 'up', 'down'][n % 3]).ok);
      assert.ok(model.set('performance.bendRange', n + 3).ok);
    }
  }
  for (const [profile, targets] of [
    ['whole-board', ['whole-board']], ['split-sides', ['left', 'right']],
    ['six-zones', ['left-1', 'left-2', 'left-3', 'right-1', 'right-2', 'right-3']],
  ]) {
    assert.ok(model.set('midi.routingProfile', profile).ok);
    for (const [n, target] of targets.entries()) {
      assert.ok(model.set('midi.routeTarget', target).ok);
      assert.equal(model.get('performance.pressureMode'), ['off', 'poly', 'channel'][n % 3], target);
      assert.equal(model.get('performance.pressureBend'), ['off', 'up', 'down'][n % 3], target);
      assert.equal(model.get('performance.bendRange'), n + 3, target);
    }
  }
  assert.deepEqual(engine.drain(), []);
});

test('Performance response drafts cancel cleanly and commit to the route captured at entry', async () => {
  const { model, engine } = await harness();
  assert.ok(model.set('performance.routeTarget', 'left').ok);
  const saved = engine.settings.encode();
  assert.ok(model.beginEdit('performance.pressureMode'));
  assert.ok(model.setEditValue('channel'));
  assert.equal(model.get('midi.pressure'), 'off');
  assert.ok(model.cancelEdit());
  assert.equal(model.get('performance.pressureMode'), 'off');
  assert.deepEqual(engine.settings.encode(), saved);
  assert.ok(model.beginEdit('performance.pressureMode'));
  assert.ok(model.setEditValue('poly'));
  assert.ok(model.set('midi.routeTarget', 'right').ok);
  assert.ok(model.commitEdit().ok);
  assert.equal(model.get('performance.pressureMode'), 'off', 'Changing the selector does not retarget the LEFT draft.');
  assert.ok(model.set('performance.routeTarget', 'left').ok);
  assert.equal(model.get('midi.pressure'), 'poly');
  const beforeInvalid = engine.settings.encode();
  for (const [id, value] of [
    ['performance.pressureMode', 'pitch-bend'], ['performance.pressureBend', 'channel'],
    ['performance.bendRange', 0], ['performance.bendRange', 25],
  ]) assert.equal(model.set(id, value).ok, false, id);
  assert.deepEqual(engine.settings.encode(), beforeInvalid);
  assert.deepEqual(engine.drain(), []);
});

test('Performance response aliases restore through the existing V15 route codec without extra storage', async () => {
  const source = await harness();
  for (const [target, pressure, bend, range] of [
    ['left', 'poly', 'up', 4], ['right', 'channel', 'down', 17],
  ]) {
    assert.ok(source.model.set('performance.routeTarget', target).ok);
    assert.ok(source.model.set('performance.pressureMode', pressure).ok);
    assert.ok(source.model.set('performance.pressureBend', bend).ok);
    assert.ok(source.model.set('performance.bendRange', range).ok);
  }
  const payload = source.engine.settings.encode();
  assert.equal(source.engine.settings.payloadVersion(payload), 15);
  assert.equal(payload.length, 5633);
  const cold = await harness();
  assert.equal(cold.engine.settings.apply(payload), true);
  for (const [target, pressure, bend, range] of [
    ['left', 'poly', 'up', 4], ['right', 'channel', 'down', 17],
  ]) {
    assert.ok(cold.model.set('midi.routeTarget', target).ok);
    assert.equal(cold.model.get('performance.pressureMode'), pressure);
    assert.equal(cold.model.get('midi.pressure'), pressure);
    assert.equal(cold.model.get('performance.pressureBend'), bend);
    assert.equal(cold.model.get('midi.pressureBend'), bend);
    assert.equal(cold.model.get('performance.bendRange'), range);
    assert.equal(cold.model.get('midi.bendRange'), range);
  }
  assert.deepEqual(cold.engine.settings.encode(), payload);
  assert.deepEqual(cold.engine.drain(), [], 'Restoring saved expression policies emits no MIDI.');
});

test('the scale shortcut picker exposes the catalog and layout cycling through canonical C assignments', async () => {
  const {model,engine,state} = await harness();
  const assignment = model.definitions.get('engines.assignment');
  assert.equal(assignment.dynamic,'scale-shortcuts');
  assert.deepEqual(model.options(assignment).map(option=>option.value),[0,1,70]);
  assert.ok(model.set('engines.shortcut','U1').ok);
  assert.ok(model.set('engines.assignment',1).ok);
  assert.equal(engine.firmware.scaleShortcut(9),1);
  assert.ok(model.set('engines.shortcut','U2').ok);
  assert.ok(model.set('engines.assignment',70).ok);
  assert.equal(model.format(assignment),'Cycle layout');
  assert.equal(engine.firmware.scaleShortcut(10),70);
  assert.equal(state.instanceCount(index('engines.assignment')),12);
  assert.ok(model.set('engines.shortcut','U1').ok);
  assert.equal(model.get('engines.assignment'),1);
  assert.equal(model.format(assignment),'Minor');
});

test('a real held sensor rejects response edits without changing owners or closing the draft', async () => {
  const { model, engine, state } = await harness();
  const layout = Array.from({ length: 96 }, () => ({ notes: [60, 64, 67, 0, 0, 0, 0], mask: 7, color: 1 }));
  assert.ok(engine.firmware.layout(layout).ok);
  for (let n = 0; n < 8; n++) assert.ok(engine.firmware.sample(0, 100, n).ok);
  assert.ok(engine.firmware.sample(0, 2000, 8).ok);
  assert.ok(engine.firmware.sample(0, 2000, 9).ok);
  engine.drain();
  const owner = engine.owner(0), original = model.get('sensitivity.attack');
  assert.equal(model.beginEdit('sensitivity.attack'), true);
  assert.equal(model.setEditValue(original + 1), true);
  const before = state.info().revision;
  assert.equal(model.commitEdit().ok, false);
  assert.equal(model.get('sensitivity.attack'), original);
  assert.equal(model.snapshot().editing, true);
  assert.equal(state.info().revision, before);
  assert.deepEqual(engine.owner(0), owner);
  assert.deepEqual(engine.drain(), []);
  assert.match(model.snapshot().notice, /applied|Release|release|affected/i);
});

test('live Arp and USB recording permissions change C runtime without draft-only edits', async () => {
  const { model, state, engine } = await harness();
  assert.equal(model.set('arps.enabled', true).ok, true);
  assert.equal(model.get('arps.enabled'), true);
  assert.equal(state.get(index('arps.enabled')), 1);
  assert.equal(engine.arp.snapshot(0).enabled, true);
  assert.equal(model.set('midi.remoteRecord', true).ok, true);
  assert.equal(state.get(index('midi.remoteRecord')), 1);
  assert.deepEqual(engine.drain(), []);
  assert.equal(engine.stats().heldPads, 0);
  assert.equal(engine.sequencer.snapshot().remote.record, true);
  assert.equal(engine.sequencer.snapshot().capture.state, 'off');
  assert.equal(model.snapshot().draftChanges, 0, 'Both are implemented configuration bindings; enabling recording never arms a take.');
});

test('invalid web values preserve C configuration and revision', async () => {
  const { model, state } = await harness();
  const invalid = [
    ['performance.velocity', 0], ['performance.velocity', 128], ['performance.velocity', 1.5],
    ['arps.enabled', 1], ['arps.enabled', 'true'], ['midi.assignmentKind', {}],
    ['midi.remoteLow', 100], ['chords.noteSlots', '111111x'], ['missing-setting', 0],
  ];
  for (const [id, value] of invalid) {
    const before = model.exportDraft(), revision = state.info().revision;
    assert.equal(model.set(id, value).ok, false, `${id}: ${JSON.stringify(value)}`);
    assert.deepEqual(model.exportDraft(), before);
    assert.equal(state.info().revision, revision);
  }
});

test('unknown focus targets preserve the C cursor and an open editor', async () => {
  const { model, state } = await harness();
  assert.equal(model.focus('performance', 'performance.velocity'), true);
  assert.equal(model.beginEdit('performance.velocity'), true);
  const before = model.snapshot(), native = state.info();
  assert.equal(model.focus('missing-group'), false);
  assert.equal(model.focus('performance', 'missing-row'), false);
  assert.deepEqual(model.snapshot(), before);
  assert.deepEqual(state.info(), native);
});

test('planned actions stay unavailable while information rows are read-only through the web model', async () => {
  const { model, engine, state } = await harness();
  const before = state.info().revision;
  for (const row of rows.filter(row => !editable(row) && row.status !== 'live')) {
    assert.equal(model.beginEdit(row.id), row.type === 'info', row.id);
    assert.equal(model.edit, null, row.id);
    if (row.type === 'info') {
      assert.equal(model.format(row), row.default, row.id);
      assert.equal(model.compact(row), row.group === 'engines' ? 'Planned' : 'INFO', row.id);
      assert.equal(model.snapshot().notice, '', row.id);
    }
    assert.equal(model.set(row.id, true).ok, false, row.id);
  }
  assert.equal(state.info().revision, before);
  assert.deepEqual(engine.drain(), []);
});

test('future visibility filters presentation without changing canonical C row indices', async () => {
  const { model, state } = await harness();
  const views = model.groups.find(group => group.id === 'views');
  for (const [view, viewRows] of [
    ['notes', ['lights.view', 'views.header', ...Array.from({length:6},(_,n)=>`views.detail${n+1}`), 'lights.animations']],
    ['piano', ['lights.view', 'views.slot1', 'views.slot2', 'views.header', 'lights.animations']],
    ['simple', ['lights.view', 'views.slot1', 'views.slot2', 'views.header', 'lights.animations']],
    ...['chladni','orbit','waves','fifths'].map(view => [view, ['lights.view', 'views.slot1', 'views.slot2', 'views.header', 'views.motion', 'lights.animations']]),
  ]) {
    assert.ok(model.set('lights.view', view).ok);
    for (const future of [false, true]) {
      assert.ok(state.designMode(future).ok);
      for (const group of model.groups) {
        const nativeVisible = group.rows.filter(row => state.visible(index(row.id)));
        const visible = model.visibleRows(group);
        const present = row => row.status === 'live' || group.id === 'engines' && row.type === 'info';
        assert.deepEqual(visible, nativeVisible.filter(present));
        assert.deepEqual(model.visibleRows(group, true), nativeVisible);
        if (!future) assert.ok(nativeVisible.every(present));
        for (const row of visible) {
          assert.equal(model.focus(group.id, row.id), true);
          assert.equal(state.info().cursor, group.rows.indexOf(row));
        }
      }
      assert.deepEqual(model.visibleRows(views).map(row => row.id), viewRows);
    }
  }
  assert.ok(model.set('lights.view', 'notes').ok);
  assert.equal(model.focus('views', 'views.slot2'), false, 'Detailed uses its six rows instead of the compact information slots.');
  assert.equal(model.focus('views', 'views.motion'), false, 'A graphical motion option cannot become an editor in Detailed.');
});

test('the Guitar shortcut runs its C action and unsupported play views stay filtered', async () => {
  const { model, engine } = await harness();
  const shortcut = model.definitions.get('performance.guitarShortcut');
  const beforeView = model.get('lights.view');
  assert.equal(shortcut.status, 'live');
  assert.equal(model.format(shortcut), 'Run');
  assert.equal(model.beginEdit(shortcut.id), true);
  assert.equal(model.edit, null);
  assert.equal(model.get('performance.melodyLayout'), 'guitar');
  assert.equal(engine.firmware.state().melodyLayout, 5);
  const overview = model.definitions.get('lights.view');
  assert.deepEqual(model.options(overview).map(option => option.value), ['notes', 'piano', 'simple', 'chladni', 'orbit', 'waves', 'fifths']);
  for (const unavailable of ['layouts','routes','removed-layouts','removed-routes','pretty','tracks'])
    assert.equal(model.set(overview.id, unavailable).ok, false, unavailable);
  assert.equal(model.get(overview.id), beforeView, 'Guitar changes the note layout while preserving the chosen screen.');
});

test('web selectors convert identities while retaining independent C processor instances', async () => {
  const { model, state, engine } = await harness();
  for (const [target, enabled, channel] of [[1, true, 3], [2, false, 9]]) {
    assert.ok(model.set('arps.target', target).ok);
    assert.ok(model.set('arps.enabled', enabled).ok);
    assert.ok(model.set('arps.channel', channel).ok);
  }
  assert.ok(model.set('arps.target', 1).ok);
  assert.equal(state.get(index('arps.target')), 0, 'Native selects use ordinals; the web displays their original identities.');
  assert.equal(model.get('arps.enabled'), true);
  assert.equal(model.get('arps.channel'), 3);
  assert.ok(model.set('arps.target', 2).ok);
  assert.equal(model.get('arps.enabled'), false);
  assert.equal(model.get('arps.channel'), 9);
  assert.ok(model.set('chords.strummerTarget', 1).ok);
  assert.ok(model.set('chords.channel', 4).ok);
  assert.ok(model.set('chords.strummerTarget', 2).ok);
  assert.equal(model.get('chords.channel'), 1);
  assert.ok(model.set('chords.channel', 8).ok);
  assert.ok(model.set('chords.strummerTarget', 1).ok);
  assert.equal(model.get('chords.channel'), 4);
  assert.ok(model.set('sequencer.track', 1).ok);
  assert.ok(model.set('sequencer.name', 'Bass').ok);
  assert.ok(model.set('sequencer.track', 21).ok);
  assert.ok(model.set('sequencer.name', 'Lead').ok);
  assert.ok(model.set('sequencer.track', 1).ok);
  assert.equal(model.get('sequencer.name'), 'Bass');
  assert.ok(model.set('sequencer.track', 21).ok);
  assert.equal(model.get('sequencer.name'), 'Lead');
  assert.deepEqual(engine.drain(), []);
});

test('the quick-page snapshot follows C Arp rows and target instead of the previous category', async () => {
  const { model, state, engine } = await harness();
  assert.ok(model.focus('performance', 'performance.velocity'));
  const oldGroup = state.info().group;
  assert.ok(engine.arp.configure(1, { gatePercent: 63, pattern: 'down', channel: 8 }).ok);
  assert.ok(engine.arp.openQuick(1).ok);
  const snapshot = model.snapshot();
  assert.equal(state.info().group, oldGroup, 'Opening quick controls preserves the full-menu cursor history.');
  assert.equal(snapshot.level, 'arp');
  assert.equal(snapshot.groupId, 'arps');
  assert.equal(snapshot.groupLabel, 'Arp 2');
  assert.equal(snapshot.arpTarget, 1);
  assert.equal(snapshot.quickKeys, true);
  assert.equal(snapshot.selectedId, 'arps.target');
  assert.equal(snapshot.cursor, 0);
  assert.deepEqual(snapshot.rows.map(row => row.id), engine.arp.quickRows().map(row => MENU_ROWS[row].id));
  assert.equal(snapshot.count, 15);
  assert.equal(snapshot.rows.find(row => row.id === 'arps.target').value, '2');
  assert.equal(snapshot.rows.find(row => row.id === 'arps.gate').value, '63');
  assert.equal(snapshot.rows.find(row => row.id === 'arps.pattern').value, 'Down');
  assert.equal(snapshot.rows.find(row => row.id === 'arps.channel').value, '9');
});

test('clicking quick settings navigates C without leaving the page or applying a cancelled draft', async () => {
  const { model, state, engine } = await harness();
  assert.ok(engine.arp.openQuick(0).ok);
  const transition = state.info().transitionRevision;
  assert.equal(model.focusQuick('arps.gate'), true);
  assert.equal(model.snapshot().selectedId, 'arps.gate');
  assert.equal(model.snapshot().cursor, state.info().quickCursor);
  assert.equal(state.info().transitionRevision, transition, 'Moving the arrow does not start a slide.');
  assert.equal(model.beginEdit('arps.gate'), true);
  assert.equal(model.setEditValue(71), true);
  assert.equal(engine.arp.snapshot(0).gatePercent, 50);
  assert.equal(model.focusQuick('arps.enabled'), true);
  assert.equal(model.snapshot().editing, false);
  assert.equal(model.snapshot().level, 'arp');
  assert.equal(model.snapshot().selectedId, 'arps.enabled');
  assert.equal(engine.arp.snapshot(0).gatePercent, 50);
  const before = model.snapshot();
  assert.equal(model.focusQuick('performance.velocity'), false);
  assert.equal(model.focusQuick('missing-setting'), false);
  assert.deepEqual(model.snapshot(), before);
  assert.equal(model.beginEdit('arps.enabled'), true);
  assert.equal(model.setEditValue(true), true);
  assert.equal(model.commitEdit().ok, true);
  assert.equal(engine.arp.snapshot(0).enabled, true);
  assert.equal(model.snapshot().rows.find(row => row.id === 'arps.enabled').value, 'ON');
  assert.equal(state.info().transitionRevision, transition);
  assert.equal(model.navigate('select'), true);
  assert.equal(model.snapshot().level, 'play');
  assert.equal(model.focusQuick('arps.enabled'), false);
});

test('quick page has independent C targets and closes when opening the full category or Play', async () => {
  const { model, engine } = await harness();
  assert.ok(engine.arp.openQuick(0).ok);
  assert.equal(model.focusQuick('arps.quickKeys'), true);
  assert.equal(model.beginEdit('arps.quickKeys'), true);
  assert.equal(model.setEditValue(false), true);
  assert.equal(model.commitEdit().ok, true);
  assert.equal(model.snapshot().quickKeys, false);
  assert.ok(engine.arp.select(1).ok);
  assert.equal(model.snapshot().groupLabel, 'Arp 2');
  assert.equal(model.snapshot().selectedId, 'arps.quickKeys');
  assert.equal(model.focus('arps', 'arps.pattern'), true);
  assert.equal(model.snapshot().quickOpen, false);
  assert.equal(model.snapshot().level, 'rows');
  assert.equal(model.snapshot().selectedId, 'arps.pattern');
  assert.ok(engine.arp.openQuick(0).ok);
  model.play();
  assert.equal(model.snapshot().level, 'play');
  assert.equal(model.snapshot().quickOpen, false);
});

test('Arp selectors expose implemented patterns and identify Hold and future behavior honestly', async () => {
  const { model, engine } = await harness();
  const pattern = model.definitions.get('arps.pattern');
  assert.equal(pattern.status, 'live');
  assert.equal(pattern.type, 'select');
  assert.deepEqual(model.options(pattern).map(option => option.value), ['up', 'down', 'up-down', 'down-up', 'as-played']);
  assert.ok(model.set('arps.pattern', 'as-played').ok);
  assert.equal(engine.arp.snapshot(0).pattern, 'as-played');
  const trigger = model.definitions.get('arps.trigger');
  assert.equal(trigger.type, 'info');
  assert.match(model.format(trigger), /Hold only.*Latch unavailable/);
  assert.equal(model.set(trigger.id, 'latch').ok, false);
  assert.equal(model.definitions.get('arps.releaseTail').status, 'design');
  assert.equal(model.definitions.get('arps.keepKey').status, 'design');
  assert.equal(model.definitions.get('arps.toStrum').status, 'design');
});

test('route options follow C profile constraints and keep channel assignments', async () => {
  const { model, engine } = await harness();
  const row = rows[index('midi.routeTarget')];
  assert.deepEqual(model.options(row).map(option => option.value), ['left', 'right']);
  assert.ok(model.set('midi.channel', 2).ok);
  assert.ok(model.set('midi.routeTarget', 'right').ok);
  assert.ok(model.set('midi.channel', 5).ok);
  assert.ok(model.set('midi.routingProfile', 'six-zones').ok);
  assert.deepEqual(model.options(row).map(option => option.value), ['left-1', 'left-2', 'left-3', 'right-1', 'right-2', 'right-3']);
  assert.equal(model.set('midi.routeTarget', 'left').ok, false);
  assert.ok(model.set('midi.routeTarget', 'right-3').ok);
  assert.ok(model.set('midi.channel', 11).ok);
  assert.ok(model.set('midi.routingProfile', 'split-sides').ok);
  assert.ok(model.set('midi.routeTarget', 'left').ok);
  assert.equal(model.get('midi.channel'), 2);
  assert.ok(model.set('midi.routeTarget', 'right').ok);
  assert.equal(model.get('midi.channel'), 5);
  assert.ok(model.set('midi.routingProfile', 'six-zones').ok);
  assert.ok(model.set('midi.routeTarget', 'right-3').ok);
  assert.equal(model.get('midi.channel'), 11);
  assert.deepEqual(engine.drain(), []);
});

test('the projected event history is the bounded C history', async () => {
  const { model, state } = await harness();
  for (let n = 0; n < 100; n++) assert.ok(model.set('arps.enabled', !!(n & 1)).ok);
  assert.equal(model.events.length, 64);
  assert.deepEqual(model.events.map(event => event.sequence), state.events().map(event => event.sequence));
  assert.equal(model.events.at(-1).sequence - model.events[0].sequence, 63);
  assert.ok(model.exportDraft().instances, 'Export includes independent C instances, not only current targets.');
});

test('menu metadata has unique identities and only approved feature references', async () => {
  assert.equal(new Set(STUDIO_MENU.groups.map(group => group.id)).size, STUDIO_MENU.groups.length);
  assert.equal(new Set(rows.map(row => row.id)).size, rows.length);
  const contract = JSON.parse(await readFile(new URL('../../core/feature_contract.json', import.meta.url), 'utf8'));
  assert.equal(contract.format, 'thy666.feature-contract');
  assert.equal(contract.version, 1);
  const { features } = contract;
  for (const row of rows) {
    assert.ok(row.feature_ids.length > 0, row.id);
    for (const id of row.feature_ids) assert.ok(['keep', 'modify'].includes(features[id]?.decision), `${row.id}: ${id}`);
  }
});

test('compact OLED labels remain unique and destination combinations remain distinguishable', async () => {
  const { model } = await harness();
  const oledLabel = row => (row.shortLabel || row.label).normalize('NFKD').replace(/[^\x20-\x7e]/g, '').toUpperCase().slice(0, 15);
  for (const group of STUDIO_MENU.groups) {
    const labels = group.rows.map(oledLabel);
    assert.equal(new Set(labels).size, labels.length, group.id);
  }
  const row = rows[index('midi.destinations')], options = model.options(row);
  assert.equal(options.length, 7);
  const labels = options.map(option => {
    assert.ok(model.set(row.id, option.value).ok);
    return model.compact(row);
  });
  assert.equal(new Set(labels).size, 7);
  // The physical value column admits eight 4-pixel glyphs at a 5-pixel advance.
  for (const label of labels) assert.ok(label.length > 0 && label.length <= 8, label);
});
