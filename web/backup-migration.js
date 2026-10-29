/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent, off-device normalization through the shared settings codec.
 * Each call creates an isolated C engine; the connected mirror is untouched.
 */
export function createBackupPayloadMigrator({createEngine}={}) {
  if(typeof createEngine!=='function')throw new TypeError('Backup migration needs an isolated shared C engine.');
  return async (payload,{sourceVersion,targetVersion})=>{
    if(!(payload instanceof Uint8Array)||![13,14,15,16,17].includes(sourceVersion)||![15,16,17,18].includes(targetVersion)||sourceVersion>=targetVersion)
      throw new Error('This client cannot migrate that settings version. No restore was sent.');
    const engine=await createEngine(),settings=engine?.settings;
    if(settings?.version!==targetVersion||settings.payloadVersion(payload)!==sourceVersion||
      !settings.validate(payload)||!settings.apply(payload.slice()))
      throw new Error('The shared C settings codec rejected this backup. No restore was sent.');
    const normalized=settings.encode();
    if(!(normalized instanceof Uint8Array)||settings.payloadVersion(normalized)!==targetVersion||!settings.validate(normalized))
      throw new Error('The shared C settings migration could not be verified. No restore was sent.');
    return normalized.slice();
  };
}
