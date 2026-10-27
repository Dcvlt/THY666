/* UI drafts and ABI dispatch only. Validation, library storage and music live
 * in the shared C core. This panel never connects to MIDI or device hardware. */
const capacities=[8,24,3];
const labels=['Scale','Chord','Six-chord set'];
const validSlot=(kind,slot)=>Number.isInteger(kind)&&kind>=0&&kind<3&&
  Number.isInteger(slot)&&slot>=0&&slot<capacities[kind];
const clone=record=>record?{...record,...(record.notes?{notes:[...record.notes]}:{}),
  ...(record.chords?{chords:[...record.chords]}:{})}:null;
const fresh=(kind,slot)=>kind===0?{used:true,name:`Scale ${slot+1}`,mask:1}:
  kind===1?{used:true,name:`Chord ${slot+1}`,root:60,notes:[60,64,67,0,0,0,0],mask:7}:
    {used:true,name:`Set ${slot+1}`,chords:Array(6).fill(255)};
const invalid={ok:false,code:1,status:'invalid'};

export function createLibraryLabModel(library,{onChange=()=>{},onError=()=>{}}={}) {
  if(!library?.record||!library?.records||!library?.set||!library?.capture||
    !library?.delete||!library?.exportFile||!library?.importFile)
    throw new TypeError('Shared C library API is required.');
  let kind=0,slot=0,current=library.record(0,0),draft=current?.used?clone(current):fresh(0,0);
  let dirty=false,confirmDelete=false,notice='Edit a draft, then Apply. Changes stay in this local preview.';
  let lastResult={ok:true,code:0,status:'ok'};
  const report=(label,result)=>{
    lastResult=result||invalid;
    const reason=lastResult.status==='in-use'?'In use: remove its active selection or saved references first.':
      lastResult.status==='busy'?'Busy: release pads and stop playback; for import, close the OLED library editor, then retry.':
        lastResult.status==='draft-pending'?'The draft has not been saved.':
          lastResult.status==='invalid'&&!lastResult.message?'Check the name, ranges and saved chord references.':
        lastResult.message||lastResult.status||'Rejected by the shared core.';
    notice=lastResult.ok?label:`${label}: ${reason}`;
    if(!lastResult.ok)onError(new Error(notice));
    return lastResult;
  };
  const reload=()=>{
    current=library.record(kind,slot);
    draft=current?.used?clone(current):fresh(kind,slot);
    dirty=false;confirmDelete=false;
  };
  const pending=()=>report('Apply or Cancel the current draft first',{ok:false,code:10,status:'draft-pending'});
  return {
    snapshot() {
      const latest=library.record(kind,slot);
      if(!dirty && JSON.stringify(latest)!==JSON.stringify(current)) {
        current=latest;draft=current?.used?clone(current):fresh(kind,slot);
      } else current=latest;
      return {kind,slot,current:clone(current),draft:clone(draft),dirty,confirmDelete,notice,
        result:{...lastResult},records:capacities.map((_,type)=>library.records(type).map(clone))};
    },
    select(nextKind,nextSlot=0) {
      if(!validSlot(nextKind,nextSlot))return report('Selection',invalid);
      if(nextKind===kind&&nextSlot===slot)return {ok:true,code:0,status:'ok'};
      if(dirty)return pending();
      kind=nextKind;slot=nextSlot;reload();
      return report(`${labels[kind]} ${slot+1} selected`,{ok:true,code:0,status:'ok'});
    },
    update(changes) {
      if(!changes||typeof changes!=='object'||Array.isArray(changes))return report('Draft',invalid);
      const allowed=kind===0?['name','mask']:kind===1?['name','root','notes','mask']:['name','chords'];
      if(Object.keys(changes).some(key=>!allowed.includes(key)))return report('Draft',invalid);
      draft=clone({...draft,...changes,used:true});dirty=true;confirmDelete=false;
      return report('Draft changed · Apply to save',{ok:true,code:0,status:'ok'});
    },
    apply() {
      const result=report('Apply',library.set(kind,slot,clone(draft)));
      if(result.ok){reload();notice=`${labels[kind]} ${slot+1} applied to the shared core`;onChange();}
      return result;
    },
    cancel() {reload();return report('Draft cancelled; saved record restored',{ok:true,code:0,status:'ok'});},
    capture() {
      const response=library.capture(kind,slot);
      if(!response?.ok||!response.record)return report('Copy current',response?.ok?invalid:response);
      draft=clone(response.record);draft.used=true;dirty=true;confirmDelete=false;
      return report('Current material copied into the draft · Apply to save',{ok:true,code:0,status:'ok'});
    },
    requestDelete() {
      if(!library.record(kind,slot)?.used)return report('Delete',invalid);
      confirmDelete=true;
      return report('Confirm deletion of this saved record below',{ok:true,code:0,status:'ok'});
    },
    cancelDelete() {confirmDelete=false;return report('Deletion cancelled',{ok:true,code:0,status:'ok'});},
    deleteConfirmed() {
      if(!confirmDelete)return report('Delete confirmation required',invalid);
      const result=report('Delete',library.delete(kind,slot));
      if(result.ok){reload();notice='Saved record deleted';onChange();}
      return result;
    },
    exportFile() {
      const bytes=library.exportFile();
      if(!(bytes instanceof Uint8Array)) {report('Export',invalid);return null;}
      report('Exported saved library · unapplied drafts are excluded',{ok:true,code:0,status:'ok'});
      return bytes;
    },
    importFile(bytes) {
      if(dirty)return pending();
      const result=report('Import',library.importFile(bytes));
      if(result.ok){reload();notice='THYL library imported and applied to the shared core';onChange();}
      return result;
    },
    openEditor() {
      if(dirty)return pending();
      const result=report('Open OLED editor',library.openEditor?.(kind,slot)||invalid);
      if(result.ok)onChange();return result;
    },
    failure(error) {return report('Library',{ok:false,code:1,status:'invalid',message:error?.message||String(error)});},
  };
}

