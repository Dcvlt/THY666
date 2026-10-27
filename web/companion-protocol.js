/* SPDX-License-Identifier: GPL-3.0-or-later — frozen COMPANION/1 visual snapshot. */
export const COMPANION_SNAPSHOT_BYTES=1680;
const string=(bytes,offset,length)=>{
  const part=bytes.subarray(offset,offset+length),end=part.indexOf(0);
  return new TextDecoder('ascii').decode(end<0?part:part.subarray(0,end));
};
const mask=(bytes,offset,count)=>Array.from({length:count},(_,n)=>Boolean(bytes[offset+(n>>>3)]&(1<<(n&7))));
export function decodeCompanionSnapshot(bytes,identity=null){
  if(!(bytes instanceof Uint8Array)||bytes.length!==COMPANION_SNAPSHOT_BYTES)
    throw new Error('Unsupported companion snapshot size.');
  const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  if(string(bytes,0,8)!=='THYCMP1'||data.getUint16(8,true)!==1||data.getUint16(10,true)!==bytes.length)
    throw new Error('Unsupported companion snapshot format.');
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
    cursor:bytes[1659],selectedTrack:bytes[1660],view:bytes[1661],engine:bytes[1662],currentValue:data.getInt32(1664,true)};
  if(!/^[0-9a-f]{16}$/i.test(result.build)||!/^[0-9a-f]{24}$/i.test(result.uid)||!/^[0-9a-f]{64}$/i.test(result.schema))
    throw new Error('Invalid snapshot identity.');
  if(identity&&(result.uid.toLowerCase()!==identity.uid?.toLowerCase()||result.build!==identity.build||result.schema!==identity.schema))
    throw new Error('Snapshot does not match the connected device identity.');
  if(result.root>11||result.heldPads>96||result.noteCount>128||(result.inspect.logical>95&&result.inspect.logical!==255)||
    (result.inspect.electrical>95&&result.inspect.electrical!==255))
    throw new Error('Invalid live snapshot fields.');
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
