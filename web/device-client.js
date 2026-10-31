/* SPDX-License-Identifier: GPL-3.0-or-later
 * One explicitly opened serial connection, bounded commands and exclusive
 * transactions. Construction never connects, writes or asks for a port.
 */
export const BACKUP_HEADER_BYTES=64;
export const BACKUP_MAX_BYTES=13918;
// One bounded Client contract for backup inspection, migration and update
// verification. Regression checks bind the upper bound to the generated C
// codec, so a settings upgrade cannot silently leave a stale verifier behind.
export const BACKUP_SETTINGS_MIN_VERSION=13;
export const BACKUP_SETTINGS_CURRENT_VERSION=19;
export const supportsBackupSettingsVersion=version=>Number.isInteger(version)&&
  version>=BACKUP_SETTINGS_MIN_VERSION&&version<=BACKUP_SETTINGS_CURRENT_VERSION;
export const canMigrateBackupSettings=(source,target)=>supportsBackupSettingsVersion(source)&&
  supportsBackupSettingsVersion(target)&&target>=15&&source<target;
export function deviceCRC32(bytes){
  let crc=0xffffffff;
  for(const byte of bytes){crc^=byte;for(let bit=0;bit<8;bit++)crc=(crc>>>1)^((-(crc&1))&0xedb88320);}
  return (~crc)>>>0;
}
const hex=bytes=>Array.from(bytes,value=>value.toString(16).padStart(2,'0')).join('');
const unhex=text=>{
  if(!/^(?:[0-9a-f]{2})+$/i.test(text))throw new Error('Invalid hexadecimal device data.');
  return Uint8Array.from(text.match(/../g),value=>parseInt(value,16));
};
export function deviceFields(line,prefix){
  if(!line.startsWith(prefix+' '))throw new Error('Unexpected device response.');
  const fields={};
  for(const item of line.slice(prefix.length+1).split(' ')){
    const at=item.indexOf('=');if(at<1)continue;
    const key=item.slice(0,at);if(Object.hasOwn(fields,key))throw new Error('Duplicate device response field.');
    fields[key]=item.slice(at+1);
  }
  return fields;
}
const integer=(value,min,max,label)=>{
  if(!/^\d+$/.test(String(value)))throw new Error(`Invalid ${label}.`);
  const result=Number(value);
  if(!Number.isSafeInteger(result)||result<min||result>max)throw new Error(`Invalid ${label}.`);
  return result;
};
const crcValue=value=>{
  if(!/^[0-9a-f]{8}$/i.test(value||''))throw new Error('Invalid device checksum.');
  return parseInt(value,16)>>>0;
};
export function inspectDeviceBackup(bytes,identity=null){
  if(!(bytes instanceof Uint8Array)||bytes.length<BACKUP_HEADER_BYTES+1||bytes.length>BACKUP_MAX_BYTES)
    throw new Error('This is not a complete THY666 backup file.');
  const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  if(String.fromCharCode(...bytes.subarray(0,4))!=='THYB'||view.getUint16(4,true)!==1||view.getUint16(6,true)!==64)
    throw new Error('Unsupported THY666 backup format.');
  const payloadBytes=view.getUint32(8,true),payloadCRC=view.getUint32(12,true);
  if(payloadBytes!==bytes.length-64||view.getUint32(60,true)!==deviceCRC32(bytes.subarray(0,60))||
    payloadCRC!==deviceCRC32(bytes.subarray(64)))throw new Error('Backup length or checksum does not match.');
  const info={fileVersion:1,settingsVersion:view.getUint16(16,true),target:view.getUint16(18,true),
    compatibility:view.getUint32(20,true),uid:hex(bytes.subarray(24,36)),build:hex(bytes.subarray(36,44)),
    schemaPrefix:hex(bytes.subarray(44,52)),capabilities:view.getUint32(52,true),payloadBytes,payloadCRC,
    payload:bytes.subarray(64),bytes:bytes.length};
  if(!supportsBackupSettingsVersion(info.settingsVersion)||info.target!==0x413||info.compatibility!==0x10001||info.capabilities!==1||view.getUint32(56,true)!==0)
    throw new Error('This backup targets an unsupported device or settings version.');
  if(identity&&(info.uid!==identity.uid?.toLowerCase()||!supportsBackupSettingsVersion(Number(identity.settings))||info.settingsVersion>Number(identity.settings)||
    info.target!==Number(identity.target)||info.compatibility!==Number(identity.compatibility)))
    throw new Error('This backup does not belong to the connected board or its supported format.');
  return info;
}

