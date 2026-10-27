/* SPDX-License-Identifier: GPL-3.0-or-later
 * The desktop frame owns presentation and explicit window gestures only.
 * It never discovers a device, opens USB, starts music or installs firmware.
 */
const SVG='http://www.w3.org/2000/svg';
const DIRECTIONS=['n','ne','e','se','s','sw','w','nw'];
const FRAME_WIDTH=1440,FRAME_HEIGHT=900;
// Clockwise perimeter pieces. Gaps between pieces interrupt the red trace,
// while the closed silhouette bridges them to keep the entire interior solid.
const FRAME_SEGMENTS=[
  [[2,118],[2,15],[15,2],[267,2],[279,9],[464,9],[472,2],[968,2],[976,9],[1161,9],[1173,2],[1425,2],[1438,15],[1438,118]],
  [[1438,143],[1438,306],[1432,315],[1432,585],[1438,594],[1438,757]],
  [[1438,782],[1438,885],[1425,898],[1173,898],[1161,891],[976,891],[968,898],[472,898],[464,891],[279,891],[267,898],[15,898],[2,885],[2,782]],
  [[2,757],[2,594],[8,585],[8,315],[2,306],[2,143]],
];
const pointPath=points=>`M${points.map(point=>point.join(' ')).join('L')}`;
export function desktopFrameGeometry(){
  const points=FRAME_SEGMENTS.flat();
  return{
    viewBox:`0 0 ${FRAME_WIDTH} ${FRAME_HEIGHT}`,
    outline:`${pointPath(points)}Z`,
    outer:pointPath(FRAME_SEGMENTS[0])+pointPath(FRAME_SEGMENTS[2]),
    sides:pointPath(FRAME_SEGMENTS[1])+pointPath(FRAME_SEGMENTS[3]),
    clip:`polygon(${points.map(([x,y])=>`${x/FRAME_WIDTH*100}% ${y/FRAME_HEIGHT*100}%`).join(',')})`,
  };
}

