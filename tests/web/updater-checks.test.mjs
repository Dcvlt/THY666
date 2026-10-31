import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {BACKUP_SETTINGS_CURRENT_VERSION, BACKUP_SETTINGS_MIN_VERSION, supportsBackupSettingsVersion} from '../../web/device-client.js';
import { UPDATE_HEALTH_CHECKS, checkUpdateIdentity, checkUpdateHealth as rawHealth } from '../../web/updater-checks.js';

const BUILD = '1234567890abcdef', UID = '0123456789abcdef01234567';
const operation = () => ({ firmware_kind: 'thy666', simulation: false, expected_build: BUILD, target_uid: UID });
const identity=version=>({uid:UID,build:BUILD,settings:String(version)});
const checkUpdateHealth=(id,line,op,backup=identity(Number(/\bversion=(\d+)/.exec(line)?.[1])))=>rawHealth(id,line,op,backup);

// Synthetic healthy counterparts of firmware/instrument/protocol.c and the
// exact report shapes exercised by tests/core/test_instrument_protocol.c.
// The final STATUS layout is human text and legitimately contains a space.
const reports = {
  status: `STATUS THYINSTRUMENT/1 build=${BUILD} ready=1 uptime_ms=12345 errors=0 presses=12 releases=12 voices=0 layout=MAJOR C\r\n`,
  io: 'IO din_ready=1 din_errors=0 din_queued=0 din_tx=100 din_accept=12 usb_ready=1 usb_mounted=1 usb_accepted=12 din_events=12 usb_dropped=0 transport_fault=0 core_queued=0 core_pending=0 din_pending=0 usb_pending=0 held=0 blocked=0 warmed=96 led_errors=0 led_frames=100 led_fault=0\r\n',
  settings: 'SETTINGS version=13 status=saved loaded=1 loaded_version=13 migration_pending=0 recovered=0 generation=43 saves=5 errors=0 rejected=3 backend=internal-sectors10-11 updater_preservation=unverified writes=none\r\n',
  display: 'DISPLAY version=2 period_ms=16 spi_hz=328125 frames=1 chunks=2 command_bytes=3 data_bytes=4 busy_deferrals=5 last_started_ms=6 last_completed_ms=7 last_duration_ms=1 max_duration_ms=9 last_render_gap_ms=10 max_render_gap_ms=4294967295 active=1 page=7 column=127 faulted=0 submitted=11 coalesced=12 unchanged=13 failed_chunks=0 generation=15 last_generation_age_ms=16 max_generation_age_ms=17 dirty_bytes=18 valid_bytes=1024 initial_complete=1 last_render_us=19 max_render_us=20 last_chunk_us=21 max_chunk_us=4294967295\r\n',
};
function replace(id, key, value) {
  const expression = new RegExp(`\\b${key}=[^\\s]+`);
  assert.match(reports[id], expression, `Synthetic ${id} report must contain ${key}`);
  return reports[id].replace(expression, `${key}=${value}`);
}
function without(id, key) { return reports[id].replace(new RegExp(`\\b${key}=[^\\s]+\\s*`), ''); }

