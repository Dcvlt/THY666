/* SPDX-License-Identifier: GPL-3.0-or-later
 * Decorative interface glyphs. Accessible names belong to their button/link.
 * Physical instrument symbols and the supplied THY666 logo remain separate.
 */
const UI_ICON_PATHS = Object.freeze({
  close: 'M6 6l12 12M18 6 6 18',
  settings: 'M10.06 4.76L10.69 2.09L13.31 2.09L13.94 4.76L15.75 5.50L18.09 4.07L19.93 5.91L18.50 8.25L19.24 10.06L21.91 10.69L21.91 13.31L19.24 13.94L18.50 15.75L19.93 18.09L18.09 19.93L15.75 18.50L13.94 19.24L13.31 21.91L10.69 21.91L10.06 19.24L8.25 18.50L5.91 19.93L4.07 18.09L5.50 15.75L4.76 13.94L2.09 13.31L2.09 10.69L4.76 10.06L5.50 8.25L4.07 5.91L5.91 4.07L8.25 5.50ZM15.2 12a3.2 3.2 0 1 1-6.4 0 3.2 3.2 0 1 1 6.4 0Z',
  board: 'M3 4h18v16H3ZM3 8h18M8 8v12m8-12v12M3 14h18',
  parameters: 'M4 6h16M4 12h16M4 18h16M8 3v6m8 0v6m-6 0v6',
  harmony: 'm12 2 9 10-9 10L3 12Zm0 5 5 5-5 5-5-5Z',
  library: 'M9 17V5l11-2v12M9 8l11-2M9 17c0 4-6 5-6 2 0-2 4-4 6-2Zm11-2c0 4-6 5-6 2 0-2 4-4 6-2Z',
  backups: 'M7 3v16m-4-4 4 4 4-4M17 21V5m-4 4 4-4 4 4',
  diagnostics: 'M2 12h4l3-7 5 14 3-7h5',
  help: 'M9 8c0-4 7-4 7 0 0 3-4 3-4 6m0 3v2M5 3h14l3 3v12l-3 3H5l-3-3V6Z',
  expand: 'M14 4h6v6M20 4l-6 6M10 20H4v-6M4 20l6-6',
  info: 'M12 10v8m0-13v2M5 2h14l3 3v14l-3 3H5l-3-3V5Z',
  check: 'm5 12 5 5L20 6',
  warning: 'M12 3 2 21h20ZM12 9v5m0 3v2',
  arrowRight: 'M3 12h17m-6-6 6 6-6 6',
  download: 'M12 2v13m-5-5 5 5 5-5M3 16v5h18v-5',
  upload: 'M12 16V3m-5 5 5-5 5 5M3 16v5h18v-5',
  chevronDown: 'm5 8 7 7 7-7',
});
export const UI_ICON_NAMES = Object.freeze(['menu', ...Object.keys(UI_ICON_PATHS)]);

export function createUIIcon(name, {document: doc = globalThis.document} = {}) {
  if (!UI_ICON_NAMES.includes(name)) throw new RangeError(`Unknown interface icon: ${name}`);
  const make = tag => doc.createElementNS ? doc.createElementNS('http://www.w3.org/2000/svg', tag) : doc.createElement(tag);
  const svg = make('svg');
  svg.setAttribute('viewBox', name === 'menu' ? '0 0 28 24' : '0 0 24 24');
  svg.setAttribute('class', 'hud-glyph'); svg.setAttribute('data-ui-icon', name);
  svg.setAttribute('aria-hidden', 'true'); svg.setAttribute('focusable', 'false');
  const paths = name === 'menu' ? [0, 5, 10] : [null];
  for (const offset of paths) {
    const path = make('path');
    path.setAttribute('d', name === 'menu' ? 'M2 9h5l2-2h10l2-2h5' : UI_ICON_PATHS[name]);
    if (offset !== null) path.setAttribute('transform', `translate(0 ${offset})`);
    path.setAttribute('fill', 'none'); path.setAttribute('stroke', 'currentColor');
    path.setAttribute('stroke-width', '1.7'); path.setAttribute('stroke-linecap', 'butt');
    path.setAttribute('stroke-linejoin', 'miter'); svg.append(path);
  }
  return svg;
}

/** Optional decorative triangles for an icon frame. No listeners or timers. */
export function createUIIconCorners({document: doc = globalThis.document} = {}) {
  const node = doc.createElement('span'); node.className = 'hud-icon-corners';
  node.setAttribute('aria-hidden', 'true'); return node;
}

