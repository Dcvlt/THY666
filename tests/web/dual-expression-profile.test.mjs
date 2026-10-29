import {legacyViewInfo} from './legacy-view-fixture.mjs';
import {CURRENT_SETTINGS_VERSION,CURRENT_SETTINGS_PARAMETER_BYTES,legacyParameterPayload} from './current-settings-fixture.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {validateFirmwareExpressionProfile,sampleFirmwareExpressionProfile} from '../../web/firmware.js';
import {createMpeLabModel} from '../../web/mpe-lab.js';

const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));
const okay=result=>assert.equal(result.ok,true,result.status);
const profile=(changes={})=>({enabled:true,split:70,pressureCurve:0,aftertouchCurve:0,...changes});
const fields=value=>Object.fromEntries(Object.entries(value).filter(([key])=>key!=='overridden'));
async function fresh() {
  const engine=await createCore({bytes});
  okay(engine.firmware.setResponse(-1,{attack:1200,release:1000,floor:800,ceiling:3200,curve:0}));
  okay(engine.firmware.setResponse(-2,{attack:1200,release:1000,floor:800,ceiling:3200,curve:0}));
  return engine;
}

test('UI profile validation preserves explicit booleans, supported limits and independent curves',()=>{
  for(const value of [profile(),profile({enabled:false,split:10,pressureCurve:2,aftertouchCurve:1}),profile({split:90})])
    assert.equal(validateFirmwareExpressionProfile(value),null);
  for(const change of [{enabled:1},{enabled:'true'},{split:9},{split:91},{split:70.5},{pressureCurve:3},{aftertouchCurve:-1},{aftertouchCurve:'1'}])
    assert.ok(validateFirmwareExpressionProfile(profile(change)));
  assert.ok(validateFirmwareExpressionProfile(null));
});

test('curve sampling forwards the applied scope and raw values to C without deriving expression axes',()=>{
  const calls=[],firmware={expressionAxes(scope,raw){calls.push([scope,raw]);return{pressure:raw%7,aftertouch:127-raw%11};}};
  const samples=sampleFirmwareExpressionProfile(firmware,-2,5);
  assert.deepEqual(calls,[[-2,0],[-2,1024],[-2,2048],[-2,3071],[-2,4095]]);
  for(const sample of samples)assert.deepEqual(sample,{raw:sample.raw,pressure:sample.raw%7,aftertouch:127-sample.raw%11});
  assert.deepEqual(sampleFirmwareExpressionProfile(firmware,96),[]);
  assert.deepEqual(sampleFirmwareExpressionProfile(firmware,-1,1),[]);
});

test('C profiles default OFF and inherit independently for Left, Right and individual pads',async()=>{
  const engine=await fresh(),f=engine.firmware;
  assert.deepEqual(f.expressionProfile(-1),{enabled:false,split:70,pressureCurve:0,aftertouchCurve:0,overridden:0});
  okay(f.setExpressionProfile(-1,profile()));
  assert.deepEqual(fields(f.expressionProfile(0)),profile());assert.equal(f.expressionProfile(48).enabled,false);
  const right=profile({split:30,pressureCurve:2,aftertouchCurve:1}),local=profile({split:50,pressureCurve:1,aftertouchCurve:2});
  okay(f.setExpressionProfile(-2,right));okay(f.setExpressionProfile(0,local));
  okay(f.setExpressionProfile(-1,profile({split:60})));
  assert.deepEqual(f.expressionProfile(0),{...local,overridden:1});
  assert.equal(f.expressionProfile(1).split,60);assert.deepEqual(fields(f.expressionProfile(48)),right);
  const response=f.response(0),velocity=f.velocity(0);
  okay(f.clearExpressionProfile(0));assert.equal(f.expressionProfile(0).overridden,0);assert.equal(f.expressionProfile(0).split,60);
  assert.deepEqual(f.response(0),response);assert.deepEqual(f.velocity(0),velocity);
});

