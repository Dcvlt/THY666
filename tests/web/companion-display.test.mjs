import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {CANONICAL_GLYPHS,analyzeCompanionFrame,reconstructCompanionFrame,frameToPath,renderCompanionScreen} from '../../web/companion-display.js';
import {createCore} from '../../web/core.js';
import {MENU_ROWS,STUDIO_MENU} from '../../web/studio-menu.js';

const glyphSource=await readFile(new URL('../../core/thy_ui.c',import.meta.url),'utf8');
const cGlyphs=Object.fromEntries([...glyphSource.matchAll(/case '([^']+)': bits=0x([0-9a-f]+)u;/g)].map(([,c,bits])=>[c,Number.parseInt(bits,16)]));
cGlyphs[' ']=0;
const bit=(frame,x,y)=>Boolean(frame[(y>>>3)*128+x]&(1<<(y&7)));
function put(frame,x,y,on=true){const at=(y>>>3)*128+x,mask=1<<(y&7);if(on)frame[at]|=mask;else frame[at]&=~mask;}
function rectangle(frame,x,y,w,h,on=true){for(let dy=0;dy<h;dy++)for(let dx=0;dx<w;dx++)put(frame,x+dx,y+dy,on);}
function draw(frame,x,y,text,{scale=1,inverse=false,dim=false}={}){
  for(const c of text){const bits=cGlyphs[c]||0;
    for(let col=0;col<4;col++)for(let row=0;row<5;row++)if(bits&(1<<(col*8+row)))
      for(let dx=0;dx<scale;dx++)for(let dy=0;dy<scale;dy++){
        const px=x+col*scale+dx,py=y+row*scale+dy;if(!dim||!((px+py)&1))put(frame,px,py,!inverse);
      }
    x+=5*scale;
  }
}
const parity=frame=>{const model=analyzeCompanionFrame(frame);assert.deepEqual(reconstructCompanionFrame(model),frame);return model;};

test('enhanced glyph definitions exactly match the shared original C font with no ambiguous bitmaps',()=>{
  assert.deepEqual(CANONICAL_GLYPHS,cGlyphs);
  assert.equal(new Set(Object.values(cGlyphs)).size,Object.keys(cGlyphs).length);
  for(const frame of [null,[],new Uint8Array(1023),new Uint8Array(1025)])assert.throws(()=>analyzeCompanionFrame(frame),TypeError);
});

test('exact normal/inverse/large/dithered labels lift without changing frame content',()=>{
  const frame=new Uint8Array(1024);
  draw(frame,2,1,'SETTINGS 2/13');
  draw(frame,6,12,'C#4 A3',{scale:2});
  rectangle(frame,10,29,95,7);draw(frame,12,30,'AFTERTOUCH ON',{inverse:true});
  draw(frame,9,44,'C4 Eb4',{scale:2,dim:true});
  draw(frame,2,58,'> APPLY  < BACK');
  const original=frame.slice(),model=parity(frame);
  for(const text of ['SETTINGS 2/13','C#4 A3','AFTERTOUCH ON','C4 Eb4','> APPLY  < BACK'])assert.ok(model.texts.some(run=>run.text===text),text);
  assert.ok(model.texts.some(run=>run.inverse));assert.ok(model.texts.some(run=>run.dim));
  assert.deepEqual(frame,original,'Enhancement never mutates received snapshot bytes.');
});

test('partial glyphs, unknown artwork and dense changing graphics stay as exact vector pixels',()=>{
  let seed=0x13572468;
  for(let example=0;example<32;example++){
    const frame=new Uint8Array(1024);
    for(let n=0;n<frame.length;n++){seed=(Math.imul(seed,1664525)+1013904223)>>>0;frame[n]=seed>>>24;}
    const model=parity(frame);assert.ok(frameToPath(model.residual).length);
  }
  const frame=new Uint8Array(1024);draw(frame,2,12,'READY');put(frame,14,13,!bit(frame,14,13));
  const model=parity(frame);assert.equal(model.texts.some(run=>run.text==='READY'),false,'No dictionary completion repairs a damaged glyph.');
  const without=analyzeCompanionFrame(frame,{liftText:false});assert.deepEqual(without.residual,frame);assert.deepEqual(without.texts,[]);
});

test('residual SVG paths encode every source pixel exactly',()=>{
  const frame=new Uint8Array(1024);rectangle(frame,0,0,128,1);rectangle(frame,0,63,128,1);put(frame,127,31);draw(frame,3,12,'MENU');
  const path=frameToPath(frame),restored=new Uint8Array(1024);
  for(const [,x,y,width,back]of path.matchAll(/M(\d+) (\d+)h(\d+)v1h-(\d+)z/g)){
    assert.equal(width,back);rectangle(restored,Number(x),Number(y),Number(width),1);
  }
  assert.deepEqual(restored,frame);
});

const wasm=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));
const row=id=>{const n=MENU_ROWS.findIndex(item=>item.id===id);assert.notEqual(n,-1,id);return n;};
const group=id=>STUDIO_MENU.groups.findIndex(item=>item.id===id);
const okay=result=>assert.equal(result.ok,true,result.status||result.message);
async function fresh(){const engine=await createCore({bytes:wasm});okay(engine.firmware.layout(Array.from({length:96},(_,pad)=>({notes:pad<48?[60,64,67,0,0,0,0]:[62,0,0,0,0,0,0],mask:pad<48?7:1,color:1}))));return engine;}
const actual=(engine,now=100)=>parity(engine.state.frame({now,animations:false}));

