import test from 'node:test';
import assert from 'node:assert/strict';
import {createLibraryLabModel,initializeLibraryLab} from '../../web/library-lab.js';

const okay={ok:true,code:0,status:'ok'};
const empty=kind=>kind===0?{used:false,name:'',mask:0}:kind===1?
  {used:false,name:'',root:0,notes:Array(7).fill(0),mask:0}:{used:false,name:'',chords:Array(6).fill(0)};
const copy=object=>structuredClone(object);
function fixture() {
  const calls=[],errors=[];let changes=0,rejection=null;
  const records=[8,24,3].map((count,kind)=>Array.from({length:count},()=>empty(kind)));
  const file=new Uint8Array(756);file.set([84,72,89,76]);
  const command=(name,action)=>{
    calls.push(name);if(rejection){const result=rejection;rejection=null;return result;}
    action?.();return {...okay};
  };
  const api={
    record:(kind,slot)=>copy(records[kind]?.[slot]??null),
    records:kind=>copy(records[kind]??[]),
    set:(kind,slot,record)=>command(['set',kind,slot,copy(record)],()=>{records[kind][slot]=copy(record);}),
    delete:(kind,slot)=>command(['delete',kind,slot],()=>{records[kind][slot]=empty(kind);}),
    capture:(kind,slot)=>({...command(['capture',kind,slot]),record:copy(kind===0?
      {used:true,name:'COPIED SCALE',mask:0x295}:kind===1?
        {used:true,name:'COPIED CHORD',root:0,notes:[0,12,7,99,0,64,127],mask:0x45}:
        {used:true,name:'COPIED SET',chords:[0,255,1,255,2,255]})}),
    exportFile:()=>{calls.push(['export']);return file.slice();},
    importFile:bytes=>command(['import',bytes.slice()],()=>{records[0][0]={used:true,name:'IMPORTED',mask:1};}),
    openEditor:(kind,slot)=>command(['open',kind,slot]),
  };
  const options={onChange:()=>changes++,onError:error=>errors.push(error)};
  return {api,records,calls,errors,options,file,changes:()=>changes,
    reject:(code,status)=>{rejection={ok:false,code,status};}};
}

test('library inspection and drafts issue no storage, playing or device commands',()=>{
  const f=fixture(),model=createLibraryLabModel(f.api,f.options);
  let state=model.snapshot();assert.equal(state.kind,0);assert.equal(state.slot,0);
  assert.equal(state.current.used,false);assert.equal(state.draft.mask,1);assert.equal(state.dirty,false);
  assert.deepEqual(state.records.map(group=>group.length),[8,24,3]);
  model.update({name:'MY SCALE',mask:0x295});state=model.snapshot();
  assert.equal(state.dirty,true);assert.equal(state.draft.name,'MY SCALE');
  assert.equal(f.records[0][0].used,false);assert.deepEqual(f.calls,[]);assert.equal(f.changes(),0);
  state.draft.name='MUTATED';state.records[0][0].used=true;
  assert.equal(model.snapshot().draft.name,'MY SCALE');assert.equal(f.records[0][0].used,false);
  assert.throws(()=>createLibraryLabModel({}),/Shared C library/);
});

test('Apply dispatches exact draft, Cancel reloads saved values, selection protects pending edits',()=>{
  const f=fixture(),model=createLibraryLabModel(f.api,f.options);
  model.update({name:'PENTATONIC',mask:0x295});
  assert.equal(model.select(1,0).status,'draft-pending');assert.equal(model.snapshot().kind,0);
  assert.equal(model.apply().ok,true);assert.equal(f.changes(),1);
  assert.deepEqual(f.calls[0],['set',0,0,{used:true,name:'PENTATONIC',mask:0x295}]);
  model.update({name:'UNSAVED',mask:1});model.cancel();
  assert.equal(model.snapshot().draft.name,'PENTATONIC');assert.equal(model.snapshot().dirty,false);
  assert.equal(model.select(1,23).ok,true);assert.equal(model.snapshot().slot,23);
  for(const [kind,slot] of [[-1,0],[3,0],[0,8],[1,24],[2,3],[0,.5]])
    assert.equal(model.select(kind,slot).ok,false);
  assert.equal(model.update({channel:2}).ok,false);assert.equal(f.calls.length,1);
});

test('Copy current is an unsaved C-provided draft and retains every inactive note slot',()=>{
  const f=fixture(),model=createLibraryLabModel(f.api,f.options);
  model.select(1,7);assert.equal(model.capture().ok,true);
  assert.deepEqual(model.snapshot().draft,{used:true,name:'COPIED CHORD',root:0,notes:[0,12,7,99,0,64,127],mask:0x45});
  assert.equal(model.snapshot().dirty,true);assert.equal(f.records[1][7].used,false);
  assert.deepEqual(f.calls,[['capture',1,7]]);assert.equal(f.changes(),0);
  model.apply();assert.equal(f.records[1][7].notes[3],99);assert.equal(f.records[1][7].root,0);
  assert.equal(f.changes(),1);
});

