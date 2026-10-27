/* SPDX-License-Identifier: GPL-3.0-or-later
 * Client owns connection and navigation. The C engine still owns music,
 * menus and settings; the help center is read-only and independently routed.
 */
import { STUDIO_MENU, MENU_ROWS } from './studio-menu.js';
import { createDeviceClient, inspectDeviceBackup } from './device-client.js';
import { decodeCompanionSnapshot, paintDeviceOLED } from './companion-protocol.js';
import { renderCompanionScreen } from './companion-display.js';
export { renderCompanionScreen };
import { initializeHelp } from './help.js';
import { initializeBackupPanel } from './backup-panel.js';
import { initializeUpdaterPanel } from './updater-panel.js';
import { createBrowserUpdaterClient } from './browser-updater-client.js';
import { initializeAppSettings } from './app-settings.js';
import { initializeHarmonyGuide } from './harmony-guide.js';
import { initializeCockpit } from './cockpit.js';

const icons={board:'▦',harmony:'◇',parameters:'◈',library:'♫',backups:'⇅',diagnostics:'⌁'};
const devicePages=new Set(['parameters','library','backups','diagnostics']);
const el=(tag,className='',text)=>{
  const node=document.createElement(tag);node.className=className;if(text!==undefined)node.textContent=String(text);return node;
};
const button=(text,className='',action)=>{const node=el('button',className,text);node.type='button';if(action)node.addEventListener('click',action);return node;};
const clear=node=>{if(node)node.replaceChildren();return node;};
const sameValues=(a,b)=>a===b||Boolean(a&&b&&a.length===b.length&&a.every((value,index)=>value===b[index]));
const cockpitFields=['heldPads','noteCount','tempo','scale','harmony','root','clockSource','clockStatus','mpe','arp1','arp2','running','recording'];
function sameCockpit(a,b){
  return Boolean(a&&b&&cockpitFields.every(key=>a[key]===b[key])&&a.inspect?.logical===b.inspect?.logical&&
    ['held','pressure','aftertouchStage','sounding','pool'].every(key=>sameValues(a[key],b[key])));
}
function expandIcon(){
  const svg=document.createElementNS('http://www.w3.org/2000/svg','svg');svg.setAttribute('viewBox','0 0 24 24');svg.setAttribute('aria-hidden','true');
  const path=document.createElementNS('http://www.w3.org/2000/svg','path');path.setAttribute('d','M14 4h6v6M20 4l-6 6M10 20H4v-6M4 20l6-6');
  path.setAttribute('fill','none');path.setAttribute('stroke','currentColor');path.setAttribute('stroke-width','1.6');svg.append(path);return svg;
}

function buildBoardOutline(){
  const wrap=el('div','companion-board-standby');
  const make=(tag,attributes)=>{const n=document.createElementNS('http://www.w3.org/2000/svg',tag);for(const [key,value] of Object.entries(attributes))n.setAttribute(key,String(value));return n;};
  const svg=make('svg',{viewBox:'0 0 1280 320',role:'img','aria-label':'TheoryBoard wireframe. Connect a physical board to use its controls.',focusable:'false'});
  svg.append(make('path',{class:'standby-hull',d:'M2 56V18L18 2H234L250 15H1030L1046 2H1262L1278 18V56M2 264V302L18 318H234L250 305H1030L1046 318H1262L1278 302V264'}));
  svg.append(make('path',{class:'standby-circuit',d:'M8 64V106L15 113V207L8 214V256M1272 64V106L1265 113V207L1272 214V256M24 10H210L222 23H268M1256 10H1070L1058 23H1012M24 310H210L222 297H268M1256 310H1070L1058 297H1012'}));
  for(const x of [20,746])for(let column=0;column<12;column++){
    svg.append(make('rect',{class:'standby-touch',x:x+column*43,y:29,width:39,height:23}));
    for(let row=0;row<4;row++)svg.append(make('rect',{class:'standby-pad',x:x+column*43,y:66+row*45,width:39,height:39}));
  }
  svg.append(make('path',{class:'standby-screen',d:'M560 32H720V113H560ZM565 38H604M565 108H715M715 38V64'}));
  // Physical order: Left, Up, Select, Down, Right.
  const navigationAngles=[-90,0,null,180,90];
  for(let n=0;n<5;n++)svg.append(make(n===2?'ellipse':'path',n===2?{class:'standby-control',cx:640,cy:137,rx:10,ry:7}:{class:'standby-control',d:`M${570+n*35} 129l9 16h-18Z`,transform:`rotate(${navigationAngles[n]} ${570+n*35} 137)`}));
  for(const [y,count] of [[174,4],[195,3],[216,4],[237,2],[258,1]])for(let n=0;n<count;n++){
    const x=640+(n-(count-1)/2)*31;svg.append(make('path',{class:'standby-control',d:`M${x} ${y-8}l13 8-13 8-13-8Z`}));
  }
  for(let n=0;n<34;n++){const x=348+n*18;svg.append(make('path',{class:'standby-tick',d:`M${x} 8v${n%4?3:6}M${x} 312v-${n%4?3:6}`}));}
  wrap.append(svg);return wrap;
}