/** One width-aware perimeter for idle, hover, selected and pressed states.
 * All coordinates stay inside the element, including a three-pixel leading stroke.
 * Called only when a component is created or resized, never by device polling.
 */
export function uiOutlineGeometry(width, height, {kind = 'action', origin = 'top'} = {}) {
  if (![width,height].every(value => Number.isFinite(value) && value >= 4)) return null;
  if (!['action','choice','icon','pattern','notice'].includes(kind) || !['top','bottom','corner','left'].includes(origin)) throw new RangeError('Unknown outline variant.');
  const w = Math.floor(width * 10) / 10, h = Math.floor(height * 10) / 10;
  const inset = 1.5;
  const cut = Math.min(kind === 'action' ? 6 : 8, (w-2*inset)/3, (h-2*inset)/3);
  const point = ([x,y]) => `${Number(x.toFixed(2))} ${Number(y.toFixed(2))}`;
  const closed = points => `M${point(points[0])}${points.slice(1).map(p=>`L${point(p)}`).join('')}Z`;
  const length = points => points.reduce((sum,p,i)=>sum+Math.hypot(points[(i+1)%points.length][0]-p[0],points[(i+1)%points.length][1]-p[1]),0);
  if (kind === 'notice') {
    const c = Math.min(8,(w-3)/6,(h-3)/4), depth = Math.min(4,(h-3)/4,(w-3)/16);
    const start = Math.max(w*.3,inset+c), end = w-start, right = w-inset, bottom = h-inset;
    const points = [[inset,h/2],[inset,inset+c],[inset+c,inset],[start,inset],
      [start+depth,inset+depth],[end-depth,inset+depth],[end,inset],[right-c,inset],
      [right,inset+c],[right,bottom-c],[right-c,bottom],[end,bottom],
      [end-depth,bottom-depth],[start+depth,bottom-depth],[start,bottom],
      [inset+c,bottom],[inset,bottom-c]];
    const outline = closed(points);
    return {outline,idle:outline,fraction:Math.min(.25,36/length(points))};
  }
  if (kind === 'pattern') {
    const rail = inset => {
      const c=Math.min(8,(w-2*inset)/4,(h-2*inset)/4),right=w-inset,bottom=h-inset;
      return `M${inset} ${h*.32}V${inset+c}L${inset+c} ${inset}H${right-c}L${right} ${inset+c}V${h*.32}M${right} ${h*.68}V${bottom-c}L${right-c} ${bottom}H${inset+c}L${inset} ${bottom-c}V${h*.68}`;
    };
    return {outline:rail(1),idle:rail(Math.min(6,(Math.min(w,h)-2)/4)),fraction:1};
  }
  if (kind === 'icon') {
    const gap = Math.min(8, (w-3)/3, (h-3)/3);
    const outline = `M${inset} ${h-inset-gap}V${cut+inset}L${cut+inset} ${inset}H${w-inset-gap}M${w-inset} ${inset+gap}V${h-cut-inset}L${w-cut-inset} ${h-inset}H${inset+gap}`;
    return {outline, idle: outline, fraction: 1};
  }
  const points = [[inset,inset],[w-cut-inset,inset],[w-inset,cut+inset],[w-inset,h-inset],[cut+inset,h-inset],[inset,h-cut-inset]];
  const start = origin === 'bottom' ? 3 : origin === 'corner' ? 1 : origin === 'left' ? 5 : 0;
  const ordered = [...points.slice(start),...points.slice(0,start)];
  const outline = closed(ordered);
  const perimeter = length(points);
  return {outline, idle: outline, fraction: Math.min(.25, (origin === 'corner' ? 28 : 24) / perimeter)};
}

/** Upgrade native controls without owning actions, focus or musical state.
 * One observer for size changes and one for added/removed controls. Telemetry
 * text changes never measure layout; disconnected controls are unobserved.
 */
