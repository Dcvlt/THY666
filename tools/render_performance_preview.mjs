/* Local-only visual scenes captured from the actual portable C engine.
 * Synthetic musical input; no device, network, audio or MIDI connection.
 * First stage `tools/build_site.py --output build/performance-preview`.
 */
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
import {createCore} from '../web/core.js';
import {decodeCompanionSnapshot,decodePerformanceSnapshot} from '../web/companion-protocol.js';
import {MENU_ROWS} from '../web/studio-menu.js';

const root=fileURLToPath(new URL('../',import.meta.url)),args=process.argv.slice(2);
if(args.length&&!(args.length===2&&args[0]==='--output'))throw new Error('Use --output build/performance-preview');
const output=path.resolve(root,args[1]||'build/performance-preview');
if(!output.startsWith(path.join(root,'build')+path.sep))throw new Error('Preview must stay in an ignored build subdirectory.');
const wasm=await readFile(path.join(root,'web/generated/core.wasm'));
const manifest=JSON.parse(await readFile(path.join(root,'web/generated/build-info.json'),'utf8'));
if(createHash('sha256').update(wasm).digest('hex')!==manifest.artifacts.webassembly.sha256)throw new Error('Rebuild the portable core before capturing scenes.');
const okay=result=>{if(!result?.ok)throw new Error(`Synthetic C scene failed: ${result?.message||result?.status||result?.code}`);};
const mpeOutputRow=MENU_ROWS.findIndex(row=>row.id==='performance.mpeOutput');
const layout=Array.from({length:96},(_,pad)=>({notes:pad<48?[48+(pad%5)*4,52+(pad%5)*4,55+(pad%5)*4,0,0,0,0]:[72+(pad%12),0,0,0,0,0,0],mask:pad<48?7:1,color:1}));
async function fresh(){const e=await createCore({bytes:wasm});okay(e.firmware.layout(layout));okay(e.advance(0));return e;}
function settle(e,first,last,step=1){for(let now=first;now<=last;now+=step){okay(e.advance(now));e.drain();}}
function capture(e,id,title,description,now,detail='live'){
  e.state.frame({scaleName:'SYNTHETIC C',now});
  const base=e.state.companionSnapshot(now),performance=e.state.performanceSnapshot(now);
  const b=decodeCompanionSnapshot(base),p=decodePerformanceSnapshot(performance,{uid:b.uid,build:b.build,schema:b.schema});
  if(p.mpe&&p.tracks.some(track=>track.running))throw new Error('A preview cannot combine incompatible musical engines.');
  const expression={available:true,pending:false,mpeOutput:[e.state.instance(mpeOutputRow,1),e.state.instance(mpeOutputRow,2)],aftertouchStage:b.aftertouchStage};
  return {id,title,description,detail,expression,base:Array.from(base),performance:Array.from(performance)};
}
async function mpe({upperZone=false,strum=false,sustain=false}={}){
  const e=await fresh();okay(e.firmware.setExpressionProfile(-1,{enabled:true,split:50,pressureCurve:0,aftertouchCurve:0}));
  okay(e.firmware.performance({strum,delay:80}));okay(e.expression.configure({enabled:true,upperZone}));
  const pads=strum||sustain?1:5;
  for(let pad=0;pad<pads;pad++)okay(e.firmware.pressPad(pad));
  if(!strum)settle(e,1,12);else{okay(e.advance(1));e.drain();}
  for(let pad=0;pad<pads;pad++){okay(e.expression.axes(pad,24+pad*23,12+pad*18));okay(e.expression.bend(pad,6400+pad*700));}
  if(!strum)settle(e,13,24);
  if(sustain){for(const voice of e.expression.voices())okay(e.sustain(voice.destinations,voice.channel,true));okay(e.release(0));settle(e,25,28);}
  // Let the bounded C service visit every member; advancing once never replays
  // its backlog. Reserved and sustain scenes intentionally keep their timing.
  if(!strum&&!sustain)settle(e,25,100,5);
  return e;
}
async function sequence(source='internal'){
  const e=await fresh();okay(e.rhythm.configure({source,quantization:'immediate'}));
  for(let index=0;index<21;index++){
    const start=index%4*240,end=start+(4+index%5)*960;
    okay(e.sequencer.add(index,[{tick:end-120,duration:120,pitch:48+index,value:90}]));okay(e.sequencer.loop(index,start,end));
    okay(e.sequencer.configureTrack(index,{muted:index%5===1,oneShot:index%7===2}));
  }
  okay(e.sequencer.open('tracks'));
  if(source==='usb'){
    // Arm the song's tracks through the same explicit Play All action used on
    // the board before the external transport starts their shared clock.
    okay(e.sequencer.playAll());
    okay(e.rhythm.midi(0xf8,0,0,0));okay(e.rhythm.midi(0xfa,0,0,0));
    for(let pulse=1;pulse<=24;pulse++){okay(e.rhythm.midi(0xf8,0,0,pulse*20));e.drain();}
  }else{okay(e.rhythm.start());okay(e.advance(375));e.drain();}
  const tracks=e.rhythm.snapshot().tracks;
  if(!tracks.every(track=>track.enabled&&track.running)||!tracks.some(track=>track.playheadTick>track.loopStart))
    throw new Error('Synthetic sequencer scene must contain active, advanced playheads.');
  return e;
}
const scenes=[];
let engine=await mpe();scenes.push(capture(engine,'mpe-members','MPE · 15 voices','Five synthetic triads occupy all member channels. Bars show controller values accepted into the C MIDI queue.',100));
engine=await mpe({upperZone:true});scenes.push(capture(engine,'mpe-upper','MPE · upper zone','The same bounded voice pool with upper-zone channel assignment.',100));
engine=await mpe({strum:true});scenes.push(capture(engine,'mpe-reserved','MPE · strum reservations','Future strum members remain reserved; requested expression differs from the last accepted values.',2));
engine=await mpe({sustain:true});scenes.push(capture(engine,'mpe-sustain','MPE · sustained release','Released member voices stay leased while the synthetic sustain pedal remains down.',30));
engine=await sequence();scenes.push(capture(engine,'seq-running','Sequencer · 21 tracks','All track records come from the C scheduler, including muted and one-shot settings. Positions are frozen at capture.',375));
engine=await sequence('usb');okay(engine.rhythm.midi(0xfc,0,0,480));scenes.push(capture(engine,'seq-paused','Sequencer · paused','An actual USB Stop preserves the sampled position without inventing forward motion.',481));
engine=await sequence('usb');okay(engine.advance(1100));engine.drain();scenes.push(capture(engine,'seq-clock-lost','Sequencer · clock lost','The engine detects the missing USB clock; the widget reports suspension instead of showing Playing.',1100));
const first=scenes[0];scenes.push({...first,id:'stale',title:'Detail · stale',description:'Presentation-only stale wrapper around the same C capture. Previous detailed readings must clear.',detail:'stale'});
scenes.push({...first,id:'unsupported',title:'Detail · older firmware',description:'Presentation-only capability fallback. Aggregate input remains available; detailed meters request updated firmware.',detail:'unsupported'});
await mkdir(output,{recursive:true});
await writeFile(path.join(output,'performance-presets.json'),JSON.stringify({format:'thy666.local-performance-preview',version:1,coreSha256:manifest.artifacts.webassembly.sha256,scenes}));
await writeFile(path.join(output,'performance-preview.html'),`<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>THY666 · Local performance preview</title>
<link rel="stylesheet" href="./client/typography.css"><link rel="stylesheet" href="./client/companion-theme.css"><link rel="stylesheet" href="./client/ui-system.css"><link rel="stylesheet" href="./client/cockpit.css">
<style>body{margin:0;padding:24px}.preview-page{max-width:1600px;margin:auto}.preview-header{display:flex;justify-content:space-between;gap:24px;flex-wrap:wrap;margin-bottom:24px}.preview-header h1{font-size:22px}.preview-badge{color:var(--theme-bright);font:11px var(--font-accent);letter-spacing:.08em}.preview-controls{display:flex;gap:16px;align-items:end;flex-wrap:wrap}.preview-description{max-width:80ch;color:var(--ui-muted);line-height:1.7}.preview-placeholder{padding:12px 24px;text-align:center;color:var(--ui-muted);font-size:12px}.preview-controls select{max-width:100%}</style>
<script type="module" src="./performance-preview.js"></script></head>
<body class="companion-app" data-appearance="dark" data-theme="red"><main class="preview-page"><header class="preview-header"><div><p class="preview-badge">LOCAL PREVIEW · SYNTHETIC INPUT · NO HARDWARE</p><h1>Performance telemetry</h1><p class="preview-description">Frozen snapshots from the actual C engine. Choose a scene; positions never advance between samples.</p></div><form class="preview-controls"><label class="hud-field-label">Scene<select class="hud-field" id="scene"></select></label><label class="hud-field-label">Appearance<select class="hud-field" id="appearance"><option value="dark">Dark</option><option value="light">Light</option></select></label></form></header><p id="scene-description" class="preview-description"></p><section id="performance-cockpit" aria-label="Synthetic performance widgets"><div class="instrument preview-placeholder">Shared C renderer and telemetry · no physical device is connected</div></section></main></body></html>`);
await writeFile(path.join(output,'performance-preview.js'),`import {initializeCockpit} from './client/cockpit.js';
import {decodeCompanionSnapshot,decodePerformanceSnapshot} from './client/companion-protocol.js';
import {initializeUIComponents} from './client/ui-system.js';
const fixtures=await (await fetch('./performance-presets.json')).json();
const cockpit=initializeCockpit({mount:document.querySelector('#performance-cockpit')}),picker=document.querySelector('#scene');
for(const scene of fixtures.scenes){const option=document.createElement('option');option.value=scene.id;option.textContent=scene.title;picker.append(option);}
function show(){const scene=fixtures.scenes.find(item=>item.id===picker.value),base=decodeCompanionSnapshot(Uint8Array.from(scene.base)),detail=decodePerformanceSnapshot(Uint8Array.from(scene.performance));
base.context={expression:scene.expression,performance:{supported:scene.detail!=='unsupported',available:scene.detail==='live',stale:scene.detail==='stale',pending:false,snapshot:scene.detail==='live'?detail:null}};
cockpit.update(base,{preview:true});document.querySelector('#scene-description').textContent=scene.description;}
picker.addEventListener('change',show);document.querySelector('#appearance').addEventListener('change',event=>{document.body.dataset.appearance=event.target.value;document.documentElement.dataset.appearance=event.target.value;});
initializeUIComponents();show();
`);
console.log(`Local C-snapshot preview: ${path.relative(root,output)}/performance-preview.html (${scenes.length} scenes)`);
