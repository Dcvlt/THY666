import test from 'node:test';
import assert from 'node:assert/strict';
import {decodePerformanceSnapshot} from '../../web/companion-protocol.js';
import {performancePacket,performanceIdentity as identity} from './performance-fixture.mjs';

test('performance telemetry preserves uint64 clock, all member slots and all exact track positions',()=>{
  const bytes=performancePacket({voices:15,tracks:21,position:(1n<<63n)+17n});
  const value=decodePerformanceSnapshot(bytes,identity);
  assert.equal(value.position,(1n<<63n)+17n);assert.equal(value.ppqn,960);assert.equal(value.tempo,120);
  assert.equal(value.voices.length,15);assert.equal(value.voices[14].channel,15);assert.equal(value.voices[14].pressure,112);
  assert.equal(value.voices[0].pendingAttack,true);assert.equal(value.voices[0].soundingDestinations,0);
  assert.equal(value.voices[3].sustained,true);assert.equal(value.voices[3].state,2);
  assert.equal(value.tracks.length,21);assert.equal(value.tracks[20].playhead,7200);assert.equal(value.tracks[20].loopStart,4800);
  assert.equal(value.tracks[1].muted,true);assert.equal(value.tracks[2].oneShot,true);
  bytes.fill(0);assert.equal(value.voices[14].pressure,112,'Decoded scalar records are detached from mutable transport memory.');
});

test('performance decoder supports upper-zone mapping and sliced buffers',()=>{
  const b=performancePacket({voices:15,tracks:1,upperZone:true}),padded=new Uint8Array(1040);padded.set(b,9);
  const result=decodePerformanceSnapshot(padded.subarray(9,1033),identity);
  assert.equal(result.managerChannel,15);assert.equal(result.voices[0].channel,14);assert.equal(result.voices[14].channel,0);
});

test('performance decoder rejects malformed identities, voice routes/states and track coordinates',()=>{
  assert.throws(()=>decodePerformanceSnapshot(new Uint8Array(1023)),/size/);
  for(const [index,mutate] of [
    b=>b[0]=0,b=>b[8]=2,b=>b[11]=0,b=>b[32]=48,b=>b[72]=99,b=>b[28]=0,b=>b[30]=97,b=>b[31]=15,
    b=>b[152]|=32,b=>b[153]=3,b=>b[154]=2,b=>b[155]=4,b=>b[156]=6,b=>b[157]=21,b=>b[158]=21,b=>b[159]=0,
    b=>b[160]=3,b=>b[161]=0,b=>b[163]=96,b=>b[165]=0,b=>b[166]=2,b=>b[167]|=16,b=>b[168]=0,b=>b[169]=3,b=>b[170]=128,
    b=>new DataView(b.buffer).setUint16(172,16384,true),b=>b[179]=96,
    b=>b[540]|=64,b=>b[541]=3,b=>b[542]=3,b=>b[543]=6,
    b=>new DataView(b.buffer).setUint32(528,3840,true),b=>new DataView(b.buffer).setUint32(532,0,true),
  ].entries()){
    const b=performancePacket({voices:2,tracks:1});mutate(b);assert.throws(()=>decodePerformanceSnapshot(b,identity),undefined,`mutation ${index}`);
  }
  const free=performancePacket();free[170]=1;assert.throws(()=>decodePerformanceSnapshot(free,identity),/free/);
  const absent=performancePacket();absent[540]=2;assert.throws(()=>decodePerformanceSnapshot(absent,identity),/absent/);
  const member=performancePacket({voices:2});member[159]=1;assert.throws(()=>decodePerformanceSnapshot(member,identity),/member/);
  assert.throws(()=>decodePerformanceSnapshot(performancePacket(),{...identity,uid:'f'.repeat(24)}),/identity/);
});