export function initializeCompanionShell({engine,studio,groups,applyPayload,onPreviewStart,onInspect,
  notify:externalNotice=()=>{},getPreviewSnapshot,createClient=createDeviceClient,createUpdaterHelper,runtime=null}={}){
  const oldMain=document.querySelector('.app-shell > main'),oldHeader=document.querySelector('.masthead');
  if(!oldMain||!oldHeader||!engine||!studio)throw new Error('The board and shared engine must be initialized first.');
  document.title='THY666 · Client';document.body.classList.add('companion-app');
  let page='board',area='client',display='web',snapshot=null,previewSnapshot=null,expanded=false;
  let previewPayload=null,restoringPreview=null,polling=false,disposed=false,lastGeneration=null,lastRevision='',lastValues=0,readIndex=0,selectedPad=0;
  let pollTimer=null,pollEpoch=0,pollConnectionActive=false,hostActive=true,lastConnectionKey='';
  let updater=null;
  let renderedSnapshot=null,renderedWeb=null,renderedOLED=null,renderedCockpit=null,lastCockpitAt=0;
  const records=new Map(),panes=new Map(),subButtons=new Map(),rowIndex=new Map(MENU_ROWS.map((row,n)=>[row.id,n]));
  const oldActions=oldHeader.querySelector('.header-actions');
  const top=el('header','companion-topbar');const brand=oldHeader.querySelector('.wordmark');
  brand.setAttribute('aria-label','THY666 client home');brand.href='#client/board';
  for(const node of [...brand.children])if(node.tagName.toLowerCase()==='span')node.remove();
  brand.classList.add('companion-centered-brand');
  top.append(brand);
  const projectSite=document.documentElement.dataset.projectSite==='true';
  const mainNav=el('nav','companion-main-nav');mainNav.setAttribute('aria-label','Application areas');
  const updateAreaButton=button(projectSite?'Install':'Update','',()=>showArea('update'));
  const clientAreaButton=button('Client','is-selected',()=>showArea('client'));
  const docsAreaButton=button(projectSite?'User guide':'Documentation','',()=>{showArea('documentation');help.open('getting-started');});
  mainNav.append(updateAreaButton,clientAreaButton,docsAreaButton);
  if(projectSite){
    const home=el('a','button','Project');home.href='../index.html';
    home.addEventListener('click',event=>{if(updater?.usbBusy||client.busy)event.preventDefault();});
    mainNav.append(home);
  }
  const headerActions=el('div','companion-header-actions');headerActions.append(mainNav);top.append(headerActions);
  const connection=el('div','companion-connection'),status=el('span','connection-status','');status.setAttribute('role','status');status.hidden=true;
  const connect=button('Connect board','button companion-connect',()=>void toggleConnection());
  connection.append(status,connect);headerActions.append(connection);if(oldActions){oldActions.classList.add('legacy-status');top.append(oldActions);}oldHeader.replaceWith(top);
  const workspace=el('div','companion-workspace'),rail=el('aside','companion-rail'),content=el('main','companion-content');
  const clientNav=el('nav','companion-client-nav');clientNav.setAttribute('aria-label','Client pages');
  const railToggle=button('','button rail-toggle',()=>setRailCollapsed(!workspace.classList.contains('rail-collapsed')));
  railToggle.append(el('span','rail-icon','«'),el('span','rail-toggle-text','Collapse'));railToggle.setAttribute('aria-controls','companion-client-navigation');
  clientNav.id='companion-client-navigation';rail.append(railToggle,clientNav);
  for(const [id,label] of [['board','Board'],['parameters','Parameters'],['harmony','Harmony'],['library','Songs & library'],['backups','Backups'],['diagnostics','Diagnostics']]){
    const nav=button('',id==='board'?'is-selected':'',()=>showPage(id));nav.dataset.page=id;
    nav.setAttribute('aria-label',label);nav.dataset.label=label;nav.title=label;nav.append(el('span','rail-icon',icons[id]),el('span','rail-text',label));clientNav.append(nav);subButtons.set(id,nav);
    const pane=el('section','companion-page');pane.id=`companion-${id}`;pane.hidden=id!=='board';panes.set(id,pane);content.append(pane);
  }
  initializeHarmonyGuide(panes.get('harmony'),engine.guidance);
  workspace.append(rail,content);
  const updatePane=el('section','companion-page companion-update-area');updatePane.id='companion-update';updatePane.hidden=true;
  panes.set('update',updatePane);
  const docsMount=el('section','companion-docs');docsMount.hidden=true;docsMount.id='documentation';
  const notice=el('div','companion-notice');notice.setAttribute('role','status');notice.hidden=true;
  oldMain.before(notice,workspace,updatePane,docsMount);
  const pageTitle=el('h1','','Board'),firmwareLabel=el('p','eyebrow','THY666');
  const boardHead=el('div','companion-page-head');const titleWrap=el('div','');
  titleWrap.append(firmwareLabel,pageTitle);
  const toolbar=el('div','board-toolbar');
  const webButton=button('Web','is-selected',()=>setDisplay('web')),oledButton=button('OLED','',()=>setDisplay('oled'));
  const displaySwitch=el('div','segmented');displaySwitch.setAttribute('role','group');displaySwitch.setAttribute('aria-label','Board display');displaySwitch.append(webButton,oledButton);
  const expand=button('','button button-quiet screen-expand',()=>toggleDisplaySize());expand.append(expandIcon());expand.setAttribute('aria-label','Expand screen');expand.setAttribute('aria-expanded','false');expand.setAttribute('aria-controls','companion-instrument-screen');expand.title='Expand screen';
  const fitButton=button('Fit','button button-small is-selected',()=>{fit=true;resizeBoard();});
  const actualButton=button('1:1','button button-small',()=>{fit=false;resizeBoard();});let fit=true;
  const labelsButton=button('Labels','button button-small',()=>{instrument.classList.toggle('show-pad-labels');labelsButton.classList.toggle('is-selected',instrument.classList.contains('show-pad-labels'));});
  toolbar.append(fitButton,actualButton,labelsButton);
  boardHead.append(titleWrap,toolbar);panes.get('board').append(boardHead);
  const instrument=document.querySelector('.instrument'),face=instrument.querySelector('.controller-face'),scroll=instrument.querySelector('.controller-scroll');
  const padNodes=[...instrument.querySelectorAll('.pad')];
  const centerNodes=[...instrument.querySelectorAll('[data-control="nav"]'),...instrument.querySelectorAll('[data-control="function"]')];
  const upperNodes=[...instrument.querySelectorAll('[data-control="root"]')].map((node,index)=>[node,19+index])
    .concat([...instrument.querySelectorAll('[data-control="mode"]')].map((node,index)=>[node,31+index]));
  const oledCanvas=document.querySelector('#oled-canvas');oledCanvas.setAttribute('aria-label','Live instrument OLED: 128 by 64 pixels');
  const displayTools=el('div','companion-display-tools');displayTools.append(displaySwitch,expand);instrument.querySelector('.physical-screen').id='companion-instrument-screen';instrument.querySelector('.physical-screen').before(displayTools);
  const scaleWrap=el('div','companion-board-scale');face.replaceWith(scaleWrap);scaleWrap.append(face);
  const legacyTools=instrument.querySelector('.display-lab-tools'),routes=instrument.querySelector('.controller-routes'),expression=instrument.querySelector('.expression-strip');
  const webScreen=el('div','companion-web-screen');webScreen.setAttribute('aria-label','Enhanced instrument display');
  instrument.querySelector('.physical-screen').append(webScreen);
  const cockpitMount=el('div','companion-cockpit');cockpitMount.append(instrument);panes.get('board').append(cockpitMount);
  const cockpit=initializeCockpit({mount:cockpitMount});
  const standby=buildBoardOutline();cockpitMount.querySelector('.cockpit-deck').prepend(standby);
  const onboarding=el('p','companion-onboarding','Connect a theoryboard with THY666 firmware or ');
  const installLink=el('a','','install the firmware on your device');installLink.href='#update';
  installLink.addEventListener('click',event=>{
    if(event.ctrlKey||event.metaKey||event.shiftKey||event.altKey||event.button>0)return;
    event.preventDefault();showArea('update');
  });
  onboarding.append(installLink);panes.get('board').append(onboarding);
  const parameterHead=el('div','companion-page-head');parameterHead.append(el('div','',''));
  parameterHead.firstChild.append(el('p','eyebrow','PLAYABILITY & CONFIGURATION'),el('h1','','Parameters'));
  const parameterHelp=button('Read the settings guide','button button-quiet');parameterHelp.dataset.help='settings';parameterHead.append(parameterHelp);
  const parameterSource=el('p','client-parameter-source','Connect your board to edit parameters.');
  panes.get('parameters').append(parameterHead,parameterSource,document.querySelector('#engine-studio'));
  const backupHead=el('div','companion-page-head');backupHead.append(el('div','',''));backupHead.firstChild.append(el('p','eyebrow','YOUR SAVED INSTRUMENT'),el('h1','','Backups'));
  const backupHelp=button('What is included?','button button-quiet');backupHelp.dataset.help='backup';backupHead.append(backupHelp);panes.get('backups').append(backupHead);
  const backupMount=el('div','backup-mount');panes.get('backups').append(backupMount);
  const updateMount=el('div','updater-mount');panes.get('update').append(updateMount);
  const libraryHead=el('div','companion-page-head');libraryHead.append(el('div','',''));libraryHead.firstChild.append(el('p','eyebrow','COMPOSE & ORGANIZE'),el('h1','','Songs & library'));
  const libraryHelp=button('Workflow guide','button button-quiet');libraryHelp.dataset.help='sequencer-editor';libraryHead.append(libraryHelp);
  const libraryNote=el('p','client-parameter-source','Use the shared engine to try sequences and custom musical material.');
  const libraryTabs=el('div','client-local-tabs'),songMount=el('div','client-local-panel'),libraryMount=el('div','client-local-panel');libraryMount.hidden=true;
  for(const [id,label,mount] of [['songs','Sequencer',songMount],['library','Custom library',libraryMount]]){
    const tab=button(label,id==='songs'?'is-selected':'',()=>{songMount.hidden=id!=='songs';libraryMount.hidden=id!=='library';for(const b of libraryTabs.children)b.classList.toggle('is-selected',b===tab);});libraryTabs.append(tab);
  }
  songMount.append(document.querySelector('#sequencer-lab'));libraryMount.append(document.querySelector('#library-lab'));
  const legacyTransfer=songMount.querySelector('[data-device="connect"]')?.closest('details');if(legacyTransfer)legacyTransfer.hidden=true;
  panes.get('library').append(libraryHead,libraryNote,libraryTabs,songMount,libraryMount);
  const debugHead=el('div','companion-page-head');debugHead.append(el('div','',''));debugHead.firstChild.append(el('p','eyebrow','OBSERVE & UNDERSTAND'),el('h1','','Diagnostics'));
  const debugHelp=button('Telemetry guide','button button-quiet');debugHelp.dataset.help='companion';debugHead.append(debugHelp);panes.get('diagnostics').append(debugHead);
  const telemetry=el('div','client-telemetry'),telemetryText=el('pre','telemetry-output','Connect to read device telemetry.');
  const telemetryControls=el('div','telemetry-controls'),telemetryPick=el('select','');telemetryPick.setAttribute('aria-label','Telemetry report');
  for(const name of ['status','io','settings','display']){const option=el('option','',name);option.value=name;telemetryPick.append(option);}
  let telemetryReading=false;
  const readTelemetry=button('Read report','button',()=>{
    if(telemetryReading||!client.connected||updater?.usbBusy)return;
    const command=telemetryPick.value;telemetryReading=true;readTelemetry.disabled=true;telemetryPick.disabled=true;
    telemetryText.textContent='Reading…';
    void run(async()=>{
      try{telemetryText.textContent=await client.telemetry(command);}
      catch(error){telemetryText.textContent='Read failed. Try again.';throw error;}
      finally{telemetryReading=false;readTelemetry.disabled=!client.connected||Boolean(updater?.usbBusy);telemetryPick.disabled=readTelemetry.disabled;}
    });
  });
  telemetryControls.append(telemetryPick,readTelemetry);telemetry.append(el('h2','','Device telemetry'),telemetryControls,telemetryText);panes.get('diagnostics').append(telemetry);
  const debugTabs=el('div','client-local-tabs'),debugPanels=el('div','debug-panels');panes.get('diagnostics').append(debugTabs,debugPanels);
  const debugEntries=[['ownership','Ownership & events',[]],['sensors','Sensors',[document.querySelector('#firmware-studio')]],['arps','Arps',[document.querySelector('#arp-lab')]],['expression','MPE',[document.querySelector('#mpe-lab')]],['clock','Clock',[document.querySelector('#rhythm-lab')]],['reference','Reference tools',[]]];
  for(const [id,label,nodes] of debugEntries){
    const pane=el('div','debug-panel');pane.dataset.debug=id;pane.hidden=id!=='ownership';
    for(const node of nodes)if(node)pane.append(node);
    if(id==='ownership'){for(const selector of ['.diagnostic-grid','.timeline-panel']){const node=document.querySelector(selector);if(node)pane.append(node);}}
    if(id==='sensors'&&!pane.childNodes.length){const node=document.querySelector('.firmware-studio');if(node)pane.append(node);}
    if(id==='reference'){for(const node of [legacyTools,routes,expression,document.querySelector('.control-surface')])if(node)pane.append(node);}
    const tab=button(label,id==='ownership'?'is-selected':'',()=>{for(const node of debugPanels.children)node.hidden=node!==pane;for(const b of debugTabs.children)b.classList.toggle('is-selected',b===tab);});debugTabs.append(tab);debugPanels.append(pane);
  }
  // Initializers previously inserted these independently. They are now mounted
  // once in named workspaces, never repeated below the board.
  for(const node of [...oldMain.children]){
    if(node.matches('.intro,.lab-navigation,footer'))node.remove();
    else if(node.id==='error-banner')notice.after(node);
    else if(node.classList.contains('firmware-studio')||node.querySelector?.('#fs-response-title'))debugPanels.querySelector('[data-debug="sensors"]').append(node);
    else if(node.children.length)debugPanels.querySelector('[data-debug="reference"]').append(node);
  }
  oldMain.remove();
  const help=initializeHelp({mount:docsMount,onNavigate:()=>showArea('documentation')});
  const appSettings=initializeAppSettings({onPollIntervalChange:ms=>{if(pollConnectionActive&&!disposed)schedulePolling(ms);}});
  const client=createClient({expectedSchema:engine.state.schemaHash(),onState:updateConnection});
  const backup=initializeBackupPanel({mount:backupMount,client,getEngine:()=>engine,onApplied:async()=>{await readConfiguration();await poll();},notify:message=>tell(message),runtime});
  // Preserve explicit native/helper integrations. Supported browser sessions
  // use WebHID; the Rust helper launch fragment retains its existing route.
  const launchFragment=window.__thyUpdateLaunchFragment||location.hash||'';
  const browserDirect=!runtime&&!createUpdaterHelper&&!launchFragment.startsWith('#update?')&&Boolean(navigator.hid)&&globalThis.isSecureContext===true;
  updater=initializeUpdaterPanel({mount:updateMount,client,runtime:browserDirect?{kind:'browser-direct'}:runtime,
    createHelper:browserDirect?()=>createBrowserUpdaterClient({
      wasmURL:new URL('./generated/browser-updater.wasm',import.meta.url),
      coreManifestURL:new URL('./generated/browser-updater.json',import.meta.url),
      ...(projectSite?{releasesURL:new URL('../releases.json',import.meta.url)}:{}),
    }):createUpdaterHelper,
    connectApplication:()=>toggleConnection(),onCompanion:()=>showPage('board'),onRestored:async()=>{await readConfiguration();await poll();},notify:message=>{if(area!=='update')tell(message);},onOwnershipChange:()=>updateConnection(client.status)});
  const bridge={
    get connected(){return client.connected;},get compatible(){return client.compatible&&!updater?.usbBusy;},
    get:id=>updater?.usbBusy?null:records.get(id)||null,
    instance(id){return instanceFor(MENU_ROWS[rowIndex.get(id)]);},
    select(id){if(client.connected&&!updater?.usbBusy)void refreshRecords([id]).catch(error=>tell(error.message));},
    async commit(id,instance,raw){if(updater?.usbBusy)throw new Error('The updater owns USB. Wait for it to finish.');await client.set(rowIndex.get(id),instance,raw);records.clear();await readConfiguration();await refreshRecords(currentRows());await poll();},
    async action(id,instance){
      if(updater?.usbBusy)throw new Error('The updater owns USB. Wait for it to finish.');
      const row=MENU_ROWS[rowIndex.get(id)],target=instanceFor(row);
      if(!client.compatible||row?.type!=='action'||row.status!=='live'||!Number.isInteger(target)||instance!==target)
        throw new Error('Read the current device action target before running this action.');
      await client.action(rowIndex.get(id),instance);records.clear();await poll();await refreshRecords(currentRows());
    },
    error:error=>tell(error.message),
  };
  studio.setDeviceBridge?.(bridge);
  function tell(message){notice.textContent=String(message);notice.hidden=!message;externalNotice(message);}
  async function run(operation){try{return await operation();}catch(error){tell(error.message);return null;}}
  function updateConnection(state){
    if(!state.connected&&!state.connecting&&previewPayload)void restorePreview();
    const updateBusy=Boolean(updater?.usbBusy);
    // Busy/progress notifications occur for every serial transaction/chunk.
    // Structural board state changes only at connection or ownership edges.
    const connectionKey=[state.connected,state.connecting,state.compatible,state.identity?.build,client.available,updateBusy].join(':');
    if(connectionKey===lastConnectionKey){
      if(area==='client'&&page==='backups')backup?.refresh();
      if(area==='update'||updater?.modalOpen||updateBusy)updater?.refresh();
      return;
    }
    lastConnectionKey=connectionKey;
    status.textContent=updateBusy?'Updater owns USB':state.connecting?'Connecting':state.connected&&!state.compatible?'Read-only':'';status.hidden=!status.textContent;
    status.dataset.mode=state.connected?'connected':'preview';connect.textContent=state.connected?'Disconnect':'Connect board';connect.disabled=state.connecting||client.available===false||updateBusy;
    connect.title=updateBusy?'The updater owns USB. Wait for its terminal status before reconnecting the application.':client.available===false?
      runtime?.kind==='desktop'?'Native USB is unavailable. Restart the desktop app and check its diagnostics.':'USB requires Web Serial. Open this app in Chrome or Edge over HTTPS or localhost to connect.':'';
    firmwareLabel.textContent=state.connected?`THY666 / ${state.identity.build.slice(0,8)}`:'THY666';
    parameterSource.textContent=state.connected?(state.compatible?'RIGHT applies on the board and autosave follows.':'This client and firmware have different menus. Monitoring and backups remain available; parameter writes are disabled.'):'Connect your board to edit parameters.';
    libraryNote.textContent=state.connected?'Use the board or Parameters to edit songs and library settings. These diagrams mirror the shared engine.':'Connect your board to inspect songs and library settings.';
    document.body.classList.toggle('device-connected',state.connected);
    face.inert=!state.connected||updateBusy;workspace.inert=updateBusy;clientAreaButton.disabled=updateBusy;instrument.hidden=!state.connected;standby.hidden=state.connected;onboarding.hidden=state.connected;
    toolbar.hidden=!state.connected;displayTools.hidden=!state.connected;
    for(const node of face.querySelectorAll('button'))node.disabled=!state.connected||updateBusy;
    cockpit.update(state.connected?snapshot:null,{connected:state.connected});
    if(!state.connected){expanded=false;cockpitMount.classList.remove('screen-expanded');expand.setAttribute('aria-expanded','false');}
    resizeBoard();
    for(const [id,nav] of subButtons){nav.disabled=updateBusy||devicePages.has(id)&&!state.connected;nav.title=updateBusy?'The updater owns USB. Wait for it to finish.':nav.disabled?`${nav.dataset.label} — Connect your board to use this page`:nav.dataset.label;nav.setAttribute('aria-disabled',String(nav.disabled));}
    if(!state.connected&&area==='client'&&devicePages.has(page))showPage('board');
    // The client serializes reads behind a snapshot already in flight. Do not
    // flicker this control disabled on every background poll: a pointer click
    // can otherwise lose its mouse-up while the next snapshot starts.
    readTelemetry.disabled=!state.connected||telemetryReading||updateBusy;telemetryPick.disabled=!state.connected||telemetryReading||updateBusy;
    if(state.connected!==pollConnectionActive){pollConnectionActive=state.connected;if(state.connected)schedulePolling(0);else cancelPolling();}
    if(updateBusy)cancelPolling();else if(state.connected)schedulePolling(0);
    for(const host of [songMount,libraryMount,debugPanels]){
      host.inert=state.connected;
      for(const node of host.querySelectorAll('button,input,select,textarea')){
      if(state.connected){if(!node.dataset.connectionDisabled){node.dataset.connectionDisabled=String(node.disabled);node.disabled=true;}}
      else if(node.dataset.connectionDisabled!==undefined){node.disabled=node.dataset.connectionDisabled==='true';delete node.dataset.connectionDisabled;}
      }
    }
    // Help triggers remain available on read-only pages.
    for(const node of document.querySelectorAll('[data-help]'))node.disabled=false;
    backup?.refresh();
    updater?.refresh();
  }
  async function restorePreview(){
    if(restoringPreview)return restoringPreview;
    if(!previewPayload)return;
    const payload=previewPayload;previewPayload=null;snapshot=null;records.clear();lastGeneration=null;lastRevision='';
    renderedSnapshot=renderedWeb=renderedOLED=renderedCockpit=null;lastCockpitAt=0;
    restoringPreview=Promise.resolve().then(()=>applyPayload(payload)).then(()=>{studio.refresh();renderedSnapshot=null;render();}).catch(error=>tell(error.message)).finally(()=>{restoringPreview=null;});
    return restoringPreview;
  }
  function showArea(next){
    if(next==='client'){showPage(page);return;}
    if(!['update','documentation'].includes(next))return;
    if(next!=='client'&&area==='client'&&page==='board'&&!client.connected)onPreviewStart?.();
    selectArea(next);location.hash=next;if(next==='update')updater.refresh();
  }
  function selectArea(next){
    area=next;workspace.hidden=next!=='client';docsMount.hidden=next!=='documentation';updatePane.hidden=next!=='update';
    for(const [id,nav] of [['update',updateAreaButton],['client',clientAreaButton],['documentation',docsAreaButton]]){
      nav.classList.toggle('is-selected',next===id);nav.setAttribute('aria-pressed',String(next===id));
    }
    if(pollConnectionActive)schedulePolling();
  }
  function showPage(next){
    // Keep existing links/API callers working while Update is a separate area.
    if(next==='update'){showArea('update');return;}
    if(updater?.usbBusy){tell('The updater owns USB. Wait for it to finish before opening Client.');return;}
    if(devicePages.has(next)&&!client.connected){tell('Connect your board to open this page. Board, Harmony, Update and Documentation remain available offline.');next='board';}
    if(!panes.has(next))next='board';if(page==='board'&&next!=='board'&&!client.connected)onPreviewStart?.();page=next;selectArea('client');
    for(const [id,pane] of panes)pane.hidden=id!==next;
    for(const [id,nav] of subButtons){nav.classList.toggle('is-selected',id===next);nav.setAttribute('aria-current',id===next?'page':'false');}
    location.hash=`client/${next}`;if(next==='board'){resizeBoard();render();}if(next==='parameters')void refreshRecords(currentRows()).catch(error=>tell(error.message));backup.refresh();updater.refresh();
  }
  function setRailCollapsed(collapsed,persist=true){
    workspace.classList.toggle('rail-collapsed',collapsed);railToggle.setAttribute('aria-expanded',String(!collapsed));
    const label=collapsed?'Expand sidebar':'Collapse sidebar';railToggle.setAttribute('aria-label',label);railToggle.title=label;
    railToggle.querySelector('.rail-icon').textContent=collapsed?'»':'«';railToggle.querySelector('.rail-toggle-text').textContent=collapsed?'Expand':'Collapse';
    if(persist)try{globalThis.localStorage?.setItem('thy666.companion.sidebar.v1',collapsed?'collapsed':'expanded');}catch{/* Browser storage may be unavailable. */}
    resizeBoard();
  }
  function resizeBoard(){
    const width=expanded?1536:1280,scale=fit?Math.min(1.2,Math.max(.2,(scroll.clientWidth-4)/width)):1;
    face.style.width=width+'px';face.style.minWidth=width+'px';face.style.transform=`scale(${scale})`;
    scaleWrap.style.width=width*scale+'px';scaleWrap.style.height=face.offsetHeight*scale+'px';
    fitButton.classList.toggle('is-selected',fit);actualButton.classList.toggle('is-selected',!fit);
  }
  const resizeObserver=globalThis.ResizeObserver?new ResizeObserver(resizeBoard):null;resizeObserver?.observe(scroll);
  function setDisplay(next){
    display=next;webButton.classList.toggle('is-selected',next==='web');oledButton.classList.toggle('is-selected',next==='oled');
    webButton.setAttribute('aria-pressed',String(next==='web'));oledButton.setAttribute('aria-pressed',String(next==='oled'));
    instrument.querySelector('#hardware-display').hidden=next!=='oled';webScreen.hidden=next!=='web';render();
  }
  function toggleDisplaySize(){
    expanded=!expanded;cockpitMount.classList.toggle('screen-expanded',expanded);expand.setAttribute('aria-expanded',String(expanded));
    const label=expanded?'Collapse screen':'Expand screen';expand.setAttribute('aria-label',label);expand.title=label;
    resizeBoard();
  }
  function currentRows(){return studio.model.groups.find(group=>group.id===studio.snapshot().groupId)?.rows.filter(row=>row.status==='live'&&!row.retired&&!['info','text'].includes(row.type)).map(row=>row.id)||[];}
  function actionProbe(row){
    if(!row)return null;
    const count=engine.state.instanceCount(rowIndex.get(row.id));
    if(count===1)return {count,global:true};
    // Prefer projected family metadata if present. Older JS menu contracts
    // expose only C's instance count, so disambiguate equal-sized families by
    // their canonical row namespace/binding, never by a mirrored selector.
    const kinds=['global','arp','strummer','track','route','view','shortcut','pad','side'];
    const declared=row.instance_kind??row.instanceKind??row.family;
    let kind=typeof declared==='number'?kinds[declared]:declared;
    if(!kind){
      if(count===2)kind=row.id.startsWith('arps.')?'arp':row.id.startsWith('chords.')?'strummer':null;
      else if(count===9)kind=row.id.startsWith('views.')?'view':row.binding?.startsWith('midi.')?'route':null;
      else kind=({3:'side',12:'shortcut',21:'track',98:'pad'})[count];
    }
    const probeId=({arp:'arps.enabled',strummer:'chords.continuous',track:'sequencer.channel',
      route:'midi.channel',view:'views.slot1',shortcut:'engines.assignment',pad:'sensitivity.amount',
      side:'sensitivity.velocityMode'})[kind];
    const probeIndex=rowIndex.get(probeId),probe=MENU_ROWS[probeIndex];
    if(!probe||probe.status!=='live'||['action','info','text'].includes(probe.type)||
      engine.state.instanceCount(probeIndex)!==count)return null;
    return {count,index:probeIndex};
  }
  function instanceFor(row){
    if(!row||!client.connected||!client.compatible)return null;
    const cached=records.get(row.id);
    if(Number.isInteger(cached?.instance))return cached.instance;
    return row.type==='action'&&actionProbe(row)?.global?0:null;
  }
  async function refreshRecords(ids){
    if(!client.connected||!client.compatible||updater?.usbBusy)return;
    for(const id of [...new Set(ids)]){
      if(updater?.usbBusy)return;
      const index=rowIndex.get(id),row=MENU_ROWS[index];
      if(!row||row.status!=='live'||['info','text'].includes(row.type))continue;
      if(row.type==='action'){
        const probe=actionProbe(row);
        if(!probe){records.delete(id);continue;}
        if(probe.global){records.set(id,{instance:0,raw:0,value:null});continue;}
        records.delete(id);
        let result;
        try{result=await client.read(probe.index,255);}
        catch(error){studio.refresh();throw error;}
        if(!Number.isInteger(result.instance)||result.instance<0||result.instance>=probe.count)
          throw new Error('The device returned an invalid action target.');
        records.set(id,{instance:result.instance,raw:0,value:null});continue;
      }
      const result=await client.read(index,255);records.set(id,{...result,raw:result.value,value:studio.model.decode(row,result.value)});
    }
    studio.refresh();
  }
  async function readConfiguration(){
    if(updater?.usbBusy)return;
    const bytes=await client.downloadBackup(),info=inspectDeviceBackup(bytes,client.backupIdentity);
    if(!client.compatible)return;
    await applyPayload(info.payload);records.clear();lastGeneration=snapshot?.savedGeneration??null;
    await refreshRecords(currentRows());
  }
  async function toggleConnection(){
    if(updater.usbBusy){tell('The updater owns USB. Check its status before reconnecting the application.');return;}
    if(client.connected){await client.disconnect();await restorePreview();studio.refresh();render();return;}
    await run(async()=>{
      await restoringPreview;
      onPreviewStart?.();previewPayload=engine.settings.encode();
      await client.connect();await readConfiguration();await poll();tell('');
    });
  }
  async function poll(){
    if(polling||!client.connected||client.busy||updater?.usbBusy||disposed||!hostActive||document.hidden||area==='update'||page==='backups')return;polling=true;
    try{
      const packet=await client.snapshot();if(disposed)return;snapshot=decodeCompanionSnapshot(packet.bytes,client.identity);
      if(!hostActive||document.hidden||area==='update'||updater?.usbBusy)return;
      const signature=`${snapshot.instrumentRevision}:${snapshot.stateRevision}:${snapshot.layoutRevision}`;
      const generationChanged=lastGeneration!==null&&lastGeneration!==snapshot.savedGeneration;
      lastGeneration=snapshot.savedGeneration;
      if(generationChanged){await readConfiguration();lastGeneration=snapshot.savedGeneration;}
      else if(area==='client'&&page==='parameters'&&(signature!==lastRevision||Date.now()-lastValues>1000)){
        const ids=currentRows(),selected=studio.snapshot().selectedId;
        const slice=ids.slice(readIndex,readIndex+5);readIndex=ids.length?(readIndex+5)%ids.length:0;
        await refreshRecords([selected,...slice]);lastValues=Date.now();
      }
      lastRevision=signature;render();
    }catch(error){if(!/^(?:COMPANION\/1 )?ERR (?:busy|rate|snapshot-active)/.test(error.message))tell(error.message);}
    finally{polling=false;}
  }
  function render(){
    if(!client.connected){cockpit.update(null,{connected:false});return;}
    const live=snapshot;if(!live||!hostActive||document.hidden||area!=='client'||page!=='board')return;
    // Musical feedback remains immediate. Only an otherwise unchanged HUD's
    // diagnostic OLED-age counter is coalesced to one update per second.
    if(!sameCockpit(renderedCockpit,live)||Date.now()-lastCockpitAt>=1000){
      cockpit.update(live,{connected:true});renderedCockpit=live;lastCockpitAt=Date.now();
    }
    if(display==='web'&&(!renderedWeb||renderedWeb.panelVisible!==live.panelVisible||!sameValues(renderedWeb.frame,live.frame))){
      renderCompanionScreen(webScreen,live);renderedWeb=live;
    }
    if(display==='oled'&&(!renderedOLED||!sameValues(renderedOLED.frame,live.frame))){
      paintDeviceOLED(oledCanvas,live.frame);renderedOLED=live;
    }
    if(renderedSnapshot!==live){
      for(const node of padNodes){
        const id=Number(node.dataset.pad),rgb=live.rgb[id],peak=Math.max(...rgb);
        if(!renderedSnapshot||!sameValues(renderedSnapshot.rgb[id],rgb)){
          node.style.setProperty('--note-light',rgb.map(value=>peak?Math.round(value*255/peak):0).join(' '));
          node.style.setProperty('--note-level',Math.min(1,peak/8));
        }
        if(!renderedSnapshot||renderedSnapshot.held[id]!==live.held[id]){
          node.classList.toggle('is-held',live.held[id]);node.setAttribute('aria-pressed',String(live.held[id]));
        }
      }
      centerNodes.forEach((node,n)=>{
        if(!renderedSnapshot||renderedSnapshot.center[n]!==live.center[n]){node.style.setProperty('--control-level',live.center[n]/255);node.dataset.level=live.center[n];}
        if(!renderedSnapshot||renderedSnapshot.controlHeld[n]!==live.controlHeld[n])node.setAttribute('aria-pressed',String(live.controlHeld[n]));
      });
      for(const [node,n]of upperNodes)if(!renderedSnapshot||renderedSnapshot.controlHeld[n]!==live.controlHeld[n])node.setAttribute('aria-pressed',String(live.controlHeld[n]));
    }
    renderedSnapshot=live;

  }
  async function boardControl(target){
    if(updater?.usbBusy)return;
    if(target.matches('.pad')){selectedPad=Number(target.dataset.pad);await client.inspectPad(selectedPad);render();return;}
    const bank=['nav','function','root','mode'].indexOf(target.dataset.control);
    if(bank>=0){const list=[...instrument.querySelectorAll(`[data-control="${target.dataset.control}"]`)];await client.control(bank,list.indexOf(target));records.clear();await poll();}
  }
  const intercept=event=>{
    if(event.target.closest?.('.desktop-window-chrome'))return;
    if(updater.modalOpen&&!event.target.closest?.('.updater-confirmation')){event.preventDefault();event.stopImmediatePropagation();return;}
    if(client.connected&&[songMount,libraryMount,debugPanels].some(host=>host.contains(event.target))){event.preventDefault();event.stopImmediatePropagation();return;}
    const target=event.target.closest?.('.controller-face button');if(!target||!target.matches('.pad,[data-control]'))return;
    event.preventDefault();event.stopImmediatePropagation();
    if(client.connected&&event.type==='click')void run(()=>boardControl(target));
  };
  for(const type of ['pointerdown','pointerup','pointercancel','lostpointercapture','click','beforeinput','input','change'])document.addEventListener(type,intercept,true);
  const keyGuard=event=>{
    if(event.target.closest?.('.desktop-window-chrome')){event.stopPropagation();return;}
    if(updater?.usbBusy&&event.target.closest?.('.controller-face,.engine-groups,.engine-editor')){event.preventDefault();event.stopImmediatePropagation();return;}
    if(appSettings.modalOpen||updater.modalOpen){event.stopPropagation();return;}
    // Header, sidebar and presentation controls keep native keyboard activation.
    // Their Enter/arrow keys must never navigate the physical instrument.
    if(event.target.closest?.('.companion-topbar,.companion-rail,.board-toolbar,.companion-display-tools')&&['ArrowLeft','ArrowUp','ArrowRight','ArrowDown','Enter',' '].includes(event.key)){
      event.stopPropagation();return;
    }
    const instrumentKey=/^[0-9qwertyuiop ]$/i.test(event.key)||['ArrowLeft','ArrowUp','ArrowRight','ArrowDown','Enter','Escape'].includes(event.key);
    // Stop the instrument's window handlers without preventing native input,
    // link/button activation or the help dialog's own keyboard handling.
    if(area!=='client'||page!=='board'||help.contextOpen){
      if(instrumentKey)event.stopPropagation();return;
    }
    if(event.target.matches?.('input,textarea,select,[contenteditable="true"]')){if(instrumentKey)event.stopPropagation();return;}
    if(!client.connected){if(instrumentKey)event.stopPropagation();return;}
    const control=event.target.closest?.('.controller-face button');
    if(control&&['Enter',' '].includes(event.key)){event.preventDefault();event.stopImmediatePropagation();if(event.type==='keydown'&&!event.repeat)void run(()=>boardControl(control));return;}
    const nav={ArrowLeft:'left',ArrowUp:'up',Enter:'select',ArrowDown:'down',ArrowRight:'right'};
    if(nav[event.key]||/^[0-9qwertyuiop ]$/i.test(event.key)||event.key==='Escape'){
      event.preventDefault();event.stopImmediatePropagation();
      if(event.type==='keydown'&&!event.repeat&&nav[event.key])void run(()=>client.navigate(nav[event.key]));
    }
  };
  document.addEventListener('keydown',keyGuard,true);document.addEventListener('keyup',keyGuard,true);
  function cancelPolling(){++pollEpoch;if(pollTimer!==null){clearTimeout(pollTimer);pollTimer=null;}}
  function refreshInterval(){return area==='client'&&['board','parameters'].includes(page)?appSettings.pollIntervalMs:Math.max(1000,appSettings.pollIntervalMs);}
  function schedulePolling(delay=refreshInterval()){
    cancelPolling();if(disposed||!pollConnectionActive||!hostActive||updater?.usbBusy)return;
    const epoch=pollEpoch;
    pollTimer=setTimeout(async()=>{
      pollTimer=null;if(disposed||epoch!==pollEpoch)return;
      const started=Date.now();
      try{if(!document.hidden)await poll();}
      finally{
        // Pace starts rather than adding a full interval after a transfer.
        // A slow/busy connection never accumulates overlapping snapshots.
        if(!disposed&&pollConnectionActive&&epoch===pollEpoch)
          schedulePolling(Math.max(5,refreshInterval()-Math.max(0,Date.now()-started)));
      }
    },delay);
  }
  function setHostActive(active){
    const next=Boolean(active);if(next===hostActive||disposed)return;
    hostActive=next;
    updater?.setHostActive?.(hostActive);
    if(!hostActive)cancelPolling();else{render();if(pollConnectionActive)schedulePolling(0);}
  }
  const visibility=()=>{if(document.hidden)cancelPolling();else if(hostActive&&pollConnectionActive){render();schedulePolling(0);}};
  document.addEventListener('visibilitychange',visibility);
  const unloadUpdate=event=>{
    if(!(browserDirect||runtime?.kind==='browser-direct')||!updater?.usbBusy&&!(area==='update'&&client.busy))return;
    event.preventDefault();event.returnValue='';
  };
  window.addEventListener('beforeunload',unloadUpdate);
  const route=()=>{const hash=location.hash.slice(1);if(hash==='update')showArea('update');else if(hash.startsWith('documentation')){showArea('documentation');if(!help.selected)help.open('getting-started');}else if(hash.startsWith('client/'))showPage(hash.slice(7));else showPage('board');};
  window.addEventListener('hashchange',route);setDisplay('web');route();updateConnection(client.status);
  let savedRail=false;try{savedRail=globalThis.localStorage?.getItem('thy666.companion.sidebar.v1')==='collapsed';}catch{/* Use the expanded navigation without storage. */}setRailCollapsed(savedRail,false);resizeBoard();
  return {
    client,help,updater,get connected(){return client.connected;},get snapshot(){return snapshot;},
    updatePreview(){/* The disconnected board has no playable preview. */},
    render,showPage,showArea,setHostActive,
    destroy(){disposed=true;cancelPolling();resizeObserver?.disconnect();window.removeEventListener('hashchange',route);window.removeEventListener('beforeunload',unloadUpdate);document.removeEventListener('visibilitychange',visibility);for(const type of ['pointerdown','pointerup','pointercancel','lostpointercapture','click','beforeinput','input','change'])document.removeEventListener(type,intercept,true);document.removeEventListener('keydown',keyGuard,true);document.removeEventListener('keyup',keyGuard,true);help.destroy();backup.destroy();updater.destroy();appSettings.destroy();cockpit.destroy();void client.disconnect();},
  };
}
