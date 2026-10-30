// Current test contract. Historical destination offsets stay in their tests.
export const CURRENT_SETTINGS_VERSION=19;
export const CURRENT_SETTINGS_PARAMETER_BYTES=5042;
export const CURRENT_EMPTY_SETTINGS_BYTES=5662;

// Test-only construction of the frozen V12–V15 parameter/library projection.
// V16-V19 append preferences after the old 5013-byte prefix, before THYS.
// Production migration always runs the shared C codec, never this helper.
export function legacyParameterPayload(payload,version){
  if(!(payload instanceof Uint8Array)||payload[6]!==CURRENT_SETTINGS_VERSION||version<12||version>15||
    new TextDecoder().decode(payload.subarray(CURRENT_SETTINGS_PARAMETER_BYTES,CURRENT_SETTINGS_PARAMETER_BYTES+4))!=='THYS')
    throw new Error('Expected a current settings payload and a frozen V12–V15 fixture version.');
  const result=new Uint8Array(payload.length-(CURRENT_SETTINGS_PARAMETER_BYTES-5013));
  result.set(payload.subarray(0,5013));result.set(payload.subarray(CURRENT_SETTINGS_PARAMETER_BYTES),5013);result[6]=version;
  return result;
}
