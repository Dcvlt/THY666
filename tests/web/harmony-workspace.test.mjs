import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {decodeHarmonySnapshot} from '../../web/companion-protocol.js';
import {MENU_ROWS} from '../../web/studio-menu.js';
const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));

test('actual shared C workspace projects through the same800-byte serializer and explicit key actions',async()=>{
  const e=await createCore({bytes});
  assert.equal(e.state.set(MENU_ROWS.findIndex(row=>row.id==='harmony.palette'),1).ok,true);
  const layout=Array.from({length:96},(_,pad)=>({notes:pad<48?[60+pad%12,64+pad%12,67+pad%12,0,0,0,0]:[60+pad%12,0,0,0,0,0,0],mask:pad<48?7:1,color:0}));
  assert.equal(e.firmware.layout(layout).ok,true);
  let now=0;
  const advance=()=>{for(let n=0;n<160;n++)assert.equal(e.advance(now++).ok,true);};
  const action=(code,value=0)=>{assert.equal(e.state.harmonyAction(code,value).ok,true);advance();};
  const snapshot=()=>decodeHarmonySnapshot(e.state.harmonySnapshot(now));
  action(0,1);let s=snapshot();assert.equal(s.active,true);assert.equal(s.cacheReady,true);
  action(7);s=snapshot();assert.equal(s.inputMask,0x91);assert.equal(s.inputLabel,'C MAJ');
  assert.ok(s.rows.length>0&&s.rows.length<=9);
  assert.equal(s.baseMask,0x91);assert.equal(s.baseLabel,'C MAJ');assert.equal(s.rightMask,0);
  for(const row of s.rows){
    assert.ok(row.pad<48);
    const actual=s.padMasks[row.pad];
    assert.equal(row.mask,actual,'Every recommendation names a pad in the actual playing map.');
  }
  const before=s.keyMask;action(1,1);s=snapshot();assert.equal(s.page,1);assert.ok(s.count<=24);
  assert.ok(s.rows.every(row=>row.family<99&&row.reason===255&&row.pad===255));
  const scale=s.rows[0];action(10,0);s=snapshot();assert.equal(s.page,2);assert.equal(s.targetRoot,scale.root);assert.equal(s.targetFamily,scale.family);
  action(1,2);action(3,7);action(4,0);s=snapshot();
  assert.equal(s.keyMask,before,'Destination browsing preserves the current key.');
  assert.equal(s.targetRoot,7);assert.equal(s.canApply,true);assert.equal(s.rows.length,0);
  action(5,1);s=snapshot();assert.equal(s.sourceRoot,7);assert.equal(s.keyMask,s.targetMask);
  const setting=(id,value)=>{const row=MENU_ROWS.findIndex(row=>row.id===id);assert.ok(row>=0);assert.equal(e.state.set(row,value).ok,true);advance();};
  setting('harmony.view',1);s=snapshot();assert.equal(s.view,1);
  setting('harmony.bass',1);setting('harmony.low',36);setting('harmony.high',100);s=snapshot();assert.equal(s.bassRoot,true);assert.equal(s.lowNote,36);assert.equal(s.highNote,100);
  setting('system.swapClusters',1);s=snapshot();assert.equal(s.clusterSwapped,true);
  for(let n=0;n<48;n++){
    assert.equal(s.physicalCurrentPads[n],false);assert.equal(s.physicalCurrentPads[n+48],s.currentPads[n]);
    assert.equal(s.physicalSelectedPads[n+48],s.selectedPads[n]);assert.equal(s.physicalSuggestedPads[n+48],s.suggestedPads[n]);
  }
  assert.deepEqual(e.drain(),[],'Workspace actions never audition or inject notes.');
});

test('accepted note-half input joins C analysis while held and leaves the reference after release',async()=>{
  const e=await createCore({bytes});
  assert.equal(e.state.set(MENU_ROWS.findIndex(row=>row.id==='harmony.palette'),1).ok,true);
  const layout=Array.from({length:96},(_,pad)=>({notes:pad<48?[60,64,67,0,0,0,0]:[60,0,0,0,0,0,0],mask:pad<48?7:1,color:0}));
  assert.equal(e.firmware.layout(layout).ok,true);
  for(const half of [-1,-2])assert.equal(e.firmware.setResponse(half,{attack:1200,release:1000,floor:800,ceiling:3200,curve:0}).ok,true);
  assert.equal(e.state.harmonyAction(0,1).ok,true);
  let now=0;
  const sample=(pad,raw,count)=>{for(let n=0;n<count;n++){assert.equal(e.firmware.sample(pad,raw,now++).ok,true);assert.equal(e.advance(now++).ok,true);}};
  const snapshot=()=>decodeHarmonySnapshot(e.state.harmonySnapshot(now));
  sample(0,100,8);sample(52,100,8);sample(0,1300,2);
  let s=snapshot();assert.equal(s.baseMask,0x91);assert.equal(s.inputMask,0x91);assert.equal(s.currentRoot,0);assert.equal(s.currentPad,0);
  sample(52,1300,2);s=snapshot();assert.equal(s.rightMask,2);assert.equal(s.inputMask,0x93);assert.equal(s.baseMask,0x91);
  assert.equal(s.currentRoot,0);assert.equal(s.currentPad,0);assert.equal(s.melodyLabel,'C#');
  sample(52,100,2);s=snapshot();assert.equal(s.rightMask,0);assert.equal(s.inputMask,0x91);
  sample(0,100,2);s=snapshot();assert.equal(s.baseMask,0x91);assert.equal(s.inputMask,0x91);
  assert.equal(e.stats().activeVoices,0);assert.equal(e.isHeld(0),false);assert.equal(e.isHeld(52),false);
});
