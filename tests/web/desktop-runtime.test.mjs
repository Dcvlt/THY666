import test from 'node:test';
import assert from 'node:assert/strict';
import {connectDesktopRuntime,createDesktopSerial,createDesktopUpdater,createDesktopSaveFile,DESKTOP_PROTOCOL} from '../../web/desktop-runtime.js';
const PROTOCOL='THY666-UPDATER/1';
const device={id:'synthetic-device',label:'Synthetic THY666',usbVendorId:0xcafe,usbProductId:0x4009};
const filter={filters:[{usbVendorId:0xcafe,usbProductId:0x4009}]};

test('browser startup stays inert and the desktop runtime probe never discovers or opens USB',async()=>{
  assert.equal(await connectDesktopRuntime({scope:{}}),null);const calls=[];
  const api={core:{async invoke(command){calls.push(command);return{protocol:DESKTOP_PROTOCOL,version:'0.1.0',simulation:true,platform:'windows'};}}};
  const result=await connectDesktopRuntime({scope:{__TAURI__:api}});
  assert.deepEqual(calls,['desktop_info']);assert.equal(result.runtime.kind,'desktop');assert.equal(result.runtime.simulation,true);
  assert.equal(result.createClient({}).available,true);result.createUpdaterHelper();assert.deepEqual(calls,['desktop_info']);
});

test('native window controls use narrow IPC and never issue a device operation',async()=>{
  const calls=[];const {runtime}=await connectDesktopRuntime({scope:{__TAURI__:{core:{async invoke(command,args){calls.push([command,args]);if(command==='desktop_info')return{protocol:DESKTOP_PROTOCOL,version:'0.1.0',simulation:true};if(command==='window_is_maximized')return{maximized:false};return null;}}}}});
  assert.equal(await runtime.window.isMaximized(),false);await runtime.window.minimize();await runtime.window.toggleMaximize();await runtime.window.beginDrag();await runtime.window.beginResize('se');await runtime.window.close();
  assert.deepEqual(calls.map(([name])=>name),['desktop_info','window_is_maximized','window_minimize','window_toggle_maximize','window_begin_drag','window_begin_resize','window_close']);
  const count=calls.length;await assert.rejects(()=>runtime.window.beginResize('arbitrary'),/Unsupported/);assert.equal(calls.length,count);
});

test('native selection and open are separate explicit steps and only the chosen opaque ID reaches Rust',async()=>{
  const calls=[];let choose;
  const serial=createDesktopSerial({invoke:async(command,args)=>{calls.push([command,args]);if(command==='companion_list')return{devices:[device]};if(command==='companion_open')return{session:'session-1',usbVendorId:0xcafe,usbProductId:0x4009};return{};},choosePort:values=>new Promise(resolve=>{assert.deepEqual(values,[device]);choose=()=>resolve(values[0]);})});
  assert.deepEqual(calls,[]);const pending=serial.requestPort(filter);await new Promise(resolve=>setImmediate(resolve));
  assert.deepEqual(calls.map(([name])=>name),['companion_list']);choose();const port=await pending;
  assert.deepEqual(calls.map(([name])=>name),['companion_list']);await port.open({baudRate:115200});
  assert.deepEqual(calls[1],['companion_open',{deviceId:'synthetic-device'}]);await port.close();
  assert.deepEqual(calls.at(-1),['companion_close',{session:'session-1'}]);
  await assert.rejects(()=>serial.requestPort({filters:[]}),/limited to/);
});

