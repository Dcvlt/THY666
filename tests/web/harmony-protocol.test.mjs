import test from 'node:test';
import assert from 'node:assert/strict';
import {decodeHarmonySnapshot,decodeCompanionSnapshot} from '../../web/companion-protocol.js';
const identity={build:'01'.repeat(8),uid:'02'.repeat(12),schema:'03'.repeat(32)};
const ascii=(b,o,s)=>b.set(new TextEncoder().encode(s),o);
export function harmonyPacket(){
  const b=new Uint8Array(800),d=new DataView(b.buffer);
  ascii(b,0,'THYHRM1');d.setUint16(8,4,true);d.setUint16(10,800,true);d.setUint32(12,19,true);d.setUint32(136,42,true);
  ascii(b,32,identity.build);ascii(b,48,identity.uid);ascii(b,72,identity.schema);
  for(const [at,v] of [[140,0xab5],[142,0x91],[144,0xad5],[146,0xa95],[148,64],[150,32],[152,0x91],[154,35]])d.setUint16(at,v,true);
  b.set([1,0,0,0,7,0,0,1,3,0,1,1,47,0,0,0],156);b[172]=1;b[177]=128;b[178]=2;b[189]=128;
  b[190]=2;b[191]=3;ascii(b,192,'C MAJ');ascii(b,224,'C Major');ascii(b,256,'G Major');ascii(b,288,'G MAJ');
  d.setUint16(512,0x91,true);d.setUint16(514,4,true);b.set([3,1,1,1,0,1,36,100,1,42,32],516);
  b[534]=32;b[546]=64;b[563]=128;b.set([0,1,2,3,4,5,6],564);b.set([4,2,1,255,2],571);
  ascii(b,576,'C MAJ');ascii(b,608,'D');
  d.setUint16(320,0x8a4,true);b.set([7,1,5,0,1,1],322);return b;
}
test('Harmony extension decodes exact identity, labels, cached rows and logical pad bits',()=>{
  const b=harmonyPacket(),padded=new Uint8Array(809);padded.set(b,4);
  const s=decodeHarmonySnapshot(padded.subarray(4,804),identity);
  assert.equal(s.revision,42);assert.equal(s.keyMask,0xab5);assert.equal(s.inputLabel,'C MAJ');assert.equal(s.selectedLabel,'G MAJ');
  assert.equal(s.canApply,true);assert.equal(s.pinned,true);assert.equal(s.cacheReady,true);
  assert.deepEqual(s.currentPads.flatMap((on,n)=>on?[n]:[]),[0,47]);
  assert.deepEqual(s.selectedPads.flatMap((on,n)=>on?[n]:[]),[1]);
  assert.deepEqual(s.suggestedPads.flatMap((on,n)=>on?[n]:[]),[47]);
  assert.deepEqual(s.rows,[{mask:0x8a4,root:7,shared:1,degree:5,reason:0,pad:1}]);
  assert.equal(s.baseMask,0x91);assert.equal(s.rightMask,4);assert.equal(s.baseLabel,'C MAJ');assert.equal(s.melodyLabel,'D');
  assert.equal(s.view,3);assert.equal(s.controlBank,1);assert.equal(s.octaveTarget,1);assert.equal(s.rightMode,1);assert.equal(s.analysis,0);
  assert.equal(s.lowNote,36);assert.equal(s.highNote,100);assert.equal(s.clusterSwapped,true);assert.equal(s.chordLayers,42);assert.equal(s.noteLayers,32);
  assert.deepEqual(s.physicalCurrentPads.flatMap((on,n)=>on?[n]:[]),[53]);
  assert.deepEqual(s.physicalSelectedPads.flatMap((on,n)=>on?[n]:[]),[54]);
  assert.deepEqual(s.physicalSuggestedPads.flatMap((on,n)=>on?[n]:[]),[95]);
  assert.deepEqual(s.scaleButtons,[0,1,2,3,4,5,6]);assert.equal(s.previewScale,4);assert.equal(s.voicing,2);assert.equal(s.chordOctave,-1);assert.equal(s.noteOctave,2);
  b[320]=0;assert.equal(s.rows[0].mask,0x8a4);
});
test('Harmony validates framing, identity, pages, pitch masks and bounded recommendations',()=>{
  assert.throws(()=>decodeHarmonySnapshot(new Uint8Array(639)),/size/);
  for(const mutate of [b=>b[0]=0,b=>b[8]=1,b=>b[157]=3,b=>b[163]=10,b=>b[162]=1,b=>b[141]=255,
    b=>b[168]=48,b=>b[170]=70,b=>b[164]=4,b=>b[327]=0,b=>b[326]=48,b=>b[325]=8,
    b=>b[27]=4,b=>b[28]=2,b=>b[29]=101,b=>b[30]=5,b=>b[516]=4,b=>b[520]=3,b=>b[524]=2,b=>b[522]=101,b=>b[525]=128,b=>b[571]=7,b=>b[574]=4,b=>b[748]=5,b=>b[749]=101,b=>b[750]=2,b=>b[751]=2,b=>b[752]=5,b=>b[753]=2]){
    const b=harmonyPacket();mutate(b);assert.throws(()=>decodeHarmonySnapshot(b,identity));
  }
  assert.throws(()=>decodeHarmonySnapshot(harmonyPacket(),{...identity,uid:'ff'.repeat(12)}),/identity/);
  const b=harmonyPacket();b[157]=1;b.set([4,2,255,255,1],323);
  const s=decodeHarmonySnapshot(b);assert.deepEqual(s.rows[0],{mask:0x8a4,root:7,family:4,extra:2,reason:255,pad:255});
});
test('base snapshot remains1680 bytes and exposes only the optional Harmony revision',()=>{
  const b=new Uint8Array(1680),d=new DataView(b.buffer);ascii(b,0,'THYCMP1');d.setUint16(8,1,true);d.setUint16(10,1680,true);
  ascii(b,32,identity.build);ascii(b,48,identity.uid);ascii(b,72,identity.schema);d.setUint32(1668,0xfedcba98,true);
  b.set([8,0,0],1160);b.set([0,0,8],1160+48*3);b[1467]=1;b[1632]=0;b[1633]=17;
  assert.equal(decodeCompanionSnapshot(b,identity).harmonyRevision,0xfedcba98);
  const before=decodeCompanionSnapshot(b,identity);b[1663]=1;d.setInt32(1664,-37,true);
  const swapped=decodeCompanionSnapshot(b,identity);assert.equal(swapped.clusterSwapped,true);assert.equal(swapped.currentValue,-37);
  assert.deepEqual(swapped.rgb,before.rgb);assert.deepEqual(swapped.held,before.held);assert.deepEqual(swapped.inspect,before.inspect);
  b.set([1,1,3,20],1672);d.setUint32(1676,0xfefdfcfb,true);
  const pending=decodeCompanionSnapshot(b,identity);
  assert.equal(pending.resetOpen,true);assert.equal(pending.resetChoice,1);assert.equal(pending.resetGroup,3);assert.equal(pending.resetTrack,20);assert.equal(pending.resetEpoch,0xfefdfcfb);
  for(const mutate of [bytes=>bytes[1672]=2,bytes=>bytes[1673]=2,bytes=>bytes[1674]=32,bytes=>bytes[1675]=21,bytes=>bytes.fill(0,1676,1680)]){
    const invalid=b.slice();mutate(invalid);assert.throws(()=>decodeCompanionSnapshot(invalid,identity));
  }
});