export function createDeviceClient({serial=globalThis.navigator?.serial,timeoutMs=3000,onState=()=>{},
  expectedSchema='',migrateBackupPayload=null,now=()=>Date.now(),sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms))}={}){
  const encoder=new TextEncoder(),decoder=new TextDecoder();
  let port=null,reader=null,writer=null,pump=null,pending=null,lineBuffer='',caps=null,backupCaps=null,padMapLease=null;
  let operations=0,tail=Promise.resolve(),closing=false;
  const state={connected:false,connecting:false,busy:false,compatible:false,message:'No board connected',identity:null};
  const emit=message=>{if(message)state.message=message;state.busy=operations>0;onState({...state});};
  const rejectPending=error=>{if(pending){const request=pending;pending=null;clearTimeout(request.timer);request.reject(error);}};
  function accept(line){
    line=line.trim();if(!line||!pending)return;
    if(line.startsWith('ERR ')||line.startsWith('COMPANION/1 ERR ')){rejectPending(new Error(line==='ERR unknown-command'?
      'This firmware does not support the client. Install the prepared build to connect.':line));return;}
    if(!pending.pattern.test(line))return;
    const request=pending;pending=null;clearTimeout(request.timer);request.resolve(line);
  }
  const validateCommand=command=>{
    if(typeof command!=='string'||command.length>159||/[\r\n]/.test(command)||
      !/^(?:companion |backup |song |status$|io$|settings$|display$|pad \d+$|velocity \d+$)/.test(command))
      throw new Error('Unsupported device command.');
  };
  async function nativeExchange(commands,pattern){
    if(pending)throw new Error('Another device command is in flight.');
    commands.forEach(validateCommand);
    const nativePort=port;let request;
    const failure=error=>{if(pending!==request)return;state.connected=false;state.compatible=false;rejectPending(error);emit(error.message);void nativePort.close().catch(()=>{});};
    const result=new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>failure(new Error('Device response timed out. Reconnect before sending another command; an unacknowledged edit may already have applied.')),timeoutMs*commands.length);
      pending=request={resolve,reject,timer,pattern};
    });
    Promise.resolve().then(()=>commands.length===1?nativePort.exchange(commands[0]).then(line=>[line]):nativePort.exchangeMany(commands)).then(lines=>{
      if(pending!==request)return;
      if(!Array.isArray(lines)||lines.length!==commands.length||lines.some(line=>typeof line!=='string'||line.length>4096||/[\r\n]/.test(line)))throw new Error('Invalid native device response.');
      for(const line of lines){
        if(line.startsWith('ERR ')||line.startsWith('COMPANION/1 ERR ')){rejectPending(new Error(line==='ERR unknown-command'?'This firmware does not support the client. Install the prepared build to connect.':line));return;}
        if(!pattern.test(line))throw new Error('Unexpected native device response.');
      }
      pending=null;clearTimeout(request.timer);request.resolve(lines);
    }).catch(failure);
    return result;
  }
  async function readLoop(){
    try{while(reader){const {value,done}=await reader.read();if(done)break;
      lineBuffer+=decoder.decode(value,{stream:true});
      if(lineBuffer.length>4096)throw new Error('Device response exceeded the line limit.');
      let end;while((end=lineBuffer.indexOf('\n'))>=0){accept(lineBuffer.slice(0,end));lineBuffer=lineBuffer.slice(end+1);}
    }}catch(error){rejectPending(error);}finally{
      rejectPending(new Error('Device connection closed.'));
      if(!closing){state.connected=false;state.compatible=false;emit('No board connected');}
    }
  }
  async function exchange(command,pattern){
    if(!port||(!port.exchange&&(!reader||!writer)))throw new Error('Connect the board first.');
    if(pending)throw new Error('Another device command is in flight.');
    validateCommand(command);
    if(port.exchange)return(await nativeExchange([command],pattern))[0];
    let request;
    const failure=error=>{if(pending===request)rejectPending(error);};
    const result=new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>{
        if(pending!==request)return;
        state.connected=false;state.compatible=false;
        emit('Connection timed out · reconnect to read back the device state');
        failure(new Error('Device response timed out. Reconnect before sending another command; an unacknowledged edit may already have applied.'));
      },timeoutMs);
      pending=request={resolve,reject,timer,pattern};
    });
    try{Promise.resolve(writer.write(encoder.encode(command+'\n'))).catch(failure);}catch(error){failure(error);}
    return result;
  }
  function exclusive(operation){
    operations++;emit();
    const result=tail.then(()=>{if(!state.connected)throw new Error('Connect the board first.');return operation();});
    tail=result.catch(()=>{});
    return result.finally(()=>{operations--;emit();});
  }
  async function chunks(prefix,count,expected,extra=''){
    const bytes=new Uint8Array(count);
    const stride=prefix==='companion'?Math.min(128,integer(caps.chunk,1,128,'snapshot chunk limit')):32;
    const pieces=Array.from({length:Math.ceil(count/stride)},(_,index)=>{const offset=index*stride,length=Math.min(stride,count-offset);return{offset,length,command:`${prefix} get ${extra}${offset} ${length}`};});
    const batchSize=typeof port?.exchangeMany==='function'?32:1,pattern=prefix==='companion'?/^COMPANION\/1 DATA /:/^BACKUP data /;
    for(let index=0;index<pieces.length;index+=batchSize){
      const batch=pieces.slice(index,index+batchSize);
      const lines=batchSize===1?[await exchange(batch[0].command,pattern)]:await nativeExchange(batch.map(piece=>piece.command),pattern);
      for(let partIndex=0;partIndex<batch.length;partIndex++){
      const {offset,length}=batch[partIndex],line=lines[partIndex];
      const fields=deviceFields(line,prefix==='companion'?'COMPANION/1 DATA':'BACKUP'),part=unhex(fields.hex||'');
      if(prefix==='companion'&&String(fields.seq)!==extra.trim())throw new Error('Device snapshot generation changed during transfer.');
      if(integer(fields.offset,0,count,'chunk offset')!==offset||part.length!==length)
        throw new Error('Device returned an unexpected chunk offset or length.');
      bytes.set(part,offset);
      if(prefix==='backup')emit(`Reading backup · ${offset+length}/${count} bytes`);
      }
    }
    if(deviceCRC32(bytes)!==expected)throw new Error('Device snapshot checksum does not match.');
    return bytes;
  }
  const backupStatus=async()=>{
    const fields=deviceFields(await exchange('backup status',/^BACKUP version=/),'BACKUP');
    if(fields.version!=='1'||!['idle','upload','download','applied','queued','saved-verified','error'].includes(fields.state))
      throw new Error('Unsupported backup service state.');
    return {...fields,bytes:integer(fields.bytes,0,BACKUP_MAX_BYTES,'backup length'),
      received:integer(fields.received,0,BACKUP_MAX_BYTES,'received length'),
      crc32:crcValue(fields.crc32),payloadCRC:crcValue(fields.payload_crc32),
      generation:integer(fields.generation,0,0xffffffff,'save generation')};
  };
  async function download(){
    let started=false;
    try{
      const result=deviceFields(await exchange('backup export',/^BACKUP export /),'BACKUP');started=true;
      const count=integer(result.bytes,65,BACKUP_MAX_BYTES,'backup length');
      const bytes=await chunks('backup',count,crcValue(result.crc32));
      const info=inspectDeviceBackup(bytes,backupCaps);
      if(info.settingsVersion!==Number(backupCaps.settings)||Number(result.settings)!==info.settingsVersion)
        throw new Error('The exported settings version differs from the device capabilities.');
      emit('Backup read · checksum verified');return bytes;
    }finally{if(started){try{await exchange('backup abort',/^BACKUP aborted$/);}catch{}}}
  }
  function canWrite(){if(!state.compatible)throw new Error('The device and this client use different menu versions. Controls are read-only.');}
  async function prepareBackupRestore(bytes){
    if(!state.connected||!backupCaps)throw new Error('Connect the board before preparing a backup.');
    // This step is entirely off-device. Keep the original file and provenance
    // intact, and reject a connection change before any caller can upload it.
    bytes=bytes.slice();
    const restoreIdentity=backupCaps,restorePort=port;
    const original=inspectDeviceBackup(bytes,restoreIdentity),originalHeader=bytes.slice(0,64),targetVersion=Number(restoreIdentity.settings);
    let info=original;
    if(original.settingsVersion!==targetVersion){
      if(!canMigrateBackupSettings(original.settingsVersion,targetVersion)||typeof migrateBackupPayload!=='function')
        throw new Error('This client cannot migrate that backup to the connected settings version. No restore was sent.');
      emit(`Preparing backup · upgrading settings V${original.settingsVersion} to V${targetVersion} with the shared C codec`);
      const payload=await migrateBackupPayload(original.payload.slice(),{sourceVersion:original.settingsVersion,targetVersion});
      if(!(payload instanceof Uint8Array)||!payload.length||payload.length+64>BACKUP_MAX_BYTES)
        throw new Error('The shared C settings migration returned an invalid payload. No restore was sent.');
      bytes=new Uint8Array(64+payload.length);bytes.set(originalHeader);bytes.set(payload,64);
      const header=new DataView(bytes.buffer);header.setUint32(8,payload.length,true);
      header.setUint32(12,deviceCRC32(payload),true);header.setUint16(16,targetVersion,true);
      header.setUint32(60,deviceCRC32(bytes.subarray(0,60)),true);
      info=inspectDeviceBackup(bytes,restoreIdentity);
    }
    if(!state.connected||port!==restorePort||backupCaps!==restoreIdentity)
      throw new Error('The device connection changed during backup preparation. No restore was sent.');
    return {bytes,info,original,targetVersion};
  }
  const client={
    get available(){return Boolean(serial?.requestPort);},get connected(){return state.connected;},
    get busy(){return operations>0;},get compatible(){return state.compatible;},
    get harmonySupported(){return state.connected&&caps?.harmony==='1'&&caps?.harmony_bytes==='800';},
    get performanceSupported(){return state.connected&&caps?.performance==='1'&&caps?.performance_bytes==='1024';},
    get padMapSupported(){return state.connected&&caps?.pad_map==='1'&&caps?.pad_map_bytes==='1672';},
    get resetSupported(){return state.connected&&caps?.reset==='1';},
    get identity(){return caps?{...caps}:null;},get backupIdentity(){return backupCaps?{...backupCaps}:null;},
    get status(){return {...state};},
    async connect(){
      if(state.connected)return {...caps};if(state.connecting)throw new Error('Connection is already being opened.');
      state.connecting=true;emit('Choose the TheoryBoard serial port');
      try{
        if(!serial?.requestPort)throw new Error('Web Serial is unavailable here. Board and Documentation still work offline; open this app in Chrome or Edge on localhost to connect.');
        if(port)await client.disconnect();
        port=await serial.requestPort({filters:[{usbVendorId:0xcafe,usbProductId:0x4009}]});
        const usb=port.getInfo?.();
        if(usb&&(usb.usbVendorId!==0xcafe||usb.usbProductId!==0x4009))throw new Error('The selected port is not a THY666 application interface.');
        await port.open({baudRate:115200});
        if(!port.exchange){reader=port.readable.getReader();writer=port.writable.getWriter();pump=readLoop();}
        caps=deviceFields(await exchange('companion caps',/^COMPANION\/1 CAPS /),'COMPANION/1 CAPS');
        if(!/^[0-9a-f]{24}$/i.test(caps.uid||'')||!/^[0-9a-f]{16}$/i.test(caps.build||'')||!/^[0-9a-f]{64}$/i.test(caps.schema||''))
          throw new Error('The selected device did not return a valid client identity.');
        if(caps.role!=='instrument'||caps.backup!=='1'||caps.song!=='1'||caps.companion!=='1')
          throw new Error('The selected firmware does not expose the required client services.');
        backupCaps=deviceFields(await exchange('backup caps',/^BACKUP version=/),'BACKUP');
        if(backupCaps.version!=='1'||backupCaps.uid?.toLowerCase()!==caps.uid.toLowerCase()||backupCaps.build!==caps.build||backupCaps.schema!==caps.schema)
          throw new Error('The backup and client identities disagree.');
        // writable describes temporary staging ownership (for example a journal
        // save), while schema compatibility survives that work completing.
        state.connected=true;state.identity={...caps};state.compatible=!expectedSchema||caps.schema===expectedSchema;
        emit(state.compatible?`Connected · ${caps.build.slice(0,8)}`:'Connected · menu version differs, read-only');return {...caps};
      }catch(error){await client.disconnect();throw error;}
      finally{state.connecting=false;emit();}
    },
    async disconnect(){
      closing=true;state.connected=false;state.compatible=false;rejectPending(new Error('Device disconnected.'));
      const oldReader=reader;reader=null;
      if(oldReader){try{await oldReader.cancel();}catch{}try{oldReader.releaseLock();}catch{}}
      if(pump){await pump;pump=null;}
      const oldWriter=writer;writer=null;if(oldWriter){try{oldWriter.releaseLock();}catch{}}
      const oldPort=port;port=null;if(oldPort){try{await oldPort.close();}catch{}}
      caps=backupCaps=null;padMapLease=null;lineBuffer='';closing=false;state.connected=false;state.compatible=false;state.identity=null;
      emit('No board connected');
    },
    snapshot(){return exclusive(async()=>{
      let generation=null;
      try{
        const result=deviceFields(await exchange('companion snapshot',/^COMPANION\/1 SNAPSHOT /),'COMPANION/1 SNAPSHOT');
        generation=integer(result.seq,1,0xffffffff,'snapshot generation');
        const count=integer(result.bytes,1024,4096,'snapshot length');
        const bytes=await chunks('companion',count,crcValue(result.crc32),generation+' ');
        return {generation,bytes,receivedAt:now()};
      }finally{if(generation!==null){try{await exchange(`companion release ${generation}`,/^COMPANION\/1 RELEASED/);}catch{}}}
    });},
    harmonySnapshot(){return exclusive(async()=>{
      if(!client.harmonySupported)throw new Error('This firmware does not expose the live Harmony workspace. Update it to use Harmony in the Client.');
      let generation=null;
      try{
        const result=deviceFields(await exchange('companion harmony snapshot',/^COMPANION\/1 HARMONY /),'COMPANION/1 HARMONY');
        generation=integer(result.seq,1,0xffffffff,'Harmony snapshot generation');
        const count=integer(result.bytes,800,800,'Harmony snapshot length');
        const bytes=await chunks('companion',count,crcValue(result.crc32),generation+' ');
        return {generation,bytes,receivedAt:now()};
      }finally{if(generation!==null){try{await exchange(`companion release ${generation}`,/^COMPANION\/1 RELEASED/);}catch{}}}
    });},
    performanceSnapshot(){const requestPort=port,requestCaps=caps;return exclusive(async()=>{
      if(port!==requestPort||caps!==requestCaps)throw new Error('The device connection changed during the performance request.');
      if(!client.performanceSupported)throw new Error('Detailed MPE and sequencer feedback requires newer firmware.');
      let generation=null;
      try{
        const result=deviceFields(await exchange('companion performance snapshot',/^COMPANION\/1 PERFORMANCE /),'COMPANION/1 PERFORMANCE');
        generation=integer(result.seq,1,0xffffffff,'performance snapshot generation');
        const count=integer(result.bytes,1024,1024,'performance snapshot length');
        const bytes=await chunks('companion',count,crcValue(result.crc32),generation+' ');
        return {generation,bytes,receivedAt:now()};
      }finally{if(generation!==null&&port===requestPort&&caps===requestCaps){try{await exchange(`companion release ${generation}`,/^COMPANION\/1 RELEASED/);}catch{}}}
    });},
    padMapSnapshot(knownRevision=null){const requestPort=port,requestCaps=caps;return exclusive(async()=>{
      if(port!==requestPort||caps!==requestCaps)throw new Error('The device connection changed during the pad-label request.');
      if(!client.padMapSupported)throw new Error('Labels require newer firmware.');
      if(knownRevision!==null)integer(knownRevision,0,0xffffffff,'pad-label revision');
      let keepLease=false;
      try{
        const command=`companion pad-map${knownRevision===null?'':` ${knownRevision}`}`;
        const result=deviceFields(await exchange(command,/^COMPANION\/1 PADMAP /),'COMPANION/1 PADMAP');
        if(result.unchanged==='1'){
          const revision=integer(result.revision,0,0xffffffff,'pad-label revision');
          if(knownRevision===null||revision!==knownRevision)throw new Error('The pad-label revision changed unexpectedly.');
          return {unchanged:true,revision};
        }
        const generation=integer(result.seq,1,0xffffffff,'pad-label snapshot generation');padMapLease=generation;
        if(result.pending==='1'){keepLease=true;return {pending:true,generation};}
        if(result.pending!==undefined)throw new Error('Unsupported pad-label snapshot state.');
        const count=integer(result.bytes,1672,1672,'pad-label snapshot length');
        const bytes=await chunks('companion',count,crcValue(result.crc32),generation+' ');
        return {pending:false,generation,bytes,receivedAt:now()};
      }finally{
        if(!keepLease&&padMapLease!==null&&port===requestPort&&caps===requestCaps){
          const generation=padMapLease;padMapLease=null;
          try{await exchange(`companion release ${generation}`,/^COMPANION\/1 RELEASED/);}catch{}
        }
      }
    });},
    cancelPadMap(){const requestPort=port,requestCaps=caps;return exclusive(async()=>{
      if(port!==requestPort||caps!==requestCaps||padMapLease===null)return false;
      const generation=padMapLease;padMapLease=null;
      await exchange(`companion release ${generation}`,/^COMPANION\/1 RELEASED/);return true;
    });},
    harmonyAction(action,value=0){return exclusive(async()=>{
      canWrite();if(!client.harmonySupported)throw new Error('This firmware does not expose the live Harmony workspace. Update it to use Harmony in the Client.');
      integer(action,0,10,'Harmony action');integer(value,-2147483648,2147483647,'Harmony value');
      const result=deviceFields(await exchange(`companion harmony ${caps.schema} ${action} ${value}`,/^COMPANION\/1 HARMONY /),'COMPANION/1 HARMONY');
      if(result.result!=='0')throw new Error(`The device declined this Harmony action (code ${result.result}).`);
      if(Number(result.action)!==action||Number(result.value)!==value)throw new Error('The Harmony acknowledgement does not match the action. Read the device state before continuing.');
      return result;
    });},
    harmonyControl(bank,index){return exclusive(async()=>{
      canWrite();if(!client.harmonySupported)throw new Error('This firmware does not expose the live Harmony workspace. Update it to use Harmony in the Client.');
      integer(bank,1,3,'Harmony control bank');integer(index,bank===1?7:0,bank===1?13:11,'Harmony control index');
      const result=deviceFields(await exchange(`companion harmony-control ${caps.schema} ${bank} ${index}`,/^COMPANION\/1 HCONTROL /),'COMPANION/1 HCONTROL');
      if(result.result!=='0')throw new Error(`The device declined this Harmony control (code ${result.result}).`);
      if(Number(result.bank)!==bank||Number(result.index)!==index)throw new Error('The Harmony control acknowledgement does not match the request.');
      return result;
    });},
    confirmReset(epoch,confirmed){const confirmationPort=port,confirmationCaps=caps;return exclusive(async()=>{
      canWrite();if(!client.resetSupported)throw new Error('This firmware does not expose protected reset confirmation.');
      if(port!==confirmationPort||caps!==confirmationCaps)throw new Error('The device connection changed. Review a fresh reset confirmation before continuing.');
      integer(epoch,1,0xffffffff,'reset confirmation identity');if(typeof confirmed!=='boolean')throw new Error('Reset confirmation must be explicit.');
      const value=Number(confirmed),result=deviceFields(await exchange(`companion reset-confirm ${caps.schema} ${epoch} ${value}`,/^COMPANION\/1 RESET /),'COMPANION/1 RESET');
      if(result.result!=='0')throw new Error(`The device could not complete this reset (code ${result.result}). Release pads and controls, then review its confirmation.`);
      if(Number(result.epoch)!==epoch||Number(result.confirmed)!==value)throw new Error('The reset acknowledgement does not match this confirmation. Read the device state before continuing.');
      return result;
    });},
    read(row,instance=255){return exclusive(async()=>{
      integer(row,0,65534,'menu row');integer(instance,0,255,'menu target');
      const result=deviceFields(await exchange(`companion read ${caps.schema} ${row} ${instance}`,/^COMPANION\/1 VALUE /),'COMPANION/1 VALUE');
      if(!/^-?\d+$/.test(result.value||''))throw new Error('Invalid setting value.');
      if(Number(result.row)!==row||(instance!==255&&Number(result.instance)!==instance))throw new Error('Device returned another setting or target.');
      if(!Number.isInteger(Number(result.value))||Number(result.value)< -2147483648||Number(result.value)>2147483647)throw new Error('Invalid setting value.');
      return {row:integer(result.row,0,65534,'menu row'),instance:integer(result.instance,0,254,'menu target'),value:Number(result.value)};
    });},
    set(row,instance,value){return exclusive(async()=>{
      canWrite();integer(row,0,65534,'menu row');integer(instance,0,254,'menu target');
      if(!Number.isInteger(value)||value< -2147483648||value>2147483647)throw new Error('Invalid setting value.');
      const result=deviceFields(await exchange(`companion set ${caps.schema} ${row} ${instance} ${value}`,/^COMPANION\/1 SET /),'COMPANION/1 SET');
      if(result.result!=='0')throw new Error(`The device did not apply this setting (code ${result.result}). Release affected pads and finish any device edit first.`);
      if(Number(result.row)!==row||Number(result.instance)!==instance)throw new Error('The setting acknowledgement does not match the edit; read the device state before continuing.');
      emit('Applied to device · autosave pending');return result;
    });},
    navigate(action){return exclusive(async()=>{
      canWrite();const id=['left','up','select','down','right'].indexOf(action);
      if(id<0)throw new Error('Unknown navigation control.');
      const result=deviceFields(await exchange(`companion nav ${caps.schema} ${id}`,/^COMPANION\/1 NAV /),'COMPANION/1 NAV');
      if(result.result!=='0')throw new Error('The device could not perform that navigation action.');return result;
    });},
    control(bank,index){return exclusive(async()=>{
      canWrite();integer(bank,0,3,'control bank');integer(index,0,[5,14,12,12][bank]-1,'control index');
      const result=deviceFields(await exchange(`companion control ${caps.schema} ${bank} ${index}`,/^COMPANION\/1 CONTROL /),'COMPANION/1 CONTROL');
      if(result.result!=='0')throw new Error('The device declined this control. Release it on the hardware and finish any active edit first.');return result;
    });},
    action(row,instance=0){return exclusive(async()=>{
      canWrite();integer(row,0,65534,'menu row');integer(instance,0,254,'menu target');
      const result=deviceFields(await exchange(`companion action ${caps.schema} ${row} ${instance}`,/^COMPANION\/1 ACTION /),'COMPANION/1 ACTION');
      if(result.result!=='0')throw new Error(`The device declined this action (code ${result.result}).`);return result;
    });},
    inspectPad(pad){return exclusive(()=>exchange(`companion pad ${integer(pad,0,95,'pad')}`,/^COMPANION\/1 PAD /));},
    telemetry(command){
      if(!/^(?:status|io|settings|display|pad (?:[0-8]?\d|9[0-5])|velocity (?:[0-8]?\d|9[0-5]))$/.test(command))
        return Promise.reject(new Error('Only named read-only telemetry commands are supported.'));
      return exclusive(()=>exchange(command,/^(?:STATUS|IO|SETTINGS|DISPLAY|PAD|VELOCITY) /));
    },
    downloadBackup(){return exclusive(download);},
    async prepareBackupRestore(bytes){
      const {info,original,targetVersion}=await prepareBackupRestore(bytes);
      return {sourceSettingsVersion:original.settingsVersion,settingsVersion:targetVersion,
        sourcePayloadCRC:original.payloadCRC,payloadCRC:info.payloadCRC,migrated:original.settingsVersion!==targetVersion};
    },
    restoreBackup(bytes){return exclusive(async()=>{
      // Capture both bytes and identity before asynchronous migration. The
      // original file and its provenance remain unchanged outside this call.
      const prepared=await prepareBackupRestore(bytes),{info,original,targetVersion}=prepared;bytes=prepared.bytes;
      let uploading=false,applied=false;
      try{
        const before=await backupStatus();if(!['idle','saved-verified','error'].includes(before.state))throw new Error('Device backup service is busy.');
        const ready=deviceFields(await exchange(`backup begin ${bytes.length} ${deviceCRC32(bytes)}`,/^BACKUP ready /),'BACKUP');
        uploading=true;if(Number(ready.bytes)!==bytes.length)throw new Error('Unexpected accepted backup size.');
        for(let offset=0;offset<bytes.length;offset+=32){const part=bytes.subarray(offset,offset+32);
          const reply=deviceFields(await exchange(`backup put ${offset} ${hex(part)}`,/^BACKUP received=/),'BACKUP');
          if(Number(reply.received)!==offset+part.length)throw new Error('Backup upload acknowledgement differs.');
          emit(`Sending backup · ${offset+part.length}/${bytes.length} bytes`);
        }
        const reply=deviceFields(await exchange('backup commit',/^BACKUP applied /),'BACKUP');
        applied=true;uploading=false;
        if(Number(reply.bytes)!==bytes.length||crcValue(reply.payload_crc32)!==info.payloadCRC)
          throw new Error('Applied backup identity differs; inspect the device before continuing.');
        emit('Applied to device · waiting for verified save');
        const deadline=now()+30000;let status;
        do{status=await backupStatus();
          if(status.state==='error')throw new Error('Settings were applied but saving failed. Keep the backup file.');
          if(status.state==='saved-verified')break;
          await sleep(150);
        }while(now()<deadline);
        if(status.state!=='saved-verified'||status.payloadCRC!==info.payloadCRC)
          throw new Error('Settings were applied, but the saved state could not be verified before timeout. Keep the backup file.');
        const checked=inspectDeviceBackup(await download(),backupCaps);
        if(checked.payload.length!==info.payload.length||checked.payload.some((byte,index)=>byte!==info.payload[index]))
          throw new Error('Saved settings re-export differs from the restored payload.');
        emit('Restored · saved and read back exactly');return {generation:status.generation,payloadCRC:info.payloadCRC,
          sourcePayloadCRC:original.payloadCRC,sourceSettingsVersion:original.settingsVersion,settingsVersion:targetVersion,
          migrated:original.settingsVersion!==targetVersion,verified:true};
      }finally{if(uploading&&!applied){try{await exchange('backup abort',/^BACKUP aborted$/);}catch{}}}
    });},
  };
  serial?.addEventListener?.('disconnect',event=>{if(event.target===port||event.port===port)void client.disconnect();});
  return client;
}
