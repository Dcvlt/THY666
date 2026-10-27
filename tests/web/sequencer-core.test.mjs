import {legacyViewInfo} from './legacy-view-fixture.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { crc32 } from 'node:zlib';
import { createCore } from '../../web/core.js';
import { createSequencerLabModel } from '../../web/sequencer-lab.js';
import { PreviewSettings, PREVIEW_SETTINGS_KEY } from '../../web/preview-settings.js';
import { parseMidiFile, encodeMidiFile, midiToSongBytes, sequencerToMidi } from '../../web/sequencer-midi.js';

const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));
const okay=result=>{assert.equal(result.ok,true,result.status||result.message);return result;};
const wire=events=>events.map(({destination,status,data1,data2})=>[destination,status,data1,data2]);
const layout=()=>Array.from({length:96},(_,pad)=>({notes:pad<48?[60,64,67,0,0,0,0]:[62+(pad-48)%12,0,0,0,0,0,0],mask:pad<48?7:1,color:1}));
async function fresh(){const engine=await createCore({bytes});okay(engine.firmware.layout(layout(),{anchor:60,degrees:[0,2,4,5,7,9,11],root:0}));okay(engine.firmware.performance({melodyLayout:0}));okay(engine.firmware.route(0,0,1));okay(engine.firmware.route(1,1,1));okay(engine.advance(0));return engine;}
function legacySong(modern){
  const legacy=new Uint8Array(modern.length-84),data=new DataView(legacy.buffer);
  legacy.set(modern.subarray(0,32));data.setUint16(4,1,true);data.setUint32(8,legacy.length,true);
  for(let track=0;track<21;track++){legacy.set(modern.subarray(32+track*28,56+track*28),32+track*24);legacy[55+track*24]&=127;}
  legacy.set(modern.subarray(620),536);data.setUint32(12,crc32(legacy.subarray(16))>>>0,true);return legacy;
}

test('shared sequencer starts idle, with 21 independent tracks and remote subscriptions disabled',async()=>{
  const engine=await fresh(),snapshot=engine.sequencer.snapshot();
  assert.equal(snapshot.tracks.length,21);assert.equal(snapshot.capacity,512);assert.equal(snapshot.takeCapacity,256);
  assert.equal(snapshot.count,0);assert.equal(snapshot.capture.state,'off');assert.equal(snapshot.open,false);
  assert.equal(snapshot.tracks.filter(track=>track.exists).length,0);
  assert.deepEqual(snapshot.remote,{record:false,position:false,channel:0});assert.equal(snapshot.clock.transport,'stopped');
  assert.deepEqual(engine.drain(),[]);assert.ok(snapshot.runtimeBytes<=30720);
});
test('actual C track playback owns routes independently and stops only track notes',async()=>{
  const engine=await fresh();okay(engine.sequencer.configureTrack(0,{channel:2,mask:1}));
  okay(engine.sequencer.add(0,[{tick:0,duration:480,pitch:60,value:87},{tick:960,duration:240,pitch:64,value:99}]));
  okay(engine.press({pad:1,notes:[72],channel:5,mask:1}));engine.drain();
  okay(engine.rhythm.start());okay(engine.advance(0));assert.deepEqual(wire(engine.drain()),[[0,0x92,60,87]]);
  okay(engine.sequencer.configureTrack(0,{channel:4,mask:4}));okay(engine.advance(250));
  assert.deepEqual(wire(engine.drain()),[[0,0x82,60,0]],'old route must own its note off');
  okay(engine.advance(500));assert.deepEqual(wire(engine.drain()),[[2,0x94,64,99]]);
  okay(engine.rhythm.stop());assert.deepEqual(wire(engine.drain()),[[2,0x84,64,0]]);
  assert.equal(engine.isHeld(1),true);assert.deepEqual(engine.voices().map(v=>v.note),[72]);
  okay(engine.release(1));assert.equal(engine.stats().activeVoices,0);
});
test('arm then Play records an already-held native chord with paired lifetimes and a growing first take',async()=>{
  const engine=await fresh();okay(engine.firmware.pressPad(0));engine.drain();
  okay(engine.sequencer.record());assert.equal(engine.sequencer.snapshot().capture.state,'armed');
  okay(engine.rhythm.start());okay(engine.advance(0));assert.equal(engine.sequencer.snapshot().capture.state,'recording');
  okay(engine.advance(250));okay(engine.release(0));engine.drain();
  okay(engine.sequencer.finish(true));
  const events=engine.sequencer.events(0);assert.equal(events.length,3);assert.deepEqual(events.map(e=>e.pitch),[60,64,67]);
  assert.ok(events.every(event=>event.tick===0&&event.duration===480&&event.chord));assert.equal(new Set(events.map(e=>e.group)).size,1);
  assert.equal(engine.sequencer.snapshot().capture.state,'off');assert.equal(engine.stats().heldPads,0);
});
test('Tracks uses the OLED only; LEFT steps latch their target after release and never audition',async()=>{
  const engine=await fresh();okay(engine.sequencer.open('tracks'));
  assert.equal(engine.sequencer.pad(0).role,'separator');okay(engine.firmware.pressPad(0));okay(engine.release(0));
  assert.equal(engine.sequencer.snapshot().count,0);assert.deepEqual(engine.drain(),[]);
  okay(engine.sequencer.createTrack());assert.equal(engine.sequencer.track(0).exists,true);assert.equal(engine.sequencer.track(0).stepCount,16);
  assert.equal(engine.sequencer.pad(0).role,'editor');assert.equal(engine.sequencer.pad(4).tick,240);
  assert.equal(engine.sequencer.pad(1).tick,2880,'second physical row follows all twelve top-row steps');
  assert.equal(engine.sequencer.pad(48).role,'notes');
  okay(engine.firmware.pressPad(4));okay(engine.release(4));assert.equal(engine.sequencer.snapshot().selectedStep,1);
  assert.deepEqual(engine.sequencer.events(0),[]);assert.deepEqual(engine.drain(),[]);
  okay(engine.firmware.pressPad(48));okay(engine.release(48));engine.drain();
  assert.deepEqual(engine.sequencer.events(0).map(event=>[event.tick,event.duration,event.pitch]),[[240,240,62]]);
  assert.equal(engine.sequencer.snapshot().selectedStep,1);
  okay(engine.sequencer.undo());assert.equal(engine.sequencer.events(0).length,0);
  okay(engine.sequencer.redo());assert.equal(engine.sequencer.events(0).length,1);
  assert.equal(engine.sequencer.configureView({swapped:true}).ok,false);
  assert.equal(engine.sequencer.configureView({layout:'full-time'}).ok,false);
  okay(engine.sequencer.configureView({inputLayout:'chords'}));
  okay(engine.firmware.pressPad(48));okay(engine.release(48));engine.drain();
  assert.deepEqual(engine.sequencer.events(0).map(event=>event.pitch),[60,64,67]);
});

