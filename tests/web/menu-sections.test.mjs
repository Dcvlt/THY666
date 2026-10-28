import test from 'node:test';
import assert from 'node:assert/strict';
import { STUDIO_MENU, MENU_ROWS } from '../../web/studio-menu.js';

test('all categories put basic controls before one advanced section without selectable separator rows', () => {
  assert.equal(STUDIO_MENU.groups.length, 15);
  assert.equal(MENU_ROWS.length, 347);
  for (const group of STUDIO_MENU.groups) {
    assert.ok(group.rows.some(row => !row.advanced), group.id);
    if(!['engines','engineSettings'].includes(group.id)) assert.ok(group.rows.some(row => row.advanced), group.id);
    const flags = group.rows.map(row => row.advanced);
    assert.ok(flags.every(value => typeof value === 'boolean'), group.id);
    assert.deepEqual(flags, [...flags].sort(), group.id);
    assert.ok(group.rows.every(row => row.type !== 'separator' && !row.id.includes('separator')), group.id);
    if(!['engines','engineSettings'].includes(group.id)) {
      assert.equal(group.rows.at(-1).id, `${group.id}.resetOptions`, group.id);
      assert.equal(group.rows.at(-1).icon, 'reset', group.id);
    }
  }
});

test('musical setup moves to Chords with stable logical identities and factory shortcuts', () => {
  const byId = new Map(MENU_ROWS.map(row => [row.id, row]));
  for (const name of ['root','scale','melodyLayout','guitarWindow','guitarShortcut','transpose',
    'chordOctave','melodyOctave','bassOctave','keepTranspose','liveUpdate','liveUpdateLockScale',
    'liveUpdateMuteChord','liveUpdateNoteColor']) {
    const row = byId.get(`performance.${name}`);
    assert.ok(row, name);
    assert.equal(row.group, 'chords', name);
  }
  for (const name of ['velocity','aftertouch','padOverlays','contextLocation','contextChordOctaves',
    'contextMelodyOctaves','contextBass','contextInversion'])
    assert.equal(byId.get(`performance.${name}`).group, 'performance', name);
  assert.equal(byId.get('performance.root').binding, 'core.root');
  assert.equal(byId.get('performance.scale').binding, 'core.scale');
  assert.equal(byId.get('performance.liveUpdate').binding, 'core.liveUpdate');
  assert.equal(byId.get('controls.right').default, 'chords');
  assert.equal(byId.get('lights.view').default, 'simple');
  assert.equal(byId.get('views.slot1').default, 'musical-names');
  assert.equal(byId.get('views.detail1').default, 'musical-names');
  for (const id of ['views.slot1','views.slot2',...Array.from({length:6},(_,n)=>`views.detail${n+1}`)]) {
    const options = byId.get(id).options;
    assert.equal(options.length, 13);
    assert.deepEqual(options[12], {value:'musical-names',label:'Musical names'});
    assert.deepEqual(options.slice(0,12).map(option=>option.value),
      ['off','velocity','counts','notes','scale','channels','outputs','modifiers','pressure','octaves','clock','arps']);
  }
});

test('route aftertouch is a three-way selector with separate advanced bend controls', () => {
  const byId = new Map(MENU_ROWS.map(row => [row.id, row]));
  const aftertouch = byId.get('midi.pressure');
  assert.equal(aftertouch.type, 'select');
  assert.equal(aftertouch.label, 'Aftertouch');
  assert.deepEqual(aftertouch.options.map(option => option.value), ['off', 'poly', 'channel']);
  assert.deepEqual(byId.get('midi.pressureBend').options.map(option => option.value), ['off', 'up', 'down']);
  const range = byId.get('midi.bendRange');
  assert.equal(range.min, 1); assert.equal(range.max, 24); assert.equal(range.default, 2);
  assert.equal(byId.get('midi.bendSetup').type, 'action');
});

