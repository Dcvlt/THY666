import {legacyViewInfo} from './legacy-view-fixture.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {validateFirmwareVelocity} from '../../web/firmware.js';

const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));
const okay=result=>assert.equal(result.ok,true,result.status);
const config=(changes={})=>({mode:1,curve:0,softRate:4,hardRate:128,minimum:10,maximum:120,...changes});
const response=(changes={})=>({attack:1200,release:1000,floor:800,ceiling:3200,curve:0,...changes});
const values=value=>Object.fromEntries(Object.entries(value).filter(([key])=>key!=='overridden'));
async function fresh() {
  const engine=await createCore({bytes}),firmware=engine.firmware;
  okay(firmware.layout(Array.from({length:96},(_,pad)=>({notes:pad<48?[60,64,67,0,0,0,0]:[72,0,0,0,0,0,0],mask:pad<48?7:1,color:1}))));
  okay(firmware.setResponse(-1,response()));okay(firmware.route(0,3,5));
  let now=0;
  return {engine,firmware,
    sample(pad,raw,count=1){for(let n=0;n<count;n++)okay(firmware.sample(pad,raw,now++));},
    strike(pad,rate){
      this.sample(pad,100,8);
      let raw=100,firstCross=null,attackAt=null;
      for(let samples=0;samples<4096&&!firmware.sensor(pad).held;samples++){
        raw=Math.min(4095,raw+rate);
        if(raw>=firmware.response(pad).attack&&firstCross===null)firstCross=now;
        this.sample(pad,raw);
        if(firmware.sensor(pad).held)attackAt=now-1;
      }
      assert.equal(firmware.sensor(pad).held,1);
      const sensor=firmware.sensor(pad),events=engine.drain();
      this.sample(pad,100,2);
      const releases=engine.drain();
      return {sensor,events,releases,firstCross,attackAt};
    }};
}

test('strike velocity inherits by half, overrides one pad and clears independently of pressure',async()=>{
  const h=await fresh(),{firmware}=h;
  assert.equal(firmware.velocity().mode,0);
  okay(firmware.setVelocity(-2,config()));
  const local=config({curve:2,minimum:31,maximum:105});
  okay(firmware.setVelocity(95,local));okay(firmware.setResponse(95,response({curve:1})));
  okay(firmware.setVelocity(-2,config({curve:1})));
  assert.deepEqual(firmware.velocity(95),{...local,overridden:1});
  assert.deepEqual(firmware.velocity(94),{...config({curve:1}),overridden:0});
  okay(firmware.clearVelocity(95));
  assert.deepEqual(firmware.velocity(95),{...config({curve:1}),overridden:0});
  assert.equal(firmware.response(95).overridden,1);assert.equal(firmware.response(95).curve,1);
  assert.equal(firmware.velocity(0).mode,0,'Editing Right leaves inherited Left unchanged.');
  okay(firmware.resetVelocity(-2));assert.equal(firmware.velocity(-2).mode,0);
  assert.equal(firmware.response(95).overridden,1);
});

test('combined scope loading preflights pressure and velocity together before changing either',async()=>{
  const h=await fresh(),{firmware,engine}=h;
  okay(firmware.setResponse(1,response({curve:1})));
  okay(firmware.pressPad(1));engine.drain();
  const originalPressure=firmware.response(),originalVelocity=firmware.velocity(),owner=engine.owner(1);
  const nextPressure=response({attack:1400}),nextVelocity=config({curve:2});
  assert.equal(firmware.setSensitivity(-1,nextPressure,nextVelocity).ok,false,
    'The held pad overrides pressure but still inherits velocity.');
  assert.deepEqual(firmware.response(),originalPressure);assert.deepEqual(firmware.velocity(),originalVelocity);
  assert.deepEqual(engine.owner(1),owner);assert.deepEqual(engine.drain(),[]);
  okay(engine.release(1));engine.drain();
  okay(firmware.setSensitivity(-1,nextPressure,nextVelocity));
  assert.deepEqual(firmware.response(),{...nextPressure,overridden:0});
  assert.deepEqual(firmware.velocity(),{...nextVelocity,overridden:0});
  assert.equal(firmware.response(1).curve,1);assert.equal(firmware.response(1).overridden,1);
  assert.equal(firmware.velocity(1).overridden,0);
  okay(firmware.setSensitivity(95,response({attack:1500}),config({minimum:24,maximum:96})));
  assert.equal(firmware.response(95).overridden,1);assert.equal(firmware.velocity(95).overridden,1);
  const padPressure=firmware.response(95),padVelocity=firmware.velocity(95);
  assert.equal(firmware.setSensitivity(95,response({attack:900,release:1000}),config({mode:0})).ok,false);
  assert.deepEqual(firmware.response(95),padPressure);assert.deepEqual(firmware.velocity(95),padVelocity);
});

test('real timed ADC ramps produce softer and firmer DIN/USB attacks with no added trigger wait',async()=>{
  const h=await fresh();okay(h.firmware.setVelocity(-1,config()));
  const outcomes=[4,64,128].map(rate=>h.strike(0,rate));
  const velocities=outcomes.map(outcome=>outcome.events.find(event=>(event.status&0xf0)===0x90).data2);
  assert.equal(velocities[0],10);assert.equal(velocities[2],120);
  assert.ok(velocities[0]<velocities[1]&&velocities[1]<velocities[2]);
  for(const outcome of outcomes){
    assert.equal(outcome.attackAt,outcome.firstCross+1,'Attack still uses the ordinary two samples.');
    assert.equal(outcome.events.length,6);
    assert.ok(outcome.events.every(event=>(event.status&0xf0)===0x90&&event.data2===outcome.sensor.lastStrike));
    assert.ok(outcome.events.some(event=>event.destination===2&&event.din[0]===0x93));
    assert.equal(outcome.releases.length,6);assert.ok(outcome.releases.every(event=>(event.status&0xf0)===0x80));
  }
  assert.equal(h.engine.stats().activeVoices,0);
});

