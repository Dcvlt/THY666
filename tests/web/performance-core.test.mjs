import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {decodePerformanceSnapshot} from '../../web/companion-protocol.js';

const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));
const okay=value=>assert.equal(value.ok,true,value.status||value.message);
async function fresh(){
  const engine=await createCore({bytes});
  okay(engine.firmware.layout(Array.from({length:96},(_,pad)=>({
    notes:pad<48?[60,64,67,0,0,0,0]:[72,0,0,0,0,0,0],mask:pad<48?7:1,color:1,
  }))));
  okay(engine.advance(0));return engine;
}

test('actual C performance packet decodes queued MPE members without changing ownership, settings or MIDI',async()=>{
  const engine=await fresh();
  // Performance telemetry has no dependency on an OLED render/provider.
  assert.equal(engine.state.performanceSnapshot(0)?.length,1024);
  for(const value of [-1,2**32,1.5,NaN,null,'1'])assert.equal(engine.state.performanceSnapshot(value),null);
  okay(engine.expression.configure({enabled:true}));
  okay(engine.firmware.pressPad(0));engine.drain();
  okay(engine.expression.pressure(0,93));okay(engine.expression.bend(0,10000));
  for(let now=1;now<=3;now++){okay(engine.advance(now));engine.drain();}
  const voices=engine.expression.voices(),owners=engine.owner(0),stats=engine.stats(),settings=engine.settings.encode(),state=engine.state.info();
  const packet=engine.state.performanceSnapshot(10),telemetry=decodePerformanceSnapshot(packet),members=telemetry.voices.filter(voice=>voice.state);
  assert.equal(telemetry.mpe,true);assert.equal(telemetry.ppqn,960);assert.equal(telemetry.schema,engine.state.schemaHash());
  assert.deepEqual(members.map(voice=>voice.note).sort((a,b)=>a-b),[60,64,67]);
  for(const member of members){
    const source=voices.find(voice=>voice.channel===member.channel);
    assert.ok(source);assert.equal(member.pressure,source.sentPressure);assert.equal(member.upper,source.sentTimbre);assert.equal(member.bend,source.sentBend);
    assert.equal(member.soundingDestinations,member.destinations);assert.equal(member.pendingAttack,false);
  }
  assert.deepEqual(engine.expression.voices(),voices);assert.deepEqual(engine.owner(0),owners);assert.deepEqual(engine.stats(),stats);
  assert.deepEqual(engine.settings.encode(),settings);assert.deepEqual(engine.state.info(),state);assert.deepEqual(engine.drain(),[]);
  const frozen=packet.slice();okay(engine.release(0));engine.drain();okay(engine.advance(20));engine.drain();
  engine.state.performanceSnapshot(21);assert.deepEqual(packet,frozen,'Returned packet does not alias reusable WASM staging.');
});

test('actual C performance packet preserves each track loop and sampled transport position',async()=>{
  const engine=await fresh();okay(engine.rhythm.configure({quantization:'immediate'}));
  for(const [track,start,end] of [[0,240,1440],[20,480,2400]]){
    okay(engine.sequencer.add(track,[{tick:end-120,duration:120,pitch:60+track,value:90}]));
    okay(engine.sequencer.loop(track,start,end));
  }
  okay(engine.rhythm.start());okay(engine.advance(125));engine.drain();
  const transport=engine.rhythm.snapshot(),sequencer=engine.sequencer.snapshot(),stats=engine.stats();
  const telemetry=decodePerformanceSnapshot(engine.state.performanceSnapshot(130));
  assert.equal(telemetry.position,BigInt(transport.songTick));assert.equal(telemetry.transport,1);
  for(const index of [0,20]){
    const track=telemetry.tracks[index],actual=sequencer.tracks[index];
    assert.equal(track.created,true);assert.equal(track.loopStart,actual.loopStart);assert.equal(track.loopLength,actual.loopEnd-actual.loopStart);
    assert.equal(track.playhead,actual.playheadTick);assert.equal(track.generation,actual.generation);assert.equal(track.gridTicks,actual.gridTicks);
    assert.equal(track.enabled,true);assert.equal(track.running,true);assert.ok(track.playhead>track.loopStart);
  }
  assert.equal(telemetry.tracks[1].created,false);
  assert.deepEqual(engine.rhythm.snapshot(),transport);assert.deepEqual(engine.stats(),stats);assert.deepEqual(engine.drain(),[]);
  const laterCapture=decodePerformanceSnapshot(engine.state.performanceSnapshot(9000));
  assert.equal(laterCapture.position,telemetry.position,'A capture timestamp never advances the musical clock.');
  assert.deepEqual(laterCapture.tracks,telemetry.tracks);
  okay(engine.rhythm.stop());engine.drain();
  const stopped=decodePerformanceSnapshot(engine.state.performanceSnapshot(9001));assert.equal(stopped.transport,0);
  for(const index of [0,20])assert.equal(stopped.tracks[index].playhead,stopped.tracks[index].loopStart);
});

test('actual C external-clock playheads retain their sampled phase on pause and clock loss',async()=>{
  for(const ending of ['pause','clock-lost']){
    const engine=await fresh();okay(engine.rhythm.configure({source:'usb',quantization:'immediate'}));
    okay(engine.sequencer.add(0,[{tick:3000,duration:120,pitch:60,value:90}]));
    okay(engine.sequencer.playAll());
    okay(engine.rhythm.midi(0xf8,0,0,0));okay(engine.rhythm.midi(0xfa,0,0,0));
    for(let pulse=1;pulse<=24;pulse++){okay(engine.rhythm.midi(0xf8,0,0,pulse*20));engine.drain();}
    const playing=decodePerformanceSnapshot(engine.state.performanceSnapshot(480));
    assert.equal(playing.tracks[0].running,true);assert.ok(playing.tracks[0].playhead>0);
    if(ending==='pause')okay(engine.rhythm.midi(0xfc,0,0,480));
    else{okay(engine.advance(1100));engine.drain();}
    const stopped=decodePerformanceSnapshot(engine.state.performanceSnapshot(1101));
    assert.equal(stopped.tracks[0].playhead,playing.tracks[0].playhead);
    assert.equal(stopped.tracks[0].running,true,'A retained track lease is interpreted with global transport/clock state.');
    if(ending==='pause')assert.equal(stopped.transport,2);
    else{assert.equal(stopped.suspended,true);assert.equal(stopped.clockReady,false);assert.equal(stopped.clockStatus,3);}
  }
});