test('verification checklist names identity plus four distinct read-only instrument checks', () => {
  assert.deepEqual(UPDATE_HEALTH_CHECKS.map(check => check.id), ['identity', 'status', 'io', 'settings', 'display']);
  assert.ok(UPDATE_HEALTH_CHECKS.every(check => typeof check.label === 'string' && check.label.length));
  assert.equal(Object.isFrozen(UPDATE_HEALTH_CHECKS), true);
});
test('identity requires matching full firmware build and physical UID rather than filename prefixes or descriptor serials', () => {
  assert.match(checkUpdateIdentity(operation(), { build: BUILD, uid: UID }), /match/i);
  assert.doesNotThrow(() => checkUpdateIdentity(operation(), { build: BUILD.toUpperCase(), uid: UID.toUpperCase() }));
  for (const identity of [null, {}, { build: BUILD.slice(0, 8), uid: UID }, { build: BUILD, uid: '0000000000' },
    { build: 'fedcba0987654321', uid: UID }, { build: BUILD, uid: 'fedcba9876543210fedcba98' }]) {
    assert.throws(() => checkUpdateIdentity(operation(), identity));
  }
  for (const changed of [{ expected_build: null }, { expected_build: '12345678' }, { target_uid: undefined }, { target_uid: '0000000000' }]) {
    assert.throws(() => checkUpdateIdentity({ ...operation(), ...changed }, { build: BUILD, uid: UID }));
  }
});
test('real firmware report shapes pass without treating human layout text or historical nonfault counters as errors', () => {
  for (const [id, line] of Object.entries(reports)) assert.equal(typeof checkUpdateHealth(id, line, operation()), 'string');
  // User scale names are up to 12 printable ASCII characters, including '='
  // (core/thy_library.c valid_name), and STATUS appends the entire name last.
  for (const label of ['ready=0', 'errors=99', 'A = B']) assert.doesNotThrow(() => checkUpdateHealth('status', reports.status.replace('layout=MAJOR C', `layout=${label}`), operation()));
  assert.match(checkUpdateHealth('settings', reports.settings.replace('status=saved loaded=1 loaded_version=13', 'status=defaults loaded=0 loaded_version=0'), operation()), /Factory/i);
  assert.doesNotThrow(() => checkUpdateHealth('display', replace('display', 'frames', '4294967295'), operation()));
});
test('simulation and original firmware can never produce green THY666 hardware identity or health checks', () => {
  for (const context of [{ ...operation(), simulation: true }, { ...operation(), firmware_kind: 'stock', expected_build: null }]) {
    assert.throws(() => checkUpdateIdentity(context, { build: BUILD, uid: UID }));
    for (const [id, line] of Object.entries(reports)) assert.throws(() => checkUpdateHealth(id, line, context), `${id} must reject ${context.firmware_kind}/${context.simulation}`);
  }
});
test('status cannot verify a missing or changed expected build, startup fault, active notes or unfinished startup', () => {
  for (const [key, value] of [['build', 'fedcba0987654321'], ['ready', '0'], ['errors', '1'], ['voices', '1']]) {
    assert.throws(() => checkUpdateHealth('status', replace('status', key, value), operation()), key);
  }
  assert.throws(() => checkUpdateHealth('status', without('status', 'build'), { ...operation(), expected_build: undefined }));
});
test('driver errors, blocked or held pads, incomplete acquisition and disconnected MIDI stay failures', () => {
  for (const [key, value] of [['din_ready', '0'], ['usb_ready', '0'], ['usb_mounted', '0'], ['din_errors', '1'],
    ['transport_fault', '1'], ['led_errors', '1'], ['led_fault', '1'], ['held', '1'], ['blocked', '1'], ['warmed', '95'], ['warmed', '97']]) {
    assert.throws(() => checkUpdateHealth('io', replace('io', key, value), operation()), key);
  }
});

test('only valid incomplete warmup is marked as transient pad settling',()=>{
  assert.throws(()=>checkUpdateHealth('io',replace('io','warmed','0'),operation()),error=>error.code==='pads-settling');
  for(const line of [replace('io','warmed','97'),replace('io','warmed','-1'),without('io','warmed'),replace('io','blocked','1').replace('warmed=96','warmed=0'),replace('io','din_errors','1').replace('warmed=96','warmed=0')])
    assert.throws(()=>checkUpdateHealth('io',line,operation()),error=>error.code!=='pads-settling');
});
test('settings saving, unsupported formats, unavailable storage and errors never pass', () => {
  for (const status of ['pending', 'saving', 'error', 'unsupported', 'unavailable', 'unknown']) {
    assert.throws(() => checkUpdateHealth('settings', replace('settings', 'status', status), operation()), status);
  }
  for (const version of ['0', '12', String(BACKUP_SETTINGS_CURRENT_VERSION+1), '999']) assert.throws(() => checkUpdateHealth('settings', replace('settings', 'version', version), operation()), `version ${version}`);
  assert.throws(() => checkUpdateHealth('settings', replace('settings', 'errors', '1'), operation()));
});
test('display checks require the known report version, an initial completed frame and no transfer or driver faults', () => {
  for (const [key, value] of [['version', '0'], ['version', '1'], ['version', '3'], ['frames', '0'], ['initial_complete', '0'], ['faulted', '1'], ['failed_chunks', '1']]) {
    assert.throws(() => checkUpdateHealth('display', replace('display', key, value), operation()), `${key}=${value}`);
  }
});
test('all required health fields must be present', () => {
  const keys = { status: ['build', 'ready', 'errors', 'voices'], io: ['din_ready', 'usb_ready', 'usb_mounted', 'din_errors', 'transport_fault', 'led_errors', 'led_fault', 'held', 'warmed', 'blocked'], settings: ['version', 'status', 'errors'], display: ['version', 'faulted', 'failed_chunks', 'initial_complete', 'frames'] };
  for (const [id, names] of Object.entries(keys)) for (const key of names) assert.throws(() => checkUpdateHealth(id, without(id, key), operation()), `${id}/${key}`);
});
test('required numeric fields reject nonfinite, signed, hexadecimal, fractional, imprecise and overflowing text', () => {
  for (const value of ['NaN', 'Infinity', '-1', '+1', '01', '1.0', '1e0', '0x1', '4294967296', '9007199254740992']) {
    for (const [id, key] of [['status', 'ready'], ['io', 'din_ready'], ['settings', 'errors'], ['display', 'frames']]) {
      assert.throws(() => checkUpdateHealth(id, replace(id, key, value), operation()), `${id}/${value}`);
    }
  }
});
test('malformed, duplicate and conflicting report fields cannot be ignored into a pass', () => {
  for (const [id, key] of [['status', 'ready'], ['io', 'din_ready'], ['settings', 'errors'], ['display', 'faulted']]) {
    for (const duplicate of [`${key}=0`, `${key.toUpperCase()}=0`]) {
      const line = id === 'status' ? reports[id].replace(' layout=', ` ${duplicate} layout=`) : `${reports[id].trimEnd()} ${duplicate}`;
      assert.throws(() => checkUpdateHealth(id, line, operation()), `${id}/${duplicate}`);
    }
  }
  assert.throws(() => checkUpdateHealth('io', reports.io.replace(' held=0', ' broken-field held=0'), operation()));
  assert.throws(() => checkUpdateHealth('status', reports.status.replace(' ready=1', ' malformed=value=extra ready=1'), operation()));
});
test('wrong report families, failed commands, oversized output and nonstrings never pass', () => {
  for (const id of Object.keys(reports)) {
    for (const value of [null, {}, ['line'], '', 'ERR not-ready', 'OK reset', reports[id].replace(/^\S+/, 'OTHER'), `${reports[id]}${'x'.repeat(4097)}`]) {
      assert.throws(() => checkUpdateHealth(id, value, operation()), `${id}/${typeof value}`);
    }
  }
  assert.throws(() => checkUpdateHealth('unknown', reports.io, operation()));
});