test('strike curves use the C mapping while continuous pressure remains independent',async()=>{
  const velocities=[];
  for(const curve of [0,1,2]){
    const h=await fresh(),requested=config({curve});okay(h.firmware.setVelocity(0,requested));
    const outcome=h.strike(0,64);
    velocities.push(outcome.sensor.lastStrike);
    assert.equal(outcome.sensor.lastStrike,h.firmware.velocityValue(requested,outcome.sensor.strikeRate));
    assert.equal(h.firmware.response(0).curve,0);
    assert.equal(h.firmware.velocityValue(requested,0),requested.minimum);
    assert.equal(h.firmware.velocityValue(requested,4095),requested.maximum);
  }
  assert.ok(velocities[1]>velocities[0]&&velocities[0]>velocities[2]);
});

test('velocity edits cannot change a held attack; direct pointer-style presses retain the fixed fallback',async()=>{
  const h=await fresh();okay(h.firmware.setVelocity(-1,config()));okay(h.firmware.expression(87,false));
  h.sample(0,100,8);h.sample(0,1200,2);h.engine.drain();
  const owner=h.engine.owner(0),before=h.firmware.velocity(0);
  assert.equal(h.firmware.setVelocity(0,config({maximum:64})).ok,false);
  assert.deepEqual(h.firmware.velocity(0),before);assert.deepEqual(h.engine.owner(0),owner);
  assert.deepEqual(h.engine.drain(),[]);
  okay(h.firmware.route(0,7,2));h.sample(0,100,2);
  assert.ok(h.engine.drain().every(event=>event.status===0x83&&(event.destination===0||event.destination===2)));
  okay(h.firmware.pressPad(0));
  assert.ok(h.engine.drain().every(event=>event.status===0x97&&event.data2===87));
  okay(h.engine.release(0));h.engine.drain();
});

test('v13 cold restoration preserves velocity and pressure overrides without replaying strikes',async()=>{
  const h=await fresh();okay(h.firmware.setVelocity(-1,config({curve:1})));
  okay(h.firmware.setVelocity(95,config({mode:0,minimum:35,maximum:80})));
  okay(h.firmware.setResponse(95,response({attack:1450,curve:2})));
  h.strike(0,64);
  const payload=h.engine.settings.encode();assert.equal(h.engine.settings.version,15);
  const cold=await fresh();assert.equal(cold.engine.settings.apply(payload),true);
  assert.deepEqual(cold.firmware.velocity(),h.firmware.velocity());
  assert.deepEqual(cold.firmware.velocity(95),h.firmware.velocity(95));
  assert.deepEqual(cold.firmware.response(95),h.firmware.response(95));
  assert.equal(cold.firmware.sensor(0).lastStrike,0);assert.equal(cold.firmware.sensor(0).strikeRate,0);
  assert.equal(cold.engine.stats().activeVoices,0);assert.deepEqual(cold.engine.drain(),[]);
});

test('legacy v6 loads Fixed velocity while retaining its existing song and route values',async()=>{
  const h=await fresh();okay(h.firmware.setVelocity(-1,config()));
  okay(h.engine.sequencer.add(2,[{tick:240,duration:480,pitch:67,value:91}]));
  const modern=legacyViewInfo(h.engine.settings.encode()),legacy=new Uint8Array(2345+modern.length-5013);
  assert.equal(new TextDecoder().decode(modern.subarray(5013,5017)),'THYS');
  legacy.set(modern.subarray(0,2345));legacy[6]=6;legacy.set(modern.subarray(5013),2345);
  assert.equal(h.engine.settings.validate(legacy),true);
  const cold=await fresh();assert.equal(cold.engine.settings.apply(legacy),true);
  assert.equal(cold.firmware.velocity().mode,0);assert.equal(cold.firmware.velocity(95).overridden,0);
  assert.deepEqual(cold.engine.sequencer.events(2),h.engine.sequencer.events(2));
  assert.deepEqual(cold.firmware.padRoute(1),h.firmware.padRoute(1));
});

test('invalid velocity settings and unsafe MIDI ranges are rejected before mutation',async()=>{
  const {firmware}=await fresh(),before=firmware.velocity();
  for(const changes of [{softRate:128,hardRate:128},{softRate:-1},{hardRate:4096},{minimum:0},{minimum:121,maximum:120},{mode:2},{curve:3}]){
    const requested=config(changes);assert.ok(validateFirmwareVelocity(requested));
    assert.equal(firmware.setVelocity(-1,requested).ok,false);assert.deepEqual(firmware.velocity(),before);
  }
  assert.equal(validateFirmwareVelocity(config()),null);
  assert.equal(firmware.velocity(96),null);assert.equal(firmware.velocityValue(config(),-1),null);
  assert.equal(firmware.velocityValue(config(),4096),null);
  assert.deepEqual(values(firmware.velocity()),{mode:0,curve:0,softRate:2,hardRate:128,minimum:1,maximum:127});
});
