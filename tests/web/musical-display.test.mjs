import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {MENU_ROWS} from '../../web/studio-menu.js';

const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));
const row=id=>MENU_ROWS.findIndex(item=>item.id===id);
const set=(e,id,value)=>assert.ok(e.state.set(row(id),value).ok,id);
async function fresh(){
  const e=await createCore({bytes});
  const pads=Array.from({length:96},(_,p)=>({notes:p<48?[60,64,67,0,0,0,0]:[60,0,0,0,0,0,0],mask:p<48?7:1,color:1}));
  assert.ok(e.firmware.layout(pads,{anchor:60,degrees:[0,2,4,5,7,9,11]}).ok);
  return e;
}
const header=frame=>Array.from(frame.subarray(0,108));

test('shared musical identity is visible in all views and follows owned notes, not the next key',async()=>{
  const e=await fresh();
  assert.equal(e.firmware.musicalName(),'READY');
  assert.equal(e.firmware.state().displayView,4);
  assert.ok(e.firmware.pressPad(0).ok);
  assert.equal(e.firmware.musicalName(),'C MAJ');
  const expected=header(e.state.frame({animations:false}));
  for(const view of [0,1,4,5,6,7,8]){
    set(e,'lights.view',view);
    assert.deepEqual(header(e.state.frame({animations:false})),expected);
  }
  set(e,'performance.root',2);
  assert.equal(e.firmware.musicalName(),'C MAJ');
  assert.ok(e.release(0).ok);
  assert.equal(e.firmware.musicalName(),'READY');
  const events=e.drain();
  assert.deepEqual(events.filter(x=>(x.status&0xf0)===0x80 && x.destination===0).map(x=>x.data1),[60,64,67]);
});

test('an Arp phrase keeps its harmonic name throughout the gates and clears on release',async()=>{
  const e=await fresh();
  assert.ok(e.arp.configure(0,{enabled:true,layer:false,rateTicks:240,gatePercent:50}).ok);
  assert.ok(e.firmware.pressPad(0).ok);
  for(const time of [0,62,125,187,250]){
    assert.ok(e.advance(time).ok);
    assert.equal(e.firmware.musicalName(),'C MAJ');
    e.drain();
  }
  assert.ok(e.release(0).ok);
  assert.equal(e.firmware.musicalName(),'READY');
});

test('Piano halves have complementary semitone columns and silent gaps in both octaves',async()=>{
  const e=await fresh();set(e,'performance.melodyLayout',8);
  let active=0;
  const black=new Set([1,3,6,8,10]);
  for(let column=0;column<12;column++)for(let r=0;r<4;r++){
    const pad=48+column*4+r,plays=black.has(column)!==Boolean(r&1);
    const expected=plays?[60+column-(r>=2?12:0)]:[];
    assert.deepEqual(e.firmware.previewPad(pad).notes,expected);
    assert.ok(e.firmware.pressPad(pad).ok);
    const notes=e.drain().filter(x=>x.destination===0&&(x.status&0xf0)===0x90).map(x=>x.data1);
    assert.deepEqual(notes,expected);
    assert.ok(e.release(pad).ok);e.drain();active+=Number(plays);
  }
  assert.equal(active,24);
});
