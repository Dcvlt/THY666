import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));
const fresh=()=>createCore({bytes});
const okay=value=>assert.equal(value.ok,true,value.status);
const press=(engine,pad,notes=[60,64,67])=>engine.press({pad,notes,channel:0,mask:5,velocity:96});
const kind=(events,value)=>events.filter(event=>(event.status&0xf0)===value);

test('MPE defaults and quiet configuration generate no notes or automatic setup',async()=>{
  const engine=await fresh();
  assert.deepEqual(engine.expression.config(),{enabled:false,upperZone:false,members:15,bendRange:48,managerBendRange:2,glide:false,padWindow:1,semitoneWindow:2});
  okay(engine.expression.configure({enabled:true}));assert.deepEqual(engine.drain(),[]);
  assert.equal(engine.expression.configure({members:0}).ok,false);
  assert.equal(engine.expression.configure({unknown:1}).ok,false);
  assert.equal(engine.expression.configure({enabled:1}).ok,false);
  assert.equal(engine.rhythm.start().ok,false);
  assert.equal(engine.arp.configure(0,{enabled:true}).ok,false);
});

test('MPE chord attack uses independent member channels and neutral initialization before Note On',async()=>{
  const engine=await fresh();okay(engine.expression.configure({enabled:true}));okay(press(engine,0));
  const events=engine.drain(),ons=kind(events,0x90);
  assert.deepEqual(ons.filter(event=>event.destination===2).map(event=>[event.channel,event.data1]),[[1,60],[2,64],[3,67]]);
  for(const on of ons) {
    const before=events.slice(0,events.indexOf(on)).filter(event=>event.channel===on.channel&&event.destination===on.destination);
    assert.ok(before.some(event=>(event.status&0xf0)===0xd0&&event.data1===0));
    assert.ok(before.some(event=>(event.status&0xf0)===0xe0&&event.data1===0&&event.data2===64));
  }
  okay(engine.expression.pressure(0,87));assert.deepEqual(engine.drain(),[]);
  okay(engine.advance(8));let pressure=kind(engine.drain(),0xd0);
  okay(engine.advance(16));pressure.push(...kind(engine.drain(),0xd0));
  assert.equal(pressure.length,6);assert.ok(pressure.every(event=>event.data1===87&&event.data2===0));
  okay(engine.release(0));assert.equal(kind(engine.drain(),0x80).length,6);assert.equal(engine.expression.stats().active,0);
});

test('two MPE pads holding the same pitch keep independent pressure and release channels',async()=>{
  const engine=await fresh();okay(engine.expression.configure({enabled:true}));
  okay(press(engine,0,[60]));okay(press(engine,4,[60]));engine.drain();
  const [first,second]=engine.expression.voices();assert.notEqual(first.channel,second.channel);
  okay(engine.expression.pressure(0,25));okay(engine.expression.pressure(4,105));okay(engine.advance(8));
  const pressure=kind(engine.drain(),0xd0).filter(event=>event.destination===2);
  assert.deepEqual(pressure.map(event=>[event.channel,event.data1]),[[first.channel,25],[second.channel,105]]);
  okay(engine.release(0));assert.deepEqual(kind(engine.drain(),0x80).filter(event=>event.destination===2).map(event=>event.channel),[first.channel]);
  assert.equal(engine.expression.voices().filter(voice=>voice.state===1).length,1);
  okay(engine.release(4));engine.drain();assert.equal(engine.stats().activeVoices,0);
});

test('MPE neighbor gesture bends the whole original chord and stays consumed after source release',async()=>{
  const engine=await fresh();okay(engine.expression.configure({enabled:true,glide:true}));
  okay(press(engine,0));engine.drain();okay(engine.expression.pressure(0,80));
  okay(press(engine,4,[62,65,69]));assert.equal(kind(engine.drain(),0x90).length,0);
  assert.equal(engine.expression.stats().consumed,1);okay(engine.expression.pressure(4,80));
  okay(engine.advance(8));okay(engine.advance(16));engine.drain();
  const voices=engine.expression.voices();assert.equal(voices.length,3);
  assert.ok(voices.every(voice=>voice.bend===8362&&voice.modifier===5&&voice.modifierPressure===80));
  okay(engine.release(0));engine.drain();assert.equal(engine.expression.stats().active,0);
  assert.equal(engine.expression.configure({enabled:false}).ok,false);
  okay(engine.release(4));assert.deepEqual(engine.drain(),[]);
  okay(engine.expression.configure({enabled:false}));
});

test('MPE rejects a whole oversized chord and leaves held voices unchanged',async()=>{
  const engine=await fresh();okay(engine.expression.configure({enabled:true,members:3}));okay(press(engine,0));engine.drain();
  const before=engine.expression.voices();assert.equal(press(engine,4,[62]).ok,false);
  assert.deepEqual(engine.expression.voices(),before);assert.deepEqual(engine.drain(),[]);
  okay(engine.release(0));engine.drain();okay(press(engine,4,[62]));
  assert.equal(kind(engine.drain(),0x90).length,2);
});

test('MPE settings restore quietly and preserve their exact versioned payload',async()=>{
  const source=await fresh();okay(source.expression.configure({enabled:true,upperZone:true,members:7,glide:true,padWindow:2}));
  const payload=source.settings.encode(),cold=await fresh();assert.equal(source.settings.version,13);
  assert.equal(cold.settings.apply(payload),true);assert.deepEqual(cold.expression.config(),source.expression.config());
  assert.deepEqual(cold.settings.encode(),payload);assert.deepEqual(cold.drain(),[]);assert.equal(cold.expression.stats().active,0);
  okay(press(cold,0,[60]));assert.equal(kind(cold.drain(),0x90)[0].channel,14);
});

test('MPE setup is explicit, bounded and quiet-only',async()=>{
  const engine=await fresh();assert.equal(engine.expression.sendSetup().ok,false);
  okay(engine.expression.configure({enabled:true,members:3}));okay(engine.expression.sendSetup());
  const setup=engine.drain();assert.ok(setup.length>0&&setup.every(event=>(event.status&0xf0)===0xb0));
  assert.ok(setup.some(event=>event.data1===100&&event.data2===6));
  okay(press(engine,0,[60]));engine.drain();assert.equal(engine.expression.sendSetup().ok,false);assert.deepEqual(engine.drain(),[]);
});
