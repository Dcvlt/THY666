import {legacyViewInfo} from './legacy-view-fixture.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {MENU_ROWS} from '../../web/studio-menu.js';
import {validateFirmwareResponse,validateFirmwareReleasePolicy} from '../../web/firmware.js';

const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));
const okay=value=>assert.equal(value.ok,true,value.status||value.message);
const row=id=>MENU_ROWS.findIndex(value=>value.id===id);
const response=(changes={})=>({attack:1200,release:1000,floor:800,ceiling:3200,curve:0,...changes});
const velocity=(changes={})=>({mode:1,curve:0,softRate:4,hardRate:128,minimum:10,maximum:120,...changes});
const stripPolicy=payload=>{
  assert.equal(payload[6],13,'Fixture projects the frozen V7 prefix from current V13 settings.');
  payload=legacyViewInfo(payload);
  assert.equal(new TextDecoder().decode(payload.subarray(5013,5017)),'THYS');
  const legacy=new Uint8Array(3217+payload.length-5013);legacy.set(payload.subarray(0,3217));legacy[6]=7;
  // A v7 file had one common response/velocity family. Project the explicitly
  // configured Left cluster into that frozen legacy family for this fixture.
  legacy.set(payload.subarray(3508,3517),59);
  legacy.set(payload.subarray(3517,3525),2345);
  for(let pad=0;pad<96;pad++){
    if(!legacy[188+20*pad])legacy.set(legacy.subarray(59,68),189+20*pad);
    if(!legacy[2353+9*pad])legacy.set(legacy.subarray(2345,2353),2354+9*pad);
  }
  legacy.set(payload.subarray(5013),3217);return legacy;
};
async function fresh(){return createCore({bytes});}

test('aftertouch and detector ranges validate independently in JavaScript and C',async()=>{
  const engine=await fresh(),firmware=engine.firmware;
  const independent=response({floor:2000,ceiling:3000});
  assert.equal(validateFirmwareResponse(independent),null);okay(firmware.setResponse(-1,independent));
  assert.equal(firmware.response().floor,2000);assert.equal(firmware.response().attack,1200);
  assert.ok(validateFirmwareResponse(response({release:1200})));
  assert.ok(validateFirmwareResponse(response({floor:3000,ceiling:3000})));
  assert.equal(firmware.setResponse(-1,response({floor:3000,ceiling:3000})).ok,false);
  assert.equal(validateFirmwareReleasePolicy({linked:1,gap:23}),null);
  for(const policy of [{linked:2,gap:23},{linked:1,gap:0},{linked:0,gap:4096},null]) {
    assert.ok(validateFirmwareReleasePolicy(policy));assert.equal(firmware.setReleasePolicy(-1,policy).ok,false);
  }
  assert.deepEqual(engine.drain(),[]);
});

test('basic Sensitivity uses the shared C amount and links only release, preserving precise raw values',async()=>{
  const engine=await fresh(),firmware=engine.firmware;
  const start=firmware.sensitivity();assert.ok(start>0&&start<=100);
  assert.deepEqual(firmware.releasePolicy(),{linked:1,gap:23,overridden:0});
  okay(firmware.setSensitivityAmount(-1,start));assert.equal(firmware.response().attack,1377);
  okay(firmware.setSensitivityAmount(-1,start-1));
  const adjusted=firmware.response().attack;assert.ok(adjusted>1377);assert.equal(firmware.response().release,adjusted-23);
  okay(firmware.setReleasePolicy(-1,{linked:1,gap:80}));assert.equal(firmware.response().release,adjusted-80);
  okay(firmware.setResponse(-1,{...firmware.response(),floor:2000}));assert.equal(firmware.releasePolicy().linked,1);
  okay(firmware.setResponse(-1,{...firmware.response(),release:1200}));assert.equal(firmware.releasePolicy().linked,0);
  assert.equal(firmware.setTrigger(-1,800).ok,false);assert.equal(firmware.response().attack,adjusted);
  okay(firmware.setTrigger(-1,1500));assert.equal(firmware.response().release,1200);
  okay(firmware.setReleasePolicy(-1,{linked:1,gap:31}));assert.equal(firmware.response().release,1469);
  assert.equal(firmware.response().floor,2000);assert.deepEqual(engine.drain(),[]);
});