test('all seven real C Play views and animation frames retain their content, graphs and slot choices',async()=>{
  const engine=await fresh();okay(engine.firmware.pressPad(0));engine.drain();const owner=engine.owner(0);
  const definition=MENU_ROWS[row('lights.view')],views=[];
  for(let value=0;value<definition.options.length;value++){
    if(!engine.state.allowed(row('lights.view'),value))continue;
    okay(engine.state.set(row('lights.view'),value));okay(engine.state.play());
    for(const now of [100,140,280]){
      const frame=engine.state.frame({now,animations:true}),before=frame.slice();parity(frame);assert.deepEqual(frame,before);
    }
    const model=actual(engine,500);assert.ok(model.texts.length);views.push(definition.options[value].value);
  }
  assert.deepEqual(views,['notes','piano','simple','chladni','orbit','waves','fifths']);
  assert.deepEqual(engine.owner(0),owner);
});

test('actual C menu lists, numeric drafts and every information page reconstruct without abbreviated replacements',async()=>{
  const engine=await fresh();okay(engine.state.designMode(true));
  let menus=0,information=0;
  for(let index=0;index<MENU_ROWS.length;index++){
    const definition=MENU_ROWS[index];if(!engine.state.visible(index))continue;
    const focused=engine.state.focus(group(definition.group),index);if(!focused.ok)continue;
    const model=actual(engine);assert.ok(model.texts.length,definition.id);menus++;
    if(definition.type==='info'){
      okay(engine.state.navigate('right'));const page=actual(engine);assert.ok(page.texts.length,definition.id);information++;
      okay(engine.state.navigate('left'));
    }
  }
  assert.ok(menus>200);assert.equal(information,MENU_ROWS.filter(item=>item.type==='info').length);
  okay(engine.state.focus(group('performance'),row('performance.velocity')));okay(engine.state.begin(row('performance.velocity')));okay(engine.state.stage(112));
  const editing=actual(engine);assert.ok(editing.texts.some(run=>run.inverse&&run.text.includes('112')));okay(engine.state.cancel());
});

test('actual C quick Arp, sequencer grids/options, library, calibration and credits all retain exact content',async()=>{
  const engine=await fresh();
  okay(engine.arp.openQuick(0));assert.ok(actual(engine).texts.length);okay(engine.state.play());
  okay(engine.sequencer.createTrack());assert.ok(actual(engine).texts.length);okay(engine.sequencer.configureView({selectedStep:3}));actual(engine);
  okay(engine.state.play());okay(engine.calibration.open());assert.ok(actual(engine).texts.length);okay(engine.calibration.close());
  okay(engine.firmware.library.openEditor(1,0));assert.ok(actual(engine).texts.length);okay(engine.state.play());
  const about=await fresh();okay(about.state.focus(group('system'),row('system.about')));okay(about.state.begin(row('system.about')));
  const credits=actual(about);assert.match(credits.transcript,/OPEN/);assert.match(credits.transcript,/INSTRUMENT/);
});

test('actual C navigation transition frames retain partial glyphs and moving scenes',async()=>{
  const engine=await fresh();okay(engine.state.navigationAnimations(true));
  engine.state.frame({now:0,animations:true});okay(engine.state.navigate('select'));
  for(const now of [1,20,55,100,160,220])parity(engine.state.frame({now,animations:true}));
  okay(engine.state.navigate('right'));
  for(const now of [221,240,280,330,400])parity(engine.state.frame({now,animations:true}));
  okay(engine.state.navigate('left'));
  for(const now of [401,420,460,520,600])parity(engine.state.frame({now,animations:true}));
});

class Element{
  constructor(document,tag){this.ownerDocument=document;this.tagName=tag;this.attributes={};this.dataset={};this.children=[];this._text='';}
  setAttribute(key,value){this.attributes[key]=String(value);}append(...nodes){this.children.push(...nodes);}replaceChildren(...nodes){this.children=[...nodes];}
  set textContent(value){this._text=String(value);this.children=[];}get textContent(){return this._text+this.children.map(n=>n.textContent).join('');}
  set innerHTML(_){throw new Error('Untrusted text must never become HTML.');}
}
const mount=()=>{const document={createElement(tag){return new Element(this,tag);},createElementNS(_ns,tag){return new Element(this,tag);}};return document.createElement('main');};
test('render uses only frame bytes, respects panel visibility and replaces stale content without source badges',()=>{
  const host=mount(),frame=new Uint8Array(1024);draw(frame,2,12,'C FRAME CONTENT');
  const snapshot={frame,panelVisible:true,level:2,harmony:'FABRICATED',currentRow:999,currentValue:123};
  const model=renderCompanionScreen(host,snapshot,{source:'Preview'});assert.match(host.textContent,/C FRAME CONTENT/);assert.doesNotMatch(host.textContent,/FABRICATED|Preview|123/);
  const root=host.children[0];assert.equal(root.dataset.panel,'visible');
  assert.equal(renderCompanionScreen(host,{...snapshot,level:0,harmony:'OTHER'}),model);assert.equal(host.children[0],root,'Metadata cannot make a different independent screen.');
  renderCompanionScreen(host,{...snapshot,panelVisible:false});assert.equal(host.children[0].dataset.panel,'off');assert.equal(host.textContent,'');
  renderCompanionScreen(host,{frame:null});assert.equal(host.children[0].dataset.panel,'unavailable');assert.equal(host.textContent,'');
  renderCompanionScreen(host,snapshot);assert.match(host.textContent,/C FRAME CONTENT/);
  put(frame,127,63);assert.notEqual(renderCompanionScreen(host,snapshot),model,'A source frame mutated in place is still detected.');
});
