import test from 'node:test';
import assert from 'node:assert/strict';
import {desktopFrameGeometry,initializeDesktopChrome} from '../../web/desktop-chrome.js';

class Element{
  constructor(doc,tag){this.ownerDocument=doc;this.tagName=tag;this.className='';this.dataset={};this.attributes={};this.children=[];this.parentNode=null;this.listeners=new Map();this.hidden=false;this._text='';
    const properties=new Map(),priorities=new Map();this.style={setProperty(name,value,priority=''){properties.set(name,String(value));priorities.set(name,priority);},getPropertyValue(name){return properties.get(name)||'';},getPropertyPriority(name){return priorities.get(name)||'';},removeProperty(name){const old=properties.get(name)||'';properties.delete(name);priorities.delete(name);return old;}};
    this.classList={add:name=>{this.className=[...new Set([...this.className.split(/\s+/).filter(Boolean),name])].join(' ');},remove:name=>{this.className=this.className.split(/\s+/).filter(value=>value!==name).join(' ');}};}
  set textContent(value){this._text=String(value);this.children=[];}get textContent(){return this._text+this.children.map(node=>node.textContent).join('');}
  set innerHTML(_){throw Error('Window chrome uses safe DOM construction.');}
  setAttribute(name,value){this.attributes[name]=String(value);if(name==='id')this.id=String(value);if(name==='class')this.className=String(value);}getAttribute(name){return this.attributes[name]??null;}
  append(...nodes){for(const node of nodes){node.remove();node.parentNode=this;this.children.push(node);}}
  prepend(...nodes){for(const node of [...nodes].reverse()){node.remove();node.parentNode=this;this.children.unshift(node);}}
  before(node){if(this.parentNode)this.parentNode.insertBefore(node,this);}
  insertBefore(node,sibling){if(!sibling){this.append(node);return;}node.remove();node.parentNode=this;this.children.splice(this.children.indexOf(sibling),0,node);}
  get nextSibling(){return this.parentNode?.children[this.parentNode.children.indexOf(this)+1]||null;}
  remove(){if(this.parentNode)this.parentNode.children=this.parentNode.children.filter(node=>node!==this);this.parentNode=null;}
  matches(selector){return selector.split(',').some(branch=>{const value=branch.trim();return value[0]==='.'?this.className.split(/\s+/).includes(value.slice(1)):value[0]==='#'?this.id===value.slice(1):value===this.tagName;});}
  querySelector(selector){return this.all().slice(1).find(node=>node.matches(selector))||null;}
  all(){return[this,...this.children.flatMap(node=>node.all())];}
  closest(selector){for(let node=this;node;node=node.parentNode)if(node.matches(selector))return node;return null;}
  contains(node){return this.all().includes(node);}
  addEventListener(type,listener){if(!this.listeners.has(type))this.listeners.set(type,[]);this.listeners.get(type).push(listener);}
  removeEventListener(type,listener){this.listeners.set(type,(this.listeners.get(type)||[]).filter(value=>value!==listener));}
}
const flush=()=>new Promise(resolve=>setImmediate(resolve));
function fixture({isMaximized=async()=>false,subscribe}={}){
  const doc={createElement(tag){return new Element(this,tag);},createElementNS(_,tag){return new Element(this,tag);},querySelector(selector){return this.body.querySelector(selector);}};
  doc.documentElement=doc.createElement('html');doc.body=doc.createElement('body');doc.documentElement.append(doc.body);
  const startup=doc.createElement('section');startup.id='companion-startup';const shell=doc.createElement('div');shell.className='app-shell';
  const image=doc.createElement('img');image.className='companion-brand-logo';image.setAttribute('src','./assets/thy666-web-logo.svg');shell.append(image);
  const sentinel=doc.createElement('script');doc.body.append(startup,shell,sentinel);
  const calls=[];let stateListener,removed=0;
  const native={isMaximized:()=>{calls.push(['isMaximized']);return isMaximized();}};
  for(const method of ['minimize','toggleMaximize','beginDrag','beginResize','close'])native[method]=async(...args)=>{calls.push([method,...args]);};
  const runtime={kind:'desktop',window:native,subscribeWindowState:callback=>{stateListener=callback;return subscribe?subscribe(callback):()=>{removed++;};}};
  const fire=(target,type,extra={})=>{const event={target,button:0,isPrimary:true,detail:1,preventDefault(){this.defaultPrevented=true;},...extra};for(let node=target;node;node=node.parentNode)for(const listener of node.listeners.get(type)||[])listener(event);return event;};
  return{doc,shell,startup,sentinel,runtime,native,calls,fire,state:value=>stateListener(value),removed:()=>removed,button:action=>doc.body.all().find(node=>node.dataset.windowAction===action),handles:()=>doc.body.all().filter(node=>node.dataset.desktopResize)};
}

