/* SPDX-License-Identifier: GPL-3.0-or-later
 * Narrow adapters for the bundled desktop window. The existing client owns
 * console validation and sequencing; Rust alone opens native USB handles.
 */
import { createDeviceClient, isProfileCommand } from './device-client.js';
import { UPDATE_HELPER_PROTOCOL, UPDATE_PACKAGE_MAX_BYTES } from './updater-client.js';

export const DESKTOP_PROTOCOL='THY666-DESKTOP/1';
const VID=0xcafe,PID=0x4009;
const errorOf=value=>{const error=new Error(typeof value==='string'?value:value?.message||'The native operation could not finish.');if(value?.kind==='rejected')error.helperRejected=true;return error;};
const validCommand=command=>typeof command==='string'&&command.length>0&&command.length<=159&&!/[\r\n\x00-\x1f\x7f-\uffff]/.test(command)&&(/^(?:companion |backup |song |status$|io$|settings$|display$|pads$|pad \d+$|velocity \d+$)/.test(command)||isProfileCommand(command));

// This application-owned chooser replaces the browser serial chooser. Opening
// it is read-only; Connect is a separate explicit selection, even for one port.
export function chooseDesktopPort(devices,{document:doc=globalThis.document}={}){
  if(!doc?.createElement)throw new Error('The device chooser is unavailable.');
  return new Promise((resolve,reject)=>{
    const make=(tag,cls,text)=>{const node=doc.createElement(tag);if(cls)node.className=cls;if(text!==undefined)node.textContent=text;return node;};
    const overlay=make('div','thy-help-overlay desktop-port-chooser'),card=make('section','thy-help-dialog');
    card.setAttribute('role','dialog');card.setAttribute('aria-modal','true');card.setAttribute('aria-labelledby','desktop-port-title');
    const title=make('h2','','Connect your TheoryBoard');title.id='desktop-port-title';
    const copy=make('p','',devices.length?'Choose the THY666 board to connect.':'No THY666 application was found. Connect the board in normal playing mode, or use Update to install THY666 first.');
    const label=make('label','desktop-port-label','Board'),select=make('select');select.setAttribute('aria-label','TheoryBoard');
    for(const device of devices){const option=make('option','',device.label);option.value=device.id;select.append(option);}label.append(select);label.hidden=!devices.length;
    const actions=make('div','desktop-port-actions'),cancel=make('button','button','Cancel'),connect=make('button','button','Connect');cancel.type=connect.type='button';connect.disabled=!devices.length;
    const previous=doc.activeElement;
    const finish=(device,error)=>{doc.removeEventListener('keydown',keys,true);overlay.remove();previous?.focus?.();if(error)reject(error);else resolve(device);};
    const keys=event=>{if(event.key==='Escape'){event.preventDefault();finish(null,new Error('Connection cancelled.'));}else if(event.key==='Tab'){
      const focusable=devices.length?[select,cancel,connect]:[cancel];const index=focusable.indexOf(doc.activeElement);event.preventDefault();focusable[(index+(event.shiftKey?-1:1)+focusable.length)%focusable.length].focus();
    }};
    cancel.addEventListener('click',()=>finish(null,new Error('Connection cancelled.')));
    connect.addEventListener('click',()=>{const device=devices.find(item=>item.id===select.value);if(device)finish(device);});
    actions.append(cancel,connect);card.append(title,copy,label,actions);overlay.append(card);doc.body.append(overlay);doc.addEventListener('keydown',keys,true);(devices.length?select:cancel).focus();
  });
}