test('settings health accepts v14 while retaining v13 compatibility', () => {
  assert.match(checkUpdateHealth('settings', reports.settings.replace('version=13', 'version=14'), operation()), /Saved settings/);
  assert.throws(() => checkUpdateHealth('settings', reports.settings.replace('version=13', `version=${BACKUP_SETTINGS_CURRENT_VERSION+1}`), operation()), /not supported/);
});

 test('settings health accepts current v15 without dropping prior format support',()=>{
  assert.match(checkUpdateHealth('settings',reports.settings.replace('version=13','version=15'),operation()),/Saved settings/);
 });

test('Harmony v16 storage passes only when idle and healthy', () => {
  const report = reports.settings.replace('version=13', 'version=16');
  assert.match(checkUpdateHealth('settings', report, operation()), /Saved settings/);
  assert.match(checkUpdateHealth('settings', report.replace('status=saved', 'status=defaults'), operation()), /Factory settings/);
  for (const status of ['pending', 'saving', 'error'])
    assert.throws(() => checkUpdateHealth('settings', report.replace('status=saved', `status=${status}`), operation()));
  assert.throws(() => checkUpdateHealth('settings', report.replace('errors=0', 'errors=1'), operation()));
});

test('the central version contract matches both current C source and generated C codec',async()=>{
  const header=await readFile(new URL('../../core/thy_settings.h',import.meta.url),'utf8');
  const sourceVersion=Number(/^#define THY_SETTINGS_VERSION (\d+)u$/m.exec(header)?.[1]);
  const engine=await createCore({bytes:await readFile(new URL('../../web/generated/core.wasm',import.meta.url))});
  assert.equal(BACKUP_SETTINGS_CURRENT_VERSION,sourceVersion,'A settings schema change must update the single Client contract.');
  assert.equal(BACKUP_SETTINGS_CURRENT_VERSION,engine.settings.version,'The Client contract must match the shipped C codec.');
  for(let version=BACKUP_SETTINGS_MIN_VERSION;version<=engine.settings.version;version++){
    assert.equal(supportsBackupSettingsVersion(version),true);
    assert.match(rawHealth('settings',replace('settings','version',version),operation(),identity(version)),/Saved settings/);
  }
  for(const version of [BACKUP_SETTINGS_MIN_VERSION-1,engine.settings.version+1,19.5,'19',null,NaN])
    assert.equal(supportsBackupSettingsVersion(version),false);
});

test('current V19 and legacy V18 verify only when the connected capabilities agree',()=>{
  for(const version of [18,19]){
    const report=replace('settings','version',version);
    assert.match(rawHealth('settings',report,operation(),identity(version)),/Saved settings/);
    for(const caps of [undefined,{},identity(version===19?18:19),{...identity(version),uid:'f'.repeat(24)},
      {...identity(version),build:'f'.repeat(16)},{...identity(version),settings:'019'},identity(20)])
      assert.throws(()=>rawHealth('settings',report,operation(),caps),/capabilities disagree/);
    for(const [key,value] of [['status','saving'],['status','unsupported'],['errors','1']])
      assert.throws(()=>rawHealth('settings',report.replace(new RegExp(`\\b${key}=[^\\s]+`),`${key}=${value}`),operation(),identity(version)));
  }
});
