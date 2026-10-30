import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {runInNewContext} from 'node:vm';
import {initializeUIComponents} from '../../web/ui-system.js';
import {initializeAppSettings} from '../../web/app-settings.js';

const source=await readFile(new URL('../../web/app.js',import.meta.url),'utf8');
const styles=await readFile(new URL('../../web/companion-theme.css',import.meta.url),'utf8');

test('the actual archive-free Client initializer mounts every pad for snapshot RGB styles without inventing colors',async()=>{
  // Execute the real public initializer. Its dependencies are inert mounting
  // stubs; no archive, musical engine, USB connection or browser is involved.
  const begin=source.indexOf('async function initializeConnectedCompanion() {');
  const end=source.indexOf('\nasync function initialize() {',begin);
  assert.ok(begin>=0&&end>begin);
  const clusters=new Map(['#chord-pads','#melody-pads','#library-lab'].map(id=>[id,{children:[],append(node){this.children.push(node);}}]));
  const document={documentElement:{dataset:{}},createElement(tag){return {
    tagName:tag.toUpperCase(),dataset:{},style:{},attributes:{},setAttribute(name,value){this.attributes[name]=value;},
  };}};
  const model=()=>({refresh(){}});
  let ready=0,mounted=0,uiStarted=0;
  const context={document,$:id=>clusters.get(id),desktopBindingsPromise:Promise.resolve(null),SCALE_CATALOG:[],
    createCore:async()=>({state:{navigationAnimations(){},boot:{skip(){}}}}),
    initializeFirmwareStudio:model,initializeEngineStudio:model,initializeRhythmLab:model,
    initializeArpLab:model,initializeSequencerLab:model,initializeMpeLab:model,initializeLibraryLab:model,
    initializeCompanionShell:()=>{mounted++;return {};},
    initializeUIComponents:()=>{assert.equal(mounted,1,'Decorate the completed Client shell.');uiStarted++;},
    startup:{ready(){assert.equal(uiStarted,1,'The normal Client must start shared decoration before it is revealed.');ready++;}},window:{}};
  const initialize=runInNewContext(source.slice(begin,end)+'\ninitializeConnectedCompanion;',context);
  await initialize();
  assert.equal(mounted,1);assert.equal(uiStarted,1);assert.equal(ready,1);
  assert.match(styles,/\.pad\[data-light\]\s*\{/,'the actual theme gates musical RGB backgrounds on this mount marker');
  assert.match(styles,/background-color:\s*rgb\(var\(--note-light\)/);
  for(const [side,id] of ['#chord-pads','#melody-pads'].entries()){
    const pads=clusters.get(id).children;assert.equal(pads.length,48);
    for(const [offset,pad] of pads.entries()){
      assert.equal(pad.className,'pad');assert.equal(pad.dataset.pad,side*48+offset);
      assert.ok(Object.hasOwn(pad.dataset,'light'),`logical pad ${side*48+offset} must match .pad[data-light]`);
      assert.equal(pad.style.gridColumn,String(Math.floor(offset/4)+1));
      assert.equal(pad.style.gridRow,String(offset%4+1));
      assert.equal(pad.style['--note-light'],undefined,'only a valid snapshot supplies hue');
      assert.equal(pad.style['--note-level'],undefined,'unknown brightness must not be guessed');
    }
  }
});

// A bounded DOM/observer fixture exercises the production decorator through the
// real public startup path. It has no port, updater, MIDI or network API.
function interfaceFixture(){
  const pending=[];let changes=null,resize=null;
  class Node{
    constructor(tag,doc){this.tagName=tag.toUpperCase();this.ownerDocument=doc;this.nodeType=1;this.children=[];this.parentNode=null;this.className='';this.dataset={};this.style={};this.attributes={};this.listeners=new Map();this._text='';this.hidden=false;this.disabled=false;}
    get classList(){return {contains:name=>this.className.split(/\s+/).includes(name),add:(...names)=>{this.className=[...new Set([...this.className.split(/\s+/).filter(Boolean),...names])].join(' ');}};}
    set textContent(value){const removed=[...this.children];this._text=String(value);this.children=[];for(const node of removed)node.parentNode=null;this.mutate([],removed);}
    get textContent(){return this._text+this.children.map(node=>node.textContent).join('');}
    setAttribute(name,value){this.attributes[name]=String(value);if(name==='class')this.className=String(value);}
    getAttribute(name){return this.attributes[name]??null;}
    get isConnected(){return this===this.ownerDocument.body||Boolean(this.ownerDocument.body?.contains(this));}
    append(...nodes){for(const node of nodes){node.remove();node.parentNode=this;this.children.push(node);}this.mutate(nodes,[]);}
    remove(){if(!this.parentNode)return;const parent=this.parentNode;parent.children=parent.children.filter(node=>node!==this);this.parentNode=null;parent.mutate([],[this]);}
    mutate(addedNodes,removedNodes){if(changes?.active&&changes.root.contains(this))pending.push({type:'childList',target:this,addedNodes,removedNodes});}
    contains(node){return node===this||this.children.some(child=>child.contains(node));}
    matches(selector){return selector.split(',').some(part=>{part=part.trim();return part.startsWith('.')?this.classList.contains(part.slice(1)):this.tagName===part.toUpperCase();});}
    querySelectorAll(selector){return this.children.flatMap(node=>[...(node.matches(selector)?[node]:[]),...node.querySelectorAll(selector)]);}
    querySelector(selector){return this.querySelectorAll(selector)[0]||null;}
    closest(selector){for(let node=this;node;node=node.parentNode)if(node.matches(selector))return node;return null;}
    getBoundingClientRect(){return {width:this.hidden?0:120,height:this.hidden?0:44};}
    addEventListener(type,run){if(!this.listeners.has(type))this.listeners.set(type,new Set());this.listeners.get(type).add(run);}
    removeEventListener(type,run){this.listeners.get(type)?.delete(run);}
    focus(){this.ownerDocument.activeElement=this;}
  }
  const view={listeners:new Map(),addEventListener:Node.prototype.addEventListener,removeEventListener:Node.prototype.removeEventListener};
  const doc={defaultView:view,hidden:false,listeners:new Map(),addEventListener:Node.prototype.addEventListener,removeEventListener:Node.prototype.removeEventListener};
  doc.createElement=tag=>new Node(tag,doc);doc.createElementNS=(_,tag)=>new Node(tag,doc);
  doc.documentElement=doc.createElement('html');doc.body=doc.createElement('body');doc.documentElement.append(doc.body);doc.activeElement=doc.body;
  view.MutationObserver=class{constructor(run){this.run=run;this.active=false;changes=this;}observe(root){this.root=root;this.active=true;}disconnect(){this.active=false;}};
  view.ResizeObserver=class{constructor(run){this.run=run;this.nodes=new Set();resize=this;}observe(node){this.nodes.add(node);this.run([{target:node}]);}unobserve(node){this.nodes.delete(node);}disconnect(){this.nodes.clear();}};
  const flush=()=>{let passes=0;while(pending.length){assert.ok(++passes<20,'Decoration settles without a mutation loop.');const batch=pending.splice(0);if(changes.active)changes.run(batch);}};
  return {doc,view,flush,get changes(){return changes;},get resize(){return resize;}};
}

test('public Client startup decorates disconnected controls and keeps later dialogs/pages live without restarting observers',async()=>{
  const f=interfaceFixture(),doc=f.doc,clusters=new Map();
  for(const id of ['#chord-pads','#melody-pads','#library-lab']){const mount=doc.createElement('div');doc.body.append(mount);clusters.set(id,mount);}
  const model=()=>({refresh(){}});let settings,decorator,starts=0;
  const context={document:doc,$:id=>clusters.get(id),desktopBindingsPromise:Promise.resolve(null),SCALE_CATALOG:[],
    createCore:async()=>({state:{navigationAnimations(){},boot:{skip(){}}}}),
    initializeFirmwareStudio:model,initializeEngineStudio:model,initializeRhythmLab:model,
    initializeArpLab:model,initializeSequencerLab:model,initializeMpeLab:model,initializeLibraryLab:model,
    initializeCompanionShell(){
      const navigation=doc.createElement('button');navigation.className='hud-choice hud-choice--nav';navigation.textContent='Board';doc.body.append(navigation);
      settings=initializeAppSettings({mount:doc.body,storage:{getItem(){return null;},setItem(){}}});return {};
    },
    initializeUIComponents(){starts++;return decorator=initializeUIComponents({root:doc.body});},
    startup:{ready(){assert.equal(starts,1,'Shared controls are initialized before the public app is revealed.');}},window:{}};
  const begin=source.indexOf('async function initializeConnectedCompanion() {'),end=source.indexOf('\nasync function initialize() {',begin);
  await runInNewContext(source.slice(begin,end)+'\ninitializeConnectedCompanion;',context)();f.flush();
  const outlined=node=>{assert.ok(node.classList.contains('hud-traced'));assert.equal(node.querySelectorAll('.hud-outline').length,1);assert.equal(node.dataset.traceReady,'true');};
  outlined(doc.body.querySelector('.hud-choice'));outlined(doc.body.querySelector('.app-settings-toggle'));
  assert.ok(clusters.get('#chord-pads').children.every(node=>!node.classList.contains('hud-traced')),'Physical pads stay outside interface decoration.');
  settings.open();f.flush();const done=doc.body.querySelector('.app-settings-dialog').querySelector('.hud-action');outlined(done);
  done.textContent='Done';f.flush();outlined(done);
  settings.close();f.flush();assert.ok(!f.resize.nodes.has(done),'Detached dialog controls release their measurements.');
  const pageAction=doc.createElement('button');pageAction.className='hud-action';pageAction.textContent='Read report';doc.body.append(pageAction);f.flush();outlined(pageAction);assert.equal(starts,1);
  settings.open();f.flush();outlined(doc.body.querySelector('.app-settings-dialog').querySelector('.hud-action'));settings.close();f.flush();
  settings.destroy();decorator.destroy();assert.equal(f.resize.nodes.size,0);assert.equal(f.changes.active,false);
});
