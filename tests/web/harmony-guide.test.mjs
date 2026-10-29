import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {initializeHarmonyGuide} from '../../web/harmony-guide.js';
const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));

// A small DOM fixture; all analysis below executes the real compiled C.
class Element {
  constructor(doc,tag){this.ownerDocument=doc;this.tagName=tag;this.children=[];this.attrs={};this.events={};this.dataset={};this.style={setProperty:(k,v)=>this.style[k]=String(v)};this.classList={add:()=>{},toggle:()=>{}};this.textContent='';}
  append(...nodes){this.children.push(...nodes);}
  replaceChildren(...nodes){this.children=nodes;}
  setAttribute(k,v){this.attrs[k]=v;}
  addEventListener(k,fn){this.events[k]=fn;}
  focus(){this.ownerDocument.activeElement=this;}
}
const all=n=>[n,...n.children.flatMap(all)];
const text=n=>[n.textContent,...n.children.map(text)].join(' ');
function fixture(options){const doc={createElement:tag=>new Element(doc,tag)};const root=new Element(doc,'main');const ui=initializeHarmonyGuide(root,options);return {root,doc,ui};}

test('guidance ranks scale/chord choices and exposes common-note modulation through WASM',async()=>{
  const e=await createCore({bytes}),g=e.guidance;
  const key=g.scale(0,0),target=g.scale(0,7);
  assert.equal(g.families.length,99);assert.equal(key,0xab5);
  assert.ok(g.chords(key,0,0x091).some(c=>c.root===7&&c.mask===0x8a4));
  assert.equal(g.label(0x221,5),'F MAJ'); // F A C, rooted F rather than invented slash bass.
  const found=g.discover(key);assert.ok(found.matches.some(s=>s.family===0&&s.root===0));
  assert.ok(found.matches.some(s=>s.family===5&&s.root===9));
  assert.deepEqual(g.modulation(key,target,0x091),{common:0xa95,added:64,removed:32,pivot:0x091,pivotRoot:0,currentFits:1});
  for(const bad of [-1,4096,1.5,NaN])assert.throws(()=>g.discover(bad),RangeError);
  assert.throws(()=>g.discover(0),RangeError);assert.throws(()=>g.chords(key,1),RangeError);
});

