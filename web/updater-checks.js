/* Basic read-only installation checks. Passing these is not a playing test,
 * raw-flash readback, backup restoration or interrupted-update qualification. */
import {supportsBackupSettingsVersion} from './device-client.js';
export const UPDATE_HEALTH_CHECKS = Object.freeze([
  { id: 'identity', label: 'Correct board and firmware' },
  { id: 'status', label: 'Instrument startup' },
  { id: 'io', label: 'Pads, lights and MIDI drivers' },
  { id: 'settings', label: 'Settings storage' },
  { id: 'display', label: 'Display driver' },
]);

function fields(line, prefix) {
  if (typeof line !== 'string' || line.length > 4096 || !line.startsWith(prefix)) throw new Error('The board returned an unexpected health report.');
  const result = {};
  let machine=line.slice(prefix.length);
  // STATUS ends with the user-editable scale name. It can legally contain
  // spaces and '=' signs, so it must never become machine health fields.
  if(prefix==='STATUS THYINSTRUMENT/1 '){const at=machine.indexOf(' layout=');if(at>=0)machine=machine.slice(0,at);}
  for (const item of machine.trim().split(/\s+/)) {
    const match = /^([a-z_]+)=([^\s=]+)$/i.exec(item);
    if (!match) throw new Error('The health report contains a malformed value.');
    const key = match[1].toLowerCase();
    if (Object.hasOwn(result, key)) throw new Error('The health report contains duplicate values.');
    result[key] = match[2];
  }
  return result;
}
function number(values, key) {
  if (!/^(?:0|[1-9][0-9]*)$/.test(values[key] || '')) throw new Error('The board did not report all required health values.');
  const value = Number(values[key]); if (!Number.isSafeInteger(value) || value > 0xffffffff) throw new Error('A health value is outside its supported range.');
  return value;
}
function equals(values, key, expected, message) { if (number(values, key) !== expected) throw new Error(message); }
export function checkUpdateIdentity(operation, identity) {
  if (operation?.simulation || operation?.firmware_kind === 'stock') throw new Error('This operation cannot verify THY666 hardware.');
  if (!/^[0-9a-f]{16}$/i.test(operation?.expected_build || '') || !/^[0-9a-f]{24}$/i.test(operation?.target_uid || ''))
    throw new Error('The update did not provide a complete firmware and board identity.');
  if (identity?.build?.toLowerCase() !== operation.expected_build.toLowerCase() || identity?.uid?.toLowerCase() !== operation.target_uid.toLowerCase())
    throw new Error('This is a different board or firmware. Keep your backup and check the selected device.');
  return 'The running build and board identity match.';
}
export function checkUpdateHealth(id, line, operation, backupIdentity) {
  if (operation?.simulation || operation?.firmware_kind === 'stock' || !/^[0-9a-f]{16}$/i.test(operation?.expected_build || ''))
    throw new Error('Only a supported THY666 installation can run these health checks.');
  if (id === 'status') {
    const v = fields(line, 'STATUS THYINSTRUMENT/1 ');
    if (v.build?.toLowerCase() !== operation.expected_build?.toLowerCase()) throw new Error('The running firmware changed during verification.');
    equals(v, 'ready', 1, 'The instrument has not finished starting. Wait, then check again.');
    equals(v, 'errors', 0, 'The instrument reports a startup error. Open the details before continuing.');
    equals(v, 'voices', 0, 'Release every pad and stop playback, then check again.');
    return 'Started normally; no startup errors or sounding notes.';
  }
  if (id === 'io') {
    const v = fields(line, 'IO ');
    for (const key of ['din_ready', 'usb_ready', 'usb_mounted']) equals(v, key, 1, 'A MIDI driver is not ready. Wait, then check again.');
    for (const key of ['din_errors', 'transport_fault', 'led_errors', 'led_fault']) equals(v, key, 0, 'A pad, lighting or MIDI driver reports an error.');
    equals(v, 'held', 0, 'Release every pad, then check again.');
    equals(v, 'blocked', 0, 'A pad is blocked. Release every pad, then check again.');
    const warmed=number(v,'warmed');
    if(warmed!==96){
      const error=new Error('Pad acquisition is still settling. Wait, then check again.');
      if(warmed<96)error.code='pads-settling';throw error;
    }
    return 'All 96 pads have settled; lighting and MIDI drivers report ready.';
  }
  if (id === 'settings') {
    const v = fields(line, 'SETTINGS ');
    const version=number(v,'version');
    if (!supportsBackupSettingsVersion(version)) throw new Error('This settings format is not supported by this Client. Update the Client before verifying it.');
    if(!backupIdentity||!/^(?:0|[1-9][0-9]*)$/.test(String(backupIdentity.settings))||
      !/^[0-9a-f]{24}$/i.test(operation.target_uid||'')||!supportsBackupSettingsVersion(Number(backupIdentity.settings))||
      Number(backupIdentity.settings)!==version||backupIdentity.uid?.toLowerCase()!==operation.target_uid?.toLowerCase()||
      backupIdentity.build?.toLowerCase()!==operation.expected_build?.toLowerCase())
      throw new Error('The settings report and connected backup capabilities disagree. Reconnect the intended board before verifying it.');
    if (!['saved', 'defaults'].includes(v.status)) throw new Error(['pending', 'saving'].includes(v.status) ? 'Settings are still saving. Wait, then check again.' : 'Settings storage needs attention. Keep your configuration backup.');
    equals(v, 'errors', 0, 'Settings storage reports an error. Keep your configuration backup.');
    return v.status === 'defaults' ? 'Factory settings loaded normally.' : 'Saved settings loaded normally.';
  }
  if (id === 'display') {
    const v = fields(line, 'DISPLAY ');
    equals(v, 'version', 2, 'This display report is not supported by the verifier.');
    equals(v, 'faulted', 0, 'The display driver reports a fault.');
    equals(v, 'failed_chunks', 0, 'The display driver reports a transfer error.');
    equals(v, 'initial_complete', 1, 'The display is still starting. Wait, then check again.');
    if (number(v, 'frames') < 1) throw new Error('No completed display frame has been reported.');
    return 'The display has completed its first frame without driver faults.';
  }
  throw new Error('Unsupported verification check.');
}
