import test from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
execFileSync(process.platform==='win32'?'python':'python3',
  ['tools/build_site.py','--output','build/site-navigation-check'],{stdio:'pipe'});
const {initializeSiteNavigation}=await import('../../build/site-navigation-check/site-navigation.js');

class Element {
  constructor(doc,tag){
    this.ownerDocument=doc;this.tagName=tag;this.children=[];this.attributes={};this.events=new Map();this.hidden=false;
    this.classes=new Set();this.classList={add:n=>this.classes.add(n),remove:n=>this.classes.delete(n)};
  }
  append(...nodes){for(const node of nodes){node.remove();node.parent=this;this.children.push(node);}}
  remove(){if(this.parent)this.parent.children.splice(this.parent.children.indexOf(this),1);this.parent=null;}
  setAttribute(name,value){this.attributes[name]=String(value);}getAttribute(name){return this.attributes[name];}
  addEventListener(name,fn){this.events.set(name,fn);}removeEventListener(name){this.events.delete(name);}
  emit(name,event={}){this.events.get(name)?.({target:this,...event});}
  querySelector(selector){return this.children.find(node=>node.tagName===selector);}
  closest(selector){return this.tagName==='a'&&selector==='a[href]'?this:null;}
  focus(){this.ownerDocument.activeElement=this;}
  showModal(){this.open=true;}close(){this.open=false;this.emit('close');}
  getBoundingClientRect(){return {left:16,top:16,right:360,bottom:360};}
}
function fixture(matches=true){
  const doc={createElement:tag=>new Element(doc,tag),createElementNS:(_,tag)=>new Element(doc,tag)};
  const body=doc.body=new Element(doc,'body'),header=new Element(doc,'header'),nav=new Element(doc,'nav');
  for(const label of ['Install','Client','User guide','Source']){const a=new Element(doc,'a');a.textContent=label;nav.append(a);}
  body.append(header);header.append(nav);doc.querySelector=()=>header;
  const media={matches,events:new Map(),addEventListener(name,fn){this.events.set(name,fn);},removeEventListener(name){this.events.delete(name);},resize(next){this.matches=next;this.events.get('change')?.();}};
  const controller=initializeSiteNavigation({document:doc,media});
  const panel=body.children.find(node=>node.tagName==='dialog'),toggle=header.children.find(node=>node.tagName==='button');
  return {doc,body,header,nav,media,controller,panel,toggle,dismiss:panel.children[0].children[1]};
}

test('mobile navigation retains each existing link and uses shared menu/close icon controls',()=>{
  const f=fixture();
  assert.equal(f.nav.parent,f.panel);assert.equal(f.toggle.hidden,false);
  assert.equal(f.nav.children.length,4);assert.equal(f.toggle.getAttribute('aria-expanded'),'false');
  assert.equal(f.toggle.getAttribute('aria-controls'),f.panel.id);
  assert.equal(f.toggle.className,'hud-icon-button');
  assert.equal(f.toggle.children[0].getAttribute('data-ui-icon'),'menu');
  assert.equal(f.toggle.children[0].children.length,3);
  assert.equal(f.dismiss.children[0].getAttribute('data-ui-icon'),'close');
  assert.equal(f.toggle.children[1].className,'hud-icon-corners');
  f.toggle.emit('click');
  assert.equal(f.panel.open,true);assert.equal(f.toggle.getAttribute('aria-expanded'),'true');
  assert.equal(f.doc.activeElement,f.dismiss);assert.ok(f.body.classes.has('site-menu-open'));
  // A native Escape/cancel closure has the same state/focus cleanup as the close control.
  f.panel.close();
  assert.equal(f.toggle.getAttribute('aria-expanded'),'false');assert.equal(f.doc.activeElement,f.toggle);
  assert.ok(!f.body.classes.has('site-menu-open'));
  f.toggle.emit('click');f.dismiss.emit('click');assert.equal(f.panel.open,false);
  f.controller.destroy();
});

test('backdrop/link dismissal and resizing never leave duplicate links or a locked page',()=>{
  const f=fixture(),links=[...f.nav.children];
  f.toggle.emit('click');f.panel.emit('click',{clientX:30,clientY:30});
  assert.equal(f.panel.open,true); // Padding inside the panel is not the backdrop.
  f.panel.emit('click',{clientX:5,clientY:30});assert.equal(f.panel.open,false);
  f.toggle.emit('click');f.nav.emit('click',{target:links[0]});assert.equal(f.panel.open,false);
  f.toggle.emit('click');f.media.resize(false);
  assert.equal(f.panel.open,false);assert.equal(f.toggle.hidden,true);assert.equal(f.nav.parent,f.header);
  assert.ok(!f.body.classes.has('site-menu-open'));assert.deepEqual(f.nav.children,links);
  f.media.resize(true);assert.equal(f.nav.parent,f.panel);assert.equal(f.toggle.hidden,false);
  f.controller.destroy();assert.equal(f.nav.parent,f.header);assert.deepEqual(f.nav.children,links);
  assert.equal(f.media.events.size,0);assert.equal(f.panel.parent,null);assert.equal(f.toggle.parent,null);
});

test('desktop links remain inline and unsupported browsers retain ordinary navigation',()=>{
  const f=fixture(false);assert.equal(f.nav.parent,f.header);assert.equal(f.toggle.hidden,true);
  f.toggle.emit('click');assert.notEqual(f.panel.open,true);f.controller.destroy();
  const nav={},header={querySelector:()=>nav};
  const doc={querySelector:()=>header,createElement:()=>({})};
  assert.equal(initializeSiteNavigation({document:doc,media:{matches:true}}),null);
  assert.equal(initializeSiteNavigation({document:{},media:{matches:true}}),null);
});