test('live Harmony displays device labels and roles; actions wait for authoritative readback',async()=>{
  const e=await createCore({bytes}),before=e.settings.encode();
  assert.ok(e.press({pad:0,notes:[60,64,67],mask:4,channel:2}).ok);e.drain();
  const calls=[],owner=e.owner(0),{root,ui}=fixture({families:e.guidance.families,action:async(...args)=>calls.push(args),control:async(...args)=>calls.push(['control',...args]),set:async(...args)=>calls.push(['set',...args])});
  assert.match(text(root),/Connect a board/);
  const bits=(...positions)=>Array.from({length:48},(_,n)=>positions.includes(n));
  const live={active:true,page:0,keyLabel:'C Major',inputLabel:'USER CHORD',selectedLabel:'G MAJ',inputMask:0x91,keyMask:0xab5,
    sourceRoot:0,targetRoot:7,targetFamily:0,targetLabel:'G Major',currentPads:bits(0),selectedPads:bits(2),suggestedPads:bits(5),
    pinned:false,cacheReady:true,canApply:true,cursor:0,count:1,total:10,rows:[{mask:0x8a4,root:7,shared:1,degree:5,reason:0,pad:2}],
    baseMask:0x91,rightMask:0,baseLabel:'USER CHORD',melodyLabel:'',view:0,controlBank:0,octaveTarget:1,rightMode:0,analysis:0,bassRoot:false,
    lowNote:0,highNote:127,voicing:0,chordLayers:8,noteLayers:8,chordOctave:0,noteOctave:0,clusterSwapped:false,
    physicalCurrentPads:Array.from({length:96},(_,n)=>n===0),physicalSelectedPads:Array.from({length:96},(_,n)=>n===2),physicalSuggestedPads:Array.from({length:96},(_,n)=>n===5),scaleButtons:[0,1,2,3,4,5,6],previewScale:255,suggestionCount:1};
  ui.update(live,{connected:true,supported:true,writable:true,controlsReady:true});
  assert.match(text(root),/USER CHORD/);assert.match(text(root),/Row 3 · pad 1/);
  const pad=all(root).find(n=>n.attrs['aria-label']==='LEFT row 3, pad 1');assert.equal(pad.dataset.role,'selected');
  assert.equal(pad.style['grid-column'],'1');assert.equal(pad.style['grid-row'],'3');
  const destination=all(root).find(n=>n.attrs['aria-label']==='Destination root');destination.value='2';destination.events.change();
  await Promise.resolve();await Promise.resolve();assert.deepEqual(calls,[[3,2]]);
  assert.match(text(root),/C Major → G Major/,'Changing a control cannot fabricate a new working key.');
  ui.update({...live,page:1,rows:[{mask:0xab5,root:0,family:0,extra:4,reason:255,pad:255}]},{connected:true,supported:true,writable:true,controlsReady:true});
  const compare=all(root).find(n=>n.tagName==='button'&&n.textContent.startsWith('Compare C '));assert.ok(compare);compare.events.click();
  await Promise.resolve();await Promise.resolve();assert.deepEqual(calls.at(-1),[10,0]);
  ui.update({...live,page:2,rows:[]},{connected:true,supported:true,writable:true,controlsReady:true});
  all(root).find(n=>n.textContent==='Apply destination key').events.click();
  await Promise.resolve();await Promise.resolve();assert.deepEqual(calls.at(-1),[5,1]);
  const view=all(root).find(n=>n.attrs['aria-label']==='Chord bank');view.value='1';view.events.change();
  await Promise.resolve();await Promise.resolve();assert.deepEqual(calls.at(-1),['set','harmony.palette',1]);assert.equal(view.value,'0','Only readback changes the displayed preference.');
  all(root).find(n=>n.tagName==='button'&&n.textContent==='5 · Suggestions').events.click();
  await Promise.resolve();await Promise.resolve();assert.deepEqual(calls.at(-1),['control',3,0]);
  ui.update({...live,clusterSwapped:true,physicalCurrentPads:Array(96).fill(false),physicalSelectedPads:Array.from({length:96},(_,n)=>n===50),physicalSuggestedPads:Array(96).fill(false)},{connected:true,supported:true,writable:true,controlsReady:true});
  assert.equal(all(root).find(n=>n.attrs['aria-label']==='RIGHT row 3, pad 1').dataset.role,'selected');assert.equal(pad.dataset.role,'other');
  assert.match(text(root),/RIGHT chords \/ LEFT notes/);
  ui.update({...live,controlBank:1,octaveTarget:1,chordLayers:2,noteLayers:16},{connected:true,supported:true,writable:true,controlsReady:true});
  assert.equal(all(root).find(n=>n.tagName==='button'&&n.textContent==='-2 oct').attrs['aria-pressed'],'true');
  assert.equal(all(root).find(n=>n.tagName==='button'&&n.textContent==='+1 oct').attrs['aria-pressed'],'false','U3 targets the shift pair; it never changes the lower chord-layer bank.');
  ui.update(live,{connected:true,supported:true,writable:true,controlsReady:false});
  const rootButton=all(root).find(n=>n.tagName==='button'&&n.textContent==='D'),beforeControls=calls.length;
  assert.equal(rootButton.disabled,true);rootButton.events.click();await Promise.resolve();assert.equal(calls.length,beforeControls);
  ui.update({...live,scaleButtons:[70,71,2,3,4,5,6]},{connected:true,supported:true,writable:true,controlsReady:true});
  assert.equal(all(root).find(n=>n.tagName==='button'&&n.textContent.startsWith('M1 ·')).disabled,true);
  assert.equal(all(root).find(n=>n.tagName==='button'&&n.textContent.startsWith('M2 ·')).disabled,true);
  assert.equal(all(root).find(n=>n.tagName==='button'&&n.textContent.startsWith('M3 ·')).disabled,false);
  ui.update({...live,inputMask:0,inputLabel:'READY'},{connected:true,supported:true,writable:true,controlsReady:true});assert.match(text(root),/Play a chord/);
  assert.deepEqual(e.settings.encode(),before);assert.deepEqual(e.owner(0),owner);assert.deepEqual(e.drain(),[]);
  assert.ok(e.release(0).ok);assert.equal(e.drain().length,3);
});

test('old firmware and schema mismatch disable every Harmony mutation',async()=>{
  const calls=[],{root,ui}=fixture({action:async(...args)=>calls.push(args)});
  ui.update(null,{connected:true,supported:false,writable:true});assert.match(text(root),/does not expose live Harmony/);
  const enter=all(root).find(n=>n.textContent==='Enter Harmony');assert.equal(enter.disabled,true);enter.events.click();
  ui.update(null,{connected:true,supported:true,writable:false});assert.equal(enter.disabled,true);enter.events.click();
  await Promise.resolve();assert.deepEqual(calls,[]);
});
