import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {MENU_ROWS} from '../../web/studio-menu.js';
const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));
const row=id=>MENU_ROWS.findIndex(r=>r.id===id);
const set=(s,id,v)=>assert.equal(s.set(row(id),v).ok,true,id);
async function setup(){const e=await createCore({bytes});
  const pads=Array.from({length:96},(_,p)=>({notes:p<48?[60,64,67,0,0,0,0]:[60,0,0,0,0,0,0],mask:p<48?7:1,color:1}));
  pads[4]={notes:[57,62,65,0,0,0,0],mask:7,color:2};pads[52].notes[0]=62;
  assert.ok(e.firmware.layout(pads,{anchor:60,degrees:[0,2,4,5,7,9,11]}).ok);return e;}
test('Live Update feeds future right notes with key or chord tones and retains old note releases',async()=>{
  const e=await setup(),s=e.state,f=e.firmware;
  set(s,'performance.melodyLayout',1); // Scale degrees.
  assert.ok(f.pressPad(48).ok);e.drain();
  set(s,'performance.liveUpdate',1);assert.ok(f.pressPad(4).ok);e.drain();
  assert.ok(f.pressPad(52).ok);assert.equal(e.drain().find(x=>(x.status&0xf0)===0x90).data1,64);
  assert.ok(e.release(52).ok);assert.ok(e.release(48).ok);
  assert.ok(e.drain().some(x=>(x.status&0xf0)===0x80&&x.data1===60));
  assert.ok(e.release(4).ok);e.drain();
  set(s,'performance.liveUpdateLockScale',0);assert.ok(f.pressPad(4).ok);e.drain();
  assert.ok(f.pressPad(52).ok);assert.equal(e.drain().find(x=>(x.status&0xf0)===0x90).data1,65);
  assert.ok(e.release(52).ok);assert.ok(e.release(4).ok);
});
test('Mute direct chord selects harmony without a direct Note On',async()=>{
  const e=await setup(),s=e.state,f=e.firmware;
  set(s,'performance.liveUpdate',1);set(s,'performance.liveUpdateLockScale',0);set(s,'performance.liveUpdateMuteChord',1);
  assert.ok(f.pressPad(0).ok);assert.deepEqual(e.drain(),[]);
  assert.ok(f.pressPad(52).ok);assert.equal(e.drain().find(x=>(x.status&0xf0)===0x90).data1,64);
  assert.ok(e.release(52).ok);assert.ok(e.release(0).ok);
  assert.equal(e.stats().heldPads,0);
});

test('generated chord cells and Piano geometry share pitches with the actual C attacks',async()=>{
  const e=await setup(),s=e.state,f=e.firmware;
  set(s,'chords.layout',1);
  assert.deepEqual(f.chordCell(4),{mask:7,root:61,degree:255,notes:[61,65,68,0,0,0,0]});
  assert.ok(f.pressPad(4).ok);assert.deepEqual(e.drain().filter(x=>(x.status&0xf0)===0x90).map(x=>x.data1),[61,65,68]);
  set(s,'chords.layout',2);
  assert.deepEqual(f.previewPad(4).notes,[62,65,69]);
  assert.ok(e.release(4).ok);assert.deepEqual(e.drain().filter(x=>(x.status&0xf0)===0x80).map(x=>x.data1),[61,65,68]);
  set(s,'performance.melodyLayout',8);
  assert.deepEqual(f.previewPad(52).notes,[61]);assert.deepEqual(f.previewPad(49).notes,[60]);
  assert.deepEqual(f.previewPad(48).notes,[]);
  assert.ok(f.pressPad(48).ok);assert.deepEqual(e.drain(),[]);assert.ok(e.release(48).ok);
});

test('independent Drop controls preserve held ownership, with exclusive inversions retained',async()=>{
  const e=await setup(),s=e.state,f=e.firmware;
  assert.ok(f.pressPad(0).ok);e.drain();
  set(s,'chords.voicingMode',1);
  for(const control of [0,1]){assert.ok(f.control(1,control,true).ok);assert.ok(f.control(1,control,false).ok);}
  assert.equal(f.state().dropMask,3);assert.equal(f.state().dropAvailable,3);
  assert.deepEqual(f.previewPad(0).notes,[60,52,55]);
  assert.ok(e.release(0).ok);assert.deepEqual(e.drain().filter(x=>(x.status&0xf0)===0x80).map(x=>x.data1),[60,64,67]);
  set(s,'chords.voicingMode',0);set(s,'chords.inversion',1);
  assert.deepEqual(f.previewPad(0).notes,[72,64,67]);
});

