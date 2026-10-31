import test from 'node:test';
import assert from 'node:assert/strict';
import {initializeProfilePanel,profileExport,PROFILE_METRICS} from '../../web/profile-panel.js';

class Element{
  constructor(document,tag){this.ownerDocument=document;this.tagName=tag;this.children=[];this.listeners={};this.attributes={};this.style={setProperty:(k,v)=>this.style[k]=v};this.className='';this.classList={add:name=>this.className+=' '+name};this.disabled=false;this.hidden=false;this.open=false;this.value='';this._text='';}
  append(...nodes){for(const node of nodes){node.parentNode=this;this.children.push(node);}}
  remove(){if(this.parentNode)this.parentNode.children=this.parentNode.children.filter(node=>node!==this);}
  replaceChildren(...nodes){this.children=[];this.append(...nodes);}
  set textContent(value){this._text=String(value);this.children=[];}get textContent(){return this._text+this.children.map(node=>node.textContent).join('');}
  setAttribute(key,value){this.attributes[key]=String(value);}
  addEventListener(type,callback){this.listeners[type]=callback;}removeEventListener(type){delete this.listeners[type];}
  click(){if(!this.disabled)this.listeners.click?.();}
}
const flush=async()=>{for(let n=0;n<5;n++)await new Promise(resolve=>setImmediate(resolve));};
function fixture(t,{connected=true}={}){
  const doc={createElement(tag){return new Element(this,tag);}};doc.body=doc.createElement('body');
  const mount=doc.createElement('div');doc.body.append(mount);let time=0;
  const calls=[],saved=[];
  const current={version:1,id:0,active:false,durationMs:0,elapsedMs:0,hz:0,invalid:0};
  const metrics=PROFILE_METRICS.map((_,index)=>({index,unit:[15,16].includes(index)?'ms':'us',count:1,min:2,max:2,mean:2,p50_upper:3,p95_upper:3,p99_upper:3,bins:[0,0,1,...Array(17).fill(0)]}));
  const client={connected,identity:{uid:'private-device-identity',build:'ab'.repeat(8),schema:'cd'.repeat(32)},
    async getProfileCapabilities(){calls.push('caps');return{version:1,supported:true,metrics:19,bins:20,maxSeconds:60,queueUnit:'ms'};},
    async getProfileStatus(){calls.push('status');return{...current};},
    async startProfile(seconds){calls.push(['start',seconds]);Object.assign(current,{id:current.id+1,active:true,durationMs:seconds*1000,elapsedMs:0,hz:168000000});return{...current};},
    async stopProfile(id){calls.push(['stop',id]);current.active=false;return{...current};},
    async readProfileCapture(id){calls.push(['capture',id]);return{version:1,status:{...current},metrics:structuredClone(metrics)};},
  };
  const panel=initializeProfilePanel({mount,client,now:()=>time,saveFile:async(bytes,name)=>saved.push({bytes,name})});
  t.after(()=>panel.destroy());
  const walk=node=>[node,...node.children.flatMap(walk)],find=predicate=>walk(mount).find(predicate),button=label=>find(node=>node.tagName==='button'&&node.textContent===label);
  const details=mount.children[0];
  return{panel,client,calls,current,metrics,saved,details,find,button,open(){details.open=true;panel.refresh({visible:true});},advance(ms){time+=ms;},refresh(options){panel.refresh(options);}};
}

test('profiling is inert until opened and uses only caller-driven one-second device status polling',async t=>{
  const f=fixture(t);f.refresh({visible:true});await f.panel.poll();assert.deepEqual(f.calls,[]);
  f.open();await f.panel.poll();assert.deepEqual(f.calls,['caps','status']);
  f.button('Start capture').click();await flush();assert.equal(f.panel.state.status.active,true);assert.deepEqual(f.calls.at(-1),['start',30]);
  await f.panel.poll();f.advance(999);await f.panel.poll();assert.equal(f.calls.filter(c=>c==='status').length,1);
  f.current.elapsedMs=1500;f.advance(1);await f.panel.poll();
  assert.equal(f.find(n=>n.tagName==='progress').value,1500);assert.equal(f.calls.filter(c=>Array.isArray(c)&&c[0]==='capture').length,0);
  f.refresh({visible:false});f.advance(1000);await f.panel.poll();assert.equal(f.panel.state.status.active,true);assert.equal(f.calls.filter(c=>c==='status').length,2);
});

test('a capture survives navigation and disconnect, then reconnect reads device state without another start',async t=>{
  const f=fixture(t);f.open();await f.panel.poll();f.button('Start capture').click();await flush();
  f.client.connected=false;f.refresh({visible:false});await f.panel.poll();assert.equal(f.panel.state.status.active,true);
  assert.match(f.find(n=>n.className==='profile-message').textContent,/may still be running/);
  f.current.active=false;f.current.elapsedMs=30000;f.client.connected=true;f.refresh({visible:true});await f.panel.poll();
  assert.equal(f.panel.state.capture.metrics.length,19);assert.equal(f.calls.filter(c=>Array.isArray(c)&&c[0]==='start').length,1);
  assert.equal(f.calls.filter(c=>Array.isArray(c)&&c[0]==='capture').length,1);
  f.advance(1000);await f.panel.poll();assert.equal(f.calls.filter(c=>Array.isArray(c)&&c[0]==='capture').length,1);
  f.client.connected=false;f.refresh();f.button('Export JSON').click();await flush();
  const text=new TextDecoder().decode(f.saved[0].bytes);assert.match(text,/thy666.timing-profile/);assert.doesNotMatch(text,/private-device-identity|"uid"|"path"|"settings"\s*:/i);
});

