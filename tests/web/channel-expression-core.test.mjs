import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {MENU_ROWS} from '../../web/studio-menu.js';
const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));
const row=id=>MENU_ROWS.findIndex(value=>value.id===id);
const okay=value=>assert.equal(value.ok,true,value.status||value.message);
const set=(e,id,value)=>okay(e.state.set(row(id),value));
const press=(e,pad,note=60)=>e.press({pad,notes:[note],channel:3,mask:5,velocity:96});
const kind=(events,type,destination)=>events.filter(event=>(event.status&0xf0)===type&&(destination===undefined||event.destination===destination));
const bend=event=>event.data1|(event.data2<<7);
const fresh=()=>createCore({bytes});
function policy(e,pressure=2,bending=0){set(e,'midi.routeTarget',1);set(e,'midi.pressure',pressure);set(e,'midi.pressureBend',bending);}

test('ordinary Channel aftertouch merges held pads by maximum and restores the remaining owner',async()=>{
  const e=await fresh();policy(e);okay(press(e,0));okay(press(e,4));e.drain();
  okay(e.expression.axes(0,10,90));okay(e.expression.axes(4,100,35));okay(e.advance(8));
  assert.deepEqual(kind(e.drain(),0xd0).map(event=>[event.destination,event.channel,event.data1]),[[0,3,90],[2,3,90]]);
  okay(e.release(0));e.drain();okay(e.advance(16));assert.deepEqual(kind(e.drain(),0xd0).map(event=>event.data1),[35,35]);
  okay(e.release(4));e.drain();okay(e.advance(24));assert.deepEqual(kind(e.drain(),0xd0).map(event=>event.data1),[0,0]);
});

test('pressure bend uses the newest held binding while aftertouch stays independent',async()=>{
  const e=await fresh();policy(e,2,1);okay(press(e,0));e.drain();
  okay(e.expression.axes(0,64,90));okay(e.advance(8));let events=e.drain();
  assert.deepEqual(kind(events,0xd0,2).map(event=>event.data1),[90]);assert.deepEqual(kind(events,0xe0,2).map(bend),[12319]);
  okay(press(e,4));events=e.drain();assert.ok(kind(events,0xe0,2).some(event=>bend(event)===8192));
  okay(e.expression.axes(4,127,25));okay(e.advance(16));events=e.drain();
  assert.deepEqual(kind(events,0xe0,2).map(bend),[16383]);assert.equal(kind(events,0xd0,2).length,0);
  okay(e.release(4));e.drain();okay(e.advance(24));assert.deepEqual(kind(e.drain(),0xe0,2).map(bend),[12319]);
  okay(e.release(0));e.drain();okay(e.advance(32));assert.deepEqual(kind(e.drain(),0xe0,2).map(bend),[8192]);
});

test('held ordinary expression retains mode and route after future-attack settings change',async()=>{
  const e=await fresh();policy(e,2,1);okay(press(e,0));e.drain();
  policy(e,0,2);okay(e.firmware.route(0,9,1));okay(e.expression.axes(0,127,80));okay(e.advance(8));
  const events=e.drain();assert.deepEqual(kind(events,0xd0).map(event=>[event.destination,event.channel,event.data1]),[[0,3,80],[2,3,80]]);
  assert.deepEqual(kind(events,0xe0,2).map(bend),[16383]);okay(e.release(0));e.drain();
  okay(e.advance(16));assert.deepEqual(kind(e.drain(),0xe0,2).map(bend),[8192]);
});

test('ordinary channel cleanup is queued before a fresh generated Note On',async()=>{
  const e=await fresh();policy(e,2,1);okay(press(e,0));e.drain();
  okay(e.expression.axes(0,127,100));okay(e.advance(8));e.drain();okay(e.release(0));e.drain();
  const generated=e.rhythm.acquire({source:'arp1',note:62,channel:3,mask:5});okay(generated);
  const events=e.drain();
  for(const dest of [0,2]){
    const routed=events.filter(event=>event.destination===dest),at=routed.findIndex(event=>(event.status&0xf0)===0x90);
    assert.ok(at>=2);assert.ok(routed.slice(0,at).some(event=>(event.status&0xf0)===0xd0&&event.data1===0));
    assert.ok(routed.slice(0,at).some(event=>(event.status&0xf0)===0xe0&&bend(event)===8192));
  }
  okay(e.rhythm.release(generated.handle));e.drain();
});

test('USB session reset excludes old held expression and preserves DIN contribution',async()=>{
  const e=await fresh();policy(e,2,1);okay(press(e,0));e.drain();
  okay(e.expression.axes(0,127,100));okay(e.advance(8));e.drain();okay(e.rhythm.usbReset(8));
  okay(e.expression.axes(0,100,110));okay(e.advance(16));let events=e.drain();
  assert.equal(events.filter(event=>event.destination===0).length,0);assert.deepEqual(kind(events,0xd0,2).map(event=>event.data1),[110]);
  okay(press(e,4,62));e.drain();okay(e.expression.axes(4,20,40));okay(e.advance(24));events=e.drain();
  assert.deepEqual(kind(events,0xd0,0).map(event=>event.data1),[40]);
  assert.equal(kind(events,0xd0,2).length,0,'DIN maximum remains the old, still-held 110');
  okay(e.release(0));okay(e.release(4));e.drain();okay(e.advance(32));e.drain();
});

test('route expression restores quietly with defaults and exact V11 settings',async()=>{
  const source=await fresh();policy(source,2,2);set(source,'midi.bendRange',12);
  const saved=source.settings.encode(),cold=await fresh();assert.equal(cold.settings.apply(saved),true);
  assert.equal(cold.state.get(row('midi.pressure')),2);assert.equal(cold.state.get(row('midi.pressureBend')),2);
  assert.equal(cold.state.get(row('midi.bendRange')),12);assert.deepEqual(cold.settings.encode(),saved);assert.deepEqual(cold.drain(),[]);
});

test('split profile sends upper AT in ordinary MIDI while primary pressure controls bend',async()=>{
  const e=await fresh();policy(e,2,1);okay(e.firmware.setExpressionProfile(-1,{enabled:true,split:50,pressureCurve:0,aftertouchCurve:0}));
  okay(e.firmware.setResponse(-1,{attack:1200,release:1000,floor:800,ceiling:3200,curve:0}));
  assert.deepEqual(e.firmware.expressionAxes(0,1400),{pressure:63,aftertouch:0});
  assert.deepEqual(e.firmware.expressionAxes(0,2600),{pressure:127,aftertouch:63});
  okay(press(e,0));e.drain();okay(e.expression.axes(0,127,63));okay(e.advance(8));
  const events=e.drain();assert.deepEqual(kind(events,0xd0,2).map(event=>event.data1),[63]);assert.deepEqual(kind(events,0xe0,2).map(bend),[16383]);
  assert.equal(kind(events,0xb0).length,0,'CC74 is a per-note MPE mapping, not an implicit ordinary-channel CC assignment');
});
