import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {createBackupPayloadMigrator} from '../../web/backup-migration.js';
import {BACKUP_HEADER_BYTES,BACKUP_MAX_BYTES} from '../../web/device-client.js';

const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));
const fresh=()=>createCore({bytes});
const okay=value=>assert.equal(value.ok,true,value.status||value.message);

async function fixture(version) {
  const engine=await fresh(),library=engine.firmware.library;
  okay(library.set(0,0,{used:true,name:'MY SCALE',mask:0x295}));
  okay(library.set(1,0,{used:true,name:'MY CHORD',root:0,notes:[0,12,7,99,0,64,127],mask:0x45}));
  okay(library.set(2,0,{used:true,name:'MY SET',chords:[0,255,0,255,255,0]}));
  okay(engine.sequencer.add(2,[{tick:240,duration:720,pitch:65,value:104,chord:true}]));
  okay(engine.sequencer.configureTrack(2,{channel:7,mask:3}));
  const velocity=engine.firmware.velocity(52);
  okay(engine.firmware.setVelocity(52,{...velocity,mode:1,minimum:12,maximum:120}));
  okay(engine.firmware.setResponse(4,{...engine.firmware.response(4),curve:1}));
  const canonical=engine.settings.encode(),song=engine.sequencer.exportBytes();
  // V16 appends twelve Harmony/system preferences just before THYS. Form a
  // synthetic historical record without those bytes; let C validate meaning.
  const songStart=canonical.length-song.length;
  assert.deepEqual(canonical.subarray(songStart),song);
  const removed=version===17?4:version===16?7:19;
  const legacy=new Uint8Array(canonical.length-removed);
  legacy.set(canonical.subarray(0,songStart-removed));legacy.set(song,songStart-removed);
  legacy[6]=version;
  assert.equal(engine.settings.payloadVersion(legacy),version);
  assert.equal(engine.settings.validate(legacy),true);
  return {canonical,legacy,library:library.exportFile(),song:engine.sequencer.exportBytes(),
    velocity:engine.firmware.velocity(52),response:engine.firmware.response(4)};
}

for(const version of [13,14,15,16,17])test(`isolated shared C upgrades v${version} to18 with song, custom library and per-pad override`,async()=>{
  const before=await fixture(version),original=before.legacy.slice();let instances=0;
  const unrelated=await fresh(),untouched=unrelated.settings.encode();
  const migrate=createBackupPayloadMigrator({createEngine:async()=>{instances++;return fresh();}});
  const migrated=await migrate(before.legacy,{sourceVersion:version,targetVersion:18});
  assert.deepEqual(migrated,before.canonical);assert.deepEqual(before.legacy,original);
  assert.deepEqual(unrelated.settings.encode(),untouched);assert.deepEqual(unrelated.drain(),[]);
  const restored=await fresh();assert.equal(restored.settings.apply(migrated),true);
  assert.deepEqual(restored.firmware.library.exportFile(),before.library);
  assert.deepEqual(restored.sequencer.exportBytes(),before.song);
  assert.deepEqual(restored.firmware.velocity(52),before.velocity);
  assert.deepEqual(restored.firmware.response(4),before.response);
  assert.deepEqual(restored.drain(),[]);
  await migrate(before.legacy,{sourceVersion:version,targetVersion:18});
  assert.equal(instances,2,'each migration uses a new independent engine');
});

test('migration rejects unsupported directions, wrong source marker, corrupt semantics and mismatched C build',async()=>{
  const {legacy}=await fixture(13),migrate=createBackupPayloadMigrator({createEngine:fresh});
  for(const [sourceVersion,targetVersion] of [[12,17],[15,13],[13,14],[16,15],[17,17]])
    await assert.rejects(migrate(legacy,{sourceVersion,targetVersion}),/cannot migrate/);
  await assert.rejects(migrate(legacy,{sourceVersion:14,targetVersion:18}),/codec rejected/);
  const invalid=legacy.slice();invalid[0]=255;invalid[1]=255;
  await assert.rejects(migrate(invalid,{sourceVersion:13,targetVersion:18}),/codec rejected/);
  const wrong=createBackupPayloadMigrator({createEngine:async()=>({settings:{version:14}})});
  await assert.rejects(wrong(legacy,{sourceVersion:13,targetVersion:18}),/codec rejected/);
});

test('the browser bound includes an actual full512-event V18 export and its historical migration',async()=>{
  const engine=await fresh();
  for(let start=0;start<512;start+=64)okay(engine.sequencer.add(0,Array.from({length:64},(_,n)=>({tick:(start+n)*120,duration:60,pitch:36+(start+n)%48,value:93}))));
  const canonical=engine.settings.encode(),song=engine.sequencer.exportBytes(),songStart=canonical.length-song.length;
  assert.equal(canonical.length+BACKUP_HEADER_BYTES,BACKUP_MAX_BYTES,'Keep the browser file limit aligned with the actual C codec maximum.');
  const removed=19;
  const legacy=new Uint8Array(canonical.length-removed);legacy.set(canonical.subarray(0,songStart-removed));legacy.set(song,songStart-removed);legacy[6]=15;
  assert.equal(engine.settings.validate(legacy),true);
  const normalized=await createBackupPayloadMigrator({createEngine:fresh})(legacy,{sourceVersion:15,targetVersion:18});
  assert.deepEqual(normalized,canonical);
});
