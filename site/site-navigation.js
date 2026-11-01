import {createUIIcon, createUIIconCorners} from './ui-system.js';

/** Progressively enhance the existing links; the native dialog owns focus/inertness. */
export function initializeSiteNavigation({document:doc=globalThis.document,
  media=globalThis.matchMedia?.('(max-width: 720px)')}={}) {
  const header=doc?.querySelector?.('.site-header'),nav=header?.querySelector('nav');
  if(!nav||!media)return null;
  const panel=doc.createElement('dialog');
  if(typeof panel.showModal!=='function')return null;
  panel.className='site-menu hud-frame hud-frame--dialog';
  panel.id='site-mobile-menu';panel.setAttribute('aria-label','Navigation');
  const iconButton=(icon,label)=>{
    const button=doc.createElement('button');button.type='button';button.className='hud-icon-button';
    button.setAttribute('aria-label',label);
    button.append(createUIIcon(icon,{document:doc}),createUIIconCorners({document:doc}));return button;
  };
  const toggle=iconButton('menu','Open navigation');toggle.classList.add('site-menu-toggle');
  toggle.setAttribute('aria-controls',panel.id);toggle.setAttribute('aria-expanded','false');
  toggle.setAttribute('aria-haspopup','dialog');
  const heading=doc.createElement('div');heading.className='site-menu-heading';
  const title=doc.createElement('strong');title.textContent='Navigate';
  const dismiss=iconButton('close','Close navigation');heading.append(title,dismiss);
  panel.append(heading);header.append(toggle);doc.body.append(panel);
  function closed(){
    toggle.setAttribute('aria-expanded','false');doc.body.classList.remove('site-menu-open');
    if(!toggle.hidden)toggle.focus();
  }
  function close(){if(panel.open)panel.close();}
  function open(){
    if(toggle.hidden||panel.open)return;
    panel.showModal();toggle.setAttribute('aria-expanded','true');
    doc.body.classList.add('site-menu-open');dismiss.focus();
  }
  function sync(){
    toggle.hidden=!media.matches;
    if(media.matches)panel.append(nav);
    else{close();header.append(nav);}
  }
  function follow(event){if(event.target.closest?.('a[href]'))close();}
  function backdrop(event){
    if(event.target!==panel)return;
    const box=panel.getBoundingClientRect();
    if(event.clientX<box.left||event.clientX>box.right||event.clientY<box.top||event.clientY>box.bottom)close();
  }
  toggle.addEventListener('click',open);dismiss.addEventListener('click',close);
  panel.addEventListener('close',closed);panel.addEventListener('click',backdrop);
  nav.addEventListener('click',follow);media.addEventListener('change',sync);sync();
  return {close,destroy(){
    close();toggle.removeEventListener('click',open);dismiss.removeEventListener('click',close);
    panel.removeEventListener('close',closed);panel.removeEventListener('click',backdrop);
    nav.removeEventListener('click',follow);media.removeEventListener('change',sync);
    header.append(nav);panel.remove();toggle.remove();doc.body.classList.remove('site-menu-open');
  }};
}