test('Performance owns scoped velocity and the shared MPE alias without pressure masters',()=>{
  const byId=new Map(MENU_ROWS.map(row=>[row.id,row]));
  const mode=byId.get('sensitivity.velocityMode');
  assert.equal(mode.group,'performance');assert.equal(mode.binding,'velocity.mode');assert.equal(mode.advanced,false);
  assert.deepEqual(mode.options.map(option=>option.label),['Fixed','Strike','Mixed']);
  const target=byId.get('performance.velocityTarget');
  assert.equal(target.label,'Velocity for');assert.equal(target.binding,'velocity.target');
  assert.deepEqual(target.options.map(option=>option.label),['Both','Left','Right']);
  assert.equal(byId.get('performance.mpe').binding,byId.get('expression.enabled').binding);
  assert.equal(byId.get('performance.mpe').group,'performance');
  assert.equal(byId.get('performance.aftertouch').retired,true);
  assert.equal(byId.get('performance.velocitySource').retired,true);
  assert.equal(byId.get('performance.velocity').label,'Default velocity');
  assert.equal(byId.get('sensitivity.profileEnabled').label,'Aftertouch stage');
});

test('Performance exposes live route response aliases beside velocity with bend range advanced', () => {
  const byId = new Map(MENU_ROWS.map(row => [row.id, row]));
  const performance = STUDIO_MENU.groups.find(group => group.id === 'performance').rows;
  const afterVelocity = performance.findIndex(row => row.id === 'performance.velocity') + 1;
  assert.deepEqual(performance.slice(afterVelocity, afterVelocity + 3).map(row => row.id),
    ['performance.routeTarget', 'performance.pressureMode', 'performance.pressureBend']);
  for (const [alias, original] of [
    ['performance.routeTarget', 'midi.routeTarget'],
    ['performance.pressureMode', 'midi.pressure'],
    ['performance.pressureBend', 'midi.pressureBend'],
    ['performance.bendRange', 'midi.bendRange'],
  ]) {
    const row = byId.get(alias), canonical = byId.get(original);
    assert.equal(row.group, 'performance', alias);
    assert.equal(row.status, 'live', alias);
    assert.equal(row.binding, canonical.binding, alias);
    assert.equal(row.type, canonical.type, alias);
    assert.equal(row.default, canonical.default, alias);
    if (row.type === 'select')
      assert.deepEqual(row.options.map(option => option.value), canonical.options.map(option => option.value), alias);
    assert.equal(row.advanced, alias === 'performance.bendRange', alias);
  }
  assert.equal(byId.get('performance.routeTarget').label, 'Response for');
  assert.deepEqual(byId.get('performance.pressureMode').options.map(option => option.value), ['off', 'poly', 'channel']);
  assert.deepEqual(byId.get('performance.pressureBend').options.map(option => option.value), ['off', 'up', 'down']);
  assert.equal(byId.get('performance.bendRange').min, 1);
  assert.equal(byId.get('performance.bendRange').max, 24);
});

