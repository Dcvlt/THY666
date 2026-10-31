/* SPDX-License-Identifier: GPL-3.0-or-later
 * Explicit device-owned timing captures. No timer, device discovery or local
 * sample generation; the Companion's existing poll supplies status cadence.
 */
export const PROFILE_METRICS=Object.freeze([
  'Loop interval','USB service','MIDI transport service','Input service','Musical engine',
  'Display render','Display transfer','Lighting','Background service','Journal service',
  'Journal encoding','Journal validation','Journal checksum','ADC frame gap','Input service gap',
  'USB destination queue wait','DIN destination queue wait','Flash word program','Flash erase',
]);
const bound=value=>value===0xffffffff?'≥262,144':`≤${value.toLocaleString()}`;
export function profileExport(capture,{build='',schema=''}={}){
  if(!capture||capture.version!==1||!Array.isArray(capture.metrics)||capture.metrics.length!==PROFILE_METRICS.length||capture.status.active)
    throw new Error('Read a completed capture before exporting.');
  // Never copy an identity, raw console line, settings object or local path.
  const status={},keys=['version','id','active','durationMs','elapsedMs','hz','timed_out','clock_error','invalid','dropped','saturated','core_peak','usb_peak','din_peak','din_bytes_peak','usb_excluded','din_excluded','usb_discarded','din_discarded','clock_min_cycles','clock_max_cycles','engine','view','tempo_milli_bpm','settings_crc'];
  for(const key of keys)if(typeof capture.status[key]==='number'||typeof capture.status[key]==='boolean')status[key]=capture.status[key];
  return {format:'thy666.timing-profile',version:1,
    firmware:/^[0-9a-f]{16}$/i.test(build)?build:undefined,schema:/^[0-9a-f]{64}$/i.test(schema)?schema:undefined,
    histogram:{buckets:20,zeroBucket:true,overflowFrom:262144,percentiles:'upper bucket bounds'},
    contextSemantics:{settings_crc:'last saved settings payload CRC, not the current unsaved configuration'},status,
    metrics:capture.metrics.map((row,index)=>({index,name:PROFILE_METRICS[index],unit:row.unit,saturated:Boolean(row.saturated),
      ...Object.fromEntries(['count','min','max','mean','p50_upper','p95_upper','p99_upper'].map(key=>[key,row[key]])),bins:[...row.bins]}))};
}