const element=(document,tag,text,className)=>{
  const node=document.createElement(tag);
  if(text!==undefined)node.textContent=text;
  if(className)node.className=className;
  return node;
};

export function initializeLibraryLab(host,core,{onChange=()=>{},onError=()=>{},download}={}) {
  const document=host?.ownerDocument||globalThis.document;
  if(!host||!document)throw new TypeError('Library host element is required.');
  const library=core?.firmware?.library;
  const model=createLibraryLabModel(library,{onChange,onError});
  host.className=`${host.className||''} panel library-lab`.trim();
  host.setAttribute('aria-label','Shared C custom scale and chord library');
  const heading=element(document,'div',undefined,'engine-heading');
  const title=element(document,'div');
  title.append(element(document,'p','THY666 / SHARED C LIBRARY','eyebrow'),
    element(document,'h2','Custom library'),
    element(document,'p','Create scales, seven-slot chords and six-chord sets. Edit a draft, then Apply.'));
  heading.append(title,element(document,'span','C / WebAssembly','engine-scope-badge'));
  const makeField=(text,input,parent)=>{
    const label=element(document,'label',undefined,'library-field');
    label.append(element(document,'span',text),input);parent.append(label);
    input.setAttribute('aria-label',text);return input;
  };
  const makeButton=(text,action,parent)=>{
    const button=element(document,'button',text,'button button-small');
    button.type='button';button.dataset.libraryAction=action;parent.append(button);return button;
  };
  const toolbar=element(document,'div',undefined,'library-toolbar');
  const kind=makeField('Library',element(document,'select'),toolbar);kind.dataset.libraryField='kind';
  ['User scales · 8 slots','User chords · 24 slots','Six-chord sets · 3 slots'].forEach((name,index)=>{
    const option=element(document,'option',name);option.value=String(index);kind.append(option);
  });
  const slot=makeField('Named slot',element(document,'select'),toolbar);slot.dataset.libraryField='slot';
  const open=makeButton('Open OLED editor','open',toolbar);
  const usage=element(document,'p',undefined,'library-usage');usage.dataset.libraryReadout='usage';
  const fields=element(document,'div',undefined,'library-fields');
  const name=makeField('Name · 12 ASCII characters',element(document,'input'),fields);
  name.type='text';name.maxLength=12;name.autocomplete='off';name.dataset.libraryField='name';
  const state=element(document,'p',undefined,'library-record-state');state.dataset.libraryReadout='state';
  const scales=element(document,'fieldset',undefined,'library-scale-grid');
  scales.append(element(document,'legend','Scale degrees · tonic stays enabled'));
  const degreeLabels=['1 / tonic','♭2','2','♭3','3','4','♯4','5','♭6','6','♭7','7'];
  const degrees=degreeLabels.map((text,index)=>{
    const input=element(document,'input');input.type='checkbox';input.dataset.libraryDegree=String(index);
    input.disabled=index===0;makeField(text,input,scales);return input;
  });
  const chords=element(document,'fieldset',undefined,'library-chord-fields');
  chords.append(element(document,'legend','Original chord slots'));
  const root=makeField('MIDI root',element(document,'input'),chords);
  root.type='number';root.min='0';root.max='127';root.step='1';root.dataset.libraryField='root';
  const toneGrid=element(document,'div',undefined,'library-note-grid');
  const tones=Array.from({length:7},(_,index)=>{
    const row=element(document,'div',undefined,'library-note-row');
    const enabled=element(document,'input');enabled.type='checkbox';enabled.dataset.libraryToneEnabled=String(index);
    makeField(`Use slot ${index+1}`,enabled,row);
    const pitch=makeField(`Slot ${index+1} MIDI note`,element(document,'input'),row);
    pitch.type='number';pitch.min='0';pitch.max='127';pitch.step='1';pitch.dataset.libraryTone=String(index);
    toneGrid.append(row);return {enabled,pitch};
  });
  chords.append(toneGrid,element(document,'p','Inactive slots keep their original values. MIDI note zero is a valid note.','library-help'));
  const six=element(document,'fieldset',undefined,'library-six-grid');
  six.append(element(document,'legend','Six-chord references'));
  const references=Array.from({length:6},(_,index)=>{
    const select=makeField(`Chord position ${index+1}`,element(document,'select'),six);
    select.dataset.libraryReference=String(index);return select;
  });
  six.append(element(document,'p','Choose a saved chord or Empty for each position. Referenced chords cannot be deleted.','library-help'));
  fields.append(state,scales,chords,six);
  const actions=element(document,'div',undefined,'library-actions');
  const apply=makeButton('Apply draft','apply',actions);
  const cancel=makeButton('Cancel draft','cancel',actions);
  const capture=makeButton('Copy current into draft','capture',actions);
  const remove=makeButton('Delete saved record','delete',actions);
  const confirmation=element(document,'div',undefined,'library-delete-confirm');confirmation.hidden=true;
  confirmation.append(element(document,'span','Delete this saved record?'));
  const confirm=makeButton('Confirm delete','confirm-delete',confirmation);
  const keep=makeButton('Keep record','cancel-delete',confirmation);
  const files=element(document,'div',undefined,'library-files');
  const exportButton=makeButton('Export THYL','export',files);
  const importInput=makeField('Import THYL library',element(document,'input'),files);
  importInput.type='file';importInput.accept='.thyl,application/octet-stream';importInput.dataset.libraryField='import';
  files.append(element(document,'p','Local library files only. Import validates the whole file before changing the preview. On the device, library edits save with settings. Copy current captures the base chord; for a six-chord set it copies the selected saved user set.','library-help'));
  const notice=element(document,'p',undefined,'library-notice');notice.setAttribute('role','status');
  notice.setAttribute('aria-live','polite');notice.dataset.libraryReadout='notice';
  host.replaceChildren(heading,toolbar,usage,fields,actions,confirmation,files,notice);
  const listeners=[];let disposed=false,lastKey='',slotKey='',referencesKey='',importGeneration=0;
  const sync=(node,value)=>{if(document.activeElement!==node)node.value=String(value);};
  function refresh(force=false) {
    if(disposed)return;
    const value=model.snapshot(),key=JSON.stringify(value);
    if(!force&&key===lastKey)return;lastKey=key;
    kind.value=String(value.kind);
    const names=value.records[value.kind].map((record,index)=>`${String(index+1).padStart(2,'0')} · ${record?.used?record.name:'Empty'}`);
    const nextSlotKey=JSON.stringify([value.kind,names]);
    if(nextSlotKey!==slotKey){slotKey=nextSlotKey;slot.replaceChildren(...names.map((text,index)=>{
      const option=element(document,'option',text);option.value=String(index);return option;
    }));}
    slot.value=String(value.slot);sync(name,value.draft.name);
    usage.textContent=`Scales ${value.records[0].filter(record=>record?.used).length}/8 · Chords ${value.records[1].filter(record=>record?.used).length}/24 · Sets ${value.records[2].filter(record=>record?.used).length}/3`;
    state.textContent=`${value.current?.used?'Saved record':'Empty slot'} · ${value.dirty?'Unapplied draft':'No pending changes'}`;
    scales.hidden=value.kind!==0;chords.hidden=value.kind!==1;six.hidden=value.kind!==2;
    if(value.kind===0)degrees.forEach((input,index)=>{input.checked=Boolean(value.draft.mask&(1<<index));});
    if(value.kind===1) {
      sync(root,value.draft.root);
      tones.forEach(({enabled,pitch},index)=>{enabled.checked=Boolean(value.draft.mask&(1<<index));sync(pitch,value.draft.notes[index]);});
    }
    const refKey=JSON.stringify([value.records[1],value.kind===2?value.draft.chords:[]]);
    if(value.kind===2&&refKey!==referencesKey) {
      referencesKey=refKey;
      references.forEach((select,index)=>{
        const empty=element(document,'option','Empty');empty.value='255';
        const options=[empty];
        value.records[1].forEach((record,chord)=>{
          if(record?.used){const option=element(document,'option',`${String(chord+1).padStart(2,'0')} · ${record.name}`);option.value=String(chord);options.push(option);}
        });
        const selected=value.draft.chords[index];
        if(selected!==255&&!value.records[1][selected]?.used) {
          const missing=element(document,'option',`Unavailable chord ${selected+1}`);missing.value=String(selected);options.push(missing);
        }
        select.replaceChildren(...options);select.value=String(selected);
      });
    }
    apply.disabled=!value.dirty&&!value.current?.used;
    cancel.disabled=!value.dirty;remove.disabled=!value.current?.used;
    confirmation.hidden=!value.confirmDelete;open.disabled=typeof library.openEditor!=='function';
    notice.textContent=value.notice;
    notice.dataset.libraryStatus=value.result.status;
  }
  const handleError=error=>{if(disposed)return;model.failure(error);refresh();};
  const listen=(node,event,action)=>{
    const handler=()=>{
      try{const pending=action();if(pending?.catch)pending.catch(handleError);}
      catch(error){handleError(error);}
    };
    node.addEventListener(event,handler);listeners.push([node,event,handler]);
  };
  const command=action=>{action();refresh();};
  listen(kind,'change',()=>command(()=>model.select(Number(kind.value),0)));
  listen(slot,'change',()=>command(()=>model.select(model.snapshot().kind,Number(slot.value))));
  listen(name,'input',()=>command(()=>model.update({name:name.value})));
  degrees.forEach((input,index)=>listen(input,'change',()=>command(()=>{
    if(index===0)return;
    const mask=model.snapshot().draft.mask;
    model.update({mask:input.checked?mask|(1<<index):mask&~(1<<index)});
  })));
  listen(root,'input',()=>command(()=>model.update({root:root.value===''?NaN:Number(root.value)})));
  tones.forEach(({enabled,pitch},index)=>{
    listen(enabled,'change',()=>command(()=>{
      const mask=model.snapshot().draft.mask;model.update({mask:enabled.checked?mask|(1<<index):mask&~(1<<index)});
    }));
    listen(pitch,'input',()=>command(()=>{
      const notes=model.snapshot().draft.notes;notes[index]=pitch.value===''?NaN:Number(pitch.value);model.update({notes});
    }));
  });
  references.forEach((select,index)=>listen(select,'change',()=>command(()=>{
    const chords=model.snapshot().draft.chords;chords[index]=Number(select.value);model.update({chords});
  })));
  for(const [button,action] of [[apply,()=>model.apply()],[cancel,()=>model.cancel()],
    [capture,()=>model.capture()],[remove,()=>model.requestDelete()],[confirm,()=>model.deleteConfirmed()],
    [keep,()=>model.cancelDelete()],[open,()=>model.openEditor()]])listen(button,'click',()=>command(action));
  listen(exportButton,'click',async()=>{
    const bytes=model.exportFile();refresh();if(!bytes)return;
    if(download){await download(bytes,'THY666-library.thyl');return;}
    const address=URL.createObjectURL(new Blob([bytes],{type:'application/octet-stream'}));
    try{const anchor=element(document,'a');anchor.href=address;anchor.download='THY666-library.thyl';
      host.append(anchor);anchor.click();anchor.remove();}finally{URL.revokeObjectURL(address);}
  });
  listen(importInput,'change',async()=>{
    const generation=++importGeneration;
    const file=importInput.files?.[0];if(!file)return;
    if(file.size!==756){model.failure(new Error('A THYL library file must contain exactly 756 bytes.'));refresh();return;}
    const bytes=new Uint8Array(await file.arrayBuffer());
    if(disposed||generation!==importGeneration)return;
    const response=model.importFile(bytes);if(response.ok)importInput.value='';refresh();
  });
  refresh(true);
  return {refresh,model,dispose(){if(disposed)return;disposed=true;++importGeneration;
    for(const [node,event,handler] of listeners)node.removeEventListener(event,handler);
    host.replaceChildren();}};
}