test('browser initialization stays untouched; native chrome wraps only the app and probes window state without device calls',async()=>{
  assert.equal(initializeDesktopChrome({runtime:null,document:null}),null);
  const f=fixture(),chrome=initializeDesktopChrome({runtime:f.runtime,document:f.doc});await flush();
  assert.deepEqual(f.calls,[['isMaximized']]);assert.equal(f.doc.body.querySelector('.desktop-content-viewport').children[0],f.shell);
  assert.equal(f.startup.parentNode,f.doc.body);assert.equal(f.sentinel.parentNode,f.doc.body);assert.equal(f.handles().length,8);
  assert.equal(f.doc.body.querySelector('#desktop-titlebar').getAttribute('aria-label'),'THY666 window');
  for(const action of ['minimize','maximize','close']){assert.equal(f.button(action).type,'button');assert.ok(f.button(action).getAttribute('aria-label'));}
  assert.throws(()=>initializeDesktopChrome({runtime:f.runtime,document:f.doc}),/already mounted/);
  chrome.destroy();assert.deepEqual(f.doc.body.children,[f.startup,f.shell,f.sentinel]);assert.equal(f.removed(),1);chrome.destroy();
});

test('window actions require explicit gestures and titlebar buttons do not start a drag',async()=>{
  const f=fixture(),chrome=initializeDesktopChrome({runtime:f.runtime,document:f.doc});await flush();f.calls.length=0;
  const titlebar=f.doc.body.querySelector('#desktop-titlebar');
  f.fire(f.button('close'),'pointerdown');f.fire(titlebar,'pointerdown',{button:2});f.fire(titlebar,'pointerdown',{isPrimary:false});assert.deepEqual(f.calls,[]);
  f.fire(titlebar,'pointerdown');f.fire(titlebar,'dblclick');await flush();
  assert.deepEqual(f.calls,[['beginDrag'],['toggleMaximize'],['isMaximized']]);f.calls.length=0;
  f.fire(f.button('minimize'),'click');f.fire(f.button('maximize'),'click');f.fire(f.button('close'),'click');await flush();
  assert.deepEqual(f.calls,[['minimize'],['toggleMaximize'],['close'],['isMaximized']]);chrome.destroy();
});

test('all eight resize directions are explicit and maximization updates accessible controls and disables resize handles',async()=>{
  const f=fixture(),chrome=initializeDesktopChrome({runtime:f.runtime,document:f.doc});await flush();f.calls.length=0;
  for(const handle of f.handles())f.fire(handle,'pointerdown');await flush();
  assert.deepEqual(f.calls,[['beginResize','n'],['beginResize','ne'],['beginResize','e'],['beginResize','se'],['beginResize','s'],['beginResize','sw'],['beginResize','w'],['beginResize','nw']]);
  f.state({maximized:true});assert.equal(chrome.maximized,true);assert.ok(f.handles().every(node=>node.hidden));assert.equal(f.button('maximize').getAttribute('aria-label'),'Restore window');
  f.calls.length=0;f.fire(f.handles()[0],'pointerdown');assert.deepEqual(f.calls,[]);
  f.state({maximized:false});assert.ok(f.handles().every(node=>!node.hidden));assert.equal(f.button('maximize').getAttribute('aria-label'),'Maximize window');chrome.destroy();
});

test('late initial queries cannot overwrite window events, and pending subscriptions clean up after destroy',async()=>{
  let resolveQuery,resolveSubscribe,removed=0;
  const f=fixture({isMaximized:()=>new Promise(resolve=>{resolveQuery=resolve;}),subscribe:()=>new Promise(resolve=>{resolveSubscribe=resolve;})});
  const chrome=initializeDesktopChrome({runtime:f.runtime,document:f.doc});f.state({maximized:true});resolveQuery(false);await flush();assert.equal(chrome.maximized,true);
  chrome.destroy();resolveSubscribe(()=>removed++);await flush();assert.equal(removed,1);assert.equal(f.doc.body.querySelector('.desktop-window-chrome'),null);
});

function outlineVertices(path){
  assert.match(path,/^M/i);assert.match(path,/Z$/i);assert.equal((path.match(/M/gi)||[]).length,1,'the background is one closed perimeter, not independently closed decorative strokes');
  assert.doesNotMatch(path,/[HVCSQTA]/i);
  const values=path.match(/-?(?:\d*\.)?\d+(?:e[-+]?\d+)?/gi).map(Number);
  assert.equal(values.length%2,0);
  return Array.from({length:values.length/2},(_,index)=>values.slice(index*2,index*2+2));
}
function clipVertices(clip,width,height){
  assert.match(clip,/^polygon\(/);assert.match(clip,/\)$/);
  return clip.slice(8,-1).split(',').map(pair=>{
    const coordinates=[...pair.matchAll(/(-?(?:\d*\.)?\d+(?:e[-+]?\d+)?)%/gi)].map(match=>Number(match[1]));
    assert.equal(coordinates.length,2,'clip coordinates scale with the same complete viewport as the SVG');
    return[coordinates[0]*width/100,coordinates[1]*height/100];
  });
}
function contains(vertices,x,y){
  let inside=false;
  for(let i=0,j=vertices.length-1;i<vertices.length;j=i++){
    const[a,b]=vertices[i],[c,d]=vertices[j];
    if((b>y)!==(d>y)&&x<(c-a)*(y-b)/(d-b)+a)inside=!inside;
  }
  return inside;
}

