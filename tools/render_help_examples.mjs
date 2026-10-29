/* Refresh authored documentation screenshots from the actual shared C renderer.
 * Uses synthetic musical input only; never opens a device or reads a backup. */
import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { createCore } from '../web/core.js';
import { MENU_ROWS, STUDIO_MENU } from '../web/studio-menu.js';

const root = fileURLToPath(new URL('../', import.meta.url));
const bytes = await readFile(path.join(root, 'web/generated/core.wasm'));
const build = JSON.parse(await readFile(path.join(root, 'web/generated/build-info.json'), 'utf8'));
if (createHash('sha256').update(bytes).digest('hex') !== build.artifacts.webassembly.sha256)
  throw new Error('Build the shared core before rendering help examples.');
const okay = result => { if (!result.ok) throw new Error('Documentation example setup failed.'); };
const row = id => MENU_ROWS.findIndex(value => value.id === id);
const focus = (engine, id) => okay(engine.state.focus(STUDIO_MENU.groups.findIndex(group => group.id === MENU_ROWS[row(id)].group), row(id)));
const set = (engine, id, value) => {
  const definition = MENU_ROWS[row(id)];
  const encoded = definition.type === 'toggle' ? Number(value) : definition.type === 'select' ? definition.options.findIndex(option => option.value === value) : value;
  okay(engine.state.set(row(id), encoded));
};
const layout = Array.from({ length: 96 }, (_, pad) => ({
  notes: pad < 48 ? [60, 64, 67, 0, 0, 0, 0] : [60 + (pad - 48) % 12, 0, 0, 0, 0, 0, 0],
  mask: pad < 48 ? 7 : 1, color: 1,
}));
function pixels(frame) {
  let result = '';
  for (let y = 0; y < 64; y++) for (let x = 0; x < 128;) {
    const on = xx => frame[(y >> 3) * 128 + xx] & (1 << (y & 7));
    if (!on(x)) { x++; continue; }
    const start = x;
    while (x < 128 && on(x)) x++;
    result += `M${start} ${y}h${x - start}v1h-${x - start}z`;
  }
  return result;
}
const definitions = [
  ['quickstart-play', 'Simple Play · default idle screen', async engine => {
    okay(engine.state.set(row('lights.view'), 4));
  }],
  ...[['detailed', 'Detailed', 'notes'], ['chladni', 'Chladni', 'chladni'],
      ['orbit', 'Orbit', 'orbit'], ['waves', 'Waves', 'waves']].map(([id, label, value]) =>
    [id, `${label} · C major held`, async engine => {
      set(engine, 'lights.view', value); okay(engine.firmware.pressPad(0));
    }]),
  ['simple', 'Simple · C major held', async engine => {
    okay(engine.state.set(row('lights.view'), 4)); okay(engine.firmware.pressPad(0));
  }],
  ['piano', 'Piano · C major held', async engine => {
    okay(engine.state.set(row('lights.view'), 1)); okay(engine.firmware.pressPad(0));
  }],
  ['fifths', 'Circle of fifths · C major held', async engine => {
    okay(engine.state.set(row('lights.view'), 8)); okay(engine.firmware.pressPad(0));
  }],
  ['engines', 'Engines menu', async engine => {
    okay(engine.state.focus(STUDIO_MENU.groups.findIndex(group => group.id === 'engines'), row('engines.play')));
  }],
  ['arp', 'Arp quick page · Arp 1 enabled', async engine => {
    okay(engine.arp.configure(0, { enabled: true })); okay(engine.arp.openQuick(0));
  }],
  ['steps', 'T1 step editor · first step contains C major', async engine => {
    okay(engine.sequencer.createTrack());
    okay(engine.sequencer.add(0, [60, 64, 67].map(pitch => ({ tick: 0, duration: 240, pitch, value: 96 }))));
  }],
  ['track', 'T1 settings', async engine => {
    okay(engine.sequencer.createTrack()); okay(engine.state.navigate('right'));
  }],
  ['performance', 'Performance · LEFT Strike velocity', async engine => {
    set(engine, 'performance.velocityTarget', 1); set(engine, 'sensitivity.velocityMode', 1);
    focus(engine, 'performance.velocityTarget');
  }],
  ['chords', 'Chords · musical layout and voicing', async engine => {
    focus(engine, 'chords.layout');
  }],
  ...['chord-advice','scale-discovery','modulation-advice'].map((id, page) => [id, ['Harmony Fifths · C major','Scale discovery · C–E–G','Key preview · from C major'][page], async engine => {
    set(engine,'chords.layout','diatonic');
    okay(engine.state.harmonyAction(0,1));
    set(engine,'harmony.palette',1);
    okay(engine.firmware.pressPad(0));okay(engine.release(0));
    for(let n=1;n<=60;n++)okay(engine.rhythm.advance(n));
    if(page)okay(engine.state.harmonyAction(1,page));
    if(page===2)okay(engine.state.harmonyAction(3,7));
    okay(engine.rhythm.advance(61));
  }]),
  ['harmony-fifths','Harmony Fifths · C major and next chord',async engine=>{
    set(engine,'chords.layout','diatonic');okay(engine.state.harmonyAction(0,1));
    set(engine,'harmony.palette',1);okay(engine.firmware.pressPad(0));okay(engine.release(0));
    for(let n=1;n<=60;n++)okay(engine.rhythm.advance(n));
  }],
  ['harmony-settings','Harmony · dedicated playing controls',async engine=>{focus(engine,'harmony.direction');}],
  ['reset-options','Reset options · Cancel is selected initially',async engine=>{
    focus(engine,'lights.resetOptions');okay(engine.state.begin(row('lights.resetOptions')));
  }],
  ['library', 'Chord editor · a synthetic C major record', async engine => {
    okay(engine.firmware.library.set(1, 0, { used: true, name: 'C MAJOR', root: 60, notes: [60,64,67,0,0,0,0], mask: 7 }));
    okay(engine.firmware.library.openEditor(1, 0));
    okay(engine.state.navigate('down')); okay(engine.state.navigate('right'));
  }],
  ['midi', 'MIDI routing · LEFT on channel 3', async engine => {
    set(engine, 'midi.routingProfile', 'split-sides'); set(engine, 'midi.routeTarget', 'left'); set(engine, 'midi.channel', 3);
    focus(engine, 'midi.routeTarget');
  }],
  ['aftertouch', 'Performance · RIGHT Poly AT response', async engine => {
    set(engine, 'performance.routeTarget', 'right'); set(engine, 'performance.pressureMode', 'poly');
    focus(engine, 'performance.routeTarget');
  }],
  ['mpe', 'Expression · Lower-zone MPE enabled', async engine => {
    set(engine, 'expression.enabled', true); focus(engine, 'expression.enabled');
  }],
  ['sensitivity', 'Sensitivity · individual RIGHT pad selected', async engine => {
    set(engine, 'sensitivity.target', 49); focus(engine, 'sensitivity.target');
  }],
  ['pick', 'Pick pad · waiting for a deliberate pad selection', async engine => {
    focus(engine, 'sensitivity.pick'); okay(engine.state.navigate('right'));
  }],
  ['calibration', 'Calibration · LEFT cluster, first pad, Soft stage', async engine => {
    for (let sample = 0; sample < 8; sample++) for (let pad = 0; pad < 96; pad++)
      okay(engine.firmware.sample(pad, 20, sample * 10 + 1));
    okay(engine.calibration.open());
  }],
  ['at-stage', 'Aftertouch stage · RIGHT cluster, 70 percent split', async engine => {
    set(engine, 'sensitivity.target', 97); set(engine, 'sensitivity.profileEnabled', true);
    set(engine, 'sensitivity.profileSplit', 70); focus(engine, 'sensitivity.profileSplit');
  }],
  ['draft', 'Root draft · C sharp has not been applied', async engine => {
    focus(engine, 'performance.root'); okay(engine.state.begin(row('performance.root'))); okay(engine.state.adjust(1));
  }],
  ['applied', 'Root applied · C sharp is committed to core state', async engine => {
    focus(engine, 'performance.root'); okay(engine.state.begin(row('performance.root'))); okay(engine.state.adjust(1)); okay(engine.state.commit());
  }],
  ['tracks', 'Tracks · two created tracks, transport stopped', async engine => {
    okay(engine.sequencer.createTrack());
    okay(engine.sequencer.add(0, [60,64,67].map(pitch => ({tick:0,duration:240,pitch,value:96}))));
    okay(engine.sequencer.createTrack()); okay(engine.sequencer.open('tracks'));
  }],
  ['recording', 'T1 · a synthetic take is recording', async engine => {
    okay(engine.sequencer.createTrack()); okay(engine.sequencer.action('record')); okay(engine.sequencer.open('tracks'));
    if (engine.sequencer.snapshot().capture.state !== 'recording') throw new Error('Recording example did not enter capture.');
  }],
  ['lights', 'Lights · harmonic-root colors and held style', async engine => {
    focus(engine, 'lights.chordColor');
  }],
  ['storage', 'Storage · example current-state and empty-song usage', async engine => {
    focus(engine, 'system.storage'); okay(engine.state.navigate('right'));
  }],
];
const frames = {};
for (const [id, label, configure] of definitions) {
  const engine = await createCore({ bytes });
  engine.state.boot.skip(); okay(engine.firmware.layout(layout));
  okay(engine.rhythm.advance(0)); okay(engine.state.play());
  await configure(engine);
  frames[id] = { label, pixels: pixels(engine.state.frame({ now: 1000, scaleName: 'MAJOR C', animations: false })) };
}
const target = path.join(root, 'web/help-topics.js');
const original = await readFile(target, 'utf8');
const generated = '/* BEGIN GENERATED HELP FRAMES */\n' +
  'export const HELP_SCREEN_FRAMES = Object.freeze(' + JSON.stringify(frames, null, 2) + ');\n' +
  '/* END GENERATED HELP FRAMES */';
const revised = original.replace(/\/\* BEGIN GENERATED HELP FRAMES \*\/[\s\S]*?\/\* END GENERATED HELP FRAMES \*\//, generated);
if (revised === original && !original.includes(generated)) throw new Error('Missing help screenshot markers.');
if (process.argv.includes('--check')) {
  if (revised !== original) throw new Error('Documentation screenshots differ; run node tools/render_help_examples.mjs.');
} else await writeFile(target, revised, 'utf8');
console.log(`${Object.keys(frames).length} shared C documentation screenshots ${process.argv.includes('--check') ? 'verified' : 'rendered'}; synthetic inputs only.`);
