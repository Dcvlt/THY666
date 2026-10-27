import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {MENU_ROWS} from '../../web/studio-menu.js';

const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));
const fresh=()=>createCore({bytes});
const okay=value=>assert.equal(value.ok,true,value.status||value.message);
const scale={used:true,name:'PENTATONIC',mask:0x295};
const chord={used:true,name:'ZERO CHORD',root:0,notes:[0,12,7,99,0,64,127],mask:0x45};
const set={used:true,name:'MY SET',chords:[0,255,0,255,255,0]};
async function playable() {
  const engine=await fresh();
  okay(engine.firmware.layout(Array.from({length:96},(_,pad)=>({notes:[60,64,67,0,0,0,0],mask:pad<48?7:1,color:1})),
    {anchor:60,degrees:[0,2,4,5,7,9,11],root:0}));
  return engine;
}

test('library ABI exposes all bounded slots without changing MIDI or settings',async()=>{
  const engine=await fresh(),library=engine.firmware.library,before=engine.settings.encode();
  assert.deepEqual(library.capacities,{scales:8,chords:24,sets:3});
  assert.deepEqual([0,1,2].map(kind=>library.records(kind).length),[8,24,3]);
  assert.ok([0,1,2].every(kind=>library.records(kind).every(record=>record&&record.used===false)));
  assert.deepEqual(library.records(3),[]);assert.equal(library.record(0,8),null);
  assert.equal(library.record(1,24),null);assert.equal(library.record(2,3),null);
  assert.deepEqual(engine.settings.encode(),before);assert.deepEqual(engine.drain(),[]);
});

test('actual C setters retain MIDI zero, masks, inactive values and six-set references',async()=>{
  const engine=await fresh(),library=engine.firmware.library;
  okay(library.set(0,0,scale));okay(library.set(1,0,chord));okay(library.set(2,0,set));
  assert.deepEqual(library.record(0,0),scale);assert.deepEqual(library.record(1,0),chord);
  assert.deepEqual(library.record(2,0),set);assert.deepEqual(engine.drain(),[]);
  const file=library.exportFile();assert.equal(file.length,756);assert.deepEqual(Array.from(file.subarray(0,4)),[84,72,89,76]);
  const cold=await fresh();okay(cold.firmware.library.importFile(file));
  for(const [kind,record] of [[0,scale],[1,chord],[2,set]])assert.deepEqual(cold.firmware.library.record(kind,0),record);
  assert.deepEqual(cold.firmware.library.exportFile(),file);assert.deepEqual(cold.drain(),[]);
});

test('C rejects invalid records and linked deletion atomically instead of normalizing or truncating',async()=>{
  const engine=await fresh(),library=engine.firmware.library;
  okay(library.set(0,0,scale));okay(library.set(1,0,chord));okay(library.set(2,0,set));
  const before=library.exportFile();
  for(const [kind,value] of [[0,{...scale,name:''}],[0,{...scale,name:'             '}],
    [0,{...scale,name:'TOO LONG NAME'}],[0,{...scale,name:'BAD\0NAME'}],[0,{...scale,name:'é'}],
    [0,{...scale,mask:0}],[0,{...scale,mask:2}],[0,{...scale,mask:4097}],
    [1,{...chord,root:128}],[1,{...chord,mask:128}],[1,{...chord,mask:0}],
    [1,{...chord,notes:[0,12,7,128,0,64,127]}],
    [1,{...chord,notes:[0,12,7,256,0,64,127]}],
    [2,{...set,chords:[23,255,0,255,255,0]}],[2,{...set,chords:[24,255,0,255,255,0]}],
    [0,{...scale,used:false}],[1,{...chord,used:1}]]) {
    assert.equal(library.set(kind,0,value).ok,false,JSON.stringify(value));
    assert.deepEqual(library.exportFile(),before);
  }
  assert.equal(library.delete(1,0).status,'in-use');assert.deepEqual(library.exportFile(),before);
  okay(library.delete(2,0));okay(library.delete(1,0));
  assert.equal(library.record(1,0).used,false);assert.deepEqual(library.record(1,0).notes,Array(7).fill(0));
});

