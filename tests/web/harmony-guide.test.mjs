import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createCore} from '../../web/core.js';
import {initializeHarmonyGuide} from '../../web/harmony-guide.js';
const bytes=await readFile(new URL('../../web/generated/core.wasm',import.meta.url));

// A small DOM fixture; all analysis below executes the real compiled C.
class Element {
  constructor(doc,tag){this.ownerDocument=doc;this.tagName=tag;this.children=[];this.attrs={};this.events={};this.classList={add:()=>{}};this.textContent='';}
  append(...nodes){this.children.push(...nodes);}
  replaceChildren(...nodes){this.children=nodes;}
  setAttribute(k,v){this.attrs[k]=v;}
  addEventListener(k,fn){this.events[k]=fn;}
  focus(){this.ownerDocument.activeElement=this;}
}
const all=n=>[n,...n.children.flatMap(all)];
const text=n=>[n.textContent,...n.children.map(text)].join(' ');
function fixture(guidance){const doc={createElement:tag=>new Element(doc,tag)};const root=new Element(doc,'main');initializeHarmonyGuide(root,guidance);return {root,doc};}

test('guidance ranks scale/chord choices and exposes common-note modulation through WASM',async()=>{
  const e=await createCore({bytes}),g=e.guidance;
  const key=g.scale(0,0),target=g.scale(0,7);
  assert.equal(g.families.length,99);assert.equal(key,0xab5);
  assert.ok(g.chords(key,0,0x091).some(c=>c.root===7&&c.mask===0x8a4));
  assert.equal(g.label(0x221,5),'F MAJ'); // F A C, rooted F rather than invented slash bass.
  const found=g.discover(key);assert.ok(found.matches.some(s=>s.family===0&&s.root===0));
  assert.ok(found.matches.some(s=>s.family===5&&s.root===9));
  assert.deepEqual(g.modulation(key,target,0x091),{common:0xa95,added:64,removed:32,pivot:0x091,pivotRoot:0,currentFits:1});
  for(const bad of [-1,4096,1.5,NaN])assert.throws(()=>g.discover(bad),RangeError);
  assert.throws(()=>g.discover(0),RangeError);assert.throws(()=>g.chords(key,1),RangeError);
});

test('advisory interactions never change settings, held voices or MIDI queues',async()=>{
  const e=await createCore({bytes}),before=e.settings.encode();
  assert.ok(e.press({pad:0,notes:[60,64,67],mask:4,channel:2}).ok);e.drain();
  const owner=e.owner(0),{root,doc}=fixture(e.guidance);
  assert.match(text(root),/local musical sketches/);
  const card=all(root).find(n=>n.tagName==='button'&&n.children.some(c=>c.textContent==='C MAJ7'));
  assert.ok(card);card.events.click();assert.equal(doc.activeElement.attrs['aria-label'],'Include C');
  const scale=all(root).find(n=>n.tagName==='button'&&text(n).includes('Compare as destination'));
  scale.events.click();assert.equal(doc.activeElement.attrs['aria-label'],'Destination root');
  all(root).find(n=>n.textContent==='Use destination as working key').events.click();
  all(root).find(n=>n.textContent==='Clear notes').events.click();
  assert.match(text(root),/No key is inferred from silence/);
  assert.deepEqual(e.settings.encode(),before);assert.deepEqual(e.owner(0),owner);assert.deepEqual(e.drain(),[]);
  assert.ok(e.release(0).ok);assert.equal(e.drain().length,3);
});
