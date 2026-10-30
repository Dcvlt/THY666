/* SPDX-License-Identifier: GPL-3.0-or-later
 * Client owns connection and navigation. The C engine still owns music,
 * menus and settings; the help center is read-only and independently routed.
 */
import { STUDIO_MENU, MENU_ROWS } from './studio-menu.js';
import { createDeviceClient, inspectDeviceBackup } from './device-client.js';
import { createCore } from './core.js';
import { createBackupPayloadMigrator } from './backup-migration.js';
import { decodeCompanionSnapshot, decodeHarmonySnapshot, decodePadMapSnapshot, paintDeviceOLED } from './companion-protocol.js';
import { renderCompanionScreen } from './companion-display.js';
export { renderCompanionScreen };
import { initializeHelp } from './help.js';
import { initializeBackupPanel } from './backup-panel.js';
import { initializeUpdaterPanel } from './updater-panel.js';
import { createBrowserUpdaterClient } from './browser-updater-client.js';
import { initializeAppSettings } from './app-settings.js';
import { initializeHarmonyGuide } from './harmony-guide.js';
import { initializeCockpit, initializeDiagnostics } from './cockpit.js';
import { createUIIcon, createUIIconCorners } from './ui-system.js';

const devicePages=new Set(['parameters','harmony','library','backups','diagnostics']);
const el=(tag,className='',text)=>{
  const node=document.createElement(tag);node.className=className;if(text!==undefined)node.textContent=String(text);return node;
};
const button=(text,className='',action)=>{const node=el('button',`${className.includes('hud-choice')?'':'hud-action '}${className}`.trim(),text);node.type='button';if(action)node.addEventListener('click',action);return node;};
const clear=node=>{if(node)node.replaceChildren();return node;};
const sameValues=(a,b)=>a===b||Boolean(a&&b&&a.length===b.length&&a.every((value,index)=>value===b[index]));
const cockpitFields=['heldPads','noteCount','tempo','scale','harmony','root','clockSource','clockStatus','mpe','arp1','arp2','running','recording','engine','sequenceOpen','selectedTrack','capture','generated'];
function sameCockpit(a,b){
  return Boolean(a&&b&&cockpitFields.every(key=>a[key]===b[key])&&a.inspect?.logical===b.inspect?.logical&&
    a.context?.revision===b.context?.revision&&
    ['held','pressure','aftertouchStage','heldVelocity','velocityMode','fixedVelocity','sounding','pool'].every(key=>sameValues(a[key],b[key])));
}