test('broad controls stay basic while precise per-step, source and detail settings remain advanced', () => {
  const byId = new Map(MENU_ROWS.map(row => [row.id, row]));
  for (const id of ['engines.play', 'engines.arp', 'engines.sequencer', 'performance.melodyLayout', 'arps.enabled', 'arps.type', 'sequencer.open', 'sequencer.help', 'sequencer.tempo', 'sequencer.length', 'sequencer.grid', 'sequencer.input', 'expression.enabled', 'expression.zone', 'expression.members', 'expression.bendRange', 'expression.glide', 'midi.channel', 'midi.destinations', 'lights.enabled', 'lights.view']) {
    assert.equal(byId.get(id)?.advanced, false, id);
  }
  for (const id of ['engines.assignment', 'performance.contextLocation', 'arps.rateTicks', 'arps.sourceZone1', 'sequencer.source', 'sequencer.loopStart', 'sequencer.regionEnd', 'sequencer.stepLength', 'expression.padWindow', 'expression.interval', 'expression.managerRange', 'expression.setup', 'expression.help', 'midi.remoteLow', 'lights.keyDim', 'views.detail1']) {
    assert.equal(byId.get(id)?.advanced, true, id);
  }
  for (const [prefix, count] of [['arps.sourceZone', 6], ['arps.step', 8], ['arps.offset', 8], ['views.detail', 6]]) {
    const first = MENU_ROWS.findIndex(row => row.id === prefix + '1');
    assert.ok(first >= 0);
    assert.deepEqual(MENU_ROWS.slice(first, first + count).map(row => row.id), Array.from({ length: count }, (_, index) => prefix + (index + 1)));
  }
  const sensitivity = STUDIO_MENU.groups.find(group=>group.id==='sensitivity');
  const responseIds = ['sensitivity.attack','sensitivity.release','sensitivity.floor','sensitivity.ceiling','sensitivity.curve'];
  assert.deepEqual(sensitivity.rows.filter(row=>responseIds.includes(row.id)).map(row=>row.id),responseIds);
  for(const id of ['sensitivity.target','sensitivity.pick','sensitivity.amount','sensitivity.profileEnabled','sensitivity.profileSplit','sensitivity.calibrate','sensitivity.velocityMode','midi.pressure'])
    assert.equal(byId.get(id).advanced,false,id);
  for(const id of ['sensitivity.attack','sensitivity.release','sensitivity.linkedRelease','sensitivity.releaseGap','sensitivity.floor','sensitivity.ceiling','sensitivity.curve','sensitivity.pressureCurve','sensitivity.aftertouchCurve','sensitivity.softSpeed','sensitivity.hardSpeed','sensitivity.minVelocity','sensitivity.maxVelocity','midi.pressureBend','midi.bendRange','midi.bendSetup'])
    assert.equal(byId.get(id).advanced,true,id);
});

test('one Play engine retains layout options and Circle of fifths uses its reserved new view position', () => {
  const engines = STUDIO_MENU.groups[0];
  assert.equal(engines.id, 'engines');
  assert.equal(engines.rows[0].id, 'engines.play');
  assert.equal(engines.rows[0].binding, 'core.engine.play');
  assert.deepEqual(engines.rows.map(row => row.id), ['engines.play', 'engines.harmony', 'engines.sequencer', 'engines.arp', 'engines.drums', 'engines.settings']);
  assert.deepEqual(engines.rows.slice(0,4).map(row => row.binding), ['core.engine.play','harmony.open','sequencer.open','arps.activate']);
  for(const id of ['engines.shortcut','engines.assignment']) assert.equal(MENU_ROWS.find(row=>row.id===id).group,'controls');
  assert.equal(MENU_ROWS.some(row => row.id === 'engines.scales' || row.id === 'engines.chromatic'), false);
  const layouts = MENU_ROWS.find(row => row.id === 'performance.melodyLayout').options;
  assert.equal(layouts.find(option => option.value === 'condensed').label, 'Scale degrees');
  for (const key of ['chromatic', 'condensed', 'guitar']) assert.ok(layouts.some(option => option.value === key));
  const views = MENU_ROWS.find(row => row.id === 'lights.view').options;
  assert.equal(views.length, 9);
  assert.deepEqual(views[8], { value: 'fifths', label: 'Circle of fifths' });
});

test('sequencer entry and help precede musical settings while retired workflows retain no visible controls', () => {
  const sequencer = STUDIO_MENU.groups.find(group => group.id === 'sequencer');
  assert.deepEqual(sequencer.rows.slice(0, 3).map(row => [row.id, row.binding]), [
    ['sequencer.open', 'sequencer.open'],
    ['sequencer.help', 'sequencer.help'],
    ['sequencer.tempo', 'sequencer.tempo'],
  ]);
  const retired = ['steps', 'track', 'active', 'interaction', 'layout', 'swap', 'play', 'record', 'stop'];
  for (const name of retired) {
    const row = sequencer.rows.find(row => row.id === `sequencer.${name}`);
    assert.ok(row?.retired, name);
    assert.equal(row.advanced, true, name);
  }
  for (const name of ['length', 'grid', 'input', 'launch'])
    assert.equal(sequencer.rows.find(row => row.id === `sequencer.${name}`).advanced, false);
  assert.deepEqual(sequencer.rows.find(row => row.id === 'sequencer.length').max, 48);
});