test('the filled outline and scalable clip contain the entire interior but exclude every notch and corner cut',()=>{
  const geometry=desktopFrameGeometry(),outline=outlineVertices(geometry.outline);
  assert.equal(geometry.viewBox,'0 0 1440 900');
  const probes=[
    ['top notch',350,5,false],['inside top notch',350,12,true],['top high section',500,5,true],
    ['bottom notch',350,895,false],['inside bottom notch',350,888,true],['bottom low section',500,895,true],
    ['left recess',5,450,false],['inside left recess',11,450,true],['left outer section',5,200,true],
    ['right recess',1435,450,false],['inside right recess',1429,450,true],['right outer section',1435,200,true],
    ['upper left cut',8,8,false],['upper left interior',10,10,true],
    ['upper right cut',1432,8,false],['upper right interior',1430,12,true],
    ['lower left cut',8,892,false],['lower left interior',10,890,true],
    ['lower right cut',1432,892,false],['lower right interior',1430,890,true],
  ];
  for(const[width,height]of[[1440,960],[1368,803],[850,600]]){
    const clip=clipVertices(geometry.clip,width,height);
    for(const[label,x,y,expected]of probes){
      assert.equal(contains(outline,x,y),expected,`${label}: SVG perimeter`);
      assert.equal(contains(clip,x*width/1440,y*height/900),expected,`${label}: content clip at ${width}x${height}`);
    }
    // A single closed silhouette must not leave the transparent strips that
    // previously appeared between the rectangular content and the red frame.
    for(let y=20;y<=880;y+=43)for(let x=11;x<=1429;x+=47)
      assert.ok(contains(clip,x*width/1440,y*height/900),`unbroken interior at ${x},${y} (${width}x${height})`);
  }
});

test('a separate SVG paints the surface behind content; maximize fills the viewport and teardown restores the clip',async()=>{
  const f=fixture(),oldClip='polygon(0 0,100% 0,100% 100%)';
  f.doc.documentElement.style.setProperty('--desktop-frame-clip',oldClip,'important');
  const geometry=desktopFrameGeometry(),chrome=initializeDesktopChrome({runtime:f.runtime,document:f.doc});await flush();
  const background=f.doc.body.querySelector('#desktop-window-background'),frame=f.doc.body.querySelector('#desktop-frame');
  assert.ok(background);assert.equal(background.parentNode,f.doc.body,'the surface is outside the chrome overlay that sits above content');
  assert.equal(background.getAttribute('viewBox'),geometry.viewBox);assert.equal(background.getAttribute('preserveAspectRatio'),'none');
  assert.equal(frame.getAttribute('viewBox'),geometry.viewBox);assert.equal(frame.getAttribute('preserveAspectRatio'),'none');
  const shape=background.querySelector('.desktop-background-shape');assert.equal(shape.getAttribute('d'),geometry.outline);
  assert.equal(frame.querySelector('.desktop-frame-outer').getAttribute('d'),geometry.outer);
  assert.equal(frame.querySelector('.desktop-frame-side').getAttribute('d'),geometry.sides);
  assert.equal(f.doc.documentElement.style.getPropertyValue('--desktop-frame-clip'),geometry.clip);
  f.state({maximized:true});assert.equal(shape.getAttribute('d'),'M0 0H1440V900H0Z');
  f.state({maximized:false});assert.equal(shape.getAttribute('d'),geometry.outline);
  chrome.destroy();assert.equal(f.doc.body.querySelector('#desktop-window-background'),null);
  assert.equal(f.doc.documentElement.style.getPropertyValue('--desktop-frame-clip'),oldClip);
  assert.equal(f.doc.documentElement.style.getPropertyPriority('--desktop-frame-clip'),'important');
  const clean=fixture(),second=initializeDesktopChrome({runtime:clean.runtime,document:clean.doc});await flush();second.destroy();
  assert.equal(clean.doc.documentElement.style.getPropertyValue('--desktop-frame-clip'),'');
});

test('macOS uses left-hand close/minimize/maximize controls and right-hand identity only on that host',async()=>{
  for(const platform of ['macos','windows','linux']){
    const f=fixture();f.runtime.platform=platform;
    const chrome=initializeDesktopChrome({runtime:f.runtime,document:f.doc});await flush();
    const titlebar=f.doc.body.querySelector('#desktop-titlebar');
    const controls=f.doc.body.querySelector('.desktop-window-controls');
    assert.deepEqual(controls.children.map(button=>button.dataset.windowAction),platform==='macos'?['close','minimize','maximize']:['minimize','maximize','close']);
    assert.equal(titlebar.children[platform==='macos'?0:2],controls);
    assert.equal(titlebar.children[platform==='macos'?2:0].className,'desktop-window-identity');
    f.calls.length=0;f.fire(f.button('close'),'pointerdown');assert.deepEqual(f.calls,[]);
    f.fire(f.button('close'),'click');await flush();assert.deepEqual(f.calls,[['close']]);
    chrome.destroy();
  }
});