test('C split endpoints, saturation and primary/aftertouch curve independence use calibrated floor and ceiling',async()=>{
  const engine=await fresh(),f=engine.firmware;okay(f.setExpressionProfile(-1,profile()));
  assert.deepEqual(f.expressionAxes(-1,0),{pressure:0,aftertouch:0});
  assert.deepEqual(f.expressionAxes(-1,800),{pressure:0,aftertouch:0});
  assert.deepEqual(f.expressionAxes(-1,2480),{pressure:127,aftertouch:0});
  assert.deepEqual(f.expressionAxes(-1,3200),{pressure:127,aftertouch:127});
  assert.deepEqual(f.expressionAxes(-1,4095),{pressure:127,aftertouch:127});
  const linear=sampleFirmwareExpressionProfile(f,-1,129);
  okay(f.setExpressionProfile(-1,profile({aftertouchCurve:2})));
  const firmAftertouch=sampleFirmwareExpressionProfile(f,-1,129);
  assert.deepEqual(firmAftertouch.map(v=>v.pressure),linear.map(v=>v.pressure));
  assert.ok(firmAftertouch.some((v,i)=>v.aftertouch<linear[i].aftertouch));
  okay(f.setExpressionProfile(-1,profile({pressureCurve:1,aftertouchCurve:2})));
  const softPressure=sampleFirmwareExpressionProfile(f,-1,129);
  assert.deepEqual(softPressure.map(v=>v.aftertouch),firmAftertouch.map(v=>v.aftertouch));
  assert.ok(softPressure.some((v,i)=>v.pressure>firmAftertouch[i].pressure));
  for(let i=1;i<softPressure.length;i++) {
    assert.ok(softPressure[i].pressure>=softPressure[i-1].pressure);
    assert.ok(softPressure[i].aftertouch>=softPressure[i-1].aftertouch);
  }
});

test('OFF restores the original single-stage curve, and invalid or held-pad edits cannot partially apply',async()=>{
  const engine=await fresh(),f=engine.firmware;
  okay(f.setResponse(0,{attack:1200,release:1000,floor:800,ceiling:3200,curve:2}));
  const before=sampleFirmwareExpressionProfile(f,0,37);assert.ok(before.every(v=>v.pressure===v.aftertouch));
  okay(f.setExpressionProfile(0,profile()));okay(f.setExpressionProfile(0,profile({enabled:false})));
  assert.deepEqual(sampleFirmwareExpressionProfile(f,0,37),before);
  const saved=f.expressionProfile(0);
  for(const changes of [{enabled:1},{split:0},{split:91},{pressureCurve:3}])assert.equal(f.setExpressionProfile(0,profile(changes)).ok,false);
  assert.deepEqual(f.expressionProfile(0),saved);
  okay(engine.press({pad:0,notes:[60],mask:1,channel:0}));engine.drain();
  assert.equal(f.setExpressionProfile(0,profile()).ok,false);assert.deepEqual(f.expressionProfile(0),saved);
  okay(engine.release(0));engine.drain();
  okay(f.setResponse(1,{attack:1200,release:1000,floor:1000,ceiling:1001,curve:0}));
  const narrow=f.expressionProfile(1);assert.equal(f.setExpressionProfile(1,profile()).ok,false);assert.deepEqual(f.expressionProfile(1),narrow);
});

