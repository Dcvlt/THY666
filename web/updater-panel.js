/* SPDX-License-Identifier: GPL-3.0-or-later
 * Three deliberate update steps; native transport and physical musical state
 * remain in the helper and shared application client, never in animations.
 */
import { inspectDeviceBackup } from './device-client.js';
import { createUpdaterClient, consumeUpdaterHandoff, UPDATE_HELPER_PROTOCOL, UPDATE_PACKAGE_MAX_BYTES } from './updater-client.js';
import { createUpdateIcon } from './updater-art.js';
import { createUpdaterLayout } from './updater-layout.js';
import { UPDATE_HEALTH_CHECKS, checkUpdateIdentity, checkUpdateHealth } from './updater-checks.js';
export { createUpdaterClient, UPDATE_HELPER_PROTOCOL, UPDATE_PACKAGE_MAX_BYTES };
const terminalStates = new Set(['awaiting-verification', 'failed', 'rejected']);
const POWER_WARNING='Do not disconnect the board or let it lose power during installation. Keep USB and any connected power supply secure, and keep this updater open until the transfer finishes. Interrupting power or USB can leave the board unable to start.';
let panelSequence = 0;

export function initializeUpdaterPanel({ mount, client, createHelper = createUpdaterClient, notify = () => {}, onOwnershipChange = () => {},
  connectApplication = () => client.connect(), onCompanion = () => {}, onRestored = async () => {}, handoff = null,
  operationId = () => globalThis.crypto.randomUUID(), setTimer = globalThis.setTimeout, clearTimer = globalThis.clearTimeout,runtime=null } = {}) {
  if (!mount?.ownerDocument || !client?.downloadBackup || typeof createHelper !== 'function')
    throw new TypeError('Update requires a DOM mount, the shared device client and an updater helper factory.');
  const doc = mount.ownerDocument, prefix = `thy-updater-${++panelSequence}`, listeners = [];
  const helper = createHelper();
  let disposed = false, working = false, fileTicket = 0, selected = null, inspected = null, devices = [];
  let backup = null, operation = null, locked = false, dialog = null, confirmationKind = null, returnFocus = null, previewConfirm = null, applicationVerification = null;
  let preservationRequested = false, restorationState = 'idle', restorationError = '';
  let progressTimer = null, helperSimulation = false, rejectionPending = null,hostActive=true;
  let step = 1, backupState = 'idle', source = 'thy666', releases = [], catalogConfigured = false, detectedUID = null, verifying = false;
  const health = new Map(); let healthError = '', packageTicket = 0, packageReadoutKey = '';
  let notice = '', noticeKind = 'info';
  const directBrowser=runtime?.kind==='browser-direct';
  let quietDesktopLaunch=runtime?.kind==='desktop'||directBrowser;
  const el = (tag, className = '', text) => {
    const node = doc.createElement(tag); if (className) node.className = className;
    if (text !== undefined) node.textContent = String(text); return node;
  };
  const listen = (node, type, handler, options) => {
    node.addEventListener(type, handler, options);
    listeners.push(() => node.removeEventListener(type, handler, options));
  };
  const button = (text, action) => {
    const node = el('button', 'button', text); node.type = 'button'; node.dataset.updaterAction = action; return node;
  };
  const {root,boardPane,stages,bodies,actions,headings,stepEntries,setup,manual,boardArt,boardIdentity,deviceActions,entry,read,skip,backupConnect,backupSummary,backupProgress,restoreChoice,restoreWanted,restoration,restorationSummary,restore,keepSettings,restoreProgress,unavailable,endpoint,token,connect,disconnect,helperSummary,browserSummary,simulationBadge,sourceButtons,releaseNotice,custom,browse,fileName,file,expectedBuild,clearFile,inspect,fileDetails,packageSummary,find,device,prepared,prepare,install,progress,operationSummary,completion,logoArt,completionTitle,completionCopy,healthList,healthRows,applicationSummary,verify,companion,installProgress,recovery,status}=createUpdaterLayout(doc,prefix);
  if(runtime?.companionLabel)companion.textContent=String(runtime.companionLabel);
  mount.replaceChildren(root);

  let previewOpen=false, previewRoot=null, cancelPreview=()=>{};
  const previewButton=button('Development preview','preview');
  previewButton.className='button update-quiet';root.querySelector('.update-wizard-heading').append(previewButton);
  listen(previewButton,'click',()=>{
    if(working||locked||operation||client.connected||client.busy)return;
    previewOpen=true;root.hidden=true;
    const view=createUpdaterLayout(doc,`${prefix}-preview`);previewRoot=view.root;mount.append(previewRoot);
    const bar=el('div','update-preview-toolbar'),label=el('strong','','DEVELOPMENT PREVIEW · No device access'),pick=el('select'),exit=button('Exit preview','preview-exit');
    pick.setAttribute('aria-label','Preview scenario');
    const scenarios=['Backup','Update','Transfer','Verify','Restore','Settings restored','Failure','Original firmware complete','Firmware verified'];
    scenarios.forEach((name,i)=>{const option=el('option','',name);option.value=String(i);pick.append(option);});pick.value='0';
    bar.append(label,pick,button('Next preview','preview-next'),exit);previewRoot.prepend(bar);
    let scene=0, animationTimer=null, animating=false, backupDone=false, previewDetected=false, previewDevice='', previewSource='thy666', previewFile='', previewChecked=false;
    cancelPreview=()=>{if(animationTimer!==null)clearTimer(animationTimer);animationTimer=null;animating=false;};
    function animate(kind){
      cancelPreview();
      if(kind==='install'){pick.value='2';paint();}
      animating=true;
      const meter=kind==='backup'?view.backupProgress:kind==='restore'?view.restoreProgress:view.installProgress;
      const caption=kind==='backup'?'Reading configuration':kind==='restore'?'Restoring and checking saved configuration':'Transferring firmware';
      let completed=0;
      function tick(){
        if(disposed||!previewOpen)return;
        paintProgress(meter,{state:completed===30?'done':'running',completed,total:30,message:`${caption} · ${Math.round(completed/30*100)}% · preview`});
        view.logoArt.dataset.state=completed===30?'finished':'busy';
        for(const node of previewRoot.querySelectorAll('[data-updater-action]'))node.disabled=!['preview-exit','preview-next','help'].includes(node.dataset.updaterAction);
        if(completed===30){
          animationTimer=setTimer(()=>{
            animationTimer=null;animating=false;
            if(kind==='backup'){backupDone=true;pick.value='1';}
            else pick.value=kind==='restore'?'5':previewSource==='stock'?'7':'3';
            paint();
          },650);
        }else{completed++;animationTimer=setTimer(tick,100);}
      }
      tick();
    }
    function paint(){
      scene=Number(pick.value);const stage=scene===0?1:scene<3||scene===7?2:3;
      view.root.dataset.step=String(stage);view.root.dataset.operation=scene===2||scene===7?'active':'idle';
      view.stages.forEach((n,i)=>{n.hidden=i!==stage;});
      view.stepEntries.forEach((n,i)=>{n.hidden=scene===7&&i===2;n.dataset.state=i+1===stage?'active':i+1<stage?'done':'pending';n.setAttribute('aria-current',i+1===stage?'step':'false');});
      view.setup.hidden=view.browserSummary.hidden=view.simulationBadge.hidden=true;
      view.status.textContent='Synthetic visual preview. No package, backup or verification result is real.';
      view.backupSummary.textContent=backupDone?'Example backup saved and checked. Ready to update.':'Example board connected. Ready to save your configuration.';
      view.backupConnect.hidden=true;view.read.hidden=backupDone;view.skip.hidden=backupDone;view.restoreChoice.hidden=false;
      view.backupProgress.box.hidden=true;view.restoreProgress.box.hidden=true;
      view.releaseNotice.textContent='Example release — for layout review only.';
      view.packageSummary.textContent='THY666 · Example release · File checks passed (preview)';
      view.custom.hidden=previewSource!=='custom'||scene!==1;view.fileName.textContent=previewFile||'No file selected';view.inspect.disabled=!previewFile;view.prepared.hidden=true;view.boardArt.hidden=false;
      view.boardIdentity.textContent=previewDevice?'Example board selected · ready in update mode':'Follow the diagram, then detect an example board.';view.entry.hidden=previewDetected;view.deviceActions.hidden=false;
      view.device.hidden=!previewDetected;view.device.parentNode.hidden=!previewDetected;
      view.find.textContent=previewDetected?'Detect again':'Detect board';
      for(const [id,node] of view.sourceButtons)node.setAttribute('aria-pressed',String(id===previewSource));
      view.packageSummary.textContent=previewSource==='stock'?'Original THY333 · Example package checked (preview)':previewSource==='custom'?(previewChecked?'Example file check passed · preview only':previewFile?'Choose Check file to preview validation.':'Select a .333 file to preview this step. Its contents will not be read.'):'THY666 · Example release checked (preview)';
      view.boardPane.hidden=scene===2;
      view.sourceButtons.values().next().value.parentNode.hidden=scene===2;view.packageSummary.hidden=scene===2;view.releaseNotice.hidden=scene===2;
      view.install.hidden=scene===2||scene===7;view.progress.hidden=true;view.recovery.hidden=scene!==6;view.recovery.open=scene===6;if(scene===6)view.bodies[3].append(view.recovery);
      view.operationSummary.parentNode.hidden=true;
      view.installProgress.box.hidden=scene!==2;view.installProgress.copy.textContent='Example transfer · 62%';view.installProgress.box.setAttribute('aria-valuenow','62');
      for(const segment of view.installProgress.art.querySelectorAll('[data-segment]'))segment.setAttribute('data-state',Number(segment.getAttribute('data-segment'))<19?'done':'pending');
      view.completion.hidden=scene<2;view.bodies[stage].prepend(view.completion);
      view.completion.dataset.state=scene===5?'restored':scene===4||scene===8?'verified':scene===7?'stock-complete':scene===6?'failure':scene===2?'busy':'finished';
      view.completionTitle.textContent=scene===2?'Installing firmware':scene===6?'Update needs attention':scene===5?'Settings Restored':scene===4||scene===8?'Firmware verified':scene===7?'Firmware installed':'Firmware installed';
      view.completionCopy.textContent=scene===6?'Example: the board disconnected. Inspect its state before any retry.':'Example state for visual review. No physical board has been changed.';
      view.healthList.hidden=scene<3||scene===7;view.applicationSummary.hidden=true;
      for(const {row,result} of view.healthRows.values()){row.dataset.state=scene===3?'pending':scene===6?'failure':'pass';result.textContent=scene===3?'Waiting':scene===6?'Needs attention':'Passed (preview)';}
      view.restoration.hidden=scene!==4;view.restorationSummary.textContent='Example backup ready. Review before restoring your saved configuration.';
      view.verify.hidden=scene!==3;view.companion.hidden=scene!==5&&scene!==8;
      view.headings[stage].parentNode.hidden=scene>=2;view.headings[stage].textContent=scenarios[scene];
      view.headings[stage].parentNode.children[1].textContent=scene===2?'Keep USB connected while firmware transfers. This preview uses synthetic progress.':stage===1?'Save your THY666 configuration before changing firmware.':stage===2?'Choose the firmware to install.':scene===4?'Restore your saved configuration after the firmware checks pass.':'Check the running firmware before returning to Client.';
      view.logoArt.dataset.state=view.completion.dataset.state;
      for(const node of previewRoot.querySelectorAll('[data-updater-action]'))node.disabled=false;
      view.inspect.disabled=!previewFile;view.install.disabled=!previewDevice||(previewSource==='custom'&&!previewChecked);
    }
    view.file.addEventListener('change',()=>{previewFile=view.file.files?.[0]?.name||'';previewChecked=false;paint();});
    view.device.addEventListener('change',()=>{previewDevice=view.device.value;paint();});
    pick.addEventListener('change',()=>{cancelPreview();paint();if(scene===2)animate('install');});
    previewRoot.addEventListener('click',event=>{
      const target=event.target.closest?.('[data-updater-action]');if(!target)return;
      event.preventDefault();event.stopPropagation();
      if(target===exit){cancelPreview();previewRoot.remove();previewRoot=null;previewOpen=false;root.hidden=false;refresh();previewButton.focus();return;}
      const action=target.dataset.updaterAction;
      if(action==='help')return;
      if(target.disabled)return;
      if(action==='install'){requestPreviewInstall(()=>animate('install'),view.install);return;}
      if(['backup','restore'].includes(action)){animate(action);return;}
      if(animating&&action!=='preview-next')return;
      cancelPreview();
      if(action==='find'){
        previewDetected=true;previewDevice='';view.device.replaceChildren();
        for(const [value,label] of [['','Choose an example board'],['preview-a','Example TheoryBoard A'],['preview-b','Example TheoryBoard B']]){const option=el('option','',label);option.value=value;view.device.append(option);}view.device.value='';paint();return;
      }
      if(action==='browse'){view.file.click();return;}
      if(action==='inspect'){previewChecked=true;paint();return;}
      if(action==='clear-file'){previewFile='';previewChecked=false;view.file.value='';paint();return;}
      if(action==='source'){previewSource=target.dataset.firmwareSource;paint();return;}
      if(action==='verify'){pick.value=backupDone&&view.restoreWanted.checked?'4':'8';paint();return;}
      if(action==='keep-settings'){pick.value='8';paint();return;}
      if(action==='companion')return;
      if(action==='back')pick.value=String(Math.max(0,scene-1));
      else pick.value=String(Math.min(6,scene+1));
      paint();if(scene===2)animate('install');
    });
    paint();pick.focus();
  });

  function setLocked(value) {
    const next = Boolean(value); if (next === locked) return;
    locked = next; onOwnershipChange(locked && !helperSimulation && operation?.simulation !== true);
  }
  function say(message, kind = 'info') {
    if (disposed) return;
    notice = String(message);noticeKind=kind;if(!quietDesktopLaunch||kind==='error')notify(notice, kind);refresh();
  }
  function cancelProgress() { if (progressTimer !== null) { clearTimer(progressTimer); progressTimer = null; } }
  function scheduleProgress() {
    cancelProgress();
    if (disposed || !hostActive || !helper.connected || operation?.state !== 'running') return;
    progressTimer = setTimer(() => { progressTimer = null; void checkProgress(); }, 500);
  }
  function currentDevice() { return devices.find(item => item.id === device.value) || null; }
  function applicationActive() { return Boolean(client.connected || client.status?.connecting); }
  function currentBackup() {
    return backup && (!client.connected || client.identity?.uid === backup.uid) ? backup : null;
  }
  function mayInstall() {
    const supported=inspected?.installable===true&&(inspected.kind==='stock'||inspected.build_identity_verified===true&&/^[0-9a-f]{16}$/.test(inspected.build_id||''));
    return step===2 && helper.connected && !applicationActive() && !client.busy && !working && !locked && !operation &&
      supported && currentDevice() && (currentBackup() || unavailable.checked === true);
  }
  function preservationResolved() { return !preservationRequested || ['verified','declined'].includes(restorationState); }
  function successfulInstallation() { return applicationVerification?.verified===true&&!operation?.simulation&&preservationResolved(); }
  function restoreIdentity() {
    if(!backup?.bytes)throw new Error('The retained configuration backup is unavailable. Keep the downloaded file.');
    checkUpdateIdentity(operation,client.identity);
    if(!client.connected||!client.backupIdentity)throw new Error('Reconnect the verified THY666 application before restoring settings.');
    const info=inspectDeviceBackup(backup.bytes,client.backupIdentity);
    if(info.uid!==operation.target_uid?.toLowerCase()||info.uid!==backup.uid)
      throw new Error('The backup does not belong to the board verified by this installation.');
    return info;
  }
  function mayRestore() {
    if(disposed||step!==3||!preservationRequested||!applicationVerification?.verified||
      operation?.state!=='awaiting-verification'||operation.simulation||operation.firmware_kind==='stock'||
      working||locked||client.busy||restorationState==='verified'||restorationState==='declined'||typeof client.restoreBackup!=='function')return false;
    try{restoreIdentity();return true;}catch{return false;}
  }
  function mayKeepSettings() {
    return !disposed&&step===3&&preservationRequested&&applicationVerification?.verified===true&&
      operation?.state==='awaiting-verification'&&!operation.simulation&&!working&&!locked&&!client.busy&&
      !['verified','declined','running'].includes(restorationState);
  }
  function showStep(next,{focus=true}={}) {
    if(next===step)return;
    step=next;root.dataset.step=String(step);
    if(step===2){boardPane.append(boardArt,boardIdentity,deviceActions);}
    stages.forEach((node,index)=>{node.hidden=index!==step;});
    if(focus){
      headings[step].focus({preventScroll:true});
      stages[step].scrollIntoView?.({block:'start',behavior:doc.defaultView?.matchMedia?.('(prefers-reduced-motion: reduce)').matches?'instant':'smooth'});
    }
  }
  function paintProgress(value,{state='idle',completed=0,total=0,message=''}={}) {
    const ratio=total>0?Math.min(1,Math.max(0,completed/total)):null;
    value.box.hidden=state==='idle';value.box.dataset.state=state;value.copy.textContent=message;
    if(ratio===null)value.box.removeAttribute('aria-valuenow');else value.box.setAttribute('aria-valuenow',String(Math.round(ratio*100)));
    value.box.setAttribute('aria-valuetext',message);
    for(const segment of value.art.querySelectorAll('[data-segment]')){
      const index=Number(segment.getAttribute('data-segment'));
      segment.setAttribute('data-state',ratio!==null&&index<Math.floor(ratio*30)?'done':state==='running'?'busy':'pending');
    }
  }
  async function loadReleases(){
    releases=[];catalogConfigured=false;
    if(typeof helper.releases!=='function')return;
    const value=await helper.releases();if(disposed)return;
    if(!Array.isArray(value?.releases)||value.releases.length>16)throw new Error('The updater returned an invalid firmware list.');
    releases=value.releases.filter(item=>item&&['thy666','stock'].includes(item.kind)&&typeof item.id==='string');catalogConfigured=value.configured===true;
  }
  async function chooseSource(kind){
    const ticket=++packageTicket;++fileTicket;source=kind;inspected=null;selected=null;file.value='';
    if(kind==='custom'){refresh();return;}
    const item=releases.find(item=>item.kind===kind&&item.available);
    if(!item){refresh();return;}
    const info=await helper.selectRelease(item.id);if(disposed||ticket!==packageTicket)return;
    if(info?.file_sha256!==item.sha256||info.kind!==kind||info.installable!==true)throw new Error('This firmware file does not match its release identity.');
    inspected={...info};selected={name:item.label,release:item.id};say(`${item.label} is ready. The board will not change until you apply it.`);
  }
  async function enterInstall(){showStep(2);if(helper.connected&&!inspected&&source!=='custom')await chooseSource(source);}
  function refresh() {
    if (disposed) return null;
    const busy = working || Boolean(helper.busy), connected = Boolean(client.connected);
    read.disabled = !connected || client.busy || busy || locked;
    restoreChoice.hidden=!connected&&!currentBackup();restoreWanted.disabled=busy||locked||Boolean(operation);
    unavailable.disabled = busy || locked || Boolean(currentBackup());
    endpoint.disabled = token.disabled = busy || helper.connected || locked;
    connect.disabled = busy || helper.connected || locked;
    disconnect.disabled = busy || !helper.connected || locked;
    file.disabled = busy || locked || Boolean(operation);
    browse.disabled=file.disabled;fileName.textContent=selected&&!selected.release?selected.name:'No file selected';
    expectedBuild.disabled = file.disabled; clearFile.disabled = file.disabled || !selected;
    inspect.disabled = busy || locked || Boolean(operation) || !selected || !helper.connected;
    find.disabled = busy || locked || Boolean(operation) || !helper.connected || applicationActive() || client.busy;
    device.disabled = find.disabled || !devices.length;
    install.disabled = !mayInstall(); progress.disabled = busy || !helper.connected || !operation;
    backupSummary.textContent = currentBackup() ? `Backup saved and checked. Keep the downloaded file · ${backup.build.slice(0,8)}.` :
      backupState==='running'?'Reading your applied settings…':unavailable.checked?'Backup skipped. No recovery copy has been created.':
      connected ? 'Your THY666 settings are ready to back up.' : 'Backup is unavailable in update mode or on firmware without the THY666 backup service.';
    helperSummary.textContent = helper.connected ? 'Local helper connected. USB ownership is reserved only when installation starts.' : 'Local helper disconnected';
    browserSummary.hidden=!directBrowser||(helper.connected&&helper.available!==false);
    browserSummary.textContent=helper.available===false?'Direct browser installation is unavailable here. Use desktop Chrome or Edge with WebHID, or the desktop updater.':helper.connected?'Browser updater ready. Board access starts only when you choose Detect board.':'Preparing the browser updater. No board is opened automatically.';
    setup.dataset.state=helper.connected?'connected':'disconnected';
    setup.querySelector('summary').querySelector('span').textContent=helper.connected?'Updater connected · connection options':'Connect the updater on this computer';
    simulationBadge.hidden = !helperSimulation && operation?.simulation !== true;
    if (!simulationBadge.hidden) helperSummary.textContent = helper.connected ? 'Local helper connected in SIMULATION mode. Its fake backend does not open a physical USB device.' : 'Simulation helper disconnected';
    const readoutKey=JSON.stringify([selected?.name,inspected,source]);
    if(readoutKey!==packageReadoutKey){
    packageReadoutKey=readoutKey;packageSummary.replaceChildren();
    if (selected) {
      packageSummary.append(el('h3', '', selected.release?(inspected?.kind==='stock'?'Original THY333':'THY666'):selected.name));
      if (inspected) {
        packageSummary.append(el('p', '', inspected.kind==='stock'?'Original THY333 v5 · exact known package':inspected.build_identity_verified && inspected.build_id ?
          `THY666 · build ${inspected.build_id.slice(0,8)}` : 'File identity needs attention. Enter the expected build under Advanced, then check again.'));
        packageSummary.append(el('p', '', inspected.installable?'File checks passed. Ready to install.':'This file is not approved for installation.'));
        const details=el('details','update-package-details');
        details.append(el('summary','','File details'),el('p','',`${inspected.file_bytes} bytes · application ${inspected.application_bytes} bytes`),el('p','',`SHA-256 ${inspected.file_sha256}`));packageSummary.append(details);
      } else packageSummary.append(el('p', '', 'Selected locally. Choose Check file to validate it with the updater.'));
    } else packageSummary.append(el('p', 'update-field-hint',source==='custom'?'Choose a .333 file, then check it before installation.':'The selected firmware will appear here when the updater provides it.'));
    }
    operationSummary.textContent = operation ? `${operation.simulation === true ? 'SIMULATED operation' : 'Operation'} ${operation.operation_id} · ${operation.state || 'outcome-unknown'} · ${operation.message || 'Check status before taking another action.'}` : 'No installation requested';
    operationSummary.parentNode.hidden=!operation;
    if (operation?.simulation === true) applicationVerification = { verified: false, state: 'simulation', scope: 'simulation' };
    else if (operation?.state === 'awaiting-verification' && connected && applicationVerification?.verified) {
      const expectedBuild = operation.expected_build || (inspected?.build_identity_verified ? inspected.build_id : null);
      const expectedUID = operation.target_uid || operation.expected_uid || backup?.uid;
      const actualBuild = client.identity?.build, actualUID = client.identity?.uid;
      if (/^[0-9a-f]{16}$/i.test(expectedBuild || '') && /^[0-9a-f]{24}$/i.test(expectedUID || '') &&
        /^[0-9a-f]{16}$/i.test(actualBuild || '') && /^[0-9a-f]{24}$/i.test(actualUID || '')) {
        const matched = actualBuild.toLowerCase() === expectedBuild.toLowerCase() && actualUID.toLowerCase() === expectedUID.toLowerCase();
        if(!matched){applicationVerification={verified:false,state:'mismatch',scope:'basic-installation-health'};health.clear();health.set('identity',{state:'failure',message:'A different board or firmware is connected.'});}
      } else applicationVerification = { verified: false, state: 'unavailable', scope: 'running-application-build-and-uid' };
    }
    applicationSummary.hidden = operation?.state !== 'awaiting-verification';
    applicationSummary.textContent = applicationVerification?.state === 'simulation' ? 'SIMULATION does not verify a physical board. A real installation still needs its own checks.' : applicationVerification?.verified ?
      'The intended board, firmware and basic driver reports passed. This is not raw flash readback; test normal playing before relying on the installation.' :
      applicationVerification?.state === 'mismatch' ? 'The connected application build or board UID differs from this update. Keep the package and backup, and inspect the running device before considering another installation.' :
      applicationVerification?.state === 'unavailable' ? 'The running application identity cannot be matched because the full expected build or target UID is unavailable. Inspect device identity and behavior before considering another installation.' :
      healthError||'Leave every pad released. Choose Connect and verify, then select your THY666 board. No automatic restore is performed.';
    root.dataset.step=String(step);
    root.dataset.operation=operation?'active':'idle';
    previewButton.disabled=busy||locked||Boolean(operation)||connected||client.busy;
    stages.forEach((node,index)=>{node.hidden=index!==step;});
    stepEntries.forEach((node,i)=>{node.hidden=i===2&&operation?.firmware_kind==='stock';const index=i+1;node.dataset.state=index===step?'active':index<step?'done':'pending';node.setAttribute('aria-current',index===step?'step':'false');});
    setup.hidden=runtime?.kind==='desktop'||directBrowser||Boolean(operation)&&locked;
    backupConnect.disabled=busy||locked||helperSimulation||client.available===false;
    backupConnect.hidden=connected||Boolean(currentBackup());
    boardArt.dataset.state=connected||currentDevice()?'detected':'waiting';
    boardArt.hidden=step===2&&(Boolean(currentDevice())||Boolean(operation));
    boardIdentity.textContent=connected?`THY666 detected · firmware ${client.identity?.build?.slice(0,8)||'identified'}`:currentDevice()?`${helperSimulation?'SIMULATED board':'TheoryBoard'} detected · update mode ready. Installed firmware cannot be identified in this mode.`:'';
    boardIdentity.hidden=!connected&&!currentDevice();
    if(connected&&/^[0-9a-f]{24}$/i.test(client.identity?.uid||''))detectedUID=client.identity.uid;
    deviceActions.hidden=Boolean(operation)||step===1||step===3;
    device.hidden=devices.length<2;device.parentNode.hidden=devices.length<2;
    entry.hidden=connected||Boolean(currentDevice());find.disabled=busy||locked||Boolean(operation)||!helper.connected||applicationActive()||client.busy;
    skip.hidden=Boolean(currentBackup());skip.disabled=busy||locked;read.hidden=!connected||Boolean(currentBackup());
    for(const node of root.querySelectorAll('[data-updater-action="back"]'))node.disabled=busy||locked||Boolean(operation);
    custom.hidden=source!=='custom'||Boolean(operation);packageSummary.hidden=Boolean(operation);
    prepared.hidden=!applicationActive()||Boolean(operation);prepare.disabled=busy||locked;
    sourceButtons.values().next().value.parentNode.hidden=Boolean(operation);
    for(const [id,node] of sourceButtons){node.setAttribute('aria-pressed',String(source===id));node.disabled=busy||locked||Boolean(operation);}
    const release=releases.find(item=>item.kind===source);
    releaseNotice.hidden=Boolean(operation);
    releaseNotice.textContent=source==='custom'?'Custom files must pass the same compatibility and integrity checks.':release&&!release.available?release.disabled_reason||'This firmware is not available in this updater package.':!catalogConfigured?'No bundled firmware catalog is configured. Use Custom file, or use an updater download containing the release packages.':release?'Firmware supplied by this updater package; no unverified download is used.':'This updater package does not contain this firmware. Choose a custom file instead.';
    const progressMatch=/Reading backup · ([0-9]+)\/([0-9]+) bytes/.exec(client.status?.message||'');
    paintProgress(backupProgress,{state:backupState,completed:currentBackup()?1:Number(progressMatch?.[1]||0),total:currentBackup()?1:Number(progressMatch?.[2]||0),message:backupSummary.textContent});
    backupSummary.hidden=!backupProgress.box.hidden;
    const complete=operation?.state==='awaiting-verification';
    const phaseCopy={opening:'Connecting to the board…',preflight:'Checking the board…',preparation:'Preparing the board…',metadata:'Preparing the transfer…',ready2:'Preparing the transfer…',application:'Transferring firmware…',checksums:'Checking the transfer…',activation:'Starting the new firmware…','application-verification':'Transfer complete'};
    const transferCopy=operation?.state==='failed'||operation?.state==='outcome-unknown'?operation.message:operation?phaseCopy[operation.phase]||'Updating the board…':'';
    paintProgress(installProgress,{state:operation?(complete?'done':operation.state==='failed'?'failure':'running'):'idle',completed:operation?.completed||0,total:operation?.phase==='application'||complete?operation.total:0,message:transferCopy});
    progress.hidden=!operation;install.hidden=Boolean(operation);find.hidden=Boolean(operation);
    healthList.hidden=operation?.firmware_kind==='stock'||Boolean(operation?.simulation);
    for(const [id,nodes] of healthRows){const value=health.get(id);nodes.row.dataset.state=value?.state||'pending';nodes.result.textContent=value?.message||'Waiting';}
    const firmwareVerified=applicationVerification?.verified===true&&!operation?.simulation;
    const verified=successfulInstallation();
    restoration.hidden=!preservationRequested||operation?.firmware_kind==='stock'||Boolean(operation?.simulation)||!complete;
    restore.disabled=!mayRestore();keepSettings.disabled=!mayKeepSettings();
    restore.hidden=['verified','declined'].includes(restorationState);keepSettings.hidden=restore.hidden;
    restoration.dataset.state=restorationState;
    restorationSummary.textContent=restorationState==='verified'?'Your configuration was restored, saved and re-exported. The complete saved payload matches the independently checked restore payload.':restorationState==='declined'?'You chose to keep the current settings. The downloaded backup remains available for a later explicit restore.':restorationState==='running'?'Restoring your configuration and checking the saved readback. Keep the board connected and every pad released.':restorationState==='failed'?`${restorationError} No restore retry was sent. Keep the downloaded backup and inspect the current device state before another explicit attempt.`:firmwareVerified?'Firmware checks passed. Restore the saved configuration next, or explicitly choose to keep the current settings.':'First verify the intended board and firmware. Your retained backup will not be written before those checks pass.';
    const restoreMatch=/Sending backup · ([0-9]+)\/([0-9]+) bytes/.exec(client.status?.message||'');
    paintProgress(restoreProgress,{state:restorationState==='running'?'running':restorationState==='verified'?'done':restorationState==='failed'?'failure':'idle',completed:restorationState==='verified'?1:Number(restoreMatch?.[1]||0),total:restorationState==='verified'?1:Number(restoreMatch?.[2]||0),message:restorationSummary.textContent});
    completion.hidden=!operation;completion.dataset.state=firmwareVerified&&restorationState==='verified'?'restored':firmwareVerified?'verified':complete&&operation?.firmware_kind==='stock'?'stock-complete':complete?'finished':operation?.state==='failed'?'failure':'busy';
    completion.dataset.identity=operation?.firmware_kind==='stock'?'other':'thy666';logoArt.dataset.state=completion.dataset.state;logoArt.dataset.identity=completion.dataset.identity;
    completionTitle.textContent=firmwareVerified?(restorationState==='verified'?'Settings Restored':'Firmware verified'):complete?(operation.simulation?'Simulation complete':operation.firmware_kind==='stock'?'Update finished':'Firmware installed'):operation?.state==='failed'?'Update needs attention':'Installing firmware';
    completionCopy.textContent=verified?(restorationState==='verified'?'Firmware checks passed and the saved configuration matches your backup. Test normal playing and persistence before relying on the installation.':restorationState==='declined'?'Firmware checks passed. You chose to keep the current settings; your backup was not restored.':'Your TheoryBoard passed the basic checks. Ready to make music.'):firmwareVerified&&preservationRequested?'The firmware is healthy. Configuration restoration is still pending.':operation?.simulation?(complete?'Practice complete. No physical firmware has been installed or verified.':'Practicing the update workflow. No physical board is accessed.'):operation?.firmware_kind==='stock'&&complete?'The original firmware transfer checks passed. It does not provide THY666 telemetry; check normal startup and playing yourself.':complete?'The transfer checks passed. Reconnect to verify the running instrument.':'Keep USB connected and the updater open.';
    if(operation&&completion.parentNode!==bodies[step===2?2:3])bodies[step===2?2:3].prepend(completion);
    verify.hidden=!complete||operation?.firmware_kind==='stock'||Boolean(operation?.simulation)||firmwareVerified;verify.disabled=busy||locked||verifying||client.available===false;
    verify.textContent=connected?'Run basic checks':'Connect and verify';companion.hidden=!verified;
    if(operation?.firmware_kind==='stock')applicationSummary.hidden=true;
    // The logo/result block owns the heading once installation begins.
    headings[2].parentNode.hidden=Boolean(operation);
    headings[3].parentNode.hidden=Boolean(operation);
    headings[2].textContent=complete&&operation?.firmware_kind==='stock'?'Firmware installed':operation?'Installing firmware':'Choose your firmware';headings[2].parentNode.children[1].textContent=complete&&operation?.firmware_kind==='stock'?'Transfer complete. Check normal startup and playing on your board.':operation?'Keep the USB cable connected. Progress below comes from the actual transfer.':'THY666 is recommended. You can also restore the original firmware or use a supported custom file.';
    headings[3].textContent=operation?.simulation?'Practice finished':operation?.firmware_kind==='stock'?'All done':verified?'Ready to play':'One last check';headings[3].parentNode.children[1].textContent=operation?.simulation?'The simulated workflow is complete. Your physical board has not been accessed.':operation?.firmware_kind==='stock'?'The original firmware transfer is finished.':'Basic checks confirm the new THY666 installation before you start playing.';
    recovery.open=operation?.state==='failed'||operation?.state==='outcome-unknown';
    status.textContent = notice;
    // Progress and check rows already own their messages. Preserve unexpected
    // failures raised outside those regions instead of hiding all notices.
    status.hidden=!notice||(Boolean(operation)&&noticeKind!=='error')||notice===healthError||(step===2&&notice===operation?.message)||notice===backupSummary.textContent;
    if(healthError||firmwareVerified)applicationSummary.hidden=true;
    if (dialog && confirmationKind!=='preview-install' && !(confirmationKind==='restore'?mayRestore():confirmationKind==='keep-settings'?mayKeepSettings():mayInstall())) closeConfirmation();
    return { helperConnected: Boolean(helper.connected), simulation: helperSimulation || operation?.simulation === true,
      usbBusy: locked && !helperSimulation && operation?.simulation !== true, selected: selected?.name || null,
      inspected: Boolean(inspected), canInstall: !install.disabled, confirmationOpen: Boolean(dialog), operation: operation ? { ...operation } : null,
      applicationVerification: applicationVerification ? { ...applicationVerification } : null,
      preservation:{requested:preservationRequested,state:restorationState,resolved:preservationResolved(),error:restorationError||null},
      step,source,backupState,notice };
  }
  async function run(action) {
    if (disposed || working) return;
    working = true; refresh();
    try { await action(); }
    catch (error) { say(error.message, 'error'); }
    finally { working = false; refresh(); }
  }
  function download(bytes, name) {
    if(runtime?.kind==='desktop')return runtime.saveFile(bytes,name);
    const platform = doc.defaultView || globalThis, BlobType = platform.Blob || globalThis.Blob, urlAPI = platform.URL || globalThis.URL;
    if (!BlobType || !urlAPI?.createObjectURL) throw new Error('This browser cannot download the backup file.');
    const url = urlAPI.createObjectURL(new BlobType([bytes], { type: 'application/octet-stream' }));
    try { const link = el('a'); link.href = url; link.download = name; link.hidden = true; doc.body.append(link); link.click(); link.remove(); }
    finally { queueMicrotask(() => urlAPI.revokeObjectURL(url)); }
  }
  async function readBackup() {
    const identity = client.backupIdentity;
    if (!client.connected || client.busy || !identity || locked) return;
    await run(async () => {
      backup = null;restorationState='idle';restorationError='';backupState='running';refresh();let bytes;
      try{bytes=await client.downloadBackup();}catch(error){backupState='failure';throw error;}
      if (disposed) return;
      let info;try{info=inspectDeviceBackup(bytes,identity);if(!client.connected||client.identity?.uid!==info.uid)throw new Error('Board connection changed while reading backup. Read a new backup.');}catch(error){backupState='failure';throw error;}
      try{await download(bytes, `THY666-${info.uid}-${info.build}.thyb`);}catch(error){backupState='failure';throw error;}
      backup = { uid: info.uid, build: info.build, bytes:bytes.slice(), payloadCRC:info.payloadCRC };backupState='done';detectedUID=info.uid;
      await enterInstall();
      unavailable.checked = false; say('Your configuration backup is saved and checked. Keep the downloaded file.', 'success');
    });
  }
  async function selectFile() {
    const ticket = ++fileTicket, value = file.files?.[0];selected = inspected = null;source='custom';++packageTicket;
    if (!value) { refresh(); return; }
    if (!Number.isInteger(value.size) || value.size < 78 + 64 || value.size > UPDATE_PACKAGE_MAX_BYTES) {
      file.value = ''; say(`Choose a .333 package between 142 and ${UPDATE_PACKAGE_MAX_BYTES} bytes.`, 'error'); return;
    }
    try {
      const bytes = new Uint8Array(await value.arrayBuffer());
      if (disposed || ticket !== fileTicket) return;
      if (bytes.length !== value.size) throw new Error('The selected package size changed while reading it.');
      selected = { name: String(value.name || 'Selected package'), bytes }; say('Package selected locally. No device or helper command was sent.');
    } catch (error) { if (!disposed && ticket === fileTicket) say(`Package rejected: ${error.message}`, 'error'); }
    finally { if (!disposed && ticket === fileTicket) { file.value = ''; refresh(); } }
  }
  function acceptOperation(value, expectedId) {
    if (!value || value.operation_id !== expectedId || !Number.isSafeInteger(value.sequence) || value.sequence < 0 ||
      typeof value.state !== 'string') throw new Error('Helper returned an uncorrelated update status. Keep the helper running and inspect its status.');
    if (operation?.operation_id === value.operation_id && value.sequence < operation.sequence) throw new Error('Helper returned an older update status.');
    if (operation?.operation_id !== value.operation_id) {applicationVerification=null;health.clear();healthError='';preservationRequested=false;restorationState='idle';restorationError='';}
    if (value.simulation === true) helperSimulation = true;
    operation = { ...value, firmware_kind:value.firmware_kind||inspected?.kind||'thy666', simulation: helperSimulation || value.simulation === true };
    rejectionPending = null;
    cancelProgress();
    // Only an explicit terminal helper result confirms that its USB handle was
    // released. A transport error leaves ownership and outcome unresolved.
    if (terminalStates.has(value.state)) setLocked(false);
    else setLocked(true);
    showStep(value.state==='awaiting-verification'&&operation.firmware_kind!=='stock'?3:2,{focus:false});
    const completed = value.state === 'awaiting-verification';
    const message = operation.simulation ? completed ? 'SIMULATION completed. No physical firmware was installed and no board was verified.' : `SIMULATION · ${value.message || `Update ${value.state}.`}` :
      completed ? 'Transfer completed. Reconnect the application explicitly and verify the running build; do not reinstall solely because verification is unavailable.' : value.message || `Update ${value.state}.`;
    say(message, value.state === 'failed' ? 'error' : 'info');
    if (value.state === 'running') scheduleProgress();
  }
  async function checkProgress() {
    cancelProgress();
    if (disposed || !helper.connected || !operation || working) return;
    await run(async () => {
      if (rejectionPending) {
        const value = await helper.status();
        if (disposed) return;
        if (!value || !Object.hasOwn(value, 'operation')) throw new Error('The helper did not report whether an update is active. Ownership remains unresolved.');
        if (value.operation?.state === 'running') { acceptOperation(value.operation, value.operation.operation_id); return; }
        if (value.operation !== null && !terminalStates.has(value.operation?.state)) throw new Error('The helper update status is unresolved. No replacement will be started.');
        rejectionPending = null; operation = null; applicationVerification = null; inspected = null; devices = []; device.replaceChildren();
        setLocked(false);
        say('The helper rejected the installation request and reports no active update. No automatic retry was sent. Inspect the package and discover the board again before another review.');
        return;
      }
      const value = await helper.progress(operation.operation_id, operation.sequence);
      if (!disposed) acceptOperation(value, operation.operation_id);
    });
  }
  function closeConfirmation({ focus = true } = {}) {
    if (!dialog) return; dialog.remove(); dialog = null;confirmationKind=null;previewConfirm=null;
    if (focus && returnFocus?.isConnected !== false) returnFocus?.focus?.(); returnFocus = null;
  }
  function requestPreviewInstall(confirmAction,trigger) {
    if(dialog)return;
    returnFocus=trigger;confirmationKind='preview-install';previewConfirm=confirmAction;
    dialog=el('div','thy-help-overlay updater-confirmation');dialog.dataset.updaterDialog='';
    const card=el('section','thy-help-dialog');card.setAttribute('role','dialog');card.setAttribute('aria-modal','true');
    const title=el('h2','','Keep your board powered');title.id=`${prefix}-preview-confirm-title`;title.className='update-warning-heading';title.prepend(createUpdateIcon(doc,'warning'));
    const warning=el('p','update-power-warning',POWER_WARNING);warning.id=`${prefix}-preview-confirm-copy`;
    card.setAttribute('aria-labelledby',title.id);card.setAttribute('aria-describedby',warning.id);
    const cancel=button('Cancel','cancel'),confirm=button('Confirm and preview update','confirm');
    card.append(title,warning,el('p','','DEVELOPMENT PREVIEW · Confirmation starts a simulated transfer only. No physical board is accessed.'),cancel,confirm);
    dialog.append(card);doc.body.append(dialog);cancel.focus();
  }
  function requestInstall() {
    if (disposed || dialog || !mayInstall()) return;
    returnFocus = doc.activeElement || install;confirmationKind='install';
    dialog = el('div', 'thy-help-overlay updater-confirmation'); dialog.dataset.updaterDialog = '';
    const card = el('section', 'thy-help-dialog'); card.setAttribute('role', 'dialog'); card.setAttribute('aria-modal', 'true');
    card.setAttribute('aria-labelledby', `${prefix}-confirm-title`); card.setAttribute('aria-describedby', `${prefix}-confirm-copy`);
    const h = el('h2', '', helperSimulation ? 'Run this simulated update once?' : 'Keep your board powered'); h.id = `${prefix}-confirm-title`;h.className='update-warning-heading';h.prepend(createUpdateIcon(doc,'warning'));
    const copy = el('p', '', helperSimulation ? 'SIMULATION uses a fake board and never opens physical USB, writes firmware or verifies a running device. This exercises the update workflow only.' : 'Release all controls and keep USB connected until the helper finishes. This writes application flash. Bootloader memory preparation runs before hardware/UID verification and may change external contents. The backup contains configuration only; it is not raw flash and provides no qualified recovery or automatic rollback. No automatic restore follows.'); copy.id = `${prefix}-confirm-copy`;
    card.append(h, el('p','update-power-warning',POWER_WARNING), copy, el('p', '', `Package: ${selected.name} · SHA-256 ${inspected.file_sha256}`),
      el('p', '', helperSimulation ? `Simulated board: ${currentDevice().label || currentDevice().id}. No physical board is selected.` : `Selected physical update-mode board: ${currentDevice().label || currentDevice().id}. Confirm this is the intended connected board; its bootloader HID serial does not establish its application UID before preparation.`),
      el('p', '', currentBackup() ? restoreWanted.checked&&inspected.kind==='thy666'?'A verified configuration backup was downloaded in this session. After firmware checks, a separate review will ask you to restore it and verify the saved readback.':'A verified configuration backup was downloaded in this session. It will not be restored by this update.':'No fresh configuration backup is available. Settings recovery is not guaranteed.'));
    const cancel = button('Cancel', 'cancel'), confirm = button(helperSimulation ? 'Run simulation once' : 'Confirm and install firmware', 'confirm'); card.append(cancel, confirm);
    dialog.append(card); doc.body.append(dialog); cancel.focus();
  }
  async function installOnce() {
    if (!dialog || !mayInstall()) { closeConfirmation(); return; }
    const request = { operation_id: operationId(), package_sha256: inspected.file_sha256,
      device_id: currentDevice().id, backup_disposition: currentBackup() ? 'downloaded' : 'unavailable',
      confirmed: true, preparation_acknowledged: true, physical_target_confirmed: true };
    if (currentBackup()||detectedUID) request.expected_uid = currentBackup()?.uid||detectedUID;
    preservationRequested=Boolean(currentBackup()&&restoreWanted.checked&&inspected.kind==='thy666'&&!helperSimulation);
    restorationState=preservationRequested?'pending':'idle';restorationError='';
    closeConfirmation({ focus: false });
    operation = { operation_id: request.operation_id, sequence: 0, state: 'outcome-unknown', simulation: helperSimulation,firmware_kind:inspected.kind,
      message: helperSimulation ? 'Simulation requested.' : 'Installation requested. Do not repeat the write.' };
    setLocked(true);
    await run(async () => {
      try { const value = await helper.install(request); if (!disposed) acceptOperation(value, request.operation_id); }
      catch (error) {
        if (error.helperRejected === true) {
          rejectionPending = request.operation_id;
          say(`The helper rejected the installation request: ${error.message} Use Check update status to resolve ownership before another action. No automatic retry was sent.`, 'error');
        } else say(`${helperSimulation ? 'Simulation' : 'Update'} outcome is unknown: ${error.message} Keep the helper running and use Check update status. No automatic retry was sent.`, 'error');
      }
    });
  }
  async function connectHelper(options={endpoint:endpoint.value,token:token.value}){
    if(locked)throw new Error('Resolve the current update status before changing helper sessions. No replacement will be started.');
    const value=await helper.connect(options);token.value='';if(disposed)return;
    helperSimulation=value?.simulation===true||value?.mode==='simulation';inspected=null;devices=[];device.replaceChildren();
    if(value?.operation)acceptOperation(value.operation,value.operation.operation_id);
    else{
      if(operation&&!terminalStates.has(operation.state))throw new Error('The previous update remains unresolved. Check its original updater before another action.');
      const replacing=Boolean(operation);operation=null;applicationVerification=null;rejectionPending=null;preservationRequested=false;restorationState='idle';restorationError='';
      if(replacing){health.clear();healthError='';backup=null;backupState='idle';restoreWanted.checked=false;unavailable.checked=false;detectedUID=null;selected=null;source='thy666';++fileTicket;++packageTicket;showStep(1);}
      try{await loadReleases();}catch{releaseNotice.textContent='The firmware list could not be read. You can still choose a custom file.';}
      if(step===2&&source!=='custom')await chooseSource(source);
      say(helperSimulation?'Simulation only. No physical board is accessed.':'');
    }
  }
  async function verifyInstallation(){
    if(!operation||operation.state!=='awaiting-verification'||operation.simulation||operation.firmware_kind==='stock'||verifying)return;
    await run(async()=>{
      verifying=true;health.clear();healthError='';applicationVerification=null;
      let checking='identity';
      try{
        if(!client.connected)await connectApplication();
        if(!client.connected)throw new Error('Choose your THY666 board to complete verification.');
        const identity={...client.identity};
        health.set('identity',{state:'busy',message:'Comparing the running firmware…'});refresh();
        const message=checkUpdateIdentity(operation,identity);health.set('identity',{state:'pass',message});
        for(const id of ['status','io','settings','display']){
          checking=id;health.set(id,{state:'busy',message:'Reading the board…'});refresh();
          checkUpdateIdentity(operation,client.identity);
          const report=await client.telemetry(id);if(disposed)return;
          const message=checkUpdateHealth(id,report,operation);health.set(id,{state:'pass',message});refresh();
        }
        checkUpdateIdentity(operation,client.identity);
        if(!client.connected)throw new Error('The board disconnected before verification finished.');
        applicationVerification={verified:true,state:'matched',scope:'basic-installation-health',actualBuild:identity.build,actualUID:identity.uid};
        say(preservationRequested&&!preservationResolved()?'Firmware checks passed. Review and restore your saved configuration, or explicitly keep the current settings.':'Firmware verified. The firmware identity and basic health checks passed.',preservationRequested&&!preservationResolved()?'info':'success');
      }catch(error){healthError=error.message;health.set(checking,{state:'failure',message:error.message});applicationVerification={verified:false,state:'failed',scope:'basic-installation-health'};throw error;}
      finally{verifying=false;}
    });
  }
  function requestRestoration(kind='restore') {
    if(disposed||dialog||!(kind==='restore'?mayRestore():mayKeepSettings()))return;
    returnFocus=doc.activeElement||(kind==='restore'?restore:keepSettings);confirmationKind=kind;
    dialog=el('div','thy-help-overlay updater-confirmation');dialog.dataset.updaterDialog='';
    const card=el('section','thy-help-dialog');card.setAttribute('role','dialog');card.setAttribute('aria-modal','true');
    card.setAttribute('aria-labelledby',`${prefix}-confirm-title`);card.setAttribute('aria-describedby',`${prefix}-confirm-copy`);
    const h=el('h2','',kind==='restore'?'Restore this saved configuration?':'Keep the current settings?');h.id=`${prefix}-confirm-title`;
    const copy=el('p','',kind==='restore'?'This replaces the connected board’s applied settings, calibration overrides, custom library and current song with the backup retained before this update. Release all pads and controls and stop playback. The restore will save, re-export and compare the complete payload; it does not write firmware.':'Your backup will not be written. The settings currently on the verified board will be kept, including factory defaults if the update reset them. Keep the downloaded backup for a later explicit restore.');copy.id=`${prefix}-confirm-copy`;
    card.append(h,copy,el('p','',`Saved configuration: build ${backup.build.slice(0,8)} · ${backup.bytes.length} bytes · payload CRC ${backup.payloadCRC.toString(16).padStart(8,'0')}`));
    const cancel=button('Cancel','cancel'),confirm=button(kind==='restore'?'Restore and verify saved settings':'Keep current settings','confirm');card.append(cancel,confirm);dialog.append(card);doc.body.append(dialog);cancel.focus();
  }
  async function restoreOnce() {
    if(confirmationKind!=='restore'||!mayRestore()){closeConfirmation();return;}
    const expectedOperation=operation.operation_id,retained=backup;
    try{restoreIdentity();}catch(error){closeConfirmation();say(error.message,'error');return;}
    closeConfirmation({focus:false});restorationState='running';restorationError='';setLocked(true);
    await run(async()=>{
      try{
        const result=await client.restoreBackup(retained.bytes.slice());
        if(disposed)return;
        if(operation?.operation_id!==expectedOperation||backup!==retained)throw new Error('The update or retained backup changed during restoration.');
        restoreIdentity();
        if(result?.verified!==true||(result.sourcePayloadCRC??result.payloadCRC)!==retained.payloadCRC)
          throw new Error('The saved restoration result does not match the retained configuration.');
        restorationState='verified';say('Settings Restored. Configuration saved and read back exactly.','success');
      }catch(error){if(!disposed){restorationState='failed';restorationError=error.message;say(`Configuration restoration needs attention: ${error.message} No automatic restore retry was sent.`,'error');}}
      finally{setLocked(false);}
      if(restorationState==='verified'&&!disposed){
        try{await onRestored();}
        catch{say('Configuration restored and verified. Reconnect Client to refresh its local display.','error');}
      }
    });
  }
  function keepCurrentSettings() {
    if(confirmationKind!=='keep-settings'||!mayKeepSettings()){closeConfirmation();return;}
    closeConfirmation();restorationState='declined';restorationError='';say('Firmware verified. You chose to keep the current settings; no configuration restore was sent.','success');
  }
  listen(root, 'click', event => {
    const target = event.target.closest?.('[data-updater-action]'); if (!target || target.disabled) return;
    const action = target.dataset.updaterAction;
    if (previewOpen) return;
    if(action==='browse'){if(!file.disabled)file.click();}
    else if (action === 'backup') void readBackup();
    else if(action==='next'){
      if(step===1&&currentBackup())void run(enterInstall);
    }
    else if(action==='back'&&!locked&&!operation){showStep(Math.max(1,step-1));refresh();}
    else if(action==='skip-backup'&&!locked){backupState='skipped';unavailable.checked=true;void run(enterInstall);}
    else if(action==='application')void run(async()=>{if(helperSimulation)throw new Error('Simulation does not connect to a physical board.');if(!client.connected)await connectApplication();refresh();});
    else if(action==='prepare')void run(async()=>{await client.disconnect();devices=[];device.replaceChildren();say('Now hold the marked LEFT pad pair while reconnecting USB, then choose Detect board.');});
    else if(action==='source')void run(()=>chooseSource(target.dataset.firmwareSource));
    else if(action==='verify')void verifyInstallation();
    else if(action==='restore')requestRestoration();
    else if(action==='keep-settings')requestRestoration('keep-settings');
    else if(action==='companion'&&successfulInstallation())onCompanion();
    else if (action === 'connect') void run(()=>connectHelper());
    else if (action === 'disconnect') void run(async () => { await helper.disconnect(); helperSimulation = false; inspected = null; devices = []; device.replaceChildren(); say('Local helper disconnected.'); });
    else if (action === 'clear-file') { ++fileTicket; selected = inspected = null; file.value = ''; refresh(); }
    else if (action === 'inspect') void run(async () => {
      inspected = null; const value = await helper.inspectPackage(selected.bytes, { name: selected.name, expected_build: expectedBuild.value.trim() });
      if (disposed) return;
      if (!value || !/^[0-9a-f]{64}$/.test(value.file_sha256 || '') || value.file_bytes !== selected.bytes.length)
        throw new Error('Helper package inspection did not match the selected file.');
      inspected = { ...value };if(!value.installable)fileDetails.open=true;say(value.installable?'File checks passed. Ready for your review.':'File identity is incomplete. Check the build value under Advanced.');
    });
    else if (action === 'find') void run(async () => {
      if (applicationActive() || client.busy) throw new Error('Disconnect the application before selecting an update-mode board.');
      devices = []; device.replaceChildren();
      const values = await helper.listDevices();
      if (disposed) return;
      if (!Array.isArray(values) || values.length > 32 || values.some(item => !item || typeof item.id !== 'string' || !item.id)) throw new Error('Helper returned invalid update-mode board choices.');
      devices = values.map(item => ({ ...item }));
      const placeholder = el('option', '', 'Choose the intended board'); placeholder.value = ''; device.append(placeholder); device.value = '';
      for (const item of devices) { const option = el('option', '', item.label || item.id); option.value = item.id; device.append(option); }
      if(devices.length===1)device.value=devices[0].id;
      say(devices.length ? 'TheoryBoard detected. No firmware command has been sent.' : 'No update-mode board found. Check the marked pads and close other updaters.');
    });
    else if (action === 'install') requestInstall();
    else if (action === 'progress') void checkProgress();
  });
  listen(file, 'change', () => { if (!working && !locked && !operation) void selectFile(); });
  listen(expectedBuild, 'input', () => { inspected = null; refresh(); });
  listen(device, 'change', refresh); listen(unavailable, 'change', refresh);listen(restoreWanted,'change',refresh);
  listen(doc, 'click', event => {
    if (!dialog?.contains(event.target)) return;
    const target = event.target.closest?.('[data-updater-action]'); if (!target) return;
    event.preventDefault(); event.stopPropagation();
    if (target.dataset.updaterAction === 'cancel') closeConfirmation();
    else if (target.dataset.updaterAction === 'confirm') {
      if(confirmationKind==='preview-install'){const action=previewConfirm;closeConfirmation();action?.();}
      else if(confirmationKind==='restore')void restoreOnce();
      else if(confirmationKind==='keep-settings')keepCurrentSettings();
      else void installOnce();
    }
  }, true);
  listen(doc, 'keydown', event => {
    if (!dialog) return;
    if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); closeConfirmation(); }
    if (event.key === 'Tab') {
      const cancel = dialog.querySelector('[data-updater-action="cancel"]'), confirm = dialog.querySelector('[data-updater-action="confirm"]');
      if (event.shiftKey && (doc.activeElement === cancel || !dialog.contains(doc.activeElement))) { event.preventDefault(); confirm.focus(); }
      else if (!event.shiftKey && (doc.activeElement === confirm || !dialog.contains(doc.activeElement))) { event.preventDefault(); cancel.focus(); }
    }
  }, true);
  refresh();
  const launch=runtime?.kind==='desktop'||directBrowser?{}:handoff||consumeUpdaterHandoff();if(launch)void run(()=>connectHelper(launch)).finally(()=>{quietDesktopLaunch=false;});
  return {
    refresh, get usbBusy() { return locked && !helperSimulation && operation?.simulation !== true; }, get simulation() { return helperSimulation || operation?.simulation === true; }, get modalOpen() { return Boolean(dialog); },
    setHostActive(active){hostActive=Boolean(active);if(!hostActive)cancelProgress();else if(operation?.state==='running'&&!working)void checkProgress();},
    destroy() {
      if (disposed) return;
      cancelPreview();closeConfirmation(); disposed = true; cancelProgress(); fileTicket++; token.value = ''; selected = inspected = backup = null;
      for (const remove of listeners.splice(0)) remove(); mount.replaceChildren();
      // Removing browser UI must never abort or repeat an in-flight native write.
      if (!locked) void helper.disconnect();
    },
  };
}