export function initializeDesktopChrome({runtime,document:doc=globalThis.document}={}){
  if(runtime?.kind!=='desktop')return null;
  const macOS=runtime.platform==='macos';
  const native=runtime.window,root=doc?.documentElement,body=doc?.body,shell=doc?.querySelector?.('.app-shell');
  if(!root||!body||!shell||!native||['minimize','toggleMaximize','isMaximized','beginDrag','beginResize','close'].some(name=>typeof native[name]!=='function'))
    throw new TypeError('The desktop window frame requires the native window interface and mounted Client.');
  if(body.querySelector('.desktop-window-chrome'))throw new Error('The desktop window frame is already mounted.');
  let disposed=false,maximized=false,unsubscribe=()=>{},stateRevision=0;
  const listeners=[],oldParent=shell.parentNode,oldSibling=shell.nextSibling;
  const shape=desktopFrameGeometry(),oldClip=root.style.getPropertyValue('--desktop-frame-clip');
  const oldClipPriority=root.style.getPropertyPriority('--desktop-frame-clip');
  const make=(tag,cls,text)=>{const node=doc.createElement(tag);if(cls)node.className=cls;if(text!==undefined)node.textContent=String(text);return node;};
  const svgNode=(tag,attributes)=>{const node=doc.createElementNS(SVG,tag);for(const[key,value]of Object.entries(attributes))node.setAttribute(key,String(value));return node;};
  const listen=(node,type,handler)=>{node.addEventListener(type,handler);listeners.push(()=>node.removeEventListener(type,handler));};
  // Paint the actual shape. BODY backgrounds propagate to a transparent root
  // canvas in HTML, where clipping BODY does not contain that background.
  const background=svgNode('svg',{id:'desktop-window-background',class:'desktop-window-background',viewBox:shape.viewBox,preserveAspectRatio:'none','aria-hidden':'true',focusable:'false'});
  const backgroundPath=svgNode('path',{class:'desktop-background-shape',d:shape.outline});background.append(backgroundPath);
  const chrome=make('div',`desktop-window-chrome${macOS?' desktop-window-macos':''}`);chrome.id='desktop-window-chrome';
  const frame=svgNode('svg',{id:'desktop-frame',class:'desktop-window-frame',viewBox:shape.viewBox,preserveAspectRatio:'none','aria-hidden':'true',focusable:'false'});
  frame.append(svgNode('path',{class:'desktop-frame-outer',d:shape.outer}),
    svgNode('path',{class:'desktop-frame-side',d:shape.sides}),
    svgNode('path',{class:'desktop-frame-trace',d:'M8 89V24L24 8h89M1327 8h89l16 16v65M8 811v65l16 16h89M1327 892h89l16-16v-65M7 370v32m0 48v11m1426-91v32m0 48v11M573 4h31m14 0h7m208 0h31m14 0h7M573 896h31m14 0h7m208 0h31m14 0h7'}));
  chrome.append(frame);
  const titlebar=make('header','desktop-titlebar');titlebar.id='desktop-titlebar';titlebar.setAttribute('aria-label','THY666 window');
  const identity=make('div','desktop-window-identity');
  const logoSource=doc.querySelector('.companion-brand-logo')?.getAttribute('src');
  if(logoSource){const logo=make('img','desktop-window-logo');logo.src=logoSource;logo.alt='';logo.draggable=false;identity.append(logo);}
  identity.append(make('span','desktop-window-name','THY666'),make('span','desktop-window-caption','INSTRUMENT CONSOLE'));
  const status=make('span','desktop-window-status');status.setAttribute('role','status');status.setAttribute('aria-live','polite');
  const controls=make('div','desktop-window-controls');controls.setAttribute('role','group');controls.setAttribute('aria-label','Window controls');
  const icons={minimize:'M5 16h14',maximize:'M6 6h12v12H6Z',restore:'M8 6V4h12v12h-2M4 8h12v12H4Z',close:'M6 6l12 12M18 6 6 18'};
  if(macOS){
    icons.minimize='M5 12h14';
    icons.maximize='M5 10V5h5M5 5l5 5M14 19h5v-5M19 19l-5-5';
    icons.restore='M5 10h5V5M10 10 5 5M19 14h-5v5M14 14l5 5';
  }
  const buttons=new Map();
  const actions=macOS?['close','minimize','maximize']:['minimize','maximize','close'];
  const labels={close:'Close THY666',minimize:'Minimize window',maximize:'Maximize window'};
  for(const action of actions){
    const label=labels[action];
    const button=make('button',`desktop-window-button desktop-window-${action}`);button.type='button';button.dataset.windowAction=action;
    button.setAttribute('aria-label',label);button.title=label;
    const icon=svgNode('svg',{viewBox:'0 0 24 24','aria-hidden':'true',focusable:'false'}),path=svgNode('path',{d:icons[action]});icon.append(path);button.append(icon);buttons.set(action,{button,path});controls.append(button);
  }
  titlebar.append(...(macOS?[controls,status,identity]:[identity,status,controls]));chrome.append(titlebar);
  const handles=[],resizeHandles=make('div','desktop-resize-handles');resizeHandles.id='desktop-resize-handles';chrome.append(resizeHandles);
  for(const direction of DIRECTIONS){
    const handle=make('div',`desktop-resize-handle desktop-resize-${direction}`);handle.dataset.desktopResize=direction;handle.setAttribute('aria-hidden','true');handles.push(handle);resizeHandles.append(handle);
    listen(handle,'pointerdown',event=>{if(!primary(event)||maximized)return;event.preventDefault();void run(()=>native.beginResize(direction));});
  }
  const viewport=make('div','desktop-content-viewport');shell.before(viewport);viewport.append(shell);
  root.style.setProperty('--desktop-frame-clip',shape.clip);
  body.prepend(background,chrome);root.classList.add('desktop-window');
  function report(error){if(!disposed)status.textContent=error?.message||'The window action could not finish.';}
  async function run(action){if(disposed)return;status.textContent='';try{return await action();}catch(error){report(error);}}
  function primary(event){return event.button===0&&event.isPrimary!==false;}
  function blank(event){return !event.target?.closest?.('button,a,input,select,textarea,[contenteditable="true"]');}
  function state(value){
    if(disposed)return;
    const next=typeof value==='boolean'?value:value?.maximized;if(typeof next!=='boolean')return;
    stateRevision++;
    maximized=next;root.dataset.windowMaximized=String(maximized);
    backgroundPath.setAttribute('d',maximized?`M0 0H${FRAME_WIDTH}V${FRAME_HEIGHT}H0Z`:shape.outline);
    const control=buttons.get('maximize'),label=maximized?'Restore window':'Maximize window';control.button.setAttribute('aria-label',label);control.button.title=label;control.path.setAttribute('d',maximized?icons.restore:icons.maximize);
    handles.forEach(handle=>{handle.hidden=maximized;});
  }
  const refresh=()=>{const revision=stateRevision;return Promise.resolve(native.isMaximized()).then(value=>{if(revision===stateRevision)state(value);});};
  listen(buttons.get('minimize').button,'click',()=>void run(()=>native.minimize()));
  listen(buttons.get('maximize').button,'click',()=>void run(async()=>{await native.toggleMaximize();await refresh();}));
  listen(buttons.get('close').button,'click',()=>void run(()=>native.close()));
  listen(titlebar,'pointerdown',event=>{if(!primary(event)||event.detail>1||!blank(event))return;event.preventDefault();void run(()=>native.beginDrag());});
  listen(titlebar,'dblclick',event=>{if(!primary(event)||!blank(event))return;event.preventDefault();void run(async()=>{await native.toggleMaximize();await refresh();});});
  void refresh().catch(report);
  if(runtime.subscribeWindowState)Promise.resolve(runtime.subscribeWindowState(state)).then(remove=>{if(typeof remove!=='function')return;if(disposed)remove();else unsubscribe=remove;}).catch(report);
  return{
    get maximized(){return maximized;},
    destroy(){
      if(disposed)return;disposed=true;unsubscribe();for(const remove of listeners)remove();
      if(viewport.contains(shell)){if(oldSibling?.parentNode===oldParent)oldParent.insertBefore(shell,oldSibling);else oldParent.append(shell);}
      viewport.remove();chrome.remove();background.remove();root.classList.remove('desktop-window');delete root.dataset.windowMaximized;
      if(oldClip)root.style.setProperty('--desktop-frame-clip',oldClip,oldClipPriority);else root.style.removeProperty('--desktop-frame-clip');
    },
  };
}
