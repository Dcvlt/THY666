// Synthetic wire data only; never bundled or used as disconnected device data.
export const performanceIdentity={uid:'0123456789abcdef01234567',build:'90d128d90324c978',schema:'ab'.repeat(32)};
export function performancePacket({voices=0,tracks=0,upperZone=false,position=123456n,sequence=1}={}){
  const b=new Uint8Array(1024),d=new DataView(b.buffer),ascii=(offset,value)=>b.set(new TextEncoder().encode(value),offset);
  ascii(0,'THYPER1');d.setUint16(8,1,true);d.setUint16(10,1024,true);d.setUint32(12,sequence,true);d.setUint32(16,1234,true);
  d.setUint32(20,19,true);d.setUint32(24,17,true);d.setUint16(28,960,true);b[30]=48;b[31]=upperZone?15:0;
  ascii(32,performanceIdentity.build);ascii(48,performanceIdentity.uid);ascii(72,performanceIdentity.schema);
  d.setBigUint64(136,position,true);d.setUint32(144,21,true);d.setUint32(148,120000,true);
  b[152]=19|(upperZone?4:0);b[153]=1;b[154]=0;b[155]=0;b[156]=0;b[157]=255;b[158]=0;b[159]=15;
  for(let slot=0;slot<15;slot++){
    const o=160+24*slot;for(const offset of [1,2,3,4,18,19])b[o+offset]=255;
    if(slot>=voices)continue;
    b[o]=slot%4===3?2:1;b[o+1]=upperZone?14-slot:slot+1;b[o+2]=60+slot;b[o+3]=slot%2?48+slot:slot;b[o+4]=b[o+3];b[o+5]=5;b[o+6]=slot%4===0?0:5;
    b[o+7]=slot%4===0?1:slot%4===3?2:slot%4===2?4:0;b[o+8]=80+slot;b[o+9]=slot%3;b[o+10]=slot*8;b[o+11]=slot*7;
    d.setUint16(o+12,8192+slot*200,true);b[o+14]=Math.min(127,slot*8+3);b[o+15]=Math.min(127,slot*7+2);d.setUint16(o+16,8192+slot*200+100,true);b[o+18]=b[o+2];b[o+19]=255;d.setUint32(o+20,100+slot,true);
  }
  for(let index=0;index<tracks;index++){
    const o=520+24*index;d.setUint32(o,index*240,true);d.setUint32(o+4,3840,true);d.setUint32(o+8,index*240+index*120,true);d.setUint32(o+12,240,true);d.setUint32(o+16,10+index,true);
    b[o+20]=35|(index%3?4:0)|(index%4===1?8:0)|(index%5===2?16:0);b[o+21]=index%7===1?1:0;b[o+22]=index%3;
  }
  return b;
}