test('C invalid, busy and in-use results preserve the exact draft and saved state',()=>{
  const f=fixture(),model=createLibraryLabModel(f.api,f.options);
  model.update({name:'DRAFT',mask:0x295});const draft=model.snapshot().draft;
  for(const [code,status,notice] of [[1,'invalid',/Check the name/],[9,'in-use',/In use/],[10,'busy',/release pads/]]) {
    f.reject(code,status);assert.equal(model.apply().status,status);
    assert.deepEqual(model.snapshot().draft,draft);assert.equal(model.snapshot().dirty,true);
    assert.equal(f.records[0][0].used,false);assert.match(model.snapshot().notice,notice);
  }
  assert.equal(f.changes(),0);assert.equal(f.errors.length,3);assert.ok(f.errors.every(error=>error instanceof Error));
});

test('deleting a saved record requires explicit inline confirmation and allows safe cancellation',()=>{
  const f=fixture();f.records[1][0]={used:true,name:'BASS',root:0,notes:[0,0,0,0,0,0,0],mask:1};
  const model=createLibraryLabModel(f.api,f.options);model.select(1,0);
  assert.equal(model.deleteConfirmed().ok,false);assert.deepEqual(f.calls,[]);
  assert.equal(model.requestDelete().ok,true);assert.equal(model.snapshot().confirmDelete,true);
  assert.deepEqual(f.calls,[]);model.cancelDelete();assert.equal(model.snapshot().confirmDelete,false);
  model.requestDelete();f.reject(9,'in-use');assert.equal(model.deleteConfirmed().status,'in-use');
  assert.equal(model.snapshot().current.used,true);assert.equal(model.snapshot().confirmDelete,true);
  assert.equal(model.deleteConfirmed().ok,true);assert.equal(model.snapshot().current.used,false);
  assert.equal(f.changes(),1);assert.equal(model.snapshot().confirmDelete,false);
});

test('THYL import and export are explicit, omit unsaved edits and retain drafts on rejection',()=>{
  const f=fixture(),model=createLibraryLabModel(f.api,f.options);
  model.update({name:'PENDING'});assert.deepEqual(model.exportFile(),f.file);
  assert.match(model.snapshot().notice,/unapplied drafts are excluded/);
  assert.equal(model.importFile(f.file).status,'draft-pending');assert.equal(f.calls.length,1);
  model.cancel();f.reject(10,'busy');assert.equal(model.importFile(f.file).status,'busy');
  assert.equal(f.records[0][0].used,false);assert.equal(f.changes(),0);
  assert.equal(model.importFile(f.file).ok,true);assert.equal(model.snapshot().draft.name,'IMPORTED');
  assert.equal(f.changes(),1);assert.equal(model.openEditor().ok,true);assert.equal(f.changes(),2);
  assert.deepEqual(f.calls.at(-1),['open',0,0]);
});

class FakeElement {
  constructor(document,tag){this.ownerDocument=document;this.tagName=tag.toUpperCase();this.children=[];
    this.dataset={};this.attributes={};this.listeners=new Map();this.value='';this.checked=false;
    this.disabled=false;this.hidden=false;this.textContent='';this.className='';this.parentNode=null;}
  setAttribute(key,value){this.attributes[key]=value;}
  append(...children){for(const child of children){child.parentNode=this;this.children.push(child);}}
  replaceChildren(...children){this.children=[];this.append(...children);}
  addEventListener(event,handler){if(!this.listeners.has(event))this.listeners.set(event,new Set());this.listeners.get(event).add(handler);}
  removeEventListener(event,handler){this.listeners.get(event)?.delete(handler);}
  dispatch(event){for(const handler of this.listeners.get(event)??[])handler({target:this});}
  click(){if(this.disabled)return;this.ownerDocument.activeElement=this;this.dispatch('click');}
  remove(){if(this.parentNode)this.parentNode.children=this.parentNode.children.filter(child=>child!==this);}
}
function domFixture() {
  const f=fixture(),document={activeElement:null,createElement(tag){return new FakeElement(this,tag);}};
  const host=document.createElement('section'),downloads=[];
  const panel=initializeLibraryLab(host,{firmware:{library:f.api}},
    {...f.options,download:(bytes,name)=>downloads.push({bytes:bytes.slice(),name})});
  const all=()=>{const nodes=[];const walk=node=>{nodes.push(node);node.children.forEach(walk);};walk(host);return nodes;};
  const find=(key,value)=>all().find(node=>node.dataset[key]===String(value));
  return {...f,document,host,panel,downloads,find,all};
}
const flush=()=>new Promise(resolve=>setImmediate(resolve));

