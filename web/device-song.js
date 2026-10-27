/* SPDX-License-Identifier: GPL-3.0-or-later
 * Explicit SONG/1 transfers only. No connection or upload occurs on page load.
 */
export function createDeviceSongClient({serial=globalThis.navigator?.serial,timeoutMs=3000,onStatus=()=>{}}={}) {
  const encoder=new TextEncoder(),decoder=new TextDecoder();
  let port=null,reader=null,writer=null,pending=null,pump=null,lineBuffer='',busy=false;
  const crc32=bytes=>{let crc=0xffffffff;for(const byte of bytes){crc^=byte;for(let bit=0;bit<8;bit++)crc=(crc>>>1)^((-(crc&1))&0xedb88320);}return(~crc)>>>0;};
  const rejectPending=error=>{if(pending){clearTimeout(pending.timer);const current=pending;pending=null;current.reject(error);}};
  const line=line=>{
    line=line.trim();if(!line||!pending)return;
    if(line.startsWith('ERR song-')){rejectPending(new Error(line));return;}
    if(!pending.pattern.test(line))return; // Banner and ordinary telemetry may interleave.
    const current=pending;pending=null;clearTimeout(current.timer);current.resolve(line);
  };
  async function readLoop(){
    try{while(reader){const {value,done}=await reader.read();if(done)break;lineBuffer+=decoder.decode(value,{stream:true});
      if(lineBuffer.length>8192)throw new Error('Device response exceeded the bounded line buffer.');
      let end;while((end=lineBuffer.indexOf('\n'))>=0){line(lineBuffer.slice(0,end));lineBuffer=lineBuffer.slice(end+1);}
    }}catch(error){rejectPending(error);}finally{rejectPending(new Error('Device serial stream closed.'));}
  }
  async function exchange(command,pattern){
    if(!writer||!reader)throw new Error('Connect the TheoryBoard first.');
    if(pending)throw new Error('Another song command is in flight.');
    if(command.length>=96||!command.startsWith('song '))throw new Error('Invalid song command.');
    let request;
    const fail=error=>{if(pending===request)rejectPending(error);};
    const response=new Promise((resolve,reject)=>{const timer=setTimeout(()=>fail(new Error('Device song response timed out.')),timeoutMs);request={resolve,reject,timer,pattern};pending=request;});
    // A disconnected USB writer may never settle. The response timer must
    // bound the command even while write() is pending, including cleanup.
    try{Promise.resolve(writer.write(encoder.encode(command+'\n'))).catch(fail);}catch(error){fail(error);}
    return response;
  }
  const parseStatus=line=>{const match=/^SONG version=(\d+) state=(idle|upload|download) bytes=(\d+) received=(\d+) crc32=([0-9a-fA-F]{8})$/.exec(line);
    if(!match||Number(match[1])!==1)throw new Error('This device does not support SONG/1.');
    return{version:1,state:match[2],bytes:Number(match[3]),received:Number(match[4]),crc32:parseInt(match[5],16)};};
  const status=async()=>parseStatus(await exchange('song status',/^SONG version=/));
  async function exclusive(operation){if(busy)throw new Error('A device song transfer is already running.');busy=true;try{return await operation();}finally{busy=false;}}
  const cleanup=async()=>{if(writer&&reader){try{await exchange('song abort',/^SONG aborted$/);}catch{/* Device expires abandoned staging after ten seconds. */}}};
  const client={
    get connected(){return Boolean(port&&reader&&writer);},get busy(){return busy;},get available(){return Boolean(serial?.requestPort);},
    async connect(){
      if(client.connected)return status();
      if(!serial?.requestPort)throw new Error('Web Serial is unavailable. Open this localhost preview in a browser with Web Serial.');
      port=await serial.requestPort({filters:[{usbVendorId:0xcafe,usbProductId:0x4009}]});
      try{await port.open({baudRate:115200});reader=port.readable.getReader();writer=port.writable.getWriter();pump=readLoop();const result=await status();onStatus('Connected / SONG 1');return result;}
      catch(error){await client.disconnect();throw error;}
    },
    async disconnect(){
      rejectPending(new Error('Device disconnected.'));
      const oldReader=reader;reader=null;if(oldReader){try{await oldReader.cancel();}catch{}try{oldReader.releaseLock();}catch{}}
      if(pump){await pump;pump=null;}
      const oldWriter=writer;writer=null;if(oldWriter){try{oldWriter.releaseLock();}catch{}}
      const oldPort=port;port=null;if(oldPort){try{await oldPort.close();}catch{}}
      lineBuffer='';onStatus('Disconnected');
    },
    status,
    async upload(bytes){return exclusive(async()=>{
      if(!(bytes instanceof Uint8Array)||bytes.length<536||bytes.length>8812)throw new Error('Song size is outside the device bounds.');
      const before=await status();if(before.state!=='idle')throw new Error('Device song service is busy; no transfer was started.');
      let started=false;
      try{
        const ready=await exchange(`song begin ${bytes.length} ${crc32(bytes)}`,/^SONG ready bytes=\d+$/);started=true;
        if(Number(ready.split('=')[1])!==bytes.length)throw new Error('Device accepted an unexpected song length.');
        for(let offset=0;offset<bytes.length;offset+=32){const part=bytes.subarray(offset,offset+32),hex=Array.from(part,byte=>byte.toString(16).padStart(2,'0')).join('');
          const response=await exchange(`song put ${offset} ${hex}`,/^SONG received=\d+$/);
          if(Number(response.split('=')[1])!==offset+part.length)throw new Error('Device song offset acknowledgement differs.');
          onStatus(`Uploading ${offset+part.length}/${bytes.length}`);
        }
        const result=await exchange('song commit',/^SONG committed bytes=\d+$/);
        if(Number(result.split('=')[1])!==bytes.length)throw new Error('Device committed an unexpected song length.');
        started=false;onStatus('Song committed / save queued');return{bytes:bytes.length,crc32:crc32(bytes)};
      }finally{if(started)await cleanup();}
    });},
    async download(){return exclusive(async()=>{
      const before=await status();if(before.state!=='idle')throw new Error('Device song service is busy; no transfer was started.');
      let started=false;
      try{
        const response=await exchange('song export',/^SONG export bytes=\d+ crc32=[0-9a-fA-F]{8}$/);started=true;
        const match=/bytes=(\d+) crc32=([0-9a-fA-F]{8})$/.exec(response),count=Number(match[1]),expected=parseInt(match[2],16);
        if(count<536||count>8812)throw new Error('Device export length exceeds song bounds.');
        const bytes=new Uint8Array(count);
        for(let offset=0;offset<count;offset+=32){const length=Math.min(32,count-offset),result=await exchange(`song get ${offset} ${length}`,/^SONG data offset=\d+ hex=[0-9a-fA-F]+$/);
          const chunk=/offset=(\d+) hex=([0-9a-fA-F]+)$/.exec(result);
          if(Number(chunk[1])!==offset||chunk[2].length!==length*2)throw new Error('Device export returned an invalid offset or chunk size.');
          for(let n=0;n<length;n++)bytes[offset+n]=parseInt(chunk[2].slice(n*2,n*2+2),16);
          onStatus(`Downloading ${offset+length}/${count}`);
        }
        if(crc32(bytes)!==expected)throw new Error('Device export CRC does not match.');
        onStatus('Device song downloaded / CRC verified');return bytes;
      }finally{if(started)await cleanup();}
    });},
  };return client;
}