export function initializeProfilePanel({mount,client,runtime=null,now=()=>Date.now(),saveFile=null}={}){
  const doc=mount.ownerDocument;
  const el=(tag,cls='',text)=>{const node=doc.createElement(tag);node.className=cls;if(text!==undefined)node.textContent=text;return node;};
  const button=(text,action)=>{const node=el('button','button hud-action',text);node.type='button';node.addEventListener('click',()=>void action());return node;};
  const panel=el('details','profile-panel hud-frame hud-frame--task hud-disclosure');
  const heading=el('summary','','Timing capture');
  const intro=el('p','profile-intro','Record a short timing sample while you play. A new capture replaces the board’s previous results.');
  const controls=el('div','profile-controls'),label=el('label','profile-duration','Duration'),duration=el('select','hud-field');
  duration.setAttribute('aria-label','Capture duration');
  for(const seconds of [15,30,60]){const option=el('option','',`${seconds} seconds`);option.value=String(seconds);duration.append(option);}duration.value='30';label.append(duration);
  const message=el('p','profile-message');message.setAttribute('role','status');
  const progress=el('progress','profile-progress');progress.max=60000;progress.value=0;progress.setAttribute('aria-label','Device capture progress');
  const results=el('div','profile-results');results.hidden=true;
  let visible=false,blocked=false,busy=false,destroyed=false,epoch=0,connection='',caps=null,status=null,capture=null,captureIdentity=null;
  let lastPoll=null,attempted='',uncertain=false,problem='',resultError='',pendingControl=null;
  const connected=()=>Boolean(client.connected&&!blocked);
  const start=button('Start capture',()=>operate(async current=>{
    const result=await client.startProfile(Number(duration.value));
    if(!current())return;
    status=result;attempted='';uncertain=false;problem=resultError='';lastPoll=now();
  },'start'));
  start.classList.add('hud-action--primary');
  const stop=button('Stop',()=>operate(async current=>{
    const result=await client.stopProfile(status.id);if(!current())return;status=result;uncertain=false;problem='';await fetchResults();
  },'stop'));
  const read=button('Read status',()=>operate(async()=>{attempted='';await readStatus();},'read'));
  read.classList.add('hud-action--quiet');
  const exportButton=button('Export JSON',()=>operate(async()=>{
    const data=new TextEncoder().encode(JSON.stringify(profileExport(capture,captureIdentity),null,2)+'\n');
    const name=`THY666-timing-${capture.status.id}.json`;
    if(saveFile)await saveFile(data,name);
    else if(runtime?.kind==='desktop')await runtime.saveFile(data,name);
    else{
      const scope=doc.defaultView||globalThis,urlAPI=scope.URL||globalThis.URL,BlobType=scope.Blob||globalThis.Blob;
      if(!urlAPI?.createObjectURL||!BlobType)throw new Error('File export is unavailable here.');
      const url=urlAPI.createObjectURL(new BlobType([data],{type:'application/json'}));
      try{const link=el('a');link.href=url;link.download=name;link.hidden=true;doc.body.append(link);link.click();link.remove();}
      finally{urlAPI.revokeObjectURL(url);}
    }
    problem='Capture exported.';
  },'export'));
  exportButton.classList.add('hud-action--quiet');
  controls.append(label,start,stop,read,exportButton);panel.append(heading,intro,controls,message,progress,results);mount.append(panel);
  function render(){
    start.disabled=busy||!connected()||client.compatible===false||!caps?.supported||Boolean(status?.active)||uncertain;
    stop.disabled=busy||!connected()||client.compatible===false||!status?.active||uncertain;
    start.title=stop.title=client.compatible===false?'Use a matching Client to start or stop captures.':'';
    read.disabled=busy||!connected()||caps?.supported===false;
    duration.disabled=busy||!connected()||Boolean(status?.active);
    exportButton.disabled=busy||!capture;
    start.textContent=status?.id?'Start new capture':'Start capture';
    progress.hidden=!status?.active;progress.max=status?.durationMs||60000;progress.value=status?.elapsedMs||0;
    message.textContent=problem||resultError||(!connected()?(status?.active?'Disconnected · capture may still be running on the board. Reconnect to read its status.':'Connect your board to capture timing.'):
      caps?.supported===false?'Update firmware to use timing capture.':busy?'Reading capture…':status?.active?
      `Capturing · ${(status.elapsedMs/1000).toFixed(1)} / ${status.durationMs/1000} seconds`:status?.id?
      status.clock_error?'Capture stopped · clock error. Export results for diagnosis.':`Capture ${status.id} complete · ${(status.elapsedMs/1000).toFixed(1)} seconds`:'Open this section to check capture support.');
  }
  function renderResults(){
    results.replaceChildren();results.hidden=!capture;if(!capture)return;
    const summary=el('div','profile-summary');
    for(const [label,value] of [['Elapsed',`${(capture.status.elapsedMs/1000).toFixed(1)} s`],['Loop samples',capture.metrics[0].count.toLocaleString()],['Invalid samples',(capture.status.invalid||0).toLocaleString()]]){
      const item=el('div','profile-summary-item');item.append(el('span','',label),el('strong','hud-type-mono',value));summary.append(item);
    }
    const note=el('p','profile-bound-note',`${capture.status.saturated||capture.status.dropped?'Some samples were dropped or saturated. ':''}Percentiles show bucket ceilings. Queue wait ends when the output buffer accepts a message.`);
    const scroll=el('div','profile-table-wrap hud-scroll-region'),table=el('table','profile-table');
    const caption=el('caption','',`Capture ${capture.status.id} · device timings`),head=el('thead'),headRow=el('tr');
    for(const text of ['Metric','Samples','Mean','Median ceiling','P95 ceiling','Maximum','Distribution']){const th=el('th','',text);th.scope='col';headRow.append(th);}head.append(headRow);
    const body=el('tbody');
    for(const row of capture.metrics){
      const tr=el('tr'),name=el('th','',PROFILE_METRICS[row.index]);name.scope='row';tr.append(name);
      const unit=` ${row.unit}`;
      for(const text of [row.count.toLocaleString(),row.count?row.mean.toLocaleString()+unit:'—',row.saturated?'Unavailable':row.count?bound(row.p50_upper)+unit:'—',row.saturated?'Unavailable':row.count?bound(row.p95_upper)+unit:'—',row.count?row.max.toLocaleString()+unit:'—'])tr.append(el('td','hud-type-mono',text));
      const cell=el('td'),histogram=el('span','profile-histogram');histogram.setAttribute('aria-hidden','true');
      const max=Math.max(1,...row.bins);
      for(const [index,value] of row.bins.entries()){const bar=el('i');bar.style.setProperty('--profile-height',`${value/max*100}%`);bar.title=`Bucket ${index}: ${value}`;histogram.append(bar);}cell.append(histogram);tr.append(cell);body.append(tr);
    }
    table.append(caption,head,body);scroll.append(table);results.append(summary,note,scroll);
  }
  async function fetchResults(){
    if(!status?.id||status.active)return;
    const key=`${connection}:${status.id}`;if(attempted===key)return;attempted=key;
    const token=epoch,identity=client.identity||{};let result;
    try{result=await client.readProfileCapture(status.id);}
    catch(error){if(!destroyed&&token===epoch)resultError='Results could not be read. Choose Read status to retry.';throw error;}
    if(destroyed||token!==epoch||!connected())return;
    capture=result;captureIdentity={build:identity.build,schema:identity.schema};status=result.status;resultError='';renderResults();
  }
  async function readStatus(){
    const token=epoch;
    if(!caps){const value=await client.getProfileCapabilities();if(destroyed||token!==epoch||!connected())return;caps=value;if(!caps.supported)return;}
    const value=await client.getProfileStatus();if(destroyed||token!==epoch||!connected())return;
    status=value;uncertain=false;problem='';
    await fetchResults();
  }
  async function operate(action,kind){
    if(busy||destroyed||kind!=='export'&&!connected())return;
    const token=epoch;busy=true;problem='';pendingControl=['start','stop'].includes(kind)?kind:null;render();
    try{await action(()=>!destroyed&&token===epoch&&connected());}
    catch(error){if(token===epoch){uncertain=Boolean(error.profileOutcomeUnknown);problem=uncertain?`${kind==='stop'?'Stop':'Start'} not confirmed. Read status before trying again.`:error.message;}}
    finally{busy=false;pendingControl=null;if(!destroyed)render();}
  }
  const opened=()=>{if(panel.open&&visible&&connected()){lastPoll=now();void operate(readStatus,'read');}};panel.addEventListener('toggle',opened);
  render();
  return {
    get state(){return {supported:caps?.supported??null,status,capture,busy,uncertain};},
    refresh({visible:show=visible,blocked:locked=blocked}={}){
      visible=Boolean(show);blocked=Boolean(locked);
      const next=client.connected?[client.identity?.uid,client.identity?.build,client.identity?.schema].join(':'):'';
      if(next!==connection){
        const interrupted=Boolean(pendingControl);epoch++;connection=next;caps=null;lastPoll=null;attempted='';
        uncertain=uncertain||interrupted;resultError='';problem=interrupted?'Capture state unknown · reconnect and read status.':'';
      }
      render();
    },
    async poll(){
      if(destroyed||!visible||!panel.open||!connected()||busy||caps?.supported===false)return;
      const time=now();if(lastPoll!==null&&time>=lastPoll&&time-lastPoll<1000)return;lastPoll=time;
      return operate(readStatus,'read');
    },
    destroy(){destroyed=true;epoch++;panel.removeEventListener('toggle',opened);panel.remove();},
  };
}
