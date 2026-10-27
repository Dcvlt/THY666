import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {MENU_ROWS} from '../../web/studio-menu.js';

const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));
const okay=value=>assert.equal(value.ok,true,value.status||value.message);
test('velocity summary includes inherited left/right cluster settings',async()=>{
  const engine=await createCore({bytes}),f=engine.firmware;
  const left=f.velocity(-1),right=f.velocity(-2);
  assert.equal(left.overridden,0);assert.equal(right.overridden,0);
  okay(f.setVelocity(-1,{...left,mode:1}));
  assert.equal(f.velocity(0).overridden,0);assert.equal(f.velocity(48).overridden,0);
  assert.equal(f.velocitySummary(),'mixed','Different inherited cluster modes are visible without per-pad overrides.');
  okay(f.setVelocity(-2,{...right,mode:1}));assert.equal(f.velocitySummary(),'strike');
});
async function fixture(){
  const engine=await createCore({bytes});let now=0;
  okay(engine.firmware.layout(Array.from({length:96},()=>({notes:[60,0,0,0,0,0,0],mask:1,color:1}))));
  const h={engine,sample(pad,raw){now+=10;okay(engine.firmware.sample(pad,raw,now));okay(engine.advance(now));},
    strikes(pad,scale=1){for(const values of [[80,120,160],[320,600,800],[1000,1600,1800]])for(let tap=0;tap<3;tap++){
      for(const raw of [...values.map(raw=>Math.round(20+(raw-20)*scale)),20,20])this.sample(pad,raw);
    }},
    pressure(pad,scale=1,leak=-1){
      const value=raw=>Math.round(20+(raw-20)*scale);
      const step=raw=>{if(leak>=0)this.sample(leak,50);this.sample(pad,value(raw));};
      for(let n=0;n<105;n++)step(160);
      for(const raw of [240,400,600,800,1000,1300,1600,1800])for(let n=0;n<3;n++)step(raw);
      if(leak>=0)this.sample(leak,20);
      this.sample(pad,20);this.sample(pad,20);
    },
    pad(pad,scale=1,leak=-1){this.strikes(pad,scale);this.pressure(pad,scale,leak);},
    get now(){return now;}};
  for(let n=0;n<8;n++)for(let pad=0;pad<96;pad++)h.sample(pad,20);
  return h;
}

test('wizard separates strikes and pressure, defers review until all pads and merges Learn with restart',async()=>{
  const h=await fixture(),{engine}=h,calibration=engine.calibration;
  const original=engine.settings.encode();assert.equal(calibration.snapshot().state,0);
  okay(calibration.open());assert.equal(calibration.snapshot().open,true);assert.equal(engine.settings.canApply(),false);
  h.strikes(0);assert.equal(calibration.snapshot().stage,3,'The fourth phase measures continuous pressure.');
  assert.equal(calibration.snapshot().completed,0,'Nine strike taps alone cannot complete pressure calibration.');
  h.pressure(0);assert.equal(calibration.snapshot().completed,1);assert.equal(calibration.completed(0),true);
  assert.equal(calibration.snapshot().target,4,'The next target is top-row column2, not column-major row2.');
  assert.equal(calibration.completed(1),false);assert.equal(calibration.candidate(1),null);
  const candidate=calibration.candidate(0);assert.equal(candidate.anchors.length,3);
  assert.ok(candidate.trigger<1377);assert.ok(candidate.release<candidate.trigger);
  assert.ok(candidate.floor<candidate.ceiling);assert.ok(candidate.softRate<candidate.hardRate);
  assert.deepEqual(engine.drain(),[]);assert.equal(engine.stats().activeVoices,0);
  assert.deepEqual(engine.settings.encode(),original,'Learning does not mutate applied settings.');
  assert.equal(calibration.action('before').ok,false);
  assert.equal(calibration.action('after').ok,false,'Playable review is unavailable for an incomplete board.');
  assert.equal(calibration.action('save').ok,false,'A partial profile cannot be saved.');
  assert.deepEqual(engine.settings.encode(),original);assert.deepEqual(engine.drain(),[]);
  okay(calibration.close());assert.equal(calibration.snapshot().open,false);assert.equal(calibration.completed(0),true);
  okay(calibration.open());assert.equal(calibration.completed(0),true);okay(calibration.action('learn'));
  h.sample(0,1800);h.sample(0,2200);h.sample(0,20);h.sample(0,20);
  assert.equal(calibration.snapshot().state,1);assert.equal(calibration.snapshot().target,0);
  assert.equal(calibration.completed(0),false,'Learn explicitly restarts the selected completed pad.');
  assert.equal(calibration.snapshot().hits,0,'Picking a pad is not the first calibration strike.');
  okay(calibration.action('cancel'));assert.equal(calibration.snapshot().open,false);assert.equal(calibration.snapshot().completed,0);
  assert.equal(calibration.candidate(0),null);assert.deepEqual(engine.settings.encode(),original);
});