test('fresh input replaces a selected step, overlapping notes form one undo gesture, and Overdub adds',async()=>{
  const engine=await fresh();okay(engine.sequencer.createTrack());okay(engine.sequencer.configureView({selectedStep:3}));
  okay(engine.firmware.pressPad(48));okay(engine.release(48));engine.drain();
  okay(engine.firmware.pressPad(49));okay(engine.firmware.pressPad(50));okay(engine.release(49));okay(engine.release(50));engine.drain();
  assert.deepEqual(engine.sequencer.events(0).map(event=>event.pitch),[63,64]);
  okay(engine.sequencer.undo());assert.deepEqual(engine.sequencer.events(0).map(event=>event.pitch),[62]);
  okay(engine.sequencer.redo());okay(engine.sequencer.configureView({overdub:true}));
  okay(engine.firmware.pressPad(51));okay(engine.release(51));engine.drain();
  assert.deepEqual(engine.sequencer.events(0).map(event=>event.pitch),[63,64,65]);
  okay(engine.sequencer.clearStep());assert.deepEqual(engine.sequencer.events(0),[]);
  okay(engine.sequencer.undo());assert.equal(engine.sequencer.events(0).length,3);
});

test('track-options preview uses the scoped C screen while preserving step pads and input',async()=>{
  const engine=await fresh();const model=createSequencerLabModel({engine,now:()=>0});
  okay(model.create());okay(model.configure({selectedStep:2}));
  const colors=engine.sequencer.colors(),roles=[engine.sequencer.pad(0).role,engine.sequencer.pad(48).role];
  okay(model.options());
  assert.equal(model.snapshot().optionsOpen,true);assert.equal(model.snapshot().optionsTrack,0);
  assert.deepEqual([engine.sequencer.pad(0).role,engine.sequencer.pad(48).role],roles);
  assert.deepEqual(engine.sequencer.colors(),colors);
  okay(engine.firmware.pressPad(48));okay(engine.release(48));engine.drain();
  assert.deepEqual(engine.sequencer.events(0).map(event=>[event.tick,event.pitch]),[[480,62]]);
  okay(engine.state.navigate('left'));
  assert.equal(model.snapshot().optionsOpen,false);assert.equal(model.snapshot().open,true);
  assert.equal(model.snapshot().view,'steps');assert.equal(model.snapshot().selectedStep,2);
  okay(engine.sequencer.createTrack());okay(engine.sequencer.select(0));okay(model.options());
  assert.equal(engine.sequencer.select(1).ok,false,'A scoped editor cannot silently move to another track.');
  assert.equal(model.snapshot().optionsTrack,0);assert.equal(model.snapshot().selected,0);
});

