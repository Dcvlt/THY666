/* Startup presentation only. It never requests a board, audio or device data. */
export const STARTUP_DURATION_MS = 1560;
export const STARTUP_REDUCED_MS = 180;

export function initializeStartup({doc=globalThis.document,view=globalThis,now=()=>performance.now(),
  schedule=(callback,ms)=>setTimeout(callback,ms),cancel=handle=>clearTimeout(handle)}={}){
  const root=doc.documentElement,screen=doc.getElementById('companion-startup');
  const shell=doc.querySelector('.app-shell'),status=doc.getElementById('startup-status');
  const reduced=Boolean(view.matchMedia?.('(prefers-reduced-motion: reduce)').matches);
  const started=Number.isFinite(view.__thyStartupStarted)?view.__thyStartupStarted:now();
  let phase=root.dataset.startup==='failed'?'failed':'loading',revealTimer=null,hideTimer=null;
  if(shell)shell.inert=true;
  function fail(){
    if(phase==='complete'||phase==='failed')return;
    phase='failed';cancel(revealTimer);cancel(hideTimer);cancel(view.__thyStartupTimer);
    root.dataset.startup='failed';if(screen)screen.dataset.state='failed';
    if(status)status.textContent='The companion could not start. Reload to try again.';
    const retry=doc.getElementById('startup-retry');
    if(retry){retry.hidden=false;retry.onclick=()=>view.location?.reload();retry.focus?.();}
  }
  function reveal(){
    if(phase!=='ready')return;
    phase='complete';root.dataset.startup='complete';
    if(shell)shell.inert=false;
    if(screen){screen.dataset.state='leaving';screen.setAttribute('aria-hidden','true');screen.inert=true;hideTimer=schedule(()=>{screen.hidden=true;},reduced?0:200);}
  }
  function ready(){
    if(phase!=='loading')return;
    // A failed entry-module watchdog is terminal; late initialization must not
    // silently reveal a partially initialized page behind its failure panel.
    if(root.dataset.startup==='failed'){phase='failed';return;}
    phase='ready';cancel(view.__thyStartupTimer);
    const remaining=Math.max(0,(reduced?STARTUP_REDUCED_MS:STARTUP_DURATION_MS)-(now()-started));
    revealTimer=schedule(reveal,remaining);
  }
  return {ready,fail,get phase(){return phase;}};
}
