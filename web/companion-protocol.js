/* SPDX-License-Identifier: GPL-3.0-or-later — frozen COMPANION/1 visual snapshot. */
export const COMPANION_SNAPSHOT_BYTES=1680;
export const HARMONY_SNAPSHOT_BYTES=800;
const string=(bytes,offset,length)=>{
  const part=bytes.subarray(offset,offset+length),end=part.indexOf(0);
  return new TextDecoder('ascii').decode(end<0?part:part.subarray(0,end));
};
const mask=(bytes,offset,count)=>Array.from({length:count},(_,n)=>Boolean(bytes[offset+(n>>>3)]&(1<<(n&7))));
export function decodeCompanionSnapshot(bytes,identity=null){
  if(!(bytes instanceof Uint8Array)||bytes.length!==COMPANION_SNAPSHOT_BYTES)
    throw new Error('Unsupported client snapshot size.');
  const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  if(string(bytes,0,8)!=='THYCMP1'||data.getUint16(8,true)!==1||data.getUint16(10,true)!==bytes.length)
    throw new Error('Unsupported client snapshot format.');
  const result={sequence:data.getUint32(12,true),now:data.getUint32(16,true),frameMs:data.getUint32(20,true),
    instrumentRevision:data.getUint32(24,true),stateRevision:data.getUint32(28,true),
    build:string(bytes,32,16),uid:string(bytes,48,24),schema:string(bytes,72,64),
    frame:bytes.slice(136,1160),rgb:Array.from({length:96},(_,n)=>Array.from(bytes.subarray(1160+n*3,1163+n*3))),
    center:Array.from(bytes.subarray(1448,1467)),held:mask(bytes,1467,96),controlHeld:mask(bytes,1479,43),
    sounding:mask(bytes,1485,128).flatMap((on,n)=>on?[n]:[]),
    pool:mask(bytes,1501,128).flatMap((on,n)=>on?[n]:[]),
    harmony:string(bytes,1517,32),scale:string(bytes,1549,32),
    group:bytes[1581],level:bytes[1582],editing:Boolean(bytes[1583]),notice:bytes[1584],
    info:Boolean(bytes[1585]),about:Boolean(bytes[1586]),design:Boolean(bytes[1587]),
    arpQuick:Boolean(bytes[1588]),sequenceOpen:Boolean(bytes[1589]),trackOptions:Boolean(bytes[1590]),
    currentRow:data.getUint16(1591,true),editRow:data.getUint16(1593,true),editInstance:bytes[1595],
    currentInstance:bytes[1596],sensitivityTarget:bytes[1597],velocityTarget:bytes[1598],routeTarget:bytes[1599],
    editValue:data.getInt32(1600,true),mpe:Boolean(bytes[1604]&1),arp1:Boolean(bytes[1604]&2),arp2:Boolean(bytes[1604]&4),
    running:Boolean(bytes[1604]&8),recording:Boolean(bytes[1604]&16),root:bytes[1605],scaleIndex:data.getUint16(1606,true),
    pressure:[bytes[1608],bytes[1609]],aftertouchStage:[Boolean(bytes[1610]),Boolean(bytes[1611])],
    fixedVelocity:[bytes[1612],bytes[1613]],velocityMode:[bytes[1614],bytes[1615]],heldVelocity:[bytes[1616],bytes[1617]],
    heldPads:bytes[1618],noteCount:bytes[1619],voices:data.getUint16(1620,true),generated:data.getUint16(1622,true),
    tempo:data.getUint32(1624,true)/1000,clockSource:bytes[1628],clockStatus:bytes[1629],transport:bytes[1630],capture:bytes[1631],
    inspect:{logical:bytes[1632],electrical:bytes[1633],raw:data.getUint16(1634,true),delta:data.getInt16(1636,true),
      pressure:bytes[1638],flags:bytes[1639],strike:bytes[1640],profile:bytes[1641],
      trigger:data.getUint16(1642,true),release:data.getUint16(1644,true),sampleMs:data.getUint32(1646,true)},
    layoutRevision:data.getUint32(1650,true),savedGeneration:data.getUint32(1654,true),panelVisible:Boolean(bytes[1658]),
    cursor:bytes[1659],selectedTrack:bytes[1660],view:bytes[1661],engine:bytes[1662],currentValue:data.getInt32(1664,true),
    clusterSwapped:Boolean(bytes[1663]),harmonyRevision:data.getUint32(1668,true),resetOpen:Boolean(bytes[1672]),resetChoice:bytes[1673],resetGroup:bytes[1674],resetTrack:bytes[1675],resetEpoch:data.getUint32(1676,true)};
  if(!/^[0-9a-f]{16}$/i.test(result.build)||!/^[0-9a-f]{24}$/i.test(result.uid)||!/^[0-9a-f]{64}$/i.test(result.schema))
    throw new Error('Invalid snapshot identity.');
  if(identity&&(result.uid.toLowerCase()!==identity.uid?.toLowerCase()||result.build!==identity.build||result.schema!==identity.schema))
    throw new Error('Snapshot does not match the connected device identity.');
  if(bytes[1663]>1||bytes[1672]>1||result.resetChoice>1||result.resetOpen&&(!result.resetEpoch||result.resetGroup>=32||result.resetTrack>20&&result.resetTrack!==255)||
    result.root>11||result.heldPads>96||result.noteCount>128||(result.inspect.logical>95&&result.inspect.logical!==255)||
    (result.inspect.electrical>95&&result.inspect.electrical!==255))
    throw new Error('Invalid live snapshot fields.');
  return result;
}
// Optional cached workspace extension. Names and recommendations come from C;
// this decoder only checks framing and bounded fields before projecting them.
export function decodeHarmonySnapshot(bytes,identity=null){
  if(!(bytes instanceof Uint8Array)||bytes.length!==HARMONY_SNAPSHOT_BYTES)
    throw new Error('Unsupported Harmony snapshot size.');
  if(bytes[748]>4)throw new Error('Invalid Harmony history count.');
  const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  if(string(bytes,0,8)!=='THYHRM1'||data.getUint16(8,true)!==4||data.getUint16(10,true)!==bytes.length)
    throw new Error('Unsupported Harmony snapshot format.');
  const u16=offset=>data.getUint16(offset,true);
  const result={sequence:data.getUint32(12,true),now:data.getUint32(16,true),build:string(bytes,32,16),
    uid:string(bytes,48,24),schema:string(bytes,72,64),revision:data.getUint32(136,true),
    keyMask:u16(140),inputMask:u16(142),targetMask:u16(144),common:u16(146),added:u16(148),removed:u16(150),pivot:u16(152),total:u16(154),
    active:Boolean(bytes[156]),page:bytes[157],sourceRoot:bytes[158],sourceFamily:bytes[159],targetRoot:bytes[160],targetFamily:bytes[161],
    cursor:bytes[162],count:bytes[163],pinned:Boolean(bytes[164]&1),cacheReady:Boolean(bytes[164]&2),
    pivotRoot:bytes[165],currentFits:Boolean(bytes[166]),canApply:Boolean(bytes[167]),currentPad:bytes[168],selectedSuggestion:bytes[169],
    targetCatalog:bytes[170],currentRoot:bytes[171],currentPads:mask(bytes,172,48),selectedPads:mask(bytes,178,48),suggestedPads:mask(bytes,184,48),
    direction:bytes[27],padMap:bytes[28],hintContrast:bytes[29],viewPicker:bytes[30],pattern:bytes[190],highlights:Boolean(bytes[191]&1),motion:Boolean(bytes[191]&2),
    inputLabel:string(bytes,192,32),keyLabel:string(bytes,224,32),targetLabel:string(bytes,256,32),selectedLabel:string(bytes,288,32),
    baseMask:u16(512),rightMask:u16(514),view:bytes[516],controlBank:bytes[517],octaveTarget:bytes[518],rightMode:bytes[519],analysis:bytes[520],
    bassRoot:Boolean(bytes[521]),lowNote:bytes[522],highNote:bytes[523],clusterSwapped:Boolean(bytes[524]),chordLayers:bytes[525],noteLayers:bytes[526],
    physicalCurrentPads:mask(bytes,528,96),physicalSelectedPads:mask(bytes,540,96),physicalSuggestedPads:mask(bytes,552,96),
    scaleButtons:Array.from(bytes.subarray(564,571)),previewScale:bytes[571],voicing:bytes[572],suggestionCount:bytes[573],
    chordOctave:data.getInt8(574),noteOctave:data.getInt8(575),baseLabel:string(bytes,576,32),melodyLabel:string(bytes,608,32),
    padMasks:Array.from({length:48},(_,n)=>u16(640+n*2)),history:Array.from({length:bytes[748]},(_,n)=>({mask:u16(736+n*2),root:bytes[744+n]})),
    outside:bytes[749],stayInKey:Boolean(bytes[750]),lockMap:Boolean(bytes[751]),palette:bytes[752],pending:Boolean(bytes[753]),reason:string(bytes,768,32)};
  if(!/^[0-9a-f]{16}$/i.test(result.build)||!/^[0-9a-f]{24}$/i.test(result.uid)||!/^[0-9a-f]{64}$/i.test(result.schema)||
    identity&&(result.uid.toLowerCase()!==identity.uid?.toLowerCase()||result.build!==identity.build||result.schema!==identity.schema))
    throw new Error('Harmony snapshot does not match the connected device identity.');
  const root=value=>value<12||value===255,pad=value=>value<48||value===255;
  if(result.page>2||result.sourceRoot>11||result.targetRoot>11||result.sourceFamily>99||result.targetFamily>98||result.count>(result.page===0?9:result.page===1?24:0)||
    result.count&&result.cursor>=result.count||!root(result.currentRoot)||!root(result.pivotRoot)||!pad(result.currentPad)||
    result.selectedSuggestion>8&&result.selectedSuggestion!==255||result.targetCatalog>69&&result.targetCatalog!==255||bytes[164]&~3||bytes[191]&~3||
    bytes[156]>1||bytes[166]>1||bytes[167]>1||result.pattern>3||
    result.view>3||result.controlBank>1||result.octaveTarget>1||result.rightMode>1||result.analysis>2||bytes[521]>1||bytes[524]>1||result.voicing>4||
    result.previewScale>6&&result.previewScale!==255||result.scaleButtons.some(value=>value>78)||
    result.direction>3||result.padMap>1||result.hintContrast>100||result.viewPicker>4||result.suggestionCount>9||Math.abs(result.chordOctave)>3||Math.abs(result.noteOctave)>3||
    bytes[748]>4||result.outside>100||bytes[750]>1||bytes[751]>1||result.palette>4||bytes[753]>1||
    result.padMasks.some(value=>value>4095)||result.history.some(item=>item.mask>4095||item.root>11)||
    result.lowNote>result.highNote||result.highNote>127||result.chordLayers>127||result.noteLayers>127||
    [result.keyMask,result.inputMask,result.targetMask,result.common,result.added,result.removed,result.pivot,result.baseMask,result.rightMask].some(value=>value>4095))
    throw new Error('Invalid Harmony snapshot fields.');
  result.rows=Array.from({length:result.count},(_,n)=>{
    const offset=320+n*8,row={mask:u16(offset),root:bytes[offset+2],shared:bytes[offset+3],degree:bytes[offset+4],reason:bytes[offset+5],pad:bytes[offset+6]};
    if(row.mask>4095||row.root>11||bytes[offset+7]!==1||result.page===0&&(row.shared>12||row.reason>7||!pad(row.pad))||result.page===1&&(row.shared>98||row.degree>12||row.reason!==255||row.pad!==255))
      throw new Error('Invalid Harmony recommendation.');
    if(result.page===1){row.family=row.shared;row.extra=row.degree;delete row.shared;delete row.degree;}
    return row;
  });
  return result;
}
export const PERFORMANCE_SNAPSHOT_BYTES=1024;
/** Frozen device positions and MIDI-FIFO-accepted member values; never a JS clock. */
export function decodePerformanceSnapshot(bytes,identity=null){
  if(!(bytes instanceof Uint8Array)||bytes.length!==PERFORMANCE_SNAPSHOT_BYTES)
    throw new Error('Unsupported performance snapshot size.');
  const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),u16=offset=>data.getUint16(offset,true),u32=offset=>data.getUint32(offset,true);
  if(string(bytes,0,8)!=='THYPER1'||u16(8)!==1||u16(10)!==bytes.length)
    throw new Error('Unsupported performance snapshot format.');
  const result={sequence:u32(12),now:u32(16),revision:u32(20),transportGeneration:u32(24),ppqn:u16(28),bendRange:bytes[30],managerChannel:bytes[31],
    build:string(bytes,32,16),uid:string(bytes,48,24),schema:string(bytes,72,64),
    position:data.getBigUint64(136,true),clockGeneration:u32(144),tempo:u32(148)/1000,
    available:Boolean(bytes[152]&1),mpe:Boolean(bytes[152]&2),upperZone:Boolean(bytes[152]&4),suspended:Boolean(bytes[152]&8),clockReady:Boolean(bytes[152]&16),
    transport:bytes[153],clockSource:bytes[154],clockStatus:bytes[155],capture:bytes[156],captureTrack:bytes[157],selectedTrack:bytes[158],members:bytes[159]};
  if(!/^[0-9a-f]{16}$/i.test(result.build)||!/^[0-9a-f]{24}$/i.test(result.uid)||!/^[0-9a-f]{64}$/i.test(result.schema)||
    identity&&(result.uid.toLowerCase()!==identity.uid?.toLowerCase()||result.build!==identity.build||result.schema!==identity.schema))
    throw new Error('Performance snapshot does not match the connected device identity.');
  if(!result.sequence||result.ppqn!==960||bytes[152]&~31||result.transport>2||result.clockSource>1||result.clockStatus>3||result.capture>5||
    result.captureTrack>20&&result.captureTrack!==255||result.selectedTrack>20||result.members>15||
    (result.available?result.bendRange<1||result.bendRange>96||result.members<1||result.managerChannel!==(result.upperZone?15:0):result.bendRange!==0||result.managerChannel!==255||result.members!==0||result.mpe||result.upperZone)||
    result.capture===0&&result.captureTrack!==255)
    throw new Error('Invalid performance snapshot fields.');
  const channels=new Set();
  result.voices=Array.from({length:15},(_,slot)=>{
    const offset=160+slot*24,v={slot,state:bytes[offset],channel:bytes[offset+1],note:bytes[offset+2],pad:bytes[offset+3],logicalPad:bytes[offset+4],
      destinations:bytes[offset+5],soundingDestinations:bytes[offset+6],pendingAttack:Boolean(bytes[offset+7]&1),sustained:Boolean(bytes[offset+7]&2),
      pendingOutput:Boolean(bytes[offset+7]&4),gliding:Boolean(bytes[offset+7]&8),velocity:bytes[offset+8],output:bytes[offset+9],
      pressure:bytes[offset+10],upper:bytes[offset+11],bend:u16(offset+12),desiredPressure:bytes[offset+14],desiredUpper:bytes[offset+15],desiredBend:u16(offset+16),
      referenceNote:bytes[offset+18],modifierPad:bytes[offset+19],gesture:u32(offset+20)};
    if(v.state===0){
      for(let n=0;n<24;n++)if(bytes[offset+n]!==([1,2,3,4,18,19].includes(n)?255:0))throw new Error('Invalid free MPE member.');
    }else if(v.state>2||slot>=result.members||v.channel!==(result.upperZone?14-slot:slot+1)||v.channel>15||v.note>127||v.pad>95||v.logicalPad>95||v.destinations<1||v.destinations>7||v.soundingDestinations&~v.destinations||
      bytes[offset+7]&~15||v.velocity<1||v.velocity>127||v.output>2||[v.pressure,v.upper,v.desiredPressure,v.desiredUpper].some(value=>value>127)||
      v.bend>16383||v.desiredBend>16383||v.referenceNote>127||v.modifierPad>95&&v.modifierPad!==255||channels.has(v.channel)||!result.available)
      throw new Error('Invalid MPE member telemetry.');
    if(v.state)channels.add(v.channel);return v;
  });
  result.tracks=Array.from({length:21},(_,index)=>{
    const offset=520+index*24,flags=bytes[offset+20],track={index,loopStart:u32(offset),loopLength:u32(offset+4),playhead:u32(offset+8),gridTicks:u32(offset+12),
      generation:u32(offset+16),created:Boolean(flags&1),enabled:Boolean(flags&2),running:Boolean(flags&4),muted:Boolean(flags&8),oneShot:Boolean(flags&16),
      validPosition:Boolean(flags&32),pending:bytes[offset+21],processor:bytes[offset+22],capture:bytes[offset+23]};
    if(!track.created){if(bytes.subarray(offset,offset+24).some(Boolean))throw new Error('Invalid absent sequencer track.');}
    else if(flags&~63||track.pending>2||track.processor>2||track.capture>5||
      track.capture!==(result.captureTrack===index?result.capture:0)||
      track.validPosition&&(!track.loopLength||!track.gridTicks||track.loopStart+track.loopLength>0xffffffff||track.playhead<track.loopStart||track.playhead>=track.loopStart+track.loopLength))
      throw new Error('Invalid sequencer track telemetry.');
    return track;
  });
  return result;
}
export const PAD_MAP_SNAPSHOT_BYTES=1672;
export function decodePadMapSnapshot(bytes,identity=null){
  if(!(bytes instanceof Uint8Array)||bytes.length!==PAD_MAP_SNAPSHOT_BYTES)
    throw new Error('Unsupported pad-label snapshot size.');
  const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  if(string(bytes,0,8)!=='THYMAP1'||data.getUint16(8,true)!==1||data.getUint16(10,true)!==bytes.length)
    throw new Error('Unsupported pad-label snapshot format.');
  const result={sequence:data.getUint32(12,true),now:data.getUint32(16,true),revision:data.getUint32(20,true),
    layoutRevision:data.getUint32(24,true),stateRevision:data.getUint32(28,true),
    build:string(bytes,32,16),uid:string(bytes,48,24),schema:string(bytes,72,64)};
  if(!/^[0-9a-f]{16}$/i.test(result.build)||!/^[0-9a-f]{24}$/i.test(result.uid)||!/^[0-9a-f]{64}$/i.test(result.schema)||
    identity&&(result.uid.toLowerCase()!==identity.uid?.toLowerCase()||result.build!==identity.build||result.schema!==identity.schema))
    throw new Error('Pad-label snapshot does not match the connected device identity.');
  result.pads=Array.from({length:96},(_,pad)=>{
    const offset=136+pad*16,kind=bytes[offset],text=bytes.subarray(offset+1,offset+16),end=text.indexOf(0);
    if(kind>4||end<0||text.subarray(0,end).some(byte=>byte<32||byte>126))
      throw new Error('Invalid pad-label record.');
    return {pad,kind,label:String.fromCharCode(...text.subarray(0,end))};
  });
  return result;
}
export const companionNoteName=note=>['C','C♯','D','E♭','E','F','F♯','G','A♭','A','B♭','B'][note%12]+(Math.floor(note/12)-1);
export function paintDeviceOLED(canvas,frame){
  if(!canvas||!(frame instanceof Uint8Array)||frame.length!==1024)return;
  const context=canvas.getContext('2d'),pixels=context.createImageData(128,64);
  for(let y=0;y<64;y++)for(let x=0;x<128;x++){
    const lit=frame[(y>>>3)*128+x]&(1<<(y&7)),at=(y*128+x)*4;
    pixels.data[at]=lit?207:0;pixels.data[at+1]=lit?232:0;pixels.data[at+2]=lit?225:0;pixels.data[at+3]=255;
  }
  context.putImageData(pixels,0,0);
}