test('the left-cluster raw wizard provides temporary playable review, tuning and saved per-pad fits',async()=>{
  const h=await fixture(),{engine}=h,calibration=engine.calibration;
  okay(calibration.open());
  const expected=[];
  for(let row=0;row<4;row++)for(let column=0;column<12;column++)expected.push(column*4+row);
  const original=engine.settings.encode();
  const rightBefore=[48,95].map(pad=>({response:engine.firmware.response(pad),velocity:engine.firmware.velocity(pad)}));
  for(const pad of expected){assert.equal(calibration.snapshot().target,pad);h.pad(pad,pad===47?.35:1+(pad%5)/10,pad===43?46:-1);}
  assert.equal(calibration.snapshot().completed,48);assert.equal(calibration.snapshot().scopeCount,48);
  assert.equal(calibration.snapshot().scopeFirst,0);assert.equal(calibration.snapshot().reviewReady,1);
  assert.equal(calibration.snapshot().state,4,'The finished cluster automatically opens calibrated audition.');
  assert.deepEqual(engine.drain(),[]);assert.deepEqual(engine.settings.encode(),original);
  assert.equal(calibration.strength(47).flagged,true);assert.ok(calibration.strength(47).percent<60);
  assert.ok(calibration.snapshot().outlierCount>=1);
  okay(calibration.action('before'));
  h.sample(0,600);h.sample(0,600);h.sample(0,20);h.sample(0,20);
  assert.deepEqual(engine.drain(),[],'Saved detection remains below its unchanged trigger in this audition.');
  okay(calibration.action('after'));
  h.sample(0,600);h.sample(0,600);h.sample(0,20);h.sample(0,20);
  const audition=engine.drain();
  assert.ok(audition.some(event=>(event.status&0xf0)===0x90&&event.data2>0),'Calibrated audition plays without applying its profile.');
  assert.ok(audition.some(event=>(event.status&0xf0)===0x80));
  assert.equal(engine.stats().activeVoices,0);assert.deepEqual(engine.settings.encode(),original);
  const initialPercent=calibration.snapshot().previewPercent;
  const tunedPercent=initialPercent<100?initialPercent+1:initialPercent-1;
  okay(calibration.setReviewSensitivity(tunedPercent));assert.equal(calibration.snapshot().previewPercent,tunedPercent);
  assert.deepEqual(engine.settings.encode(),original,'Review sensitivity remains a temporary audition value.');
  okay(calibration.action('apply'));
  for(const pad of [0,47]){
    const candidate=calibration.candidate(pad),response=engine.firmware.response(pad),velocity=engine.firmware.velocity(pad);
    assert.ok(response.attack<1377);assert.ok(response.release<response.attack);
    assert.equal(response.floor,candidate.floor);assert.equal(response.ceiling,candidate.ceiling);
    assert.equal(response.overridden,1);assert.equal(velocity.mode,1);assert.equal(velocity.overridden,1);
    assert.equal(engine.firmware.releasePolicy(pad).linked,1);
  }
  for(const [index,pad] of [48,95].entries()){
    assert.deepEqual(engine.firmware.response(pad),rightBefore[index].response);
    assert.deepEqual(engine.firmware.velocity(pad),rightBefore[index].velocity);
  }
  const guard=engine.firmware.sensor(46).guard;
  assert.ok(guard>50,'A tolerable neighbor signal creates a persistent release floor.');
  assert.ok(engine.firmware.response(46).release>=guard);
  if(calibration.snapshot().open)okay(calibration.close());
  const pads=[0,47],before=pads.map(pad=>({response:engine.firmware.response(pad),
    velocity:engine.firmware.velocity(pad),policy:engine.firmware.releasePolicy(pad)}));
  assert.ok(new Set(before.map(value=>value.response.attack)).size>1,'The raw gestures produced different per-pad fits.');
  const originalGlobal=engine.firmware.response().attack,amount=engine.firmware.sensitivity(),bounds=engine.firmware.sensitivityBounds();
  assert.ok(amount+1<=bounds.max,'One easier global Sensitivity step is within the learned quiet limits.');
  okay(engine.firmware.setSensitivityAmount(-1,amount+1));
  const newGlobal=engine.firmware.response().attack;assert.ok(newGlobal<originalGlobal);
  for(const [index,pad] of pads.entries()){
    const response=engine.firmware.response(pad),previous=before[index];
    assert.ok(response.attack<=previous.response.attack,'The cluster adjustment reaches its calibrated pad overrides.');
    assert.equal(response.release,Math.max(0,response.attack-previous.policy.gap));
    assert.deepEqual([response.floor,response.ceiling,response.curve,response.overridden],
      [previous.response.floor,previous.response.ceiling,previous.response.curve,1]);
    assert.deepEqual(engine.firmware.velocity(pad),previous.velocity);
    assert.deepEqual(engine.firmware.releasePolicy(pad),previous.policy);
  }
  assert.ok(pads.some((pad,index)=>engine.firmware.response(pad).attack<before[index].response.attack));
  assert.ok(new Set(pads.map(pad=>engine.firmware.response(pad).attack)).size>1,'Cluster feel retains individual pad differences.');
  for(const [index,pad] of [48,95].entries())assert.deepEqual(engine.firmware.response(pad),rightBefore[index].response);
  assert.deepEqual(engine.drain(),[]);
  const saved=engine.settings.encode(),cold=await createCore({bytes});
  assert.equal(engine.settings.version,15);assert.equal(saved.length,5633);
  assert.equal(cold.settings.apply(saved),true);
  assert.deepEqual(cold.firmware.response(47),engine.firmware.response(47));
  assert.deepEqual(cold.firmware.velocity(0),engine.firmware.velocity(0));
  assert.deepEqual(cold.firmware.releasePolicy(48),engine.firmware.releasePolicy(48));
  assert.equal(cold.calibration.snapshot().open,false);assert.equal(cold.calibration.snapshot().completed,0);
  assert.equal(cold.stats().activeVoices,0);assert.deepEqual(cold.drain(),[]);
  assert.equal(cold.firmware.sensor(46).guard,guard,'V9 settings retain the learned safety floor after restart.');
  okay(cold.firmware.setSensitivityAmount(-1,100));
  assert.ok(cold.firmware.response(46).release>=guard,'Maximum sensitivity still respects the persisted neighbor floor.');
  assert.equal(cold.firmware.sensor(46).guard,guard);
  assert.deepEqual(cold.drain(),[]);
});

