import test from 'node:test';
import assert from 'node:assert/strict';
import { crc32 } from 'node:zlib';
import { createDeviceSongClient } from '../../web/device-song.js';

function fixture({badOffset=false,badCrc=false,timeout=false,rejectCommit=false,busy=false,sampleLength=603}={}){
  const calls=[],sample=Uint8Array.from({length:sampleLength},(_,index)=>index%251),decoder=new TextDecoder(),encoder=new TextEncoder();
  let controller,state=busy?'upload':'idle',received=[],size=0,expected=0;
  const reply=text=>controller.enqueue(encoder.encode('THYINSTRUMENT ready=1\r\n'+text+'\r\n'));
  const port={
    readable:new ReadableStream({start(value){controller=value;}}),
    writable:new WritableStream({write(bytes){const line=decoder.decode(bytes).trim();calls.push(line);
      if(line==='song status')reply(`SONG version=1 state=${state} bytes=${size} received=${received.length} crc32=${expected.toString(16).padStart(8,'0')}`);
      else if(line.startsWith('song begin ')){const parts=line.split(' ');size=Number(parts[2]);expected=Number(parts[3]);received=[];state='upload';reply(`SONG ready bytes=${size}`);}
      else if(line.startsWith('song put ')){if(timeout)return;const [, ,offset,hex]=line.split(' ');assert.equal(Number(offset),received.length);for(let index=0;index<hex.length;index+=2)received.push(parseInt(hex.slice(index,index+2),16));reply(`SONG received=${received.length+(badOffset?1:0)}`);}
      else if(line==='song commit'){if(rejectCommit){reply('ERR song-crc-or-format');return;}assert.equal(received.length,size);assert.equal(crc32(Uint8Array.from(received))>>>0,expected);state='idle';reply(`SONG committed bytes=${size}`);}
      else if(line==='song abort'){state='idle';reply('SONG aborted');}
      else if(line==='song export'){size=sample.length;state='download';reply(`SONG export bytes=${sample.length} crc32=${((crc32(sample)^(badCrc?1:0))>>>0).toString(16).padStart(8,'0')}`);}
      else if(line.startsWith('song get ')){const [, ,offset,count]=line.split(' ').map((value,index)=>index>1?Number(value):value);reply(`SONG data offset=${offset} hex=${Array.from(sample.subarray(offset,offset+count),value=>value.toString(16).padStart(2,'0')).join('')}`);}
      else assert.fail(`Unexpected command: ${line}`);
    }}),
    async open(options){calls.push(['open',options]);},async close(){calls.push(['close']);},
  };
  const serial={async requestPort(options){calls.push(['requestPort',options]);return port;}};
  return{client:createDeviceSongClient({serial,timeoutMs:30}),calls,sample,getReceived:()=>Uint8Array.from(received)};
}

test('creating the song client never connects or sends commands; connect performs protocol handshake',async()=>{
  const {client,calls}=fixture();assert.deepEqual(calls,[]);assert.equal(client.connected,false);
  const status=await client.connect();assert.equal(status.version,1);assert.equal(status.state,'idle');assert.equal(client.connected,true);
  assert.deepEqual(calls[0],['requestPort',{filters:[{usbVendorId:0xcafe,usbProductId:0x4009}]}]);
  assert.ok(calls.includes('song status'));await client.disconnect();assert.equal(client.connected,false);
});
test('upload uses sequential bounded chunks, exact acknowledgements and CRC before commit',async()=>{
  const {client,calls,sample,getReceived}=fixture();await client.connect();const result=await client.upload(sample);
  assert.equal(result.bytes,sample.length);assert.equal(result.crc32,crc32(sample)>>>0);assert.deepEqual(getReceived(),sample);
  const puts=calls.filter(call=>typeof call==='string'&&call.startsWith('song put '));assert.equal(puts.length,Math.ceil(sample.length/32));
  assert.ok(puts.every(command=>command.length<96&&command.split(' ')[3].length<=64));assert.equal(calls.at(-1),'song commit');
  assert.equal(client.busy,false);await client.disconnect();
});
test('download pins the snapshot, checks every offset and CRC and releases device staging',async()=>{
  const {client,calls,sample}=fixture();await client.connect();assert.deepEqual(await client.download(),sample);
  assert.ok(calls.includes('song export'));assert.equal(calls.at(-1),'song abort');await client.disconnect();
});