export function createDesktopSerial({invoke,choosePort=chooseDesktopPort,metrics={}}={}){
  if(typeof invoke!=='function')throw new TypeError('Native USB requires a desktop command bridge.');
  const listeners=new Set();
  const call=async(command,args)=>{metrics.requests=(metrics.requests||0)+1;try{return await invoke(command,args);}catch(error){throw errorOf(error);}};
  return{
    addEventListener(type,listener){if(type==='disconnect')listeners.add(listener);},
    removeEventListener(type,listener){if(type==='disconnect')listeners.delete(listener);},
    async requestPort({filters=[]}={}){
      if(filters.length!==1||filters[0].usbVendorId!==VID||filters[0].usbProductId!==PID)throw new Error('Native access is limited to the THY666 application.');
      const value=await call('companion_list',{}),devices=value?.devices;
      if(!Array.isArray(devices)||devices.length>32||devices.some(item=>!item||typeof item.id!=='string'||!item.id||item.usbVendorId!==VID||item.usbProductId!==PID||typeof item.label!=='string'))throw new Error('Native device discovery returned an invalid board list.');
      const selected=await choosePort(devices);if(!devices.some(item=>item.id===selected?.id))throw new Error('Choose a listed TheoryBoard.');
      let session=null,epoch=0,tail=Promise.resolve(),closing=null;
      const fail=()=>{for(const listener of listeners)listener({target:port,port});};
      async function exchange(commands){
        if(!session)throw new Error('The native board connection is closed.');
        if(!Array.isArray(commands)||!commands.length||commands.length>32||commands.some(command=>!validCommand(command)))throw new Error('Unsupported native console command batch.');
        const ticket=epoch,handle=session;
        const operation=tail.then(async()=>{
          if(ticket!==epoch||handle!==session)throw new Error('The native board connection changed.');
          metrics.serialCommands=(metrics.serialCommands||0)+commands.length;
          if(commands.length>1)metrics.batches=(metrics.batches||0)+1;
          try{
            const result=await call(commands.length===1?'companion_exchange':'companion_exchange_many',commands.length===1?{session:handle,command:commands[0]}:{session:handle,commands});
            if(ticket!==epoch||handle!==session)throw new Error('The native board connection changed.');
            const lines=commands.length===1?[result?.line]:result?.lines;
            if(!Array.isArray(lines)||lines.length!==commands.length||lines.some(line=>typeof line!=='string'||line.length>4096||/[\r\n]/.test(line)))throw new Error('The native board returned an invalid response batch.');
            return lines;
          }catch(error){if(ticket===epoch){void port.close().catch(()=>{});fail();}throw error;}
        });
        tail=operation.catch(()=>{});return operation;
      }
      const port={
        getInfo:()=>({usbVendorId:VID,usbProductId:PID}),
        async open({baudRate}={}){
          if(session||closing)throw new Error('The native board is already open or closing.');
          if(baudRate!==115200)throw new Error('Unsupported native serial configuration.');
          const ticket=++epoch,result=await call('companion_open',{deviceId:selected.id});
          if(typeof result?.session!=='string'||!result.session||result.usbVendorId!==VID||result.usbProductId!==PID){
            if(typeof result?.session==='string'&&result.session)await call('companion_close',{session:result.session});
            throw new Error('The native board identity is invalid.');
          }
          if(ticket!==epoch){await call('companion_close',{session:result.session});throw new Error('The native board connection was cancelled.');}session=result.session;
        },
        async close(){
          ++epoch;const handle=session;session=null;if(!handle)return closing;
          closing=call('companion_close',{session:handle});try{await closing;}finally{closing=null;}
        },
        async exchange(command){return(await exchange([command]))[0];},
        exchangeMany:exchange,
      };
      return port;
    },
  };
}

export function createDesktopUpdater({invoke}={}){
  let connected=false,busy=false,epoch=0;
  const call=async(command,args={})=>{
    if(busy)throw new Error('The desktop updater already has a pending request.');busy=true;
    try{const value=await invoke(command,args);if(value?.protocol!==UPDATE_HELPER_PROTOCOL)throw new Error('The desktop updater returned an unsupported protocol.');return value;}
    catch(error){throw errorOf(error);}finally{busy=false;}
  };
  const requireConnected=()=>{if(!connected)throw new Error('The desktop updater is not ready.');};
  return{
    get available(){return typeof invoke==='function';},get connected(){return connected;},get busy(){return busy;},
    async connect(){if(connected||busy)throw new Error('The desktop updater is already opening or connected.');const ticket=++epoch;const result=await call('updater_status');if(ticket!==epoch)throw new Error('The desktop updater connection changed.');connected=true;return result;},
    async disconnect(){++epoch;connected=false;},
    async status(){requireConnected();return call('updater_status');},
    async listDevices(){requireConnected();return(await call('updater_devices')).devices;},
    async releases(){requireConnected();return call('updater_releases');},
    async selectRelease(id){requireConnected();return(await call('updater_select_release',{id})).package;},
    async inspectPackage(bytes,{name='',expected_build=''}={}){
      requireConnected();if(!(bytes instanceof Uint8Array)||bytes.length<142||bytes.length>UPDATE_PACKAGE_MAX_BYTES)throw new Error('Invalid firmware package size.');
      if(expected_build&&!/^[a-f0-9]{16}$/.test(expected_build))throw new Error('The expected build must contain 16 hexadecimal characters.');
      return(await call('updater_inspect',{bytes:Array.from(bytes),name:String(name).split(/[\\/]/).at(-1),expectedBuild:expected_build||null})).package;
    },
    async install(request){requireConnected();return(await call('updater_install',{request})).operation;},
    async progress(operationId,afterSequence){requireConnected();return(await call('updater_progress',{operationId,afterSequence})).operation;},
  };
}

