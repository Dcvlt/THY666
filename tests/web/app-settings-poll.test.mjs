import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { initializeAppSettings, APP_THEMES, APP_THEME_KEY, APP_APPEARANCES, APP_APPEARANCE_KEY, APP_POLL_KEY, APP_POLL_INTERVALS, DEFAULT_POLL_INTERVAL_MS } from '../../web/app-settings.js';

class Element {
  constructor(doc, tag) { this.ownerDocument=doc;this.tagName=tag;this.children=[];this.parentNode=null;this.dataset={};this.attributes={};this.listeners=new Map();this.className='';this._text='';this.disabled=false; }
  set textContent(value) { this._text=String(value);this.children=[]; }
  get textContent() { return this._text+this.children.map(node=>node.textContent).join(''); }
  set innerHTML(_) { throw Error('Application settings must use text nodes.'); }
  setAttribute(key,value) { this.attributes[key]=String(value); }
  append(...nodes) { for(const node of nodes){node.remove();node.parentNode=this;this.children.push(node);} }
  remove() { if(this.parentNode)this.parentNode.children=this.parentNode.children.filter(node=>node!==this);this.parentNode=null; }
  contains(node) { return node===this||this.children.some(child=>child.contains(node)); }
  matches(selector) { return selector.split(',').some(part=>part.startsWith('.')?this.className.split(/\s+/).includes(part.slice(1)):part.startsWith('[')?this.dataset[part.match(/data-([\w-]+)/)?.[1]?.replace(/-([a-z])/g,(_,letter)=>letter.toUpperCase())]===part.match(/="([^"]*)"/)?.[1]:this.tagName===part); }
  querySelectorAll(selector) { const result=[];for(const node of this.children){if(node.matches(selector))result.push(node);result.push(...node.querySelectorAll(selector));}return result; }
  querySelector(selector) { return this.querySelectorAll(selector)[0]||null; }
  addEventListener(type,listener) { if(!this.listeners.has(type))this.listeners.set(type,[]);this.listeners.get(type).push(listener); }
  removeEventListener(type,listener) { this.listeners.set(type,(this.listeners.get(type)||[]).filter(value=>value!==listener)); }
  focus() { this.ownerDocument.activeElement=this; }
}
function fixture(t,{storage=new Map(),onChange=()=>{}}={}) {
  const doc={activeElement:null,listeners:new Map(),createElement(tag){return new Element(this,tag);},addEventListener:Element.prototype.addEventListener,removeEventListener:Element.prototype.removeEventListener};
  doc.body=doc.createElement('body');
  const adapter=storage instanceof Map?{getItem:key=>storage.get(key)??null,setItem:(key,value)=>storage.set(key,String(value))}:storage;
  const settings=initializeAppSettings({mount:doc.body,storage:adapter,onPollIntervalChange:onChange});
  t.after(()=>settings.destroy());
  const change=(select,value)=>{select.value=String(value);for(const listener of select.listeners.get('change')||[])listener({target:select});};
  const fire=(target,type,extra={})=>{
    const event={target,type,stopped:false,defaultPrevented:false,preventDefault(){this.defaultPrevented=true;},stopPropagation(){this.stopped=true;},...extra};
    for(const listener of doc.listeners.get(type)||[])listener(event);
    if(!event.stopped)for(const listener of target.listeners.get(type)||[])listener(event);
    return event;
  };
  const accent=()=>doc.body.querySelector('[data-app-setting="accent"]');
  const option=id=>doc.body.querySelectorAll('.app-accent-option').find(node=>node.dataset.accent===id);
  const chooseAccent=id=>{if(doc.body.querySelector('.app-accent-options').hidden)fire(accent(),'click');fire(option(id),'click');};
  const select=()=>doc.body.querySelector('[data-app-setting="poll-interval"]');
  return {doc,settings,storage,change,select,fire,accent,option,chooseAccent};
}

test('Live refresh defaults to100 ms with five explicit transfer-limited choices and no initialization callback',t=>{
  const changes=[],f=fixture(t,{onChange:value=>changes.push(value)});
  assert.equal(DEFAULT_POLL_INTERVAL_MS,100);assert.equal(APP_POLL_KEY,'thy666.companion.pollMs.v1');
  assert.deepEqual(APP_POLL_INTERVALS.map(item=>item.ms),[50,100,250,500,1000]);
  assert.equal(f.settings.pollIntervalMs,100);assert.deepEqual(changes,[]);
  f.settings.open();const select=f.select();
  assert.equal(select.attributes['aria-label'],'Live refresh');assert.equal(select.value,'100');
  assert.deepEqual(select.children.map(option=>option.textContent),[
    'Fast · 50 ms (up to 20 Hz)','Balanced · 100 ms (up to 10 Hz)','Light · 250 ms (up to 4 Hz)',
    'Slow · 500 ms (up to 2 Hz)','Minimal · 1000 ms (up to 1 Hz)',
  ]);
  assert.match(f.doc.body.textContent,/Actual refresh follows transfer speed; busy connections and hidden tabs slow down automatically\./);
  assert.deepEqual(changes,[]);assert.equal(f.storage.has(APP_POLL_KEY),false,'Opening settings does not rewrite the refresh preference.');
});

test('each supported saved interval restores without notifying or rewriting the scheduler preference',t=>{
  for(const {ms} of APP_POLL_INTERVALS){
    const changes=[],storage=new Map([[APP_POLL_KEY,String(ms)]]),f=fixture(t,{storage,onChange:value=>changes.push(value)});
    assert.equal(f.settings.pollIntervalMs,ms);f.settings.open();assert.equal(f.select().value,String(ms));
    assert.deepEqual(changes,[]);assert.equal(storage.get(APP_POLL_KEY),String(ms));f.settings.destroy();
  }
});

test('invalid stored values use the100 ms default and never become unbounded or unsupported polling rates',t=>{
  for(const value of ['', '0', '-1', '60', '100.5', '50ms', 'Infinity', 'NaN']){
    const changes=[],f=fixture(t,{storage:new Map([[APP_POLL_KEY,value]]),onChange:value=>changes.push(value)});
    assert.equal(f.settings.pollIntervalMs,100,value);f.settings.open();assert.equal(f.select().value,'100');
    assert.deepEqual(changes,[]);f.settings.destroy();
  }
});

test('only an actual valid user change persists and notifies once, retaining the selection when reopened',t=>{
  const changes=[],f=fixture(t,{onChange:value=>changes.push(value)});f.settings.open();
  f.change(f.select(),250);assert.equal(f.settings.pollIntervalMs,250);assert.equal(f.storage.get(APP_POLL_KEY),'250');
  assert.deepEqual(changes,[250]);
  for(const value of [250,0,75,'bad',''])f.change(f.select(),value);
  assert.equal(f.settings.pollIntervalMs,250);assert.equal(f.select().value,'250');assert.deepEqual(changes,[250]);
  f.settings.setConnected(true);assert.equal(f.select().disabled,false,'Refresh remains a browser preference while connected.');
  f.settings.close();f.settings.open();assert.equal(f.select().value,'250');assert.deepEqual(changes,[250]);
  f.change(f.select(),50);assert.deepEqual(changes,[250,50]);assert.equal(f.storage.get(APP_POLL_KEY),'50');
});

test('blocked browser storage stays usable and destroying settings disables detached preference handlers',t=>{
  const changes=[],f=fixture(t,{storage:{getItem(){throw Error('blocked');},setItem(){throw Error('blocked');}},onChange:value=>changes.push(value)});
  assert.equal(f.settings.pollIntervalMs,100);assert.deepEqual(changes,[]);f.settings.open();
  const select=f.select();f.change(select,500);assert.equal(f.settings.pollIntervalMs,500);assert.deepEqual(changes,[500]);
  f.settings.destroy();assert.equal(f.doc.body.querySelector('.app-settings-overlay'),null);assert.equal(f.doc.body.querySelector('.app-settings-toggle'),null);
  f.change(select,1000);f.settings.open();
  assert.equal(f.settings.pollIntervalMs,500);assert.deepEqual(changes,[500]);assert.equal(f.settings.modalOpen,false);
  assert.equal(f.doc.listeners.get('keydown').length,0);
});

test('appearance and accent restore independently, preserving every legacy id and the live refresh preference',t=>{
  assert.deepEqual(APP_APPEARANCES.map(item=>item.id),['dark','light']);
  for(const theme of ['red','cyan','amber','mono'])for(const appearance of ['dark','light']){
    const storage=new Map([[APP_THEME_KEY,theme],[APP_APPEARANCE_KEY,appearance],[APP_POLL_KEY,'250']]);
    const before=[...storage],changes=[],f=fixture(t,{storage,onChange:value=>changes.push(value)});
    assert.equal(f.settings.theme,theme);assert.equal(f.settings.appearance,appearance);
    assert.equal(f.doc.body.dataset.theme,theme);assert.equal(f.doc.body.dataset.appearance,appearance);
    f.settings.open();
    assert.equal(f.doc.body.querySelector('[data-app-setting="accent"]').value,theme);
    assert.equal(f.doc.body.querySelector('[data-app-setting="appearance"]').value,appearance);
    assert.equal(f.select().value,'250');assert.deepEqual([...storage],before,'Opening settings never rewrites saved preferences.');
    assert.deepEqual(changes,[]);f.settings.destroy();
  }
});

test('changing appearance or spectrum accent touches only its browser preference and survives reopening',t=>{
  const changes=[],f=fixture(t,{onChange:value=>changes.push(value)});f.settings.open();
  const appearance=f.doc.body.querySelector('[data-app-setting="appearance"]'),accent=f.doc.body.querySelector('[data-app-setting="accent"]');
  assert.equal(f.doc.activeElement,appearance);
  assert.equal(f.doc.body.querySelector('.app-accent-options').children.length,13);assert.equal(f.doc.body.querySelector('.app-theme-swatches'),null);
  for(const option of f.doc.body.querySelectorAll('.app-accent-option'))assert.equal(option.children[0].dataset.accent,option.dataset.accent);
  assert.equal(new Set(APP_THEMES.map(item=>item.id)).size,13);
  f.change(appearance,'light');f.chooseAccent('purple');
  assert.equal(f.settings.appearance,'light');assert.equal(f.settings.theme,'purple');
  assert.equal(f.storage.get(APP_APPEARANCE_KEY),'light');assert.equal(f.storage.get(APP_THEME_KEY),'purple');
  assert.equal(f.storage.has(APP_POLL_KEY),false);assert.equal(f.settings.pollIntervalMs,100);assert.deepEqual(changes,[]);
  for(const id of APP_THEMES.map(item=>item.id)){f.chooseAccent(id);assert.equal(f.settings.theme,id);assert.equal(f.settings.appearance,'light');}
  f.chooseAccent('pink');const stale=f.option('red');f.settings.close();f.settings.open();
  assert.equal(f.doc.body.querySelector('[data-app-setting="appearance"]').value,'light');
  assert.equal(f.doc.body.querySelector('[data-app-setting="accent"]').value,'pink');
  f.settings.destroy();f.change(appearance,'dark');f.fire(stale,'click');
  assert.equal(f.settings.appearance,'light');assert.equal(f.settings.theme,'pink','Detached handlers cannot alter preferences.');
});

test('unsupported appearance/accent values safely fall back without erasing stored preferences',t=>{
  const storage=new Map([[APP_THEME_KEY,'unknown'],[APP_APPEARANCE_KEY,'sepia']]),f=fixture(t,{storage});
  assert.equal(f.settings.theme,'red');assert.equal(f.settings.appearance,'dark');
  assert.equal(storage.get(APP_THEME_KEY),'unknown');assert.equal(storage.get(APP_APPEARANCE_KEY),'sepia');
  f.settings.open();
  const appearance=f.doc.body.querySelector('[data-app-setting="appearance"]'),accent=f.doc.body.querySelector('[data-app-setting="accent"]');
  f.change(appearance,'unknown');assert.equal(appearance.value,'dark');assert.equal(accent.value,'red');
  const blocked=fixture(t,{storage:{getItem(){throw Error('blocked');},setItem(){throw Error('blocked');}}});blocked.settings.open();
  blocked.change(blocked.doc.body.querySelector('[data-app-setting="appearance"]'),'light');
  blocked.chooseAccent('cyan');
  assert.equal(blocked.settings.appearance,'light');assert.equal(blocked.settings.theme,'cyan');
});

test('spectrum accents and themed Web display stay readable while hardware OLED stays black',async()=>{
  const css=await readFile(new URL('../../web/companion-theme.css',import.meta.url),'utf8');
  const palette=await readFile(new URL('../../web/ui-system.css',import.meta.url),'utf8');
  const display=await readFile(new URL('../../web/companion-display.css',import.meta.url),'utf8');
  const luminance=rgb=>rgb.map(value=>value/255).map(value=>value<=.04045?value/12.92:((value+.055)/1.055)**2.4).reduce((sum,value,n)=>sum+value*[.2126,.7152,.0722][n],0);
  const contrast=(a,b)=>{const la=luminance(a),lb=luminance(b);return(Math.max(la,lb)+.05)/(Math.min(la,lb)+.05);};
  for(const {id} of APP_THEMES){
    const match=palette.match(new RegExp(`data-theme="${id}"[^\\n]+--accent-dark-rgb:\\s*([\\d ]+);\\s*--accent-light-rgb:\\s*([\\d ]+);`));
    assert.ok(match,`Palette includes ${id}`);
    const dark=match[1].split(' ').map(Number),light=match[2].split(' ').map(Number);
    assert.ok(contrast(dark,[0,0,0])>=4.5,`${id} Dark foreground contrast`);
    assert.ok(contrast(light,[229,226,218])>=4.5,`${id} Light foreground contrast`);
  }
  assert.match(display,/--display-ink:\s*var\(--ui-ink,\s*#fff\)/);
  assert.match(display,/--display-paper:\s*var\(--ui-paper,\s*#000\)/);
  assert.match(display,/> \.companion-web-screen\s*\{\s*background:\s*var\(--ui-paper,\s*#000\)/);
  assert.match(display,/\.hardware-display > canvas\s*\{[^}]*background:\s*#000/);
  const lightRule=css.match(/body\.companion-app\[data-appearance="light"\]\s*\{([^}]+)\}/)[1];
  const rgb=hex=>[1,3,5].map(at=>Number.parseInt(hex.slice(at,at+2),16));
  const paper=rgb(lightRule.match(/--ui-paper:\s*(#[a-f\d]{6})/)[1]);
  const ink=rgb(lightRule.match(/--ui-ink:\s*(#[a-f\d]{6})/)[1]);
  const alpha=Number(display.match(/data-appearance="light"[^}]+\.is-dim\s*\{\s*opacity:\s*([.\d]+)/)[1]);
  assert.ok(contrast(ink.map((value,n)=>value*alpha+paper[n]*(1-alpha)),paper)>=4.5,'Dim Web labels remain readable in Light mode.');
});

test('accent listbox previews every color and keyboard navigation commits only on Enter or Space',t=>{
  const changes=[],f=fixture(t,{onChange:value=>changes.push(value)});f.settings.open();
  const trigger=f.accent(),list=f.doc.body.querySelector('.app-accent-options');
  assert.equal(trigger.attributes['aria-label'],'Accent color');assert.equal(trigger.attributes['aria-haspopup'],'listbox');
  assert.equal(trigger.attributes['aria-expanded'],'false');assert.equal(list.attributes.role,'listbox');assert.equal(list.hidden,true);
  assert.equal(trigger.children[0].dataset.accent,'red');assert.equal(trigger.children[1].textContent,'Red');
  f.fire(trigger,'keydown',{key:'ArrowDown'});assert.equal(list.hidden,false);assert.equal(trigger.attributes['aria-expanded'],'true');assert.equal(f.doc.activeElement,f.option('red'));
  const down=f.fire(f.doc.activeElement,'keydown',{key:'ArrowDown'});assert.ok(down.stopped&&down.defaultPrevented);assert.equal(f.doc.activeElement,f.option('orange'));
  assert.equal(f.settings.theme,'red');assert.equal(f.storage.has(APP_THEME_KEY),false,'Moving focus is not a preference edit.');
  f.fire(f.doc.activeElement,'keydown',{key:'End'});assert.equal(f.doc.activeElement,f.option('mono'));
  f.fire(f.doc.activeElement,'keydown',{key:'ArrowDown'});assert.equal(f.doc.activeElement,f.option('mono'),'End is bounded.');
  f.fire(f.doc.activeElement,'keydown',{key:'ArrowUp'});assert.equal(f.doc.activeElement,f.option('pink'));
  f.fire(f.doc.activeElement,'keydown',{key:'Enter'});assert.equal(f.settings.theme,'pink');assert.equal(f.storage.get(APP_THEME_KEY),'pink');
  assert.equal(f.option('pink').attributes['aria-selected'],'true');assert.equal(f.option('red').attributes['aria-selected'],'false');
  assert.equal(trigger.children[0].dataset.accent,'pink');assert.equal(trigger.children[1].textContent,'Pink');assert.equal(list.hidden,true);assert.equal(f.doc.activeElement,trigger);
  f.fire(trigger,'keydown',{key:'Home'});assert.equal(f.doc.activeElement,f.option('red'));
  f.fire(f.doc.activeElement,'keydown',{key:'ArrowUp'});assert.equal(f.doc.activeElement,f.option('red'),'Start is bounded.');
  f.fire(f.doc.activeElement,'keydown',{key:' '});assert.equal(f.settings.theme,'red');assert.equal(list.hidden,true);
  assert.deepEqual(changes,[]);assert.equal(f.settings.pollIntervalMs,100);assert.equal(f.settings.appearance,'dark');
});

test('Escape closes the accent list first, outside clicks and Tab dismiss it, and hidden options never enter the focus trap',t=>{
  const f=fixture(t);f.settings.open();
  const trigger=f.accent(),list=f.doc.body.querySelector('.app-accent-options'),appearance=f.doc.body.querySelector('[data-app-setting="appearance"]');
  f.fire(trigger,'click');f.fire(f.doc.activeElement,'keydown',{key:'End'});
  const escape=f.fire(f.doc.activeElement,'keydown',{key:'Escape'});assert.ok(escape.stopped&&escape.defaultPrevented);
  assert.equal(list.hidden,true);assert.equal(f.settings.modalOpen,true);assert.equal(f.doc.activeElement,trigger);assert.equal(f.settings.theme,'red');
  f.fire(trigger,'click');f.fire(appearance,'click');assert.equal(list.hidden,true);assert.equal(f.settings.modalOpen,true);
  f.fire(trigger,'click');const tab=f.fire(f.doc.activeElement,'keydown',{key:'Tab'});
  assert.equal(list.hidden,true);assert.equal(f.doc.activeElement,trigger);assert.equal(tab.defaultPrevented,false,'Native Tab continues to the next ordinary field.');
  const done=f.doc.body.querySelectorAll('button').find(node=>node.textContent==='Done');
  done.focus();const wrap=f.fire(done,'keydown',{key:'Tab'});assert.equal(wrap.defaultPrevented,true);assert.equal(f.doc.activeElement,appearance);
  const reverse=f.fire(appearance,'keydown',{key:'Tab',shiftKey:true});assert.equal(reverse.defaultPrevented,true);assert.equal(f.doc.activeElement,done);
  f.fire(done,'keydown',{key:'Escape'});assert.equal(f.settings.modalOpen,false);
  assert.equal(f.doc.listeners.get('click').length,1);f.settings.destroy();assert.equal(f.doc.listeners.get('click').length,0);
});