/** Read-only settings projection. Work is bounded by the existing snapshot poll. */
export function createCockpitContextCache({read,rows=MENU_ROWS}={}){
  const indices=new Map(rows.map((row,index)=>[row.id,index]));
  const fields=[['mpeOutput','mpeOutput',0,2],['profileEnabled','aftertouchStage',0,1],
    ['pressureMode','atMode',0,2],['pressureBend','bend',0,2],['bendRange','bendRange',1,24]];
  const probes=fields.flatMap(([id,field,min,max])=>[1,2].map(instance=>({
    row:indices.get(`performance.${id}`),field,min,max,instance,
  })));
  let current=null,key=null,cursor=0,epoch=0,revision=0,busy=false,deferred=false;
  const publish=(expression,pending)=>{current={revision:++revision,expression,pending};};
  return {
    get current(){return current;},
    get deferred(){return deferred;},
    reset(){epoch++;key=null;cursor=0;current=null;deferred=false;},
    async refresh(live,{eligible=true,isCurrent=()=>true}={}){
      deferred=false;
      if(!eligible||!live||busy||!isCurrent())return current;
      const next=[live.uid,live.build,live.schema,live.stateRevision,live.savedGeneration].join(':');
      if(next!==key){
        epoch++;key=next;cursor=0;
        publish(Object.fromEntries(fields.map(([,field])=>[field,[null,null]])),true);
      }
      if(cursor>=probes.length)return current;
      const token=epoch;busy=true;
      try{
        // Never add another timer or a large settings sweep to live feedback.
        for(let count=0;count<2&&cursor<probes.length;count++){
          const probe=probes[cursor];
          if(!Number.isInteger(probe.row)){cursor++;publish(current.expression,cursor<probes.length);continue;}
          let result;
          try{result=await read(probe.row,probe.instance);}
          catch(error){
            if(token!==epoch||!isCurrent())return current;
            if(/^(?:COMPANION\/1 )?ERR (?:busy|rate|snapshot-active)/.test(error.message)){deferred=true;return current;}
            // Unsupported values remain unknown until the next settings revision.
            cursor++;publish(current.expression,cursor<probes.length);throw error;
          }
          if(token!==epoch||!isCurrent())return current;
          const value=result?.value;
          if(result?.row!==probe.row||result?.instance!==probe.instance||!Number.isInteger(value)||
            value!==-1&&(value<probe.min||value>probe.max)){
            cursor++;publish(current.expression,cursor<probes.length);
            throw new Error('The device returned invalid expression settings.');
          }
          const expression={...current.expression,[probe.field]:[...current.expression[probe.field]]};
          expression[probe.field][probe.instance-1]=probe.field==='aftertouchStage'&&value!==-1?Boolean(value):value;
          cursor++;publish(expression,cursor<probes.length);
        }
        return current;
      }finally{busy=false;}
    },
  };
}
/** Move existing navigation into a compact drawer; never duplicate device actions. */
export function initializeCompactNavigation({top,mainNav,clientNav,headerActions,rail}) {
  const doc=top.ownerDocument, media=globalThis.matchMedia?.('(max-width: 900px)');
  const toggle=button('', 'button hud-icon-button compact-nav-toggle',()=>open());
  toggle.setAttribute('aria-label','Open navigation');toggle.setAttribute('aria-expanded','false');
  toggle.setAttribute('aria-controls','compact-navigation');
  const icon=createUIIcon('menu',{document:doc});
  toggle.append(icon,createUIIconCorners({document:doc}));top.prepend(toggle);
  const overlay=el('div','compact-nav-overlay');overlay.hidden=true;
  const panel=el('section','compact-nav-panel hud-frame hud-frame--task');panel.id='compact-navigation';
  panel.setAttribute('role','dialog');panel.setAttribute('aria-modal','true');panel.setAttribute('aria-label','Navigation');
  const heading=el('div','compact-nav-heading'),closeButton=button('','button hud-icon-button',()=>close());
  closeButton.append(createUIIcon('close',{document:doc}),createUIIconCorners({document:doc}));
  closeButton.setAttribute('aria-label','Close navigation');heading.append(el('strong','','Navigate'),closeButton);
  const applications=el('div','compact-nav-group'),pages=el('div','compact-nav-group');
  applications.append(el('p','eyebrow','Application'));pages.append(el('p','eyebrow','Client'));
  panel.append(heading,applications,pages);overlay.append(panel);doc.body.append(overlay);
  let compact=false,opened=false;
  const suspended=new Map();
  function open(){
    if(!compact||opened)return;opened=true;overlay.hidden=false;
    for(const sibling of doc.body.children)if(sibling!==overlay){suspended.set(sibling,sibling.inert);sibling.inert=true;}
    toggle.setAttribute('aria-expanded','true');doc.body.classList.add('compact-nav-open');closeButton.focus();
  }
  function close(){
    if(!opened)return;opened=false;overlay.hidden=true;toggle.setAttribute('aria-expanded','false');
    for(const [node,inert] of suspended)node.inert=inert;suspended.clear();
    doc.body.classList.remove('compact-nav-open');toggle.focus();
  }
  function sync(){
    const next=Boolean(media?.matches);if(next===compact){toggle.hidden=!next;return;}
    close();compact=next;toggle.hidden=!compact;
    rail.classList.toggle('compact-nav-hidden',compact);
    if(compact){applications.append(mainNav);pages.append(clientNav);}
    else{headerActions.prepend(mainNav);rail.append(clientNav);}
  }
  const key=event=>{
    if(!opened)return;
    if(event.key==='Escape'){event.preventDefault();event.stopPropagation();close();}
    if(event.key==='Tab'){
      const items=[...panel.querySelectorAll('button,a[href]')].filter(node=>!node.disabled&&!node.hidden);
      const first=items[0],last=items.at(-1),active=doc.activeElement;
      if(event.shiftKey&&(active===first||!panel.contains(active))){event.preventDefault();last?.focus();}
      else if(!event.shiftKey&&(active===last||!panel.contains(active))){event.preventDefault();first?.focus();}
    }
  };
  const backdrop=event=>{if(event.target===overlay)close();};
  overlay.addEventListener('click',backdrop);doc.addEventListener('keydown',key,true);
  media?.addEventListener('change',sync);sync();
  return {close,destroy(){close();media?.removeEventListener('change',sync);doc.removeEventListener('keydown',key,true);overlay.removeEventListener('click',backdrop);headerActions.prepend(mainNav);rail.append(clientNav);rail.classList.remove('compact-nav-hidden');overlay.remove();toggle.remove();}};
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
  let configurationPending=false,configurationRetryAt=0;
  let harmonySnapshot=null,harmonyNeedsRefresh=true;
  let resetDialogOpen=false,resetBusy=false,resetDisplayedEpoch=0,resetFocus=null,resetRenderedFrame=null;
  let pollTimer=null,pollEpoch=0,pollConnectionActive=false,hostActive=true,lastConnectionKey='';
  let connectionEpoch=0,connectionIdentity='';
  let updater=null;
  let renderedSnapshot=null,renderedWeb=null,renderedOLED=null,renderedCockpit=null,lastCockpitAt=0;
  let cockpitActivity=null;
  let labelsEnabled=false,labelMap=null,labelMapPending=false,labelMapEpoch=0,labelMapReading=false;
  let labelStableKey='',labelInstrumentRevision=null,labelCheckAt=0;
  let labelJobStarted=0,labelJobPolls=0,labelRetryAt=0;
  const records=new Map(),panes=new Map(),subButtons=new Map(),rowIndex=new Map(MENU_ROWS.map((row,n)=>[row.id,n]));
  const oldActions=oldHeader.querySelector('.header-actions');
  const top=el('header','companion-topbar');const brand=oldHeader.querySelector('.wordmark');
  brand.setAttribute('aria-label','THY666 client home');brand.href='#client/board';
  for(const node of [...brand.children])if(node.tagName.toLowerCase()==='span')node.remove();
  brand.classList.add('companion-centered-brand');
  top.append(brand);
  const projectSite=document.documentElement.dataset.projectSite==='true';
  const mainNav=el('nav','companion-main-nav');mainNav.setAttribute('aria-label','Application areas');
  const updateAreaButton=button(projectSite?'Install':'Update','hud-choice hud-choice--nav',()=>showArea('update'));
  const clientAreaButton=button('Client','hud-choice hud-choice--nav is-selected',()=>showArea('client'));
  const docsAreaButton=button(projectSite?'User guide':'Documentation','hud-choice hud-choice--nav',()=>{showArea('documentation');help.open('getting-started');});
  for(const control of [updateAreaButton,clientAreaButton,docsAreaButton])control.dataset.traceOrigin='bottom';
  mainNav.append(updateAreaButton,clientAreaButton,docsAreaButton);
  if(projectSite){
    const home=el('a','button hud-action companion-project-link','Project');home.href='../index.html';
    home.addEventListener('click',event=>{if(updater?.usbBusy||client.busy)event.preventDefault();});
    mainNav.append(home);
  }
  const headerActions=el('div','companion-header-actions');headerActions.append(mainNav);top.append(headerActions);
  const connection=el('div','companion-connection'),status=el('span','connection-status','');status.setAttribute('role','status');status.hidden=true;
  const connect=button('Connect board','button companion-connect',()=>void toggleConnection());
  connection.append(status,connect);headerActions.append(connection);if(oldActions){oldActions.classList.add('legacy-status');top.append(oldActions);}oldHeader.replaceWith(top);
  const workspace=el('div','companion-workspace'),rail=el('aside','companion-rail'),content=el('main','companion-content hud-type-body');
  const clientNav=el('nav','companion-client-nav');clientNav.setAttribute('aria-label','Client pages');
  const railToggle=button('','button rail-toggle',()=>setRailCollapsed(!workspace.classList.contains('rail-collapsed')));
  railToggle.append(el('span','rail-icon'),el('span','rail-toggle-text','Collapse'));railToggle.firstChild.append(createUIIcon('arrowRight',{document}));railToggle.setAttribute('aria-controls','companion-client-navigation');
  clientNav.id='companion-client-navigation';rail.append(railToggle,clientNav);
  for(const [id,label] of [['board','Board'],['parameters','Parameters'],['harmony','Harmony'],['library','Songs & library'],['backups','Backups'],['diagnostics','Diagnostics']]){
    const nav=button('',id==='board'?'hud-choice hud-choice--nav is-selected':'hud-choice hud-choice--nav',()=>showPage(id));nav.dataset.page=id;nav.dataset.traceOrigin='left';
    nav.setAttribute('aria-label',label);nav.dataset.label=label;nav.title=label;nav.append(el('span','rail-icon'),el('span','rail-text',label));nav.firstChild.append(createUIIcon(id,{document}));clientNav.append(nav);subButtons.set(id,nav);
    const pane=el('section','companion-page');pane.id=`companion-${id}`;pane.hidden=id!=='board';panes.set(id,pane);content.append(pane);
  }
  const harmony=initializeHarmonyGuide(panes.get('harmony'),{families:engine.guidance.families,
    action:async(code,value)=>{if(updater?.usbBusy)throw new Error('The updater owns USB. Wait for it to finish.');
      await client.harmonyAction(code,value);harmonyNeedsRefresh=true;await poll();},
    control:async(bank,index)=>{if(updater?.usbBusy)throw new Error('The updater owns USB. Wait for it to finish.');
      await client.harmonyControl(bank,index);harmonyNeedsRefresh=true;await poll();},
    set:async(id,value)=>{if(updater?.usbBusy)throw new Error('The updater owns USB. Wait for it to finish.');
      const row=rowIndex.get(id);if(!Number.isInteger(row)||!id.startsWith('harmony.'))throw new Error('This Harmony setting is unavailable.');
      await client.set(row,0,value);records.clear();harmonyNeedsRefresh=true;await poll();},
    scaleLabel:value=>studio.model.options?.(MENU_ROWS[rowIndex.get('engines.assignment')])?.find(option=>option.value===value)?.label||`Scale ${value+1}`});
  workspace.append(rail,content);
  const updatePane=el('section','companion-page companion-update-area');updatePane.id='companion-update';updatePane.hidden=true;
  panes.set('update',updatePane);
  const docsMount=el('section','companion-docs');docsMount.hidden=true;docsMount.id='documentation';
  const notice=el('div','companion-notice hud-notice hud-notice--banner');notice.setAttribute('role','status');notice.hidden=true;
  const noticeIcon=el('span','companion-notice-icon');noticeIcon.append(createUIIcon('info',{document}));noticeIcon.setAttribute('aria-hidden','true');
  const noticeText=el('span','companion-notice-text');noticeText.setAttribute('data-notice-copy','');
  const noticeClose=button('','button hud-icon-button companion-notice-close',()=>tell(''));noticeClose.append(createUIIcon('close',{document}),createUIIconCorners({document}));noticeClose.setAttribute('aria-label','Dismiss message');
  notice.append(noticeIcon,noticeText,noticeClose);
  oldMain.before(notice,workspace,updatePane,docsMount);
  const compactNavigation=initializeCompactNavigation({top,mainNav,clientNav,headerActions,rail});
  const resetOverlay=el('div','companion-reset-overlay');resetOverlay.hidden=true;
  const resetDialog=el('section','companion-reset-dialog hud-frame hud-frame--task'),resetTitle=el('h2','','Reset options'),resetDescription=el('p','','Review the device’s captured reset scope below. Nothing is reset until you explicitly confirm this request.'),resetScreen=el('div','companion-reset-screen'),resetError=el('p','companion-reset-error hud-field-error');
  resetTitle.id='companion-reset-title';resetDescription.id='companion-reset-description';
  resetDialog.setAttribute('role','alertdialog');resetDialog.setAttribute('aria-modal','true');resetDialog.setAttribute('aria-labelledby',resetTitle.id);resetDialog.setAttribute('aria-describedby',resetDescription.id);resetError.setAttribute('role','alert');
  const resetButtons=el('div','harmony-tabs'),resetCancel=button('Cancel','button hud-action--quiet',()=>void resolveReset(false)),resetConfirm=button('Reset options','button hud-action--danger',()=>void resolveReset(true));
  resetButtons.append(resetCancel,resetConfirm);resetDialog.append(resetTitle,resetDescription,resetScreen,resetError,resetButtons);resetOverlay.append(resetDialog);document.body.append(resetOverlay);
  const pageTitle=el('h1','','Board'),firmwareLabel=el('p','eyebrow','THY666');
  const boardHead=el('div','companion-page-head');const titleWrap=el('div','');
  titleWrap.append(firmwareLabel,pageTitle);
  const toolbar=el('div','board-toolbar');
  const webButton=button('Web','is-selected',()=>setDisplay('web')),oledButton=button('OLED','',()=>setDisplay('oled'));
  const displaySwitch=el('div','segmented');displaySwitch.setAttribute('role','group');displaySwitch.setAttribute('aria-label','Board display');displaySwitch.append(webButton,oledButton);
  const expand=button('','button button-quiet hud-icon-button screen-expand',()=>toggleDisplaySize());expand.append(createUIIcon('expand',{document}),createUIIconCorners({document}));expand.setAttribute('aria-label','Expand screen');expand.setAttribute('aria-expanded','false');expand.setAttribute('aria-controls','companion-instrument-screen');expand.title='Expand screen';
  const labelsButton=button('Labels','button hud-action--compact',()=>toggleLabels());labelsButton.setAttribute('aria-pressed','false');
  toolbar.append(labelsButton);
  boardHead.append(titleWrap,toolbar);panes.get('board').append(boardHead);
  const instrument=document.querySelector('.instrument'),face=instrument.querySelector('.controller-face'),scroll=instrument.querySelector('.controller-scroll');
  const padNodes=[...instrument.querySelectorAll('.pad')];
  const padLabels=padNodes.map(node=>{
    let name=node.querySelector('.pad-name');if(!name){name=el('span','pad-name');node.append(name);}
    name.textContent='';const note=node.querySelector('.pad-note');if(note)note.textContent='';
    return {node,name,baseLabel:node.getAttribute('aria-label')||`${Number(node.dataset.pad)<48?'Left':'Right'} pad ${Number(node.dataset.pad)%48+1}`};
  });
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
  const parameterHelp=button('Read the settings guide','button hud-action--quiet');parameterHelp.dataset.help='settings';parameterHead.append(parameterHelp);
  const parameterSource=el('p','client-parameter-source hud-notice','Connect your board to edit parameters.');
  document.querySelector('#engine-studio').classList.add('hud-frame','hud-frame--task');
  panes.get('parameters').append(parameterHead,parameterSource,document.querySelector('#engine-studio'));
  const backupHead=el('div','companion-page-head');backupHead.append(el('div','',''));backupHead.firstChild.append(el('p','eyebrow','YOUR SAVED INSTRUMENT'),el('h1','','Backups'));
  const backupHelp=button('What is included?','button hud-action--quiet');backupHelp.dataset.help='backup';backupHead.append(backupHelp);panes.get('backups').append(backupHead);
  const backupMount=el('div','backup-mount');panes.get('backups').append(backupMount);
  const updateMount=el('div','updater-mount');panes.get('update').append(updateMount);
  const libraryHead=el('div','companion-page-head');libraryHead.append(el('div','',''));libraryHead.firstChild.append(el('p','eyebrow','COMPOSE & ORGANIZE'),el('h1','','Songs & library'));
  const libraryHelp=button('Workflow guide','button hud-action--quiet');libraryHelp.dataset.help='sequencer-editor';libraryHead.append(libraryHelp);
  const libraryNote=el('p','client-parameter-source hud-notice','Use the shared engine to try sequences and custom musical material.');
  const libraryTabs=el('div','client-local-tabs'),songMount=el('div','client-local-panel'),libraryMount=el('div','client-local-panel');libraryMount.hidden=true;
  for(const [id,label,mount] of [['songs','Sequencer',songMount],['library','Custom library',libraryMount]]){
    const tab=button(label,id==='songs'?'hud-choice hud-choice--nav is-selected':'hud-choice hud-choice--nav',()=>{songMount.hidden=id!=='songs';libraryMount.hidden=id!=='library';for(const b of libraryTabs.children)b.classList.toggle('is-selected',b===tab);});tab.dataset.traceOrigin='bottom';libraryTabs.append(tab);
  }
  songMount.append(document.querySelector('#sequencer-lab'));libraryMount.append(document.querySelector('#library-lab'));
  const legacyTransfer=songMount.querySelector('[data-device="connect"]')?.closest('details');if(legacyTransfer)legacyTransfer.hidden=true;
  panes.get('library').append(libraryHead,libraryNote,libraryTabs,songMount,libraryMount);
  const debugHead=el('div','companion-page-head');debugHead.append(el('div','',''));debugHead.firstChild.append(el('p','eyebrow','OBSERVE & UNDERSTAND'),el('h1','','Diagnostics'));
  const debugHelp=button('Telemetry guide','button hud-action--quiet');debugHelp.dataset.help='companion';debugHead.append(debugHelp);panes.get('diagnostics').append(debugHead);
  const diagnostics=initializeDiagnostics(panes.get('diagnostics'));
  const telemetry=el('div','client-telemetry hud-frame hud-frame--task'),telemetryText=el('pre','telemetry-output hud-scroll-region hud-type-mono','Connect to read device telemetry.');
  const telemetryControls=el('div','telemetry-controls'),telemetryPick=el('select','hud-field');telemetryPick.setAttribute('aria-label','Telemetry report');
  for(const [name,label] of [['status','Activity'],['io','MIDI & scanning'],['settings','Saved settings'],['display','Display timing']]){const option=el('option','',label);option.value=name;telemetryPick.append(option);}
  let telemetryReading=false,telemetryLive=false,telemetrySamples=0,telemetryLastRead=0,telemetryEpoch=0;
  const telemetrySession=el('p','telemetry-session','');telemetrySession.setAttribute('role','status');
  function stopTelemetry(){telemetryEpoch++;if(telemetryLive)telemetrySession.textContent=`Stopped · ${telemetrySamples} reports`;telemetryLive=false;liveTelemetry.textContent='Start live session';liveTelemetry.setAttribute('aria-pressed','false');}
  const liveTelemetry=button('Start live session','button',()=>{
    if(telemetryLive){stopTelemetry();return;}
    if(!client.connected||updater?.usbBusy)return;
    telemetryEpoch++;telemetryLive=true;telemetrySamples=0;telemetryLastRead=0;diagnostics.reset();
    liveTelemetry.textContent='Stop live session';liveTelemetry.setAttribute('aria-pressed','true');
    telemetrySession.textContent='Live · updates once per second while this page is visible.';
    void poll();
  });
  const readTelemetry=button('Read report','button',()=>{
    if(telemetryReading||!client.connected||updater?.usbBusy)return;
    const command=telemetryPick.value;telemetryReading=true;readTelemetry.disabled=true;telemetryPick.disabled=true;
    telemetryText.textContent='Reading…';
    void run(async()=>{
      try{telemetryText.textContent=await client.telemetry(command);diagnostics.report(command,telemetryText.textContent);}
      catch(error){telemetryText.textContent='Read failed. Try again.';throw error;}
      finally{telemetryReading=false;readTelemetry.disabled=!client.connected||Boolean(updater?.usbBusy);telemetryPick.disabled=readTelemetry.disabled;}
    });
  });
  const rawTelemetry=el('details','diagnostics-raw hud-disclosure');rawTelemetry.append(el('summary','','Raw report'),telemetryText);
  telemetryControls.append(telemetryPick,readTelemetry,liveTelemetry);telemetry.append(el('h2','','Monitor'),telemetryControls,telemetrySession,rawTelemetry);panes.get('diagnostics').insertBefore(telemetry,panes.get('diagnostics').querySelector('.diagnostics-dashboard'));
  const debugTabs=el('div','client-local-tabs'),debugPanels=el('div','debug-panels');const advancedDiagnostics=el('details','diagnostics-advanced hud-disclosure');advancedDiagnostics.append(el('summary','','Advanced inspection'),debugTabs,debugPanels);panes.get('diagnostics').append(advancedDiagnostics);
  const debugEntries=[['ownership','Ownership & events',[]],['sensors','Sensors',[document.querySelector('#firmware-studio')]],['arps','Arps',[document.querySelector('#arp-lab')]],['expression','MPE',[document.querySelector('#mpe-lab')]],['clock','Clock',[document.querySelector('#rhythm-lab')]],['reference','Reference tools',[]]];
  for(const [id,label,nodes] of debugEntries){
    const pane=el('div','debug-panel');pane.dataset.debug=id;pane.hidden=id!=='ownership';
    for(const node of nodes)if(node)pane.append(node);
    if(id==='ownership'){for(const selector of ['.diagnostic-grid','.timeline-panel']){const node=document.querySelector(selector);if(node)pane.append(node);}}
    if(id==='sensors'&&!pane.childNodes.length){const node=document.querySelector('.firmware-studio');if(node)pane.append(node);}
    if(id==='reference'){for(const node of [legacyTools,routes,expression,document.querySelector('.control-surface')])if(node)pane.append(node);}
    const tab=button(label,id==='ownership'?'hud-choice hud-choice--nav is-selected':'hud-choice hud-choice--nav',()=>{for(const node of debugPanels.children)node.hidden=node!==pane;for(const b of debugTabs.children)b.classList.toggle('is-selected',b===tab);});tab.dataset.traceOrigin='bottom';debugTabs.append(tab);debugPanels.append(pane);
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
  for(const head of document.querySelectorAll('.companion-page-head')){
    const rule=el('span','hud-rule companion-heading-rule');rule.setAttribute('aria-hidden','true');head.append(rule);
  }
  const help=initializeHelp({mount:docsMount,onNavigate:()=>showArea('documentation')});
  const appSettings=initializeAppSettings({onPollIntervalChange:ms=>{if(pollConnectionActive&&!disposed)schedulePolling(ms);}});
  const client=createClient({expectedSchema:engine.state.schemaHash(),onState:updateConnection,
    migrateBackupPayload:createBackupPayloadMigrator({createEngine:()=>createCore()})});
  const cockpitContext=createCockpitContextCache({read:(row,instance)=>client.read(row,instance)});
  const backup=initializeBackupPanel({mount:backupMount,client,getEngine:()=>engine,onApplied:async()=>{await readConfiguration();await poll();},runtime});
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
  function tell(message){noticeText.textContent=String(message);notice.hidden=!message;top.classList.toggle('has-notice',Boolean(message));externalNotice(message);}
  async function run(operation){try{return await operation();}catch(error){tell(error.message);return null;}}
  function updateConnection(state){
    if(!state.connected&&!state.connecting&&previewPayload)void restorePreview();
    const updateBusy=Boolean(updater?.usbBusy);
    // Busy/progress notifications occur for every serial transaction/chunk.
    // Structural board state changes only at connection or ownership edges.
    const nextIdentity=state.connected?[state.identity?.uid,state.identity?.build,state.identity?.schema,state.compatible].join(':'):'';
    const connectionKey=[state.connected,state.connecting,state.compatible,nextIdentity,client.available,updateBusy].join(':');
    if(connectionKey===lastConnectionKey){
      if(area==='client'&&page==='backups')backup?.refresh();
      if(area==='update'||updater?.modalOpen||updateBusy)updater?.refresh();
      return;
    }
    lastConnectionKey=connectionKey;
    if(nextIdentity!==connectionIdentity){
      connectionIdentity=nextIdentity;connectionEpoch++;cockpitContext.reset();snapshot=null;
      renderedSnapshot=renderedWeb=renderedOLED=renderedCockpit=null;lastCockpitAt=0;
      labelMapEpoch++;labelsEnabled=false;labelMap=null;labelMapPending=false;labelStableKey='';labelInstrumentRevision=null;
      labelJobStarted=labelJobPolls=labelRetryAt=0;
      renderPadLabels();
    }
    status.textContent=updateBusy?'Updater owns USB':state.connecting?'Connecting':state.connected&&!state.compatible?'Read-only':'';status.hidden=!status.textContent;
    status.dataset.mode=state.connected?'connected':'preview';connect.textContent=state.connected?'Disconnect':'Connect board';connect.disabled=state.connecting||client.available===false||updateBusy;
    connect.title=updateBusy?'The updater owns USB. Wait for its terminal status before reconnecting the application.':client.available===false?
      runtime?.kind==='desktop'?'Native USB is unavailable. Restart the desktop app and check its diagnostics.':'USB requires Web Serial. Open this app in Chrome or Edge over HTTPS or localhost to connect.':'';
    firmwareLabel.textContent=state.connected?`THY666 / ${state.identity.build.slice(0,8)}`:'THY666';
    if(!state.connected){harmonySnapshot=null;harmonyNeedsRefresh=true;}
    harmony.update(harmonySnapshot,{connected:state.connected,supported:client.harmonySupported,writable:state.compatible,controlsReady:harmonyControlsReady()});
    syncResetConfirmation();
    parameterSource.textContent=state.connected?(state.compatible?'RIGHT applies on the board and autosave follows.':'This client and firmware have different menus. Monitoring and backups remain available; parameter writes are disabled.'):'Connect your board to edit parameters.';
    libraryNote.textContent=state.connected?'Use the board or Parameters to edit songs and library settings. These diagrams mirror the shared engine.':'Connect your board to inspect songs and library settings.';
    document.body.classList.toggle('device-connected',state.connected);
    face.inert=!state.connected||updateBusy;workspace.inert=updateBusy;clientAreaButton.disabled=updateBusy;instrument.hidden=!state.connected;standby.hidden=state.connected;onboarding.hidden=state.connected;
    toolbar.hidden=!state.connected;displayTools.hidden=!state.connected;
    for(const node of face.querySelectorAll('button'))node.disabled=!state.connected||updateBusy;
    if(!state.connected||!snapshot)cockpit.update(null,{connected:false});
    syncCockpitActivity();
    if(!state.connected)diagnostics.snapshot(null,false);
    if(!state.connected){expanded=false;cockpitMount.classList.remove('screen-expanded');expand.setAttribute('aria-expanded','false');}
    resizeBoard();
    for(const [id,nav] of subButtons){nav.disabled=updateBusy||devicePages.has(id)&&!state.connected;nav.title=updateBusy?'The updater owns USB. Wait for it to finish.':nav.disabled?`${nav.dataset.label} — Connect your board to use this page`:nav.dataset.label;nav.setAttribute('aria-disabled',String(nav.disabled));}
    if(!state.connected&&area==='client'&&devicePages.has(page))showPage('board');
    // The client serializes reads behind a snapshot already in flight. Do not
    // flicker this control disabled on every background poll: a pointer click
    // can otherwise lose its mouse-up while the next snapshot starts.
    readTelemetry.disabled=!state.connected||telemetryReading||updateBusy;telemetryPick.disabled=!state.connected||telemetryReading||updateBusy;
    liveTelemetry.disabled=!state.connected||updateBusy;
    if(!state.connected||updateBusy)stopTelemetry();
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
    configurationPending=false;configurationRetryAt=0;
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
    if(next!=='client')stopTelemetry();
    compactNavigation.close();
    area=next;workspace.hidden=next!=='client';docsMount.hidden=next!=='documentation';updatePane.hidden=next!=='update';
    syncCockpitActivity();
    for(const [id,nav] of [['update',updateAreaButton],['client',clientAreaButton],['documentation',docsAreaButton]]){
      nav.classList.toggle('is-selected',next===id);nav.setAttribute('aria-pressed',String(next===id));
    }
    if(pollConnectionActive)schedulePolling();
    syncResetConfirmation();
  }
  function showPage(next){
    if(next!=='diagnostics')stopTelemetry();
    // Keep existing links/API callers working while Update is a separate area.
    if(next==='update'){showArea('update');return;}
    if(updater?.usbBusy){tell('The updater owns USB. Wait for it to finish before opening Client.');return;}
    if(devicePages.has(next)&&!client.connected){tell('Connect your board to open this page.');next='board';}
    if(!panes.has(next))next='board';if(page==='board'&&next!=='board'&&!client.connected)onPreviewStart?.();page=next;selectArea('client');
    for(const [id,pane] of panes)pane.hidden=id!==next;
    for(const [id,nav] of subButtons){nav.classList.toggle('is-selected',id===next);nav.setAttribute('aria-current',id===next?'page':'false');}
    syncCockpitActivity();
    location.hash=`client/${next}`;if(next==='diagnostics')diagnostics.snapshot(snapshot,client.connected);if(next==='board'){resizeBoard();render();}if(next==='harmony'){harmonyNeedsRefresh=true;void poll();}if(next==='parameters')void refreshRecords(currentRows()).catch(error=>tell(error.message));backup.refresh();updater.refresh();
  }
  function setRailCollapsed(collapsed,persist=true){
    workspace.classList.toggle('rail-collapsed',collapsed);railToggle.setAttribute('aria-expanded',String(!collapsed));
    const label=collapsed?'Expand sidebar':'Collapse sidebar';railToggle.setAttribute('aria-label',label);railToggle.title=label;
    railToggle.querySelector('.rail-icon').replaceChildren(createUIIcon('arrowRight',{document}));railToggle.querySelector('.rail-toggle-text').textContent=collapsed?'Expand':'Collapse';
    if(persist)try{globalThis.localStorage?.setItem('thy666.companion.sidebar.v1',collapsed?'collapsed':'expanded');}catch{/* Browser storage may be unavailable. */}
    resizeBoard();
  }
  function resizeBoard(){
    const width=expanded?1536:1280,scale=Math.min(1.2,Math.max(0,scroll.clientWidth-4)/width);
    face.style.width=width+'px';face.style.minWidth=width+'px';face.style.transform=`scale(${scale})`;
    scaleWrap.style.width=width*scale+'px';scaleWrap.style.height=face.offsetHeight*scale+'px';
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
    if(updater?.usbBusy)return false;
    const bytes=await client.downloadBackup(),info=inspectDeviceBackup(bytes,client.backupIdentity);
    if(!client.compatible){configurationPending=false;configurationRetryAt=0;return true;}
    await applyPayload(info.payload);records.clear();
    await refreshRecords(currentRows());
    lastGeneration=snapshot?.savedGeneration??null;configurationPending=false;configurationRetryAt=0;
    return true;
  }
  async function refreshConfigurationAutomatically(){
    if(!configurationPending||Date.now()<configurationRetryAt)return;
    // A save, physical input or frozen snapshot may temporarily own staging.
    // Keep sampling/rendering and retry at most once per second, without
    // treating a deferred mirror refresh as a failed explicit backup.
    configurationRetryAt=Date.now()+1000;
    try{await readConfiguration();}
    catch(error){if(!/^ERR backup-busy$/.test(error.message))throw error;}
  }
  async function toggleConnection(){
    if(updater.usbBusy){tell('The updater owns USB. Check its status before reconnecting the application.');return;}
    if(client.connected){await client.disconnect();await restorePreview();studio.refresh();render();return;}
    await run(async()=>{
      await restoringPreview;
      onPreviewStart?.();previewPayload=engine.settings.encode();
      try { await client.connect(); }
      catch(error){if(error.name==='NotFoundError'||/No port selected by the user/.test(error.message)){tell('Connection cancelled.');return;}throw error;}
      configurationPending=true;configurationRetryAt=0;await refreshConfigurationAutomatically();await poll();tell('');
    });
  }
  async function poll(){
    if(polling||!client.connected||client.busy||updater?.usbBusy||disposed||!hostActive||document.hidden||area==='update')return;polling=true;
    const epoch=connectionEpoch,identity={...client.identity};
    const current=()=>!disposed&&client.connected&&epoch===connectionEpoch&&
      ['uid','build','schema'].every(key=>identity[key]===client.identity?.[key]);
    try{
      // A pending label job owns staging; complete/cancel that lease before a
      // base snapshot rather than letting the two readers block one another.
      if(labelMapPending){
        if(!labelsWanted())await cancelPadLabels();else await refreshPadLabels(snapshot);
        if(!current()||labelMapPending)return;
      }
      const packet=await client.snapshot();if(!current())return;snapshot=decodeCompanionSnapshot(packet.bytes,identity);
      if(area==='client'&&page==='diagnostics')diagnostics.snapshot(snapshot,true);
      syncResetConfirmation();
      render(); // Validated musical feedback does not wait for optional settings reads.
      if(!hostActive||document.hidden||area==='update'||updater?.usbBusy)return;
      if(telemetryLive&&area==='client'&&page==='diagnostics'&&!telemetryReading&&Date.now()-telemetryLastRead>=1000){
        telemetryReading=true;telemetryLastRead=Date.now();
        const command=telemetryPick.value||'status',epoch=telemetryEpoch;
        try{
          const report=await client.telemetry(command);
          if(!disposed&&telemetryLive&&client.connected&&epoch===telemetryEpoch&&command===(telemetryPick.value||'status')){telemetryText.textContent=report;diagnostics.report(command,report);telemetrySession.textContent=`Live · ${++telemetrySamples} reports · latest ${new Date().toLocaleTimeString()}`;}
        }catch(error){if(epoch===telemetryEpoch){stopTelemetry();telemetrySession.textContent='Session stopped. Read a report to retry.';throw error;}}
        finally{telemetryReading=false;}
      }
      if(area==='client'&&page==='backups')return; // Monitor modal state without background configuration exports.
      const signature=`${snapshot.instrumentRevision}:${snapshot.stateRevision}:${snapshot.layoutRevision}`;
      if(lastGeneration===null&&!configurationPending)lastGeneration=snapshot.savedGeneration;
      if(lastGeneration!==null&&lastGeneration!==snapshot.savedGeneration)configurationPending=true;
      if(configurationPending)await refreshConfigurationAutomatically();
      else if(area==='client'&&page==='parameters'&&(signature!==lastRevision||Date.now()-lastValues>1000)){
        const ids=currentRows(),selected=studio.snapshot().selectedId;
        const slice=ids.slice(readIndex,readIndex+5);readIndex=ids.length?(readIndex+5)%ids.length:0;
        await refreshRecords([selected,...slice]);lastValues=Date.now();
      }
      if(!current())return;
      await cockpitContext.refresh(snapshot,{eligible:client.compatible&&cockpitVisible(),isCurrent:()=>current()&&cockpitVisible()});
      if(!current())return;
      if(cockpitContext.deferred){render();return;}
      // The base lease is already released before this optional cached read.
      // Hidden pages and unchanged revisions add no Harmony transfer traffic.
      if(wantsHarmonySnapshot()&&client.harmonySupported&&
        (harmonyNeedsRefresh||harmonySnapshot?.revision!==snapshot.harmonyRevision)){
        const packet=await client.harmonySnapshot();
        if(!current()||!wantsHarmonySnapshot())return;
        harmonySnapshot=decodeHarmonySnapshot(packet.bytes,identity);harmonyNeedsRefresh=false;
      }
      if(area==='client'&&page==='harmony')harmony.update(harmonySnapshot,{connected:client.connected,supported:client.harmonySupported,writable:client.compatible,controlsReady:harmonyControlsReady()});
      if(labelsWanted())await refreshPadLabels(snapshot);
      if(!current())return;
      lastRevision=signature;render();
    }catch(error){if(current()&&!/^(?:COMPANION\/1 )?ERR (?:busy|rate|snapshot-active)/.test(error.message))tell(error.message);}
    finally{polling=false;}
  }
  function cockpitVisible(){return Boolean(client.connected&&snapshot&&hostActive&&!document.hidden&&area==='client'&&page==='board'&&!updater?.usbBusy);}
  function wantsHarmonySnapshot(){return Boolean(hostActive&&!document.hidden&&area==='client'&&(page==='harmony'||page==='board'&&snapshot?.engine===6)&&!updater?.usbBusy);}
  function syncCockpitActivity(){
    if(labelMapPending&&!labelsWanted())void cancelPadLabels().catch(error=>{if(client.connected)tell(error.message);});
    const active=cockpitVisible();if(active===cockpitActivity)return;cockpitActivity=active;
    cockpitMount.classList.toggle('cockpit-motion-paused',!active);cockpit.setActive?.(active);
  }
  function labelsWanted(){return labelsEnabled&&Boolean(client.connected&&client.padMapSupported&&hostActive&&!document.hidden&&area==='client'&&page==='board'&&!updater?.usbBusy);}
  function renderPadLabels(){
    instrument.classList.toggle('show-pad-labels',labelsEnabled);labelsButton.classList.toggle('is-selected',labelsEnabled);
    labelsButton.setAttribute('aria-pressed',String(labelsEnabled));labelsButton.setAttribute('aria-busy',String(labelMapReading||labelMapPending));
    for(const {node,name,baseLabel} of padLabels){
      const record=labelsEnabled?labelMap?.pads[Number(node.dataset.pad)]:null,label=record?.label||'';
      if(name.textContent!==label)name.textContent=label;
      const description=label?`${baseLabel}: ${label}`:baseLabel;
      if(node.getAttribute('aria-label')!==description)node.setAttribute('aria-label',description);
    }
  }
  function toggleLabels(){
    if(!client.connected)return;
    if(!client.padMapSupported){tell('Labels require newer firmware.');return;}
    labelsEnabled=!labelsEnabled;
    if(labelsEnabled)labelRetryAt=0;
    if(!labelsEnabled)void cancelPadLabels().catch(error=>{if(client.connected)tell(error.message);});
    renderPadLabels();if(labelsEnabled)void poll();
  }
  async function cancelPadLabels(){
    labelMapEpoch++;const pending=labelMapPending;labelMapPending=false;
    labelJobStarted=labelJobPolls=0;
    if(pending&&client.connected)await client.cancelPadMap?.();
    renderPadLabels();
  }
  async function refreshPadLabels(live){
    if(!live||!labelsWanted()||labelMapReading)return;
    if(!labelMapPending&&Date.now()<labelRetryAt)return;
    const stable=[live.layoutRevision,live.stateRevision,live.harmonyRevision,live.engine,live.selectedTrack,live.sequenceOpen].join(':');
    const changed=stable!==labelStableKey;
    if(!labelMapPending&&labelMap&&!changed&&(live.instrumentRevision===labelInstrumentRevision||Date.now()<labelCheckAt))return;
    const epoch=connectionEpoch,token=labelMapEpoch,identity={...client.identity};
    labelMapReading=true;renderPadLabels();
    try{
      const packet=await client.padMapSnapshot(labelMap?.revision??null);
      if(disposed||!client.connected||epoch!==connectionEpoch)return;
      if(token!==labelMapEpoch||!labelsWanted()){
        if(packet.pending)await client.cancelPadMap?.();return;
      }
      if(packet.pending){
        if(labelMapPending)labelJobPolls++;else{labelJobStarted=Date.now();labelJobPolls=0;}
        labelMapPending=true;labelMap=null;
        if(labelJobPolls>=2||Date.now()-labelJobStarted>=300){
          await cancelPadLabels();labelRetryAt=Date.now()+1000;
        }
        renderPadLabels();return;
      }
      labelMapPending=false;labelJobStarted=labelJobPolls=labelRetryAt=0;
      if(!packet.unchanged)labelMap=decodePadMapSnapshot(packet.bytes,identity);
      if(!labelMap)throw new Error('The device did not return pad labels.');
      labelStableKey=stable;labelInstrumentRevision=live.instrumentRevision;labelCheckAt=Date.now()+1000;
    }catch(error){labelMapPending=false;throw error;}
    finally{labelMapReading=false;renderPadLabels();}
  }
  function contextualSnapshot(live){
    const cached=cockpitContext.current;
    const harmony=live.engine===6&&harmonySnapshot?.revision===live.harmonyRevision?harmonySnapshot:null;
    const revision=`${cached?.revision??0}:${harmony?.revision??'-'}:${live.harmonyRevision}`;
    if(live.context?.revision===revision)return live;
    return {...live,context:{revision,
      expression:cached?.expression??null,pending:cached?.pending??true,harmony}};
  }
  function harmonyControlsReady(){
    return Boolean(snapshot&&snapshot.level===0&&!snapshot.resetOpen&&!snapshot.editing&&!snapshot.info&&!snapshot.about&&!snapshot.design&&
      !snapshot.arpQuick&&!snapshot.sequenceOpen&&!snapshot.trackOptions);
  }
  function syncResetConfirmation(){
    const live=snapshot,show=Boolean(client.connected&&client.resetSupported&&live?.resetOpen&&area==='client'&&!updater?.usbBusy);
    if(!show){if(resetDialogOpen){resetDialogOpen=false;resetOverlay.hidden=true;resetDisplayedEpoch=0;resetRenderedFrame=null;resetError.textContent='';if(resetFocus?.isConnected)resetFocus.focus();resetFocus=null;}return;}
    const fresh=!resetDialogOpen||resetDisplayedEpoch!==live.resetEpoch;
    if(fresh){resetFocus=document.activeElement;resetError.textContent='';resetDisplayedEpoch=live.resetEpoch;resetRenderedFrame=null;}
    resetDialogOpen=true;resetOverlay.hidden=false;
    const group=client.compatible?STUDIO_MENU.groups[live.resetGroup]?.label:null;
    resetTitle.textContent=live.resetTrack<21?`Reset track ${live.resetTrack+1} options`:group?`Reset ${group} options`:'Device reset confirmation';
    if(!resetRenderedFrame||!sameValues(resetRenderedFrame,live.frame)){renderCompanionScreen(resetScreen,live);resetRenderedFrame=live.frame;}
    resetCancel.disabled=resetConfirm.disabled=resetBusy||!client.compatible;
    if(fresh&&!resetCancel.disabled)resetCancel.focus();
  }
  async function resolveReset(confirmed){
    if(!resetDialogOpen||resetBusy||!client.connected||!client.compatible)return;
    const epoch=resetDisplayedEpoch;resetBusy=true;resetError.textContent='';syncResetConfirmation();
    try{await client.confirmReset(epoch,confirmed);records.clear();await poll();}
    catch(error){resetError.textContent=error.message;}
    finally{resetBusy=false;syncResetConfirmation();}
  }
  function render(){
    syncCockpitActivity();
    if(!client.connected){cockpit.update(null,{connected:false});return;}
    if(!cockpitVisible())return;
    const live=contextualSnapshot(snapshot);snapshot=live;
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
    if(resetDialogOpen&&!event.target.closest?.('.companion-reset-dialog,.companion-connect')){event.preventDefault();event.stopImmediatePropagation();return;}
    if(client.connected&&[songMount,libraryMount,debugPanels].some(host=>host.contains(event.target))){event.preventDefault();event.stopImmediatePropagation();return;}
    const target=event.target.closest?.('.controller-face button');if(!target||!target.matches('.pad,[data-control]'))return;
    event.preventDefault();event.stopImmediatePropagation();
    if(client.connected&&event.type==='click')void run(()=>boardControl(target));
  };
  for(const type of ['pointerdown','pointerup','pointercancel','lostpointercapture','click','beforeinput','input','change'])document.addEventListener(type,intercept,true);
  const keyGuard=event=>{
    if(event.target.closest?.('.desktop-window-chrome')){event.stopPropagation();return;}
    if(updater?.usbBusy&&event.target.closest?.('.controller-face,.engine-groups,.engine-editor')){event.preventDefault();event.stopImmediatePropagation();return;}
    if((appSettings.modalOpen||updater.modalOpen)&&!resetDialogOpen){event.stopPropagation();return;}
    if(resetDialogOpen){
      event.stopPropagation();
      if(event.type==='keydown'&&event.key==='Escape'&&!event.repeat){event.preventDefault();void resolveReset(false);}
      else if(event.type==='keydown'&&event.key==='Tab'){const targets=[resetCancel,resetConfirm].filter(node=>!node.disabled);if(targets.length){event.preventDefault();const index=targets.indexOf(document.activeElement);targets[(index+(event.shiftKey?-1:1)+targets.length)%targets.length].focus();}}
      return;
    }
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
  function refreshInterval(){return area==='client'&&['board','parameters','harmony'].includes(page)?appSettings.pollIntervalMs:Math.max(1000,appSettings.pollIntervalMs);}
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
    syncCockpitActivity();
    updater?.setHostActive?.(hostActive);
    if(!hostActive)cancelPolling();else{render();if(pollConnectionActive)schedulePolling(0);}
  }
  const visibility=()=>{syncCockpitActivity();if(document.hidden)cancelPolling();else if(hostActive&&pollConnectionActive){render();schedulePolling(0);}};
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
    destroy(){disposed=true;compactNavigation.destroy();cancelPolling();resizeObserver?.disconnect();window.removeEventListener('hashchange',route);window.removeEventListener('beforeunload',unloadUpdate);document.removeEventListener('visibilitychange',visibility);for(const type of ['pointerdown','pointerup','pointercancel','lostpointercapture','click','beforeinput','input','change'])document.removeEventListener(type,intercept,true);document.removeEventListener('keydown',keyGuard,true);document.removeEventListener('keyup',keyGuard,true);help.destroy();backup.destroy();updater.destroy();appSettings.destroy();diagnostics.destroy();cockpit.destroy();resetOverlay.remove();void client.disconnect();},
  };
}
