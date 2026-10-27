import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {MENU_ROWS,STUDIO_MENU} from '../../web/studio-menu.js';

const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));
const okay=value=>assert.equal(value.ok,true,value.status||value.message);
const row=id=>MENU_ROWS.findIndex(value=>value.id===id);
async function fresh(now=0){const engine=await createCore({bytes});okay(engine.advance(now));return engine;}
function edit(engine,id){const index=row(id);okay(engine.state.focus(STUDIO_MENU.groups.findIndex(group=>group.id===MENU_ROWS[index].group),index));okay(engine.state.begin(index));}
const key=(engine,index,pressed)=>okay(engine.firmware.control(0,index,pressed));

test('held physical UP repeats a numeric draft, accelerates, and never catches up in bursts',async()=>{
  const engine=await fresh();edit(engine,'sensitivity.attack');
  const original=engine.state.info().editValue,payload=engine.settings.encode();
  key(engine,1,true);assert.equal(engine.state.info().editValue,original+1);
  key(engine,1,true);assert.equal(engine.state.info().editValue,original+1,'Duplicate OS edges do not make extra presses.');
  okay(engine.advance(399));assert.equal(engine.state.info().editValue,original+1);
  okay(engine.advance(400));assert.equal(engine.state.info().editValue,original+2);
  okay(engine.advance(459));assert.equal(engine.state.info().editValue,original+2);
  okay(engine.advance(460));assert.equal(engine.state.info().editValue,original+3);
  okay(engine.advance(2000));assert.equal(engine.state.info().editValue,original+4);
  okay(engine.advance(2000));assert.equal(engine.state.info().editValue,original+4);
  okay(engine.advance(2030));assert.equal(engine.state.info().editValue,original+5);
  assert.deepEqual(engine.settings.encode(),payload,'Only the draft changes.');
  assert.deepEqual(engine.drain(),[]);
  key(engine,1,false);okay(engine.advance(5000));assert.equal(engine.state.info().editValue,original+5);
  okay(engine.state.navigate('left'));assert.equal(engine.firmware.response().attack,original);
  edit(engine,'sensitivity.attack');key(engine,3,true);
  assert.equal(engine.state.info().editValue,original-1);
  okay(engine.advance(5400));assert.equal(engine.state.info().editValue,original-2);
  key(engine,3,false);okay(engine.state.cancel());
});

test('opposed UP/DOWN holds cancel repetition and a remaining held button does not restart it',async()=>{
  const engine=await fresh();edit(engine,'sensitivity.attack');
  key(engine,1,true);key(engine,3,true);
  const opposed=engine.state.info().editValue;
  okay(engine.advance(1000));assert.equal(engine.state.info().editValue,opposed);
  key(engine,3,false);okay(engine.advance(2000));assert.equal(engine.state.info().editValue,opposed);
  key(engine,1,false);key(engine,1,true);okay(engine.advance(2400));
  assert.equal(engine.state.info().editValue,opposed+2);
  key(engine,1,false);
});

test('selector, toggle, small numeric and pad-target holds remain single actions',async()=>{
  for(const id of ['performance.root','performance.padOverlays','lights.idle','sensitivity.target']){
    const engine=await fresh();edit(engine,id);key(engine,1,true);
    const first=engine.state.info().editValue;
    okay(engine.advance(2000));assert.equal(engine.state.info().editValue,first,id);
    key(engine,1,false);assert.deepEqual(engine.drain(),[]);
  }
});

test('cancel, context changes and panic end repetition while independent MIDI owners remain correct',async()=>{
  const engine=await fresh();
  okay(engine.press({pad:0,notes:[60,64,67],channel:3,mask:4,velocity:89}));engine.drain();
  edit(engine,'sensitivity.attack');key(engine,1,true);
  const held=engine.owner(0);
  okay(engine.advance(400));assert.deepEqual(engine.owner(0),held);assert.deepEqual(engine.drain(),[]);
  okay(engine.state.navigate('left'));edit(engine,'sensitivity.attack');
  const renewed=engine.state.info().editValue;
  okay(engine.advance(1000));assert.equal(engine.state.info().editValue,renewed);
  key(engine,1,false);key(engine,1,true);
  okay(engine.state.cancel());okay(engine.state.begin(row('sensitivity.attack')));
  const restarted=engine.state.info().editValue;
  okay(engine.advance(1500));assert.equal(engine.state.info().editValue,restarted,'Direct cancel/begin must not inherit a physical hold.');
  key(engine,1,false);key(engine,1,true);
  okay(engine.state.set(row('sensitivity.target'),1));
  const captured=engine.state.info().editValue;
  okay(engine.advance(2000));assert.equal(engine.state.info().editValue,captured);
  assert.deepEqual(engine.owner(0),held);assert.deepEqual(engine.drain(),[]);
  key(engine,1,false);key(engine,1,true);okay(engine.panic());
  const before=engine.state.info().editValue;
  okay(engine.advance(2500));assert.equal(engine.state.info().editValue,before);
  const cleanup=engine.drain();assert.equal(cleanup.length,3);assert.ok(cleanup.every(event=>event.status===0x83&&event.destination===2));
  key(engine,1,false);
});

test('hold timing crosses the unsigned millisecond wrap without delaying the first repeat',async()=>{
  const engine=await fresh(0xffffffff-300);edit(engine,'sensitivity.attack');
  const initial=engine.state.info().editValue;key(engine,1,true);
  okay(engine.advance(98));assert.equal(engine.state.info().editValue,initial+1);
  okay(engine.advance(99));assert.equal(engine.state.info().editValue,initial+2);
  key(engine,1,false);
});