test('native command batches serialize, are bounded and stale completions cannot rejoin a closed session',async()=>{
  const calls=[],metrics={};let finish;
  const serial=createDesktopSerial({choosePort:async values=>values[0],metrics,invoke:async(command,args)=>{
    calls.push([command,args]);if(command==='companion_list')return{devices:[device]};
    if(command==='companion_open')return{session:'session-1',usbVendorId:0xcafe,usbProductId:0x4009};
    if(command==='companion_exchange_many')return{lines:args.commands.map(value=>value.toUpperCase()+' ready=1')};
    if(command==='companion_exchange')return new Promise(resolve=>finish=()=>resolve({line:'STATUS ready=1'}));return{};
  }});
  const port=await serial.requestPort(filter);await port.open({baudRate:115200});
  assert.deepEqual(await port.exchangeMany(['status','io']),['STATUS ready=1','IO ready=1']);assert.equal(metrics.batches,1);
  const before=calls.length;await assert.rejects(()=>port.exchangeMany(Array(33).fill('status')),/batch/);await assert.rejects(()=>port.exchange('status\nsettings'),/batch/);assert.equal(calls.length,before);
  const pending=port.exchange('status');await new Promise(resolve=>setImmediate(resolve));await port.close();finish();await assert.rejects(()=>pending,/changed/);
  assert.equal(calls.filter(([name])=>name==='companion_exchange').length,1);
});

test('a native handle returned with invalid identity metadata is closed before connection rejection',async()=>{
  const calls=[];
  const serial=createDesktopSerial({choosePort:async values=>values[0],invoke:async(command,args)=>{
    calls.push([command,args]);if(command==='companion_list')return{devices:[device]};
    if(command==='companion_open')return{session:'opened-handle',usbVendorId:0xcafe,usbProductId:0xffff};return{};
  }});
  const port=await serial.requestPort(filter);await assert.rejects(()=>port.open({baudRate:115200}),/identity/);
  assert.deepEqual(calls.at(-1),['companion_close',{session:'opened-handle'}]);
});

test('desktop updater uses direct IPC, preserves exact bytes and never retries failed installation',async()=>{
  const calls=[];
  const helper=createDesktopUpdater({invoke:async(command,args)=>{calls.push([command,args]);if(command==='updater_install')throw{kind:'unknown',message:'delivery lost'};return{protocol:PROTOCOL,package:{file_bytes:142},operation:null};}});
  assert.deepEqual(calls,[]);await helper.connect();const bytes=new Uint8Array(142);await helper.inspectPackage(bytes,{name:'synthetic.333',expected_build:'0123456789abcdef'});
  assert.deepEqual(calls[1],['updater_inspect',{bytes:Array.from(bytes),name:'synthetic.333',expectedBuild:'0123456789abcdef'}]);
  await assert.rejects(()=>helper.install({operation_id:'synthetic'}),error=>error.message==='delivery lost'&&!error.helperRejected);
  assert.equal(calls.filter(([name])=>name==='updater_install').length,1);
});

test('only a definite native updater rejection takes the preflight rejection path',async()=>{
  const helper=createDesktopUpdater({invoke:async command=>{if(command==='updater_status')return{protocol:PROTOCOL};throw{kind:'rejected',message:'Client owns USB'};}});
  await helper.connect();await assert.rejects(()=>helper.install({}),error=>error.helperRejected===true&&/owns USB/.test(error.message));
  assert.equal(helper.busy,false);
});

test('desktop exports await the matching native download outcome and cancellation cannot count as a saved backup',async()=>{
  let listener,clicked=0,revoked=0,removed=0;const url='blob:http://tauri.localhost/synthetic';
  const save=createDesktopSaveFile({listen:async(name,callback)=>{assert.equal(name,'desktop-download');listener=callback;return()=>removed++;},
    URL:{createObjectURL:()=>url,revokeObjectURL:()=>revoked++},Blob,
    document:{createElement:()=>({click(){clicked++;},remove(){}}),body:{append(){}}}});
  let complete=false;const pending=save(new Uint8Array([1,2]),'synthetic.thyb').then(()=>complete=true);
  await new Promise(resolve=>setImmediate(resolve));assert.equal(clicked,1);assert.equal(complete,false);
  listener({payload:{url:'blob:other',success:true}});await Promise.resolve();assert.equal(complete,false);
  listener({payload:{url,success:true}});await pending;assert.equal(complete,true);assert.equal(revoked,1);assert.equal(removed,1);
  const cancelled=save(new Uint8Array([3]),'synthetic.thyb');await new Promise(resolve=>setImmediate(resolve));listener({payload:{url,success:false}});
  await assert.rejects(()=>cancelled,/not saved/);assert.equal(revoked,2);
  await assert.rejects(()=>save(new Uint8Array([1]),'../private.thyb'),/Unsupported/);
});