test('right-cluster review keeps left unchanged and holds notes safely across blocked review changes',async()=>{
  const h=await fixture(),{engine}=h,calibration=engine.calibration;
  okay(engine.state.set(MENU_ROWS.findIndex(row=>row.id==='sensitivity.target'),97));
  const original=engine.settings.encode(),left=engine.firmware.response(0);
  okay(calibration.open());assert.equal(calibration.snapshot().scopeFirst,48);assert.equal(calibration.snapshot().scopeCount,48);
  for(let row=0;row<4;row++)for(let column=0;column<12;column++){
    const pad=48+column*4+row;assert.equal(calibration.snapshot().target,pad);h.pad(pad,1+(pad%3)/10);
  }
  assert.equal(calibration.snapshot().completed,48);assert.equal(calibration.snapshot().state,4);
  h.sample(0,600);h.sample(0,600);h.sample(0,20);h.sample(0,20);
  assert.deepEqual(engine.drain(),[],'The non-calibrated half still uses its saved detector.');
  h.sample(48,600);h.sample(48,600);
  assert.ok(engine.drain().some(event=>(event.status&0xf0)===0x90&&event.data2>0));
  const percent=calibration.snapshot().previewPercent;
  assert.equal(calibration.setReviewSensitivity(percent<100?percent+1:percent-1).ok,false);
  for(const action of ['before','apply','discard'])assert.equal(calibration.action(action).ok,false,`${action} waits for release.`);
  assert.equal(calibration.close().ok,false);assert.equal(calibration.snapshot().open,true);
  h.sample(48,20);h.sample(48,20);
  assert.ok(engine.drain().some(event=>(event.status&0xf0)===0x80));
  assert.equal(engine.stats().activeVoices,0);
  okay(calibration.action('discard'));assert.equal(calibration.snapshot().open,false);
  assert.deepEqual(engine.settings.encode(),original,'Discard preserves every saved setting.');
  assert.deepEqual(engine.firmware.response(0),left);
});

test('unrelated pads cannot move the lit target or supply its strike and pressure samples',async()=>{
  const h=await fixture(),{engine}=h,calibration=engine.calibration;
  okay(calibration.open());
  for(const wrongPad of [34,19,48]){
    h.strikes(wrongPad);h.pressure(wrongPad);
    assert.equal(calibration.snapshot().target,0,'Background/unrelated contacts cannot choose another target.');
    assert.equal(calibration.snapshot().hits,0);
    assert.equal(calibration.snapshot().stage,0);
    assert.equal(calibration.snapshot().completed,0);
  }
  h.sample(0,80);h.sample(0,120);h.sample(0,160);
  const targetPeak=calibration.snapshot().peak,targetSamples=calibration.snapshot().samples;
  h.sample(34,2800);h.sample(34,3600);h.sample(34,20);
  assert.equal(calibration.snapshot().target,0);
  assert.equal(calibration.snapshot().peak,targetPeak);
  assert.equal(calibration.snapshot().samples,targetSamples);
  h.sample(0,20);h.sample(0,20);
  assert.equal(calibration.snapshot().hits,1,'Only releasing the real target finishes its measured strike.');
  assert.equal(calibration.completed(34),false);
  assert.deepEqual(engine.drain(),[]);assert.equal(engine.stats().activeVoices,0);
  okay(calibration.close());
});