export function createDesktopSaveFile({listen,document:doc=globalThis.document,URL:urlAPI=globalThis.URL,Blob:BlobType=globalThis.Blob,setTimer=globalThis.setTimeout,clearTimer=globalThis.clearTimeout}={}){
  let saving=false;
  return async(bytes,name)=>{
    if(saving)throw new Error('Finish the current file save first.');
    if(!(bytes instanceof Uint8Array)||!bytes.length||bytes.length>2*1024*1024||typeof name!=='string'||name.length>128||/[\\/:\x00-\x1f]/.test(name)||!/^.+\.(?:thyb|thyl|thys|mid|json)$/.test(name))throw new Error('Unsupported desktop export.');
    if(!listen||!doc||!urlAPI?.createObjectURL||!BlobType)throw new Error('Native file saving is unavailable.');
    saving=true;let url,unsubscribe=()=>{},timer;
    try{
      url=urlAPI.createObjectURL(new BlobType([bytes],{type:'application/octet-stream'}));
      let finish,fail;const result=new Promise((resolve,reject)=>{finish=resolve;fail=reject;});
      unsubscribe=await listen('desktop-download',event=>{if(event.payload?.url!==url)return;if(event.payload.success===true)finish();else fail(new Error('The file was not saved.'));});
      timer=setTimer(()=>fail(new Error('File saving has not been confirmed. Check your downloads before trying again.')),120000);
      const link=doc.createElement('a');link.href=url;link.download=name;link.hidden=true;doc.body.append(link);link.click();link.remove();
      await result;
    }finally{if(timer!==undefined)clearTimer(timer);unsubscribe();if(url)urlAPI.revokeObjectURL(url);saving=false;}
  };
}

export async function connectDesktopRuntime({scope=globalThis.window,choosePort}={}){
  const api=scope?.__TAURI__;if(!api?.core?.invoke)return null;
  const invoke=(command,args={})=>api.core.invoke(command,args);
  let info;try{info=await invoke('desktop_info');}catch(error){throw errorOf(error);}
  if(info?.protocol!==DESKTOP_PROTOCOL||typeof info.simulation!=='boolean'||typeof info.version!=='string')throw new Error('The desktop runtime is incompatible with this interface.');
  const metrics={requests:0,serialCommands:0,batches:0},serial=createDesktopSerial({invoke,choosePort,metrics});
  const listen=api.event?.listen?.bind(api.event);
  const runtime=Object.freeze({kind:'desktop',version:info.version,platform:info.platform,simulation:info.simulation,active:info.active!==false,
    metrics:()=>({...metrics}),
    saveFile:createDesktopSaveFile({listen,document:scope.document}),
    window:Object.freeze({
      minimize:()=>invoke('window_minimize'),toggleMaximize:()=>invoke('window_toggle_maximize'),
      async isMaximized(){const value=await invoke('window_is_maximized');if(typeof value?.maximized!=='boolean')throw new Error('Invalid native window state.');return value.maximized;},
      beginDrag:()=>invoke('window_begin_drag'),
      beginResize(direction){if(!['n','ne','e','se','s','sw','w','nw'].includes(direction))return Promise.reject(new Error('Unsupported resize direction.'));return invoke('window_begin_resize',{direction});},
      close:()=>invoke('window_close'),
    }),
    async subscribeActivity(callback){if(!api.event?.listen)return()=>{};return api.event.listen('desktop-activity',event=>callback(event.payload?.active!==false));},
    async subscribeNotices(callback){if(!api.event?.listen)return()=>{};return api.event.listen('desktop-close-blocked',event=>callback(event.payload?.reason||'An update is still active. Keep THY666 open until its status is resolved.'));},
    async subscribeWindowState(callback){if(!api.event?.listen)return()=>{};return api.event.listen('desktop-window-state',event=>{if(typeof event.payload?.maximized==='boolean')callback({maximized:event.payload.maximized});});},
  });
  return{runtime,createClient:options=>createDeviceClient({...options,serial}),createUpdaterHelper:()=>createDesktopUpdater({invoke})};
}
