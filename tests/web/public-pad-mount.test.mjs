import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {runInNewContext} from 'node:vm';

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
  let ready=0,mounted=0;
  const context={document,$:id=>clusters.get(id),desktopBindingsPromise:Promise.resolve(null),SCALE_CATALOG:[],
    createCore:async()=>({state:{navigationAnimations(){},boot:{skip(){}}}}),
    initializeFirmwareStudio:model,initializeEngineStudio:model,initializeRhythmLab:model,
    initializeArpLab:model,initializeSequencerLab:model,initializeMpeLab:model,initializeLibraryLab:model,
    initializeCompanionShell:()=>{mounted++;return {};},startup:{ready(){ready++;}},window:{}};
  const initialize=runInNewContext(source.slice(begin,end)+'\ninitializeConnectedCompanion;',context);
  await initialize();
  assert.equal(mounted,1);assert.equal(ready,1);
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