test('effective C bounds constrain numeric drafts and keep direct setting operations strict',async()=>{
  const engine=await fresh(),state=engine.state,firmware=engine.firmware;
  okay(firmware.setResponse(-1,response({floor:2000,ceiling:3000})));
  assert.deepEqual(state.bounds(row('sensitivity.attack')),{min:1001,max:4095});
  assert.deepEqual(state.bounds(row('sensitivity.release')),{min:0,max:1199});
  assert.deepEqual(state.bounds(row('sensitivity.floor')),{min:0,max:2999});
  assert.deepEqual(state.bounds(row('sensitivity.ceiling')),{min:2001,max:4095});
  assert.equal(state.bounds(row('performance.root')),null);
  assert.equal(state.bounds(-1),null);
  okay(state.begin(row('sensitivity.attack')));okay(state.adjust(-99999));assert.equal(state.info().editValue,1001);
  okay(state.adjust(99999));assert.equal(state.info().editValue,4095);okay(state.cancel());
  assert.equal(state.set(row('sensitivity.attack'),1000).ok,false);
  assert.equal(firmware.response().attack,1200);
  okay(state.set(row('sensitivity.target'),96));
  assert.deepEqual(state.bounds(row('sensitivity.attack')),state.bounds(row('sensitivity.attack'),95));
  assert.deepEqual(firmware.sensitivityBounds(95),state.bounds(row('sensitivity.amount'),95));
  assert.deepEqual(engine.drain(),[]);
});

test('linked scope profiles load all three configuration families atomically and reject malformed data',async()=>{
  const engine=await fresh(),firmware=engine.firmware;
  okay(firmware.layout(Array.from({length:96},()=>({notes:[60,0,0,0,0,0,0],mask:1,color:1}))));
  okay(firmware.setResponse(1,response({curve:1})));okay(firmware.pressPad(1));engine.drain();
  const before=[firmware.response(),firmware.velocity(),firmware.releasePolicy()],owner=engine.owner(1);
  const requested=response({attack:1400,release:1377,floor:2000});
  assert.equal(firmware.setSensitivity(-1,requested,velocity(),{linked:1,gap:23}).ok,false);
  assert.deepEqual([firmware.response(),firmware.velocity(),firmware.releasePolicy()],before);
  assert.deepEqual(engine.owner(1),owner);assert.deepEqual(engine.drain(),[]);
  okay(engine.release(1));engine.drain();
  assert.equal(firmware.setSensitivity(-1,{...requested,release:1376},velocity(),{linked:1,gap:23}).ok,false);
  assert.deepEqual([firmware.response(),firmware.velocity(),firmware.releasePolicy()],before);
  okay(firmware.setSensitivity(-1,requested,velocity(),{linked:1,gap:23}));
  assert.equal(firmware.response().floor,2000);assert.deepEqual(firmware.releasePolicy(),{linked:1,gap:23,overridden:0});
  okay(firmware.setSensitivity(95,response({attack:1500,release:1450}),velocity({curve:2}),{linked:1,gap:50}));
  assert.deepEqual(firmware.releasePolicy(95),{linked:1,gap:50,overridden:1});
  okay(firmware.clearResponse(95));assert.deepEqual(firmware.releasePolicy(95),{linked:1,gap:23,overridden:0});
  assert.equal(firmware.velocity(95).overridden,1);
});

test('v13 preserves linked release and independent aftertouch across cold restoration',async()=>{
  const source=await fresh();
  okay(source.firmware.setSensitivity(-1,response({release:1177,floor:2000}),velocity(),{linked:1,gap:23}));
  okay(source.firmware.setSensitivity(95,response({attack:1500,release:1000}),velocity({mode:0}),{linked:0,gap:31}));
  const payload=source.settings.encode();assert.equal(source.settings.version,13);
  assert.equal(source.settings.payloadVersion(payload),13);assert.equal(payload.length,5633);
  const cold=await fresh();assert.equal(cold.settings.apply(payload),true);
  for(const pad of [-1,95]){
    assert.deepEqual(cold.firmware.response(pad),source.firmware.response(pad));
    assert.deepEqual(cold.firmware.velocity(pad),source.firmware.velocity(pad));
    assert.deepEqual(cold.firmware.releasePolicy(pad),source.firmware.releasePolicy(pad));
  }
  assert.deepEqual(cold.drain(),[]);assert.equal(cold.stats().activeVoices,0);
});

test('historical v7 retains strike velocity, derives its existing release gap and keeps its original validation',async()=>{
  const source=await fresh();okay(source.firmware.setResponse(-1,response()));okay(source.firmware.setVelocity(-1,velocity()));
  okay(source.sequencer.add(2,[{tick:240,duration:480,pitch:67,value:91}]));
  const old=stripPolicy(source.settings.encode());assert.equal(source.settings.payloadVersion(old),7);assert.equal(source.settings.validate(old),true);
  const cold=await fresh();assert.equal(cold.settings.apply(old),true);
  assert.deepEqual(cold.firmware.velocity(),source.firmware.velocity());
  assert.deepEqual(cold.firmware.releasePolicy(),{linked:1,gap:200,overridden:0});
  assert.deepEqual(cold.sequencer.events(2),source.sequencer.events(2));
  okay(source.firmware.setResponse(-1,response({floor:2000,ceiling:3000})));
  assert.equal(source.settings.validate(source.settings.encode()),true);
  assert.equal(source.settings.validate(stripPolicy(source.settings.encode())),false,'A v7 marker cannot reinterpret its historical coupled response contract.');
});