test('THYL validation and busy checks preserve the complete applied library',async()=>{
  const engine=await fresh(),library=engine.firmware.library;
  okay(library.set(0,0,scale));const before=library.exportFile();
  for(const mutate of [file=>{file[4]=99;},file=>{file[16]^=1;},file=>{file[755]^=255;}]) {
    const corrupt=before.slice();mutate(corrupt);assert.equal(library.importFile(corrupt).ok,false);
    assert.deepEqual(library.exportFile(),before);
  }
  for(const invalid of [before.subarray(1),new Uint8Array(757),Array.from(before),null])
    assert.equal(library.importFile(invalid).ok,false);
  okay(engine.press({pad:0,notes:[60],mask:1,channel:0}));engine.drain();
  assert.equal(library.importFile(before).status,'busy');assert.deepEqual(library.exportFile(),before);
  okay(engine.release(0));engine.drain();okay(library.importFile(before));assert.deepEqual(engine.drain(),[]);
});

test('Copy current captures C material into a draft without saving it',async()=>{
  const engine=await playable(),library=engine.firmware.library,before=library.exportFile();
  const copiedScale=library.capture(0,0),copiedChord=library.capture(1,0);
  okay(copiedScale);okay(copiedChord);
  assert.equal(copiedScale.record.used,true);assert.equal(copiedScale.record.mask,0xab5);
  assert.equal(copiedChord.record.used,true);assert.deepEqual(copiedChord.record.notes.slice(0,3),[60,64,67]);
  assert.equal(copiedChord.record.mask,7);assert.deepEqual(library.exportFile(),before);
  assert.equal(library.record(0,0).used,false);assert.equal(library.record(1,0).used,false);
  assert.deepEqual(engine.drain(),[]);
});

test('full settings retain the custom library and the OLED editor opens without emitting notes',async()=>{
  const engine=await playable(),library=engine.firmware.library;
  okay(library.set(0,7,scale));okay(library.set(1,23,chord));
  okay(library.set(2,2,{...set,chords:[23,255,23,255,255,23]}));
  const payload=engine.settings.encode(),cold=await playable();assert.equal(cold.settings.apply(payload),true);
  assert.deepEqual(cold.firmware.library.exportFile(),library.exportFile());assert.deepEqual(cold.drain(),[]);
  okay(cold.firmware.library.openEditor(1,23));assert.deepEqual(cold.drain(),[]);
  assert.equal(cold.firmware.library.openEditor(1,24).ok,false);
});

test('custom-scale shortcuts expose the C-selected slot and protect active or referenced records',async()=>{
  const engine=await playable(),f=engine.firmware,library=f.library;
  const userScale=MENU_ROWS.findIndex(row=>row.id==='chords.userScale');assert.ok(userScale>=0);
  okay(library.set(0,0,scale));okay(library.set(0,7,{...scale,name:'LAST SCALE'}));
  okay(f.scaleShortcut(9,71));assert.equal(f.scaleShortcut(9),71);
  okay(f.control(3,9,true));okay(f.control(3,9,false));
  assert.equal(f.state().userScale,1);assert.equal(library.record(0,f.state().userScale-1).name,scale.name);
  assert.equal(library.capture(0,2).record.mask,scale.mask);assert.deepEqual(engine.drain(),[]);
  assert.equal(library.delete(0,0).status,'in-use');
  assert.equal(library.set(0,0,{used:false,name:'',mask:0}).status,'in-use');
  okay(engine.state.set(userScale,0));assert.equal(f.state().userScale,0);
  assert.equal(library.delete(0,0).status,'in-use','A saved U shortcut still owns the reference.');
  okay(f.scaleShortcut(9,70));okay(library.delete(0,0));
  okay(f.scaleShortcut(11,78));okay(f.control(3,11,true));okay(f.control(3,11,false));
  assert.equal(f.state().userScale,8);assert.equal(library.record(0,7).name,'LAST SCALE');
  assert.equal(f.scaleShortcut(11,79).ok,false);assert.deepEqual(engine.drain(),[]);
});