test('Keep Transpose resets only actual root or scale changes and follows C state',async()=>{
  const e=await setup(),s=e.state;
  set(s,'performance.transpose',7);set(s,'performance.keepTranspose',0);
  set(s,'performance.root',0);assert.equal(s.get(row('performance.transpose')),7);
  set(s,'performance.root',2);assert.equal(s.get(row('performance.transpose')),0);
  set(s,'performance.transpose',5);set(s,'performance.keepTranspose',1);
  set(s,'performance.root',5);assert.equal(s.get(row('performance.transpose')),5);
});

test('custom library preserves holes and MIDI zero through User and Six User layouts',async()=>{
  const e=await setup(),s=e.state,f=e.firmware;
  const record={used:true,name:'Zero chord',root:0,notes:[0,4,7,11,14,17,21],mask:5};
  assert.ok(f.library.set(1,0,record).ok);
  set(s,'chords.layout',5);assert.deepEqual(f.previewPad(0).notes,[0,7]);
  assert.deepEqual(f.chordCell(0).notes,record.notes);assert.equal(f.chordCell(0).mask,5);
  assert.deepEqual(f.previewPad(2).notes,[]); // Lower pair falls below MIDI zero.
  set(s,'chords.layout',0);
  assert.ok(f.library.set(2,0,{used:true,name:'Sparse set',chords:[0,255,255,255,255,255]}).ok);
  set(s,'chords.layout',4);assert.deepEqual(f.previewPad(0).notes,[60,67]);
  assert.deepEqual(f.previewPad(4).notes,[]);
  assert.equal(f.library.delete(1,0).status,'in-use');
});

test('copying a user chord in another key does not apply that key twice',async()=>{
  const e=await setup(),s=e.state,f=e.firmware;
  const chord={used:true,name:'C reference',root:60,notes:[60,64,67,0,9,11,127],mask:7};
  assert.ok(f.library.set(1,0,chord).ok);set(s,'performance.root',2);set(s,'chords.layout',5);
  assert.deepEqual(f.previewPad(0).notes,[62,66,69]);
  const copy=f.library.capture(1,1);assert.ok(copy.ok);
  assert.equal(copy.record.root,60);assert.deepEqual(copy.record.notes,chord.notes);
  assert.ok(f.library.set(1,1,copy.record).ok);
  assert.deepEqual(f.previewPad(1).notes,[62,66,69]);
});

test('Auto leading shares the C policy and keeps manual inversion controls inactive',async()=>{
  const e=await setup(),s=e.state,f=e.firmware;
  set(s,'chords.voicingMode',2);
  assert.deepEqual(f.previewPad(4).notes,[57,62,65]);
  assert.ok(f.pressPad(0).ok);e.drain();
  assert.deepEqual(f.previewPad(4).notes,[69,62,65]);
  for(const control of [0,1,2]){assert.ok(f.control(1,control,true).ok);assert.ok(f.control(1,control,false).ok);}
  assert.equal(f.state().inversion,0);assert.equal(f.state().dropMask,0);
  assert.ok(f.pressPad(4).ok);e.drain();
  assert.ok(e.release(0).ok);
  assert.deepEqual(e.drain().filter(x=>(x.status&0xf0)===0x80).map(x=>x.data1),[60,64,67]);
  assert.ok(e.release(4).ok);e.drain();
  set(s,'chords.voicingMode',0);
  assert.deepEqual(f.previewPad(4).notes,[57,62,65]);
});

test('fold and guitar register are persisted optional modes with frozen held releases',async()=>{
  for(const mode of [3,4]) {
    const e=await setup(),s=e.state,f=e.firmware;
    assert.ok(f.pressPad(0).ok);e.drain();
    set(s,'chords.voicingMode',mode);
    for(const control of [0,1,2]){assert.ok(f.control(1,control,true).ok);assert.ok(f.control(1,control,false).ok);}
    assert.equal(f.state().voicingMode,mode);
    const notes=f.previewPad(0).notes;
    assert.deepEqual([...new Set(notes.map(n=>n%12))].sort((a,b)=>a-b),[0,4,7]);
    if(mode===3)assert.ok(Math.max(...notes)-Math.min(...notes)<12);
    else assert.ok(notes.every(n=>n>=40&&n<=75));
    assert.ok(e.release(0).ok);
    assert.deepEqual(e.drain().filter(x=>(x.status&0xf0)===0x80).map(x=>x.data1),[60,64,67]);
    const saved=e.settings.encode(),cold=await setup();
    assert.equal(e.settings.version,15);assert.ok(cold.settings.apply(saved));
    assert.equal(cold.firmware.state().voicingMode,mode);
    assert.deepEqual(cold.firmware.previewPad(0).notes,notes);
  }
});