test('created empty tracks persist independently of notes; deletion is undoable and full songs refuse creation',async()=>{
  const engine=await fresh();okay(engine.sequencer.createTrack());okay(engine.sequencer.configureView({stepCount:48,gridTicks:480}));
  const song=engine.sequencer.exportBytes();assert.equal(new DataView(song.buffer).getUint16(4,true),2);
  okay(engine.sequencer.deleteTrack());assert.equal(engine.sequencer.track(0).exists,false);
  okay(engine.sequencer.undo());assert.equal(engine.sequencer.track(0).exists,true);
  okay(engine.sequencer.importBytes(song));assert.equal(engine.sequencer.track(0).stepCount,48);assert.equal(engine.sequencer.track(0).gridTicks,480);
  for(let track=1;track<21;track++)okay(engine.sequencer.createTrack());
  assert.equal(engine.sequencer.snapshot().tracks.filter(track=>track.exists).length,21);assert.equal(engine.sequencer.createTrack().ok,false);
});

test('Play track isolates one sequence and Play all restarts the song without releasing live owners',async()=>{
  const engine=await fresh();
  for(let track=0;track<2;track++){okay(engine.sequencer.add(track,[{tick:0,duration:960,pitch:60+track*7,value:90}]));okay(engine.sequencer.configureTrack(track,{channel:track+2,mask:1}));}
  okay(engine.press({pad:1,notes:[79],channel:8,mask:1}));engine.drain();
  okay(engine.sequencer.playTrack(0));okay(engine.advance(0));assert.deepEqual(wire(engine.drain()),[[0,0x92,60,90]]);
  assert.equal(engine.sequencer.snapshot().solo,true);assert.equal(engine.rhythm.snapshot().tracks[1].running,false);
  okay(engine.sequencer.playAll());okay(engine.advance(0));
  const all=wire(engine.drain());assert.ok(all.some(event=>event[1]===0x82&&event[2]===60));assert.ok(all.some(event=>event[1]===0x92&&event[2]===60));assert.ok(all.some(event=>event[1]===0x93&&event[2]===67));
  okay(engine.sequencer.playAll());assert.equal(engine.rhythm.snapshot().transport,'stopped');assert.equal(engine.isHeld(1),true);
  okay(engine.release(1));engine.drain();assert.equal(engine.stats().activeVoices,0);
});

test('the simple Record action starts a bounded take directly and finishes into playback after one pass',async()=>{
  const engine=await fresh();okay(engine.sequencer.createTrack());okay(engine.sequencer.open('tracks'));okay(engine.sequencer.action('record'));okay(engine.advance(0));
  assert.equal(engine.sequencer.snapshot().view,'steps','Record opens the playable editor instead of trapping input on Tracks');
  assert.equal(engine.sequencer.snapshot().capture.state,'recording');assert.equal(engine.rhythm.snapshot().transport,'playing');
  okay(engine.advance(100));okay(engine.firmware.pressPad(48));okay(engine.advance(200));okay(engine.release(48));engine.drain();
  okay(engine.advance(2000));
  assert.equal(engine.sequencer.snapshot().capture.state,'off');assert.equal(engine.rhythm.snapshot().transport,'playing');
  assert.deepEqual(engine.sequencer.events(0).map(event=>[event.pitch,event.tick,event.duration]),[[62,192,192]]);
  okay(engine.rhythm.stop());engine.drain();assert.equal(engine.stats().activeVoices,0);
});

