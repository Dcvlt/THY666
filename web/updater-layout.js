/* Presentation only: the wizard controller supplies every device state. */
import { createUpdateBoardArt, createUpdateLogoArt, createUpdateIcon, createUpdateProgressArt } from './updater-art.js';
import { UPDATE_HEALTH_CHECKS } from './updater-checks.js';

export function createUpdaterLayout(doc, prefix) {
  const el=(tag,className='',text)=>{const n=doc.createElement(tag);n.className=className;if(text!==undefined)n.textContent=String(text);return n;};
  const button=(text,action,style='')=>{const n=el('button',`button ${style}`,text);n.type='button';n.dataset.updaterAction=action;return n;};
  const field=(label,control)=>{const n=el('label','update-field',label);n.append(control);return n;};
  const input=(type,id)=>{const n=el('input');n.type=type;n.dataset.updaterField=id;n.setAttribute('autocomplete','off');n.setAttribute('spellcheck','false');return n;};
  const root=el('section','update-wizard updater-panel');root.setAttribute('aria-labelledby',`${prefix}-title`);
  const heading=el('div','update-wizard-heading'),title=el('h1','','Firmware update');title.id=`${prefix}-title`;
  const help=button('Installation guide','help','update-wizard-help');help.dataset.help='updates';help.append(createUpdateIcon(doc,'shield'));heading.append(title,help);
  const rail=el('ol','update-step-rail');rail.setAttribute('aria-label','Installation steps');
  const stepEntries=['Detect','Backup','Update','Verify'].map((label,i)=>{
    const n=el('li','update-step'),copy=el('span','update-step-copy');copy.append(el('span','update-step-label',label),el('span','update-step-caption',['Find your board','Keep your settings','Install firmware','Ready to play'][i]));
    n.append(el('span','update-step-number',i+1),copy);rail.append(n);return n;
  });
  const stages=[],bodies=[],actions=[],headings=[];
  for(const [i,title,description] of [[0,'Meet your TheoryBoard','Compatible with original firmware and THY666.'],[1,'Keep what matters','Save your THY666 settings before changing firmware. You can also skip this step.'],[2,'Choose your firmware','THY666 is recommended. You can also restore the original firmware or use a supported custom file.'],[3,'One last check','We will check the new THY666 firmware before you start playing.']]){
    const n=el('section','update-stage'),head=el('div','update-stage-heading'),h=el('h3','',title);h.setAttribute('tabindex','-1');
    n.dataset.updateStage=String(i);n.hidden=i!==0;head.append(h,el('p','',description));const body=el('div','update-stage-body'),a=el('div','update-stage-actions');n.append(head,body,a);stages.push(n);bodies.push(body);actions.push(a);headings.push(h);
  }
  const setup=el('details','update-connection-card');setup.dataset.updaterReadout='setup';
  const setupSummary=el('summary');setupSummary.append(el('span','','Connect the updater on this computer'),createUpdateIcon(doc,'usb'));
  const download=el('a','button update-secondary','Download Windows updater');download.href='./generated/updater/THY666-updater-windows-x64.zip';download.download='THY666-updater-windows-x64.zip';
  setup.append(setupSummary,el('p','update-field-hint','Open the THY666 Updater application. It opens this page and connects securely. This connects the installer only; nothing is installed yet.'),download,el('p','update-field-hint','Unzip the download and open thy666-updater.exe. Keep this local app running. macOS and Linux currently require a source build.'));
  const manual=el('details','update-manual-setup');manual.append(el('summary','','Advanced / manual connection'),el('p','update-field-hint','If you started the helper manually, enter the address and session token shown in its window. Keep other firmware updaters closed.'));
  const endpoint=input('url','endpoint');endpoint.value='http://127.0.0.1:3331';const token=input('password','token');
  const connect=button('Connect helper','connect'),disconnect=button('Disconnect helper','disconnect');
  const connectionFields=el('div','update-connection-fields');connectionFields.append(field('Helper address',endpoint),field('Session token',token));
  const helperActions=el('div','update-inline-actions');helperActions.append(connect,disconnect);manual.append(connectionFields,helperActions);
  const helperSummary=el('p','update-field-hint');helperSummary.dataset.updaterReadout='helper';setup.append(manual,helperSummary);
  const browserSummary=el('p','update-field-hint');browserSummary.dataset.updaterReadout='browser';browserSummary.hidden=true;
  const simulationBadge=el('p','update-simulation','SIMULATION / Practice only. No physical board is updated or verified.');simulationBadge.dataset.updaterReadout='simulation';simulationBadge.hidden=true;
  const boardArt=createUpdateBoardArt(doc),boardIdentity=el('p','update-identity');boardIdentity.dataset.updaterReadout='board';
  const application=button('Already using THY666? Connect for a backup','application','update-quiet'),detectNext=button('Continue','next','update-primary');
  const entry=el('ol','update-entry-steps');
  for(const [title,copy] of [
    ['Power down the device','Disconnect USB and 9V power if used. Ensure the screen and LEDs are off.'],
    ['Hold both marked pads','Hold the top-left and bottom-left large pads of the LEFT cluster.'],
    ['Power up while holding','Reconnect USB and 9V power if used. The TheoryBoard logo should appear on its own, with all LEDs off.'],
    ['Release and detect','Release both held pads, then click Detect board below.'],
  ]){const item=el('li');item.append(el('strong','',title),el('span','',copy));entry.append(item);}
  const find=button('Detect board','find','update-primary'),device=el('select');device.dataset.updaterField='device';device.setAttribute('aria-label','Update-mode board');
  const deviceActions=el('div','update-device-dock');const detectActions=el('div','update-stage-actions');detectActions.append(find);deviceActions.append(entry,detectActions,field('Detected board',device));
  bodies[0].append(application,boardArt,boardIdentity,deviceActions);actions[0].append(detectNext);
  const backupArt=el('div','update-backup-art');backupArt.append(createUpdateIcon(doc,'backup'));
  const coverage=el('div','update-backup-coverage');for(const text of ['Settings','Calibration','Chords & scales','Current song'])coverage.append(el('span','',text));backupArt.append(coverage);
  const backupDetails=el('div','update-backup-details');backupDetails.append(el('h4','','Configuration backup'),el('p','','A verified copy you can restore to THY666. This is not a full-device image and cannot repair a damaged bootloader.'));
  const backupLayout=el('div','update-backup-layout');backupLayout.append(backupArt,backupDetails);
  const backupSummary=el('p','update-status');backupSummary.dataset.updaterReadout='backup';
  const read=button('Back up my settings','backup','update-primary'),backupNext=button('Continue','next','update-primary'),skip=button('Skip backup','skip-backup','update-quiet');
  const unavailable=input('checkbox','backup-unavailable');unavailable.hidden=true;unavailable.setAttribute('aria-hidden','true');unavailable.setAttribute('tabindex','-1');
  const backupConnect=button('Connect THY666 to back up','application','update-secondary');
  const restoreWanted=input('checkbox','restore-wanted');restoreWanted.checked=false;
  const restoreChoice=field('Restore this configuration after verifying THY666',restoreWanted);restoreChoice.className='update-restore-choice';restoreChoice.hidden=true;
  bodies[1].append(backupLayout,backupSummary,backupConnect,restoreChoice,el('p','update-stage-footnote','Backups require THY666 in normal playing mode. Stock firmware and update mode do not provide a verified full dump. Restoration requires a separate review after firmware checks. No automatic restore or rollback is performed.'),unavailable);
  actions[1].append(button('Back','back','update-quiet'),read,backupNext,skip);
  const firmwareChoices=el('div','update-firmware-choices');firmwareChoices.setAttribute('role','group');firmwareChoices.setAttribute('aria-label','Firmware choices');const sourceButtons=new Map();
  for(const [id,label,description] of [['thy666','Latest THY666','Recommended / our open firmware'],['stock','Original THY333','Restore the original v5 firmware'],['custom','Custom file','A supported .333 from your computer']]){
    const choice=button('','source','update-firmware-choice');choice.dataset.firmwareSource=id;choice.append(createUpdateIcon(doc,id==='custom'?'file':'board'),el('strong','',label),el('span','',description));firmwareChoices.append(choice);sourceButtons.set(id,choice);
  }
  const releaseNotice=el('p','update-field-hint');releaseNotice.dataset.updaterReadout='releases';
  const file=input('file','file');file.accept='.333,application/octet-stream';const expectedBuild=input('text','expected-build');expectedBuild.maxLength=16;
  const inspect=button('Check file','inspect','update-secondary'),clearFile=button('Clear package','clear-file','update-quiet');
  const custom=el('div','update-file-card'),fileDetails=el('details','update-package-details');fileDetails.append(el('summary','','File identity / advanced'),field('Expected full build (optional when the standard filename identifies it)',expectedBuild),clearFile);
  custom.append(field('Choose a prepared .333 package',file),inspect,fileDetails);custom.hidden=true;
  const packageSummary=el('div','update-file-card');packageSummary.dataset.updaterReadout='package';
  const prepare=button('Prepare the board for installation','prepare','update-secondary'),prepared=el('div','update-install-preparation');prepared.append(el('p','update-field-hint','Close other firmware updaters. After saving your settings, switch the board to update mode before applying firmware.'),prepare);
  const install=button('Apply firmware','install','update-primary'),progress=button('Check update status','progress','update-secondary');
  bodies[2].append(firmwareChoices,releaseNotice,custom,packageSummary,prepared);actions[2].append(button('Back','back','update-quiet'),install,progress);
  const operationSummary=el('p','update-field-hint');operationSummary.dataset.updaterReadout='progress';
  const transferDetails=el('details','update-package-details');transferDetails.append(el('summary','','Transfer details'),operationSummary);
  const recovery=el('details','update-recovery');recovery.append(el('summary','','If the update stops'),el('p','','Keep the board connected and the updater open. Check the existing operation status before doing anything else. Do not repeat an update that may still be running.'),el('p','','If transfer has stopped, check whether the board has returned to playing mode or remains on its logo. The official updater and a known working package remain the recovery route to investigate. Recovery after an interrupted update is not yet verified.'));
  const completion=el('div','update-completion');completion.dataset.state='waiting';const logoArt=createUpdateLogoArt(doc),completionTitle=el('h4'),completionCopy=el('p','update-stage-footnote');completion.append(logoArt,completionTitle,completionCopy);
  const healthList=el('div','update-health-list'),healthRows=new Map();for(const check of UPDATE_HEALTH_CHECKS){const row=el('div','update-health-row'),result=el('span','update-health-result','Waiting');row.dataset.check=check.id;row.append(createUpdateIcon(doc,'clock'),el('span','update-health-label',check.label),result);healthList.append(row);healthRows.set(check.id,{row,result});}
  const applicationSummary=el('p','update-stage-footnote');applicationSummary.dataset.updaterReadout='verification';applicationSummary.setAttribute('aria-live','polite');
  const restoration=el('section','update-restoration');restoration.dataset.updaterReadout='restoration';
  const restorationTitle=el('h4','','Your saved configuration'),restorationSummary=el('p','update-stage-footnote');restorationSummary.dataset.updaterReadout='restoration-status';restorationSummary.setAttribute('aria-live','polite');
  const restore=button('Restore saved configuration','restore','update-primary'),keepSettings=button('Keep current settings instead','keep-settings','update-quiet');
  const restorationActions=el('div','update-inline-actions');restorationActions.append(restore,keepSettings);restoration.append(restorationTitle,restorationSummary,restorationActions);restoration.hidden=true;
  const verify=button('Connect and verify','verify','update-primary'),companion=button('Go to Companion','companion','update-primary');bodies[3].append(healthList,applicationSummary);actions[3].append(verify,companion);
  function progressBox(label){const box=el('div','update-progress');box.setAttribute('role','progressbar');box.setAttribute('aria-label',label);box.setAttribute('aria-valuemin','0');box.setAttribute('aria-valuemax','100');const art=createUpdateProgressArt(doc),copy=el('p','update-progress-copy');box.append(art,copy);return{box,art,copy};}
  const backupProgress=progressBox('Configuration backup progress'),installProgress=progressBox('Firmware transfer progress'),restoreProgress=progressBox('Configuration restoration progress');bodies[1].append(backupProgress.box);bodies[2].append(installProgress.box,transferDetails,recovery);restoration.append(restoreProgress.box);bodies[3].append(restoration);
  const status=el('p','update-status updater-notice');status.dataset.updaterReadout='notice';status.setAttribute('role','status');status.setAttribute('aria-live','polite');
  root.append(heading,rail,simulationBadge,browserSummary,setup,...stages,status);
  return{root,stages,bodies,actions,headings,stepEntries,setup,manual,boardArt,boardIdentity,application,detectNext,deviceActions,entry,read,backupNext,skip,backupConnect,backupSummary,backupProgress,restoreChoice,restoreWanted,restoration,restorationSummary,restore,keepSettings,restoreProgress,unavailable,endpoint,token,connect,disconnect,helperSummary,browserSummary,simulationBadge,sourceButtons,releaseNotice,custom,file,expectedBuild,clearFile,inspect,fileDetails,packageSummary,find,device,prepared,prepare,install,progress,operationSummary,completion,logoArt,completionTitle,completionCopy,healthList,healthRows,applicationSummary,verify,companion,installProgress,recovery,status};
}