test('panel exposes bounded named slots, fixed tonic, seven note slots and explicit draft buttons',()=>{
  const f=domFixture();
  assert.deepEqual(f.calls,[]);assert.equal(f.find('libraryField','slot').children.length,8);
  assert.equal(f.find('libraryDegree',0).disabled,true);
  assert.equal(f.find('libraryDegree',0).checked,true);
  assert.equal(f.all().filter(node=>Object.hasOwn(node.dataset,'libraryTone')).length,7);
  const kind=f.find('libraryField','kind');kind.value='1';kind.dispatch('change');
  assert.equal(f.find('libraryField','slot').children.length,24);
  const name=f.find('libraryField','name');f.document.activeElement=name;name.value='<BASS>';name.dispatch('input');
  assert.equal(name.value,'<BASS>');assert.equal(f.calls.length,0);
  f.find('libraryAction','apply').click();assert.equal(f.records[1][0].name,'<BASS>');
  assert.equal(f.find('libraryField','slot').children[0].textContent,'01 · <BASS>');
  f.find('libraryAction','delete').click();assert.equal(f.calls.filter(call=>call[0]==='delete').length,0);
  assert.equal(f.panel.model.snapshot().confirmDelete,true);
  f.find('libraryAction','cancel-delete').click();assert.equal(f.panel.model.snapshot().confirmDelete,false);
});

test('panel preserves inactive tone values and renders only used chords plus Empty in set references',()=>{
  const f=domFixture(),kind=f.find('libraryField','kind');kind.value='1';kind.dispatch('change');
  const pitch=f.find('libraryTone',6);pitch.value='127';pitch.dispatch('input');
  f.find('libraryAction','apply').click();assert.equal(f.records[1][0].notes[6],127);
  assert.equal(f.records[1][0].mask,7,'Editing an inactive value does not enable its slot.');
  kind.value='2';kind.dispatch('change');
  assert.equal(f.find('libraryField','slot').children.length,3);
  const reference=f.find('libraryReference',0);
  assert.deepEqual(reference.children.map(option=>option.value),['255','0']);
  reference.value='0';reference.dispatch('change');f.find('libraryAction','apply').click();
  assert.deepEqual(f.records[2][0].chords,[0,255,255,255,255,255]);
});

test('panel local file controls dispatch exact bytes and disposal removes listeners',async()=>{
  const f=domFixture();f.find('libraryAction','export').click();await flush();
  assert.equal(f.downloads.length,1);assert.deepEqual(f.downloads[0],{bytes:f.file,name:'THY666-library.thyl'});
  const input=f.find('libraryField','import');
  input.files=[{size:756,arrayBuffer:async()=>f.file.slice().buffer}];input.value='selected.thyl';input.dispatch('change');await flush();
  assert.equal(f.records[0][0].name,'IMPORTED');assert.equal(input.value,'');
  const oldButton=f.find('libraryAction','capture'),count=f.calls.length;
  f.panel.dispose();oldButton.click();f.panel.refresh();
  assert.equal(f.host.children.length,0);assert.equal(f.calls.length,count);
});

test('panel rejects a differently sized local file before allocating or reading it',async()=>{
  const f=domFixture(),input=f.find('libraryField','import');let reads=0;
  input.files=[{size:100000000,arrayBuffer:async()=>{reads++;return new ArrayBuffer(100000000);}}];
  input.dispatch('change');await flush();
  assert.equal(reads,0);assert.deepEqual(f.calls,[]);
  assert.match(f.find('libraryReadout','notice').textContent,/exactly 756 bytes/);
});

test('a disposed panel or superseded file selection cannot apply an older pending import',async()=>{
  const f=domFixture(),input=f.find('libraryField','import');let resolveFirst,resolveSecond;
  input.files=[{size:756,arrayBuffer:()=>new Promise(resolve=>{resolveFirst=resolve;})}];input.dispatch('change');
  input.files=[{size:756,arrayBuffer:()=>new Promise(resolve=>{resolveSecond=resolve;})}];input.dispatch('change');
  const second=f.file.slice();second[20]=22;resolveSecond(second.buffer);await flush();
  const first=f.file.slice();first[20]=11;resolveFirst(first.buffer);await flush();
  assert.equal(f.calls.filter(call=>call[0]==='import').length,1);
  assert.equal(f.calls.at(-1)[1][20],22);
  let resolveDisposed;
  input.files=[{size:756,arrayBuffer:()=>new Promise(resolve=>{resolveDisposed=resolve;})}];input.dispatch('change');
  f.panel.dispose();resolveDisposed(f.file.slice().buffer);await flush();
  assert.equal(f.calls.filter(call=>call[0]==='import').length,1);
});