test('legacy THYS v1 imports exact events and marks nonempty tracks created',async()=>{
  const engine=await fresh();okay(engine.sequencer.add(3,[{tick:240,duration:720,pitch:65,value:104,chord:true}]));
  const legacy=legacySong(engine.sequencer.exportBytes());
  const expected=engine.sequencer.events(3);assert.equal(engine.sequencer.validateBytes(legacy),true);okay(engine.sequencer.importBytes(legacy));
  assert.deepEqual(engine.sequencer.events(3),expected);assert.equal(engine.sequencer.track(3).exists,true);assert.equal(engine.sequencer.track(2).exists,false);
});
test('THYS roundtrip is CRC protected, atomic on rejection, and leaves live notes owned',async()=>{
  const engine=await fresh();okay(engine.sequencer.add(3,[{tick:240,duration:720,pitch:65,value:104,chord:true}]));
  okay(engine.sequencer.configureTrack(3,{channel:6,mask:4,processor:'arp2',oneShot:true,quantize:3,recordControllers:true,sendPressure:true}));
  const song=engine.sequencer.exportBytes(),old=engine.sequencer.events(3);assert.equal(engine.sequencer.validateBytes(song),true);
  okay(engine.press({pad:0,notes:[72],channel:1,mask:1}));engine.drain();
  const broken=song.slice();broken[broken.length-1]^=1;
  assert.equal(engine.sequencer.importBytes(broken).ok,false);assert.deepEqual(engine.sequencer.events(3),old);
  okay(engine.sequencer.importBytes(song));assert.deepEqual(engine.sequencer.events(3),old);
  assert.equal(engine.sequencer.track(3).processor,'arp2');assert.equal(engine.sequencer.track(3).oneShot,true);assert.equal(engine.sequencer.track(3).quantize,3);
  const overlong=song.slice(),badData=new DataView(overlong.buffer);badData.setUint32(56,1,true);badData.setUint32(12,crc32(overlong.subarray(16))>>>0,true);
  assert.equal(engine.sequencer.validateBytes(overlong),false,'native v2 loops must fit the 48-step editor');
  assert.equal(engine.isHeld(0),true);assert.deepEqual(engine.voices().map(v=>v.note),[72]);okay(engine.release(0));
});
test('MIDI import publishes through C validation and exports exact musical timing plus controllers',async()=>{
  const engine=await fresh();const source={ppqn:480,bpm:115,meter:{numerator:3,denominator:4},tracks:[{channel:7,length:1440,
    notes:[{start:120,duration:360,note:60,velocity:78},{start:120,duration:360,note:64,velocity:88}],events:[{tick:600,status:0xb7,data1:1,data2:90},{tick:720,status:0xc7,data1:12,data2:0}]}]};
  let now=0;const model=createSequencerLabModel({engine,now:()=>now,onEvents:()=>engine.drain()});
  okay(model.importMidi(encodeMidiFile(source)));assert.equal(engine.sequencer.snapshot().count,4);assert.equal(engine.sequencer.track(0).channel,7);
  assert.equal(engine.rhythm.snapshot().bpm,115);assert.equal(engine.rhythm.snapshot().meterNumerator,3);
  const decoded=parseMidiFile(model.exportMidi());
  assert.deepEqual(decoded.tracks[0].notes.map(n=>[n.start,n.duration,n.note,n.velocity]),[[240,720,60,78],[240,720,64,88]]);
  assert.deepEqual(decoded.tracks[0].events.map(e=>[e.tick,e.status,e.data1,e.data2]),[[1200,0xb7,1,90],[1440,0xc7,12,0]]);
  const before=engine.sequencer.exportBytes();assert.equal(model.importMidi(Uint8Array.of(1,2,3)).ok,false);assert.deepEqual(engine.sequencer.exportBytes(),before);
  assert.equal(model.snapshot().notice.includes('MIDI file'),true);
});
test('USB remote capture respects explicit permissions and channel filter without automatic echo',async()=>{
  const engine=await fresh();okay(engine.sequencer.configureTrack(0,{source:'usb'}));
  okay(engine.sequencer.configureRemote({record:true,position:false,channel:2}));
  okay(engine.sequencer.record());okay(engine.rhythm.start());okay(engine.advance(0));
  engine.receive([9,0x90,60,90],{now:10});engine.receive([8,0x80,60,0],{now:20});
  engine.receive([9,0x91,62,99],{now:30});engine.receive([8,0x81,62,0],{now:130});
  assert.deepEqual(engine.drain(),[],'accepted USB note records without echo');okay(engine.sequencer.finish(true));
  assert.deepEqual(engine.sequencer.events(0).map(e=>[e.pitch,e.value]),[[62,99]]);
  const before=engine.rhythm.snapshot().songTick;engine.receive([3,0xf2,8,0],{now:130});assert.equal(engine.rhythm.snapshot().songTick,before);
  okay(engine.sequencer.configureRemote({position:true}));engine.receive([3,0xf2,8,0],{now:130});assert.equal(engine.rhythm.snapshot().songTick,1920);
});
test('step UI source roles survive mode changes and closing until physical release',async()=>{
  const engine=await fresh();okay(engine.sequencer.createTrack());okay(engine.firmware.pressPad(48));engine.drain();
  okay(engine.sequencer.configureView({inputLayout:'chords'}));assert.equal(engine.sequencer.pad(48).role,'notes');
  okay(engine.sequencer.close());okay(engine.release(48));assert.equal(engine.stats().activeVoices,0);assert.equal(engine.stats().heldPads,0);
  assert.ok(wire(engine.drain()).every(event=>(event[1]&0xf0)===0x80));
});