export function initializeUIComponents({root = globalThis.document?.body} = {}) {
  const doc = root?.ownerDocument, view = doc?.defaultView;
  if (!root || !doc || !view?.ResizeObserver || !view?.MutationObserver) return {refresh(){},destroy(){}};
  const selector = '.hud-action,.button,.hud-choice,.hud-icon-button,.hud-pattern-frame,.hud-notice--banner';
  const records = new Map(); let disposed = false;
  const make = (tag, attributes) => {
    const node = doc.createElementNS('http://www.w3.org/2000/svg',tag);
    for (const [key,value] of Object.entries(attributes)) node.setAttribute(key,String(value));
    return node;
  };
  const draw = (node, rect) => {
    const record = records.get(node); if (!record || !node.isConnected) return;
    const kind = node.classList.contains('hud-notice--banner') ? 'notice' : node.classList.contains('hud-pattern-frame') ? 'pattern' : node.classList.contains('hud-icon-button') ? 'icon' : node.classList.contains('hud-choice') ? 'choice' : 'action';
    const origin = node.dataset.traceOrigin || (kind === 'choice' ? 'corner' : 'top');
    const key = `${rect.width.toFixed(1)}:${rect.height.toFixed(1)}:${kind}:${origin}`;
    if (record.key === key) return;
    const geometry = uiOutlineGeometry(rect.width,rect.height,{kind,origin}); if (!geometry) return;
    record.key = key; record.svg.setAttribute('viewBox',`0 0 ${rect.width} ${rect.height}`);
    for (const [index,path] of record.paths.entries()) path.setAttribute('d',index===1?geometry.idle:geometry.outline);
    record.paths[1].setAttribute('stroke-dasharray',`${geometry.fraction} 1`);
    node.dataset.traceReady = 'true';
  };
  const sizes = new view.ResizeObserver(entries => {
    for (const entry of entries) draw(entry.target,entry.target.getBoundingClientRect());
  });
  const upgrade = node => {
    if (!node.matches?.(selector) || node.closest('svg') || node.matches('.pad,.top-touch,.navigation-touch,.diamond-touch')) return;
    if (records.has(node) && node.contains(records.get(node).svg)) return;
    const svg = make('svg',{'class':'hud-outline','aria-hidden':'true',focusable:'false',preserveAspectRatio:'none'});
    const paths = ['base','idle','trace'].map(role => make('path',{'class':`hud-outline-${role}`,pathLength:1,fill:'none','vector-effect':'non-scaling-stroke'}));
    svg.append(...paths); node.append(svg);
    if (!node.classList.contains('hud-notice--banner')) node.classList.add('hud-traced','hud-motion-trace');
    records.set(node,{svg,paths,key:''}); sizes.observe(node);
  };
  const scan = node => {
    if (node.nodeType !== 1) return;
    for (const target of [...(node.matches('.hud-motion-dither')?[node]:[]),...node.querySelectorAll('.hud-motion-dither')]) {
      if (![...target.children].some(child=>child.classList.contains('hud-dither-grid'))) {
        const layer=doc.createElement('span');layer.className='hud-dither-grid';layer.setAttribute('aria-hidden','true');target.append(layer);
      }
    }
    upgrade(node); for (const item of node.querySelectorAll(selector)) upgrade(item);
  };
  const changes = new view.MutationObserver(mutations => {
    let removedElements=false;
    for (const mutation of mutations) {
      // A changed button label can replace its decorative SVG. Reattach once.
      if (mutation.type === 'childList' && mutation.target.matches?.(selector)) upgrade(mutation.target);
      for (const node of mutation.addedNodes || []) scan(node);
      if ([...(mutation.removedNodes||[])].some(node=>node.nodeType===1)) removedElements=true;
      if (mutation.type === 'attributes') { const record = records.get(mutation.target); if (record) {record.key='';draw(mutation.target,mutation.target.getBoundingClientRect());} }
    }
    if(removedElements)for (const node of records.keys()) if (!root.contains(node)) { sizes.unobserve(node);records.delete(node); }
  });
  scan(root); changes.observe(root,{childList:true,subtree:true,attributes:true,attributeFilter:['data-trace-origin']});
  const visibility = () => { doc.documentElement.dataset.uiMotion=doc.hidden?'paused':'running'; };
  visibility();doc.addEventListener('visibilitychange',visibility);
  const destroy = () => { if (disposed) return;disposed=true;changes.disconnect();sizes.disconnect();records.clear();view.removeEventListener('pagehide',leaving);doc.removeEventListener('visibilitychange',visibility); };
  const leaving = event => { if (!event.persisted) destroy(); }; view.addEventListener('pagehide',leaving);
  return {refresh(){if(!disposed)scan(root);},destroy};
}
