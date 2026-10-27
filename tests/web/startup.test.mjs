import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {initializeStartup,STARTUP_DURATION_MS,STARTUP_REDUCED_MS} from '../../web/startup.js';

function fixture({reduced=false,elapsed=0,failed=false}={}){
  const timers=[],cancelled=new Set(),root={dataset:{startup:failed?'failed':'pending'}},shell={inert:true};
  const panel={dataset:{state:'loading'},setAttribute(name,value){this[name]=value;},hidden:false};
  const status={textContent:''},retry={hidden:true,focus(){this.focused=true;}},view={__thyStartupStarted:0,__thyStartupTimer:9,matchMedia:()=>({matches:reduced}),location:{reload(){view.reloaded=true;}}};
  const doc={documentElement:root,getElementById:id=>({'companion-startup':panel,'startup-status':status,'startup-retry':retry})[id],querySelector:()=>shell};
  const startup=initializeStartup({doc,view,now:()=>elapsed,schedule:(run,ms)=>{const id=timers.length;timers.push({run,ms});return id;},cancel:id=>cancelled.add(id)});
  return {startup,root,shell,panel,status,retry,view,timers,cancelled,run(index){if(!cancelled.has(index))timers[index].run();}};
}

test('the parsed page has an early black guard and a bounded fallback before app.js',()=>{
  const html=readFileSync(new URL('../../web/index.html',import.meta.url),'utf8');
  assert.match(html,/<html[^>]+data-startup="pending"/);
  assert.ok(html.indexOf('startup-critical')<html.indexOf('./styles.css'));
  assert.ok(html.indexOf('__thyStartupFallback')<html.indexOf('./app.js'));
  assert.match(html,/<div class="app-shell" inert>/);
  assert.match(html,/setTimeout\(window\.__thyStartupFallback,16000\)/);
  assert.equal((html.match(/class="startup-tile"/g)||[]).length,14);
  assert.match(html,/class="startup-center"/);
  assert.match(html,/JavaScript is required/);
});

test('readiness preserves the intro floor then reveals an interactive shell once',()=>{
  const f=fixture({elapsed:200});f.startup.ready();f.startup.ready();
  assert.equal(f.timers.length,1);assert.equal(f.timers[0].ms,STARTUP_DURATION_MS-200);
  assert.equal(f.shell.inert,true);assert.equal(f.root.dataset.startup,'pending');
  f.run(0);assert.equal(f.startup.phase,'complete');assert.equal(f.shell.inert,false);
  assert.equal(f.root.dataset.startup,'complete');assert.equal(f.panel['aria-hidden'],'true');
  f.run(1);assert.equal(f.panel.hidden,true);
});

test('slow successful loading does not replay or extend an already completed sequence',()=>{
  const f=fixture({elapsed:6000});f.startup.ready();assert.equal(f.timers[0].ms,0);f.run(0);assert.equal(f.shell.inert,false);
});

test('reduced motion uses a short static handoff',()=>{
  const f=fixture({reduced:true});f.startup.ready();assert.equal(f.timers[0].ms,STARTUP_REDUCED_MS);f.run(0);
  assert.equal(f.timers[1].ms,0);
  const css=readFileSync(new URL('../../web/startup.css',import.meta.url),'utf8');
  assert.match(css,/@media\(prefers-reduced-motion:reduce\)/);
});

test('failure is terminal, accessible and never reveals the legacy page',()=>{
  const f=fixture();f.startup.ready();f.startup.fail(new Error('test'));
  assert.equal(f.root.dataset.startup,'failed');assert.equal(f.shell.inert,true);
  assert.equal(f.retry.hidden,false);assert.equal(f.retry.focused,true);f.retry.onclick();assert.equal(f.view.reloaded,true);
  f.run(0);f.startup.ready();assert.equal(f.shell.inert,true);assert.equal(f.panel.hidden,false);
});

test('a timed-out entry cannot be hidden by late successful initialization',()=>{
  const f=fixture({failed:true});f.startup.ready();assert.equal(f.startup.phase,'failed');assert.equal(f.timers.length,0);assert.equal(f.shell.inert,true);
});