test('created and disabled flags survive web route edits while out-of-range flags are rejected',async()=>{
  const engine=await fresh();okay(engine.sequencer.createTrack());okay(engine.sequencer.configureTrack(0,{flags:192}));
  okay(engine.sequencer.configureTrack(0,{channel:7,mask:4}));assert.equal(engine.sequencer.track(0).flags,192);assert.equal(engine.sequencer.track(0).channel,7);
  assert.equal(engine.sequencer.configureTrack(0,{flags:256}).ok,false);
});

test('browser song save/load and v15 preferences restore the song without starting playback',async()=>{
  const engine=await fresh();let now=0;const model=createSequencerLabModel({engine,now:()=>now,onEvents:()=>engine.drain()});
  okay(engine.sequencer.add(5,[{tick:480,duration:960,pitch:69,value:93,chord:true}]));
  okay(engine.sequencer.configureTrack(5,{channel:11,mask:4,processor:'arp1',quantize:4}));
  okay(engine.sequencer.select(5));okay(engine.sequencer.configureView({inputLayout:'chords',gridTicks:480,basePitch:48}));
  const records=new Map(),storage={setItem:(key,value)=>records.set(key,value),getItem:key=>records.get(key)??null};
  okay(model.saveLocal(storage));okay(engine.sequencer.clear(5));okay(model.loadLocal(storage));assert.equal(engine.sequencer.events(5).length,1);
  const expected=engine.sequencer.events(5),payload=engine.settings.encode();assert.ok(payload.length>2881);
  storage.setItem(PREVIEW_SETTINGS_KEY,JSON.stringify({format:'thy666.preview-settings',version:15,crc:engine.settings.crc(payload),payload:Array.from(payload)}));
  const cold=await fresh(),prefs=new PreviewSettings(cold,storage);assert.equal(prefs.restore(()=>true),'Saved locally');
  assert.deepEqual(cold.sequencer.events(5),expected);assert.equal(cold.sequencer.track(5).channel,11);assert.equal(cold.sequencer.track(5).processor,'arp1');
  assert.equal(cold.sequencer.snapshot().swapped,false);assert.equal(cold.sequencer.snapshot().inputLayout,'chords');assert.equal(cold.sequencer.track(5).gridTicks,480);
  assert.equal(cold.rhythm.snapshot().transport,'stopped');assert.equal(cold.stats().activeVoices,0);assert.deepEqual(cold.drain(),[]);
});

test('existing v6 browser settings containing a v1 song migrate losslessly into v2 without autoplay',async()=>{
  const engine=await fresh();okay(engine.sequencer.add(4,[{tick:720,duration:480,pitch:70,value:91,chord:true}]));
  okay(engine.sequencer.configureTrack(4,{channel:9,mask:4}));okay(engine.sequencer.select(4));
  const current=legacyViewInfo(engine.settings.encode()),song=legacySong(engine.sequencer.exportBytes()),legacy=new Uint8Array(2345+song.length);
  legacy.set(current.subarray(0,2345));legacy[6]=6;legacy.set(song,2345);assert.equal(engine.settings.validate(legacy),true);
  const records=new Map([[PREVIEW_SETTINGS_KEY,JSON.stringify({format:'thy666.preview-settings',version:6,crc:engine.settings.crc(legacy),payload:Array.from(legacy)})]]);
  const storage={getItem:key=>records.get(key)??null,setItem:(key,value)=>records.set(key,value)},cold=await fresh(),preferences=new PreviewSettings(cold,storage);
  assert.equal(preferences.restore(()=>true),'Saved locally');assert.deepEqual(cold.sequencer.events(4),engine.sequencer.events(4));
  assert.equal(cold.sequencer.track(4).exists,true);assert.equal(cold.sequencer.track(4).channel,9);assert.equal(cold.rhythm.snapshot().transport,'stopped');assert.deepEqual(cold.drain(),[]);
  preferences.tick(0);preferences.tick(1600);
  const migrated=Uint8Array.from(JSON.parse(storage.getItem(PREVIEW_SETTINGS_KEY)).payload);
  assert.equal(new TextDecoder().decode(migrated.subarray(5013,5017)),'THYS');
  assert.equal(new DataView(migrated.buffer).getUint16(5017,true),2);assert.equal(cold.settings.validate(migrated),true);
});