test('an unsupported board disables captures without starting one',async t=>{
  const f=fixture(t);f.client.getProfileCapabilities=async()=>({supported:false});f.open();await f.panel.poll();
  assert.equal(f.button('Start capture').disabled,true);assert.match(f.find(n=>n.className==='profile-message').textContent,/Update firmware/);
  f.advance(1000);await f.panel.poll();assert.deepEqual(f.calls,[]);
});

test('failed result reads are retried only by the explicit Read status action',async t=>{
  const f=fixture(t);Object.assign(f.current,{id:4,durationMs:15000,elapsedMs:15000,hz:168000000});
  const read=f.client.readProfileCapture;let reject=true;
  f.client.readProfileCapture=async id=>{if(reject){f.calls.push(['capture',id]);throw new Error('Synthetic metric read failed.');}return read(id);};
  f.open();await f.panel.poll();assert.equal(f.panel.state.capture,null);
  f.advance(1000);await f.panel.poll();assert.equal(f.calls.filter(c=>Array.isArray(c)&&c[0]==='capture').length,1);
  assert.match(f.find(n=>n.className==='profile-message').textContent,/Read status to retry/);
  reject=false;f.button('Read status').click();await flush();
  assert.equal(f.panel.state.capture.status.id,4);assert.equal(f.calls.filter(c=>Array.isArray(c)&&c[0]==='capture').length,2);
});

test('read-only connection preserves result inspection while capture controls remain disabled',async t=>{
  const f=fixture(t);f.client.compatible=false;Object.assign(f.current,{id:4,durationMs:15000,elapsedMs:15000,hz:168000000});
  f.open();await f.panel.poll();assert.equal(f.panel.state.capture.status.id,4);
  assert.equal(f.button('Start new capture').disabled,true);assert.equal(f.button('Stop').disabled,true);assert.equal(f.button('Read status').disabled,false);
});

test('uncertain stop is never retried automatically and read status recovers frozen results',async t=>{
  const f=fixture(t);f.open();await f.panel.poll();f.button('Start capture').click();await flush();
  f.client.stopProfile=async id=>{f.calls.push(['stop',id]);f.current.active=false;const error=new Error('connection lost');error.profileOutcomeUnknown=true;throw error;};
  f.button('Stop').click();await flush();assert.equal(f.panel.state.uncertain,true);
  assert.match(f.find(n=>n.className==='profile-message').textContent,/Stop not confirmed/);
  f.button('Read status').click();await flush();assert.equal(f.panel.state.uncertain,false);assert.equal(f.panel.state.capture.status.id,1);
  assert.equal(f.calls.filter(c=>Array.isArray(c)&&c[0]==='stop').length,1);
});

test('stale status completion cannot attach another device capture after reconnect',async t=>{
  const f=fixture(t);let finish;f.client.getProfileStatus=()=>new Promise(resolve=>finish=resolve);
  f.open();const pending=f.panel.poll();await flush();f.client.connected=false;f.refresh();
  f.client.identity={...f.client.identity,uid:'another-private-device'};f.client.connected=true;f.refresh();
  finish({version:1,id:99,active:true,durationMs:30000,elapsedMs:2000,hz:168000000});await pending;
  assert.equal(f.panel.state.status,null);assert.equal(f.panel.state.capture,null);
});

test('a pending start disables repetition and disconnect keeps its outcome unknown until a read',async t=>{
  const f=fixture(t);f.open();await f.panel.poll();let finish;
  f.client.startProfile=seconds=>{f.calls.push(['start',seconds]);return new Promise(resolve=>finish=resolve);};
  const start=f.button('Start capture');start.click();start.click();await flush();assert.equal(start.disabled,true);
  f.client.connected=false;f.refresh();assert.equal(f.panel.state.uncertain,true);
  finish({version:1,id:5,active:true,durationMs:30000,elapsedMs:0,hz:168000000});await flush();
  assert.equal(f.panel.state.status.id,0);assert.equal(f.calls.filter(c=>Array.isArray(c)&&c[0]==='start').length,1);
  Object.assign(f.current,{id:5,active:true,durationMs:30000,hz:168000000});f.client.connected=true;f.refresh({visible:true});await f.panel.poll();
  assert.equal(f.panel.state.status.id,5);assert.equal(f.panel.state.uncertain,false);
});

test('export keeps only the explicit numeric context and uses open-ended overflow labels',async t=>{
  const f=fixture(t);f.open();await f.panel.poll();Object.assign(f.current,{id:3,hz:168000000,durationMs:15000,elapsedMs:15000,uid:'secret',path:'private'});
  f.metrics[0].p95_upper=0xffffffff;f.advance(1000);await f.panel.poll();
  assert.match(f.details.textContent,/≥262,144 us/);
  const data=profileExport(f.panel.state.capture,{...f.client.identity,path:'private'});
  assert.equal(data.firmware,'ab'.repeat(8));assert.equal(data.schema,'cd'.repeat(32));assert.equal(data.status.uid,undefined);assert.equal(data.status.path,undefined);
  assert.throws(()=>profileExport(null),/completed/);
});