test('current settings restore cluster and individual profiles without held axes; V12, V11 and V10 migrate safely',async()=>{
  const source=await fresh(),f=source.firmware;
  okay(f.setExpressionProfile(-1,profile()));okay(f.setExpressionProfile(-2,profile({split:40,pressureCurve:2})));
  okay(f.setExpressionProfile(95,profile({split:55,aftertouchCurve:1})));
  const payload=source.settings.encode(),cold=await fresh();assert.equal(source.settings.version,CURRENT_SETTINGS_VERSION);
  assert.equal(cold.settings.apply(payload),true);
  for(const scope of [-2,-1,0,48,95])assert.deepEqual(cold.firmware.expressionProfile(scope),f.expressionProfile(scope));
  assert.deepEqual(cold.settings.encode(),payload);assert.deepEqual(cold.drain(),[]);assert.equal(cold.expression.stats().active,0);
  const song=source.sequencer.exportBytes();
  assert.deepEqual(payload.subarray(CURRENT_SETTINGS_PARAMETER_BYTES),song,'Current settings place the song after library and Harmony/system preferences.');
  const legacyPayload=legacyViewInfo(payload);
  const v12=legacyParameterPayload(legacyPayload,12);
  assert.equal(source.settings.payloadVersion(v12),12);assert.equal(source.settings.validate(v12),true);
  assert.equal(cold.settings.apply(v12),true);assert.deepEqual(cold.settings.encode(),legacyPayload);
  const v11=new Uint8Array(4263+song.length);
  v11.set(legacyPayload.subarray(0,4263));v11[6]=11;v11.set(song,4263);
  assert.equal(source.settings.payloadVersion(v11),11);assert.equal(source.settings.validate(v11),true);
  assert.equal(cold.settings.apply(v11),true);
  for(const scope of [-2,-1,0,48,95])assert.deepEqual(cold.firmware.expressionProfile(scope),f.expressionProfile(scope));
  for(const kind of [0,1,2])assert.ok(cold.firmware.library.records(kind).every(record=>!record.used));
  assert.deepEqual(cold.settings.encode(),legacyPayload);assert.deepEqual(cold.drain(),[]);
  const legacy=new Uint8Array(3748+song.length);
  legacy.set(legacyPayload.subarray(0,3748));legacy[6]=10;legacy.set(song,3748);
  assert.equal(source.settings.validate(legacy),true);
  assert.equal(cold.settings.apply(legacy),true);
  for(const scope of [-2,-1,0,48,95])assert.deepEqual(cold.firmware.expressionProfile(scope),{enabled:false,split:70,pressureCurve:0,aftertouchCurve:0,overridden:0});
  assert.equal(cold.firmware.response(-1).floor,800);assert.equal(cold.firmware.response(-1).ceiling,3200);
});

test('MPE dual-stage model preserves the other axis, uses C ownership and refuses modifier aftertouch',()=>{
  const calls=[],voice={pad:7,state:1,pressure:33,timbre:99,modifier:10};
  const expression={voices:()=>[voice],config:()=>({}),stats:()=>({}),pressure:(...args)=>{calls.push(['pressure',...args]);return{ok:true};},
    axes:(...args)=>{calls.push(['axes',...args]);return{ok:true};}};
  const rhythm={advance:time=>{calls.push(['advance',time]);return{ok:true};}};
  const model=createMpeLabModel({expression,rhythm,getProfile:()=>({enabled:true}),now:()=>16});model.selectPad(7);
  assert.equal(model.pressure(80).ok,true);assert.equal(model.aftertouch(22).ok,true);
  assert.deepEqual(calls,[['advance',16],['axes',7,80,99],['advance',16],['axes',7,33,22]]);
  const count=calls.length;assert.equal(model.aftertouch(60,9).ok,false);assert.equal(calls.length,count);
  assert.equal(model.pressure(64,9).ok,true);assert.deepEqual(calls.slice(-2),[['advance',16],['pressure',9,64]]);
});

test('MPE audition changes source pressure and CC74 independently through the actual C engine',async()=>{
  const engine=await fresh(),f=engine.firmware;
  okay(f.setExpressionProfile(0,profile()));okay(engine.expression.configure({enabled:true}));
  okay(engine.press({pad:0,notes:[60,64,67],mask:5,channel:0}));
  const initial=engine.drain();assert.equal(initial.filter(v=>(v.status&0xf0)===0xb0&&v.data1===74&&v.data2===0).length,6);
  let now=0;const model=createMpeLabModel({expression:engine.expression,rhythm:engine.rhythm,getProfile:pad=>f.expressionProfile(pad),now:()=>now});model.selectPad(0);
  okay(model.pressure(45));now=8;okay(engine.advance(now));now=16;okay(engine.advance(now));engine.drain();
  assert.ok(engine.expression.voices().every(v=>v.pressure===45&&v.timbre===0));
  okay(model.aftertouch(110));now=24;okay(engine.advance(now));now=32;okay(engine.advance(now));
  const messages=engine.drain();assert.ok(messages.some(v=>(v.status&0xf0)===0xb0&&v.data1===74&&v.data2===110));
  assert.ok(engine.expression.voices().every(v=>v.pressure===45&&v.timbre===110));
  okay(engine.release(0));const released=engine.drain();assert.equal(released.filter(v=>(v.status&0xf0)===0xb0&&v.data1===74&&v.data2===64).length,6);
});