test('the larger v2 maximum transfers without changing SONG/1 or its bounded chunk size',async()=>{
  const {client,calls,sample,getReceived}=fixture({sampleLength:8812});await client.connect();
  await client.upload(sample);assert.deepEqual(getReceived(),sample);assert.deepEqual(await client.download(),sample);
  assert.ok(calls.includes('song get 8800 12'));await client.disconnect();
});
test('bad acknowledgement and device commit rejection abort owned staging without retrying commit',async()=>{
  for(const config of [{badOffset:true},{rejectCommit:true}]){
    const {client,calls,sample}=fixture(config);await client.connect();await assert.rejects(client.upload(sample));
    assert.equal(calls.at(-1),'song abort');assert.ok(calls.filter(call=>call==='song commit').length<=1);assert.equal(client.busy,false);await client.disconnect();
  }
});
test('CRC mismatch and timeout fail cleanly and abort staging; no background reconnect',async()=>{
  const bad=fixture({badCrc:true});await bad.client.connect();await assert.rejects(bad.client.download(),/CRC/);assert.equal(bad.calls.at(-1),'song abort');await bad.client.disconnect();
  const timed=fixture({timeout:true});await timed.client.connect();await assert.rejects(timed.client.upload(timed.sample),/timed out/);assert.equal(timed.calls.at(-1),'song abort');
  assert.equal(timed.calls.filter(call=>Array.isArray(call)&&call[0]==='requestPort').length,1);await timed.client.disconnect();
});
test('busy service and invalid size do not abort another transfer or send song bytes',async()=>{
  const {client,calls,sample}=fixture({busy:true});await client.connect();await assert.rejects(client.upload(sample),/busy/);await assert.rejects(client.download(),/busy/);
  await assert.rejects(client.upload(new Uint8Array(9000)),/bounds/);assert.ok(!calls.some(call=>typeof call==='string'&&/^song (begin|put|commit|abort)/.test(call)));await client.disconnect();
});

test('a stalled serial writer is bounded by the command timeout',async()=>{
  let control;
  const port={readable:new ReadableStream({start(controller){control=controller;}}),
    writable:new WritableStream({write(){return new Promise(()=>{});}}),async open(){},async close(){}};
  const client=createDeviceSongClient({serial:{async requestPort(){return port;}},timeoutMs:10});
  await assert.rejects(client.connect(),/timed out/);assert.equal(client.connected,false);assert.ok(control);
});

test('late write rejection from a timed-out command cannot cancel a newer response',async()=>{
  let controller,rejectOld,count=0;
  const encoder=new TextEncoder(),response='SONG version=1 state=idle bytes=0 received=0 crc32=00000000\r\n';
  const writer={write(){count++;if(count===1){controller.enqueue(encoder.encode(response));return Promise.resolve();}
    if(count===2)return new Promise((resolve,reject)=>{rejectOld=reject;});
    setTimeout(()=>controller.enqueue(encoder.encode(response)),5);return Promise.resolve();},releaseLock(){}};
  const port={readable:new ReadableStream({start(value){controller=value;}}),writable:{getWriter:()=>writer},async open(){},async close(){}};
  const client=createDeviceSongClient({serial:{async requestPort(){return port;}},timeoutMs:20});
  await client.connect();await assert.rejects(client.status(),/timed out/);
  const newer=client.status();rejectOld(new Error('Late failure from the previous write'));
  assert.equal((await newer).state,'idle');await client.disconnect();
});
