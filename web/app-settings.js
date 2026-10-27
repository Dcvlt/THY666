/* Browser preferences only. No instrument or connection is accepted here. */
export const APP_THEMES=Object.freeze([
  {id:'red',label:'Red'},
  {id:'orange',label:'Orange'},
  {id:'amber',label:'Amber'},
  {id:'yellow',label:'Yellow'},
  {id:'lime',label:'Light green'},
  {id:'green',label:'Green'},
  {id:'teal',label:'Teal'},
  {id:'cyan',label:'Cyan'},
  {id:'blue',label:'Blue'},
  {id:'violet',label:'Violet'},
  {id:'purple',label:'Purple'},
  {id:'pink',label:'Pink'},
  {id:'mono',label:'Monochrome'},
]);
export const APP_THEME_KEY='thy666.companion.theme.v1';
export const APP_APPEARANCE_KEY='thy666.companion.appearance.v1';
export const APP_APPEARANCES=Object.freeze([{id:'dark',label:'Dark'},{id:'light',label:'Light'}]);
export const DEFAULT_POLL_INTERVAL_MS=100;
export const APP_POLL_KEY='thy666.companion.pollMs.v1';
export const APP_POLL_INTERVALS=Object.freeze([
  {ms:50,label:'Fast · 50 ms (up to 20 Hz)'},
  {ms:100,label:'Balanced · 100 ms (up to 10 Hz)'},
  {ms:250,label:'Light · 250 ms (up to 4 Hz)'},
  {ms:500,label:'Slow · 500 ms (up to 2 Hz)'},
  {ms:1000,label:'Minimal · 1000 ms (up to 1 Hz)'},
]);
export function initializeAppSettings({mount=document.body,storage,onOpen=()=>{},onPollIntervalChange=()=>{}}={}){
  const doc=mount.ownerDocument||document;
  const make=(tag,className,text)=>{const node=doc.createElement(tag);node.className=className;if(text!==undefined)node.textContent=text;return node;};
  if(storage===undefined){try{storage=globalThis.localStorage;}catch{storage=null;}}
  let theme='red',appearance='dark',overlay=null,returnFocus=null,accentPicker=null,disposed=false,pollIntervalMs=DEFAULT_POLL_INTERVAL_MS;
  try{const saved=storage?.getItem(APP_THEME_KEY);if(APP_THEMES.some(item=>item.id===saved))theme=saved;}catch{/* Private/file previews still work. */}
  try{const saved=storage?.getItem(APP_APPEARANCE_KEY);if(APP_APPEARANCES.some(item=>item.id===saved))appearance=saved;}catch{/* Dark is available without storage. */}
  try{const saved=Number(storage?.getItem(APP_POLL_KEY));if(APP_POLL_INTERVALS.some(item=>item.ms===saved))pollIntervalMs=saved;}catch{/* Use the healthy default without storage. */}
  function apply(value,persist=true){if(disposed||!APP_THEMES.some(item=>item.id===value))return false;theme=value;doc.body.dataset.theme=theme;if(persist)try{storage?.setItem(APP_THEME_KEY,theme);}catch{}return true;}
  function applyAppearance(value,persist=true){if(disposed||!APP_APPEARANCES.some(item=>item.id===value))return false;appearance=value;doc.body.dataset.appearance=appearance;if(persist)try{storage?.setItem(APP_APPEARANCE_KEY,appearance);}catch{}return true;}
  function applyPollInterval(value){
    if(disposed||value===pollIntervalMs||!APP_POLL_INTERVALS.some(item=>item.ms===value))return false;
    pollIntervalMs=value;try{storage?.setItem(APP_POLL_KEY,String(value));}catch{}
    onPollIntervalChange(value);return true;
  }
  apply(theme,false);applyAppearance(appearance,false);
  const toggle=make('button','app-settings-toggle','⚙');toggle.type='button';toggle.setAttribute('aria-label','App settings');toggle.setAttribute('title','App settings');toggle.setAttribute('aria-haspopup','dialog');
  mount.append(toggle);
  function close(){if(!overlay)return;accentPicker?.close(false);accentPicker=null;overlay.remove();overlay=null;returnFocus?.focus?.();returnFocus=null;}
  function makeAccentPicker(){
    const root=make('div','app-accent-picker'),trigger=make('button','app-accent-trigger'),list=make('div','app-accent-options');
    trigger.type='button';trigger.dataset.appSetting='accent';trigger.setAttribute('aria-label','Accent color');trigger.setAttribute('aria-haspopup','listbox');
    trigger.setAttribute('aria-expanded','false');trigger.setAttribute('aria-controls','app-accent-options');trigger.setAttribute('aria-describedby','app-accent-current');
    list.id='app-accent-options';list.hidden=true;list.setAttribute('role','listbox');list.setAttribute('aria-label','Accent color');
    const swatch=make('span','theme-swatch'),selected=make('span','app-accent-label');swatch.setAttribute('aria-hidden','true');selected.id='app-accent-current';trigger.append(swatch,selected);
    let active=APP_THEMES.findIndex(item=>item.id===theme);
    const options=APP_THEMES.map(item=>{
      const option=make('button','app-accent-option');option.type='button';option.tabIndex=-1;option.dataset.accent=item.id;option.setAttribute('role','option');
      const color=make('span','theme-swatch');color.dataset.accent=item.id;color.setAttribute('aria-hidden','true');
      option.append(color,make('span','app-accent-label',item.label));option.addEventListener('click',()=>choose(item.id));list.append(option);return option;
    });
    function update(){
      trigger.value=theme;swatch.dataset.accent=theme;selected.textContent=APP_THEMES.find(item=>item.id===theme).label;
      for(const option of options)option.setAttribute('aria-selected',String(option.dataset.accent===theme));
    }
    function focus(index){active=Math.max(0,Math.min(options.length-1,index));options[active].focus();options[active].scrollIntoView?.({block:'nearest'});}
    function closeList(restoreFocus=true){list.hidden=true;trigger.setAttribute('aria-expanded','false');if(restoreFocus)trigger.focus();}
    function openList(index=APP_THEMES.findIndex(item=>item.id===theme)){
      if(disposed||!overlay||accentPicker?.root!==root)return;
      list.hidden=false;trigger.setAttribute('aria-expanded','true');focus(index);
    }
    function choose(value){
      if(disposed||!overlay||accentPicker?.root!==root||!apply(value))return;
      update();closeList();
    }
    trigger.addEventListener('click',()=>{if(!list.hidden)closeList();else openList();});root.append(trigger,list);update();
    return {root,trigger,close:closeList,get expanded(){return !list.hidden;},
      navigate(key){
        if(key==='Home'){if(list.hidden)openList(0);else focus(0);}
        else if(key==='End'){if(list.hidden)openList(options.length-1);else focus(options.length-1);}
        else if(list.hidden)openList();
        else focus(active+(key==='ArrowDown'?1:-1));
      },activate(){if(list.hidden)openList();else choose(APP_THEMES[active].id);}};
  }
  function open(){
    if(disposed||overlay)return;onOpen();returnFocus=doc.activeElement||toggle;
    overlay=make('div','app-settings-overlay');const card=make('section','app-settings-dialog');
    card.setAttribute('role','dialog');card.setAttribute('aria-modal','true');card.setAttribute('aria-labelledby','app-settings-title');
    const tag=make('p','eyebrow','LOCAL / APPLICATION'),title=make('h2','','App settings');title.id='app-settings-title';
    const copy=make('p','','Appearance, accent color and live refresh are saved locally on this computer. They apply throughout the Companion and Documentation; musical pad colors and the instrument display stay unchanged.');
    const appearanceLabel=make('label','app-appearance-field','Appearance'),appearanceSelect=make('select','');appearanceSelect.dataset.appSetting='appearance';appearanceSelect.setAttribute('aria-label','Appearance');
    for(const item of APP_APPEARANCES){const option=make('option','',item.label);option.value=item.id;appearanceSelect.append(option);}appearanceSelect.value=appearance;
    appearanceSelect.addEventListener('change',()=>{if(!applyAppearance(appearanceSelect.value))appearanceSelect.value=appearance;});appearanceLabel.append(appearanceSelect);
    const label=make('div','app-theme-field');label.append(make('span','app-field-label','Accent color'));accentPicker=makeAccentPicker();label.append(accentPicker.root);
    const refreshLabel=make('label','app-theme-field app-poll-field','Live refresh'),refresh=make('select','');
    refresh.dataset.appSetting='poll-interval';refresh.setAttribute('aria-label','Live refresh');
    for(const item of APP_POLL_INTERVALS){const option=make('option','',item.label);option.value=String(item.ms);refresh.append(option);}refresh.value=String(pollIntervalMs);
    refresh.addEventListener('change',()=>{if(!applyPollInterval(Number(refresh.value)))refresh.value=String(pollIntervalMs);});refreshLabel.append(refresh);
    const refreshCopy=make('p','app-cockpit-note','Actual refresh follows transfer speed; busy connections and hidden tabs slow down automatically.');
    const done=make('button','button','Done');done.type='button';done.addEventListener('click',close);
    card.append(tag,title,copy,appearanceLabel,label,refreshLabel,refreshCopy,done);overlay.append(card);mount.append(overlay);appearanceSelect.focus();
  }
  const key=event=>{
    if(!overlay)return;
    // This listener runs during capture so instrument keys cannot see the
    // dialog. Handle the composite picker here before stopping propagation.
    if(event.key==='Escape'&&accentPicker?.expanded){event.preventDefault();event.stopPropagation();accentPicker.close();return;}
    if(accentPicker?.root.contains(event.target||doc.activeElement)){
      if(['ArrowDown','ArrowUp','Home','End'].includes(event.key)){event.preventDefault();event.stopPropagation();accentPicker.navigate(event.key);return;}
      if(['Enter',' ','Spacebar'].includes(event.key)){event.preventDefault();event.stopPropagation();accentPicker.activate();return;}
    }
    if(event.key==='Escape'){event.preventDefault();event.stopPropagation();close();return;}
    if(event.key==='Tab'){
      if(accentPicker?.expanded)accentPicker.close();
      const fields=[...overlay.querySelectorAll('select,input,button')].filter(node=>{
        if(node.disabled||(node.tabIndex??0)<0)return false;
        for(let parent=node;parent;parent=parent.parentNode)if(parent.hidden)return false;
        return true;
      }),first=fields[0],last=fields[fields.length-1];
      if(event.shiftKey&&(doc.activeElement===first||!overlay.contains(doc.activeElement))){event.preventDefault();last.focus();}
      else if(!event.shiftKey&&(doc.activeElement===last||!overlay.contains(doc.activeElement))){event.preventDefault();first.focus();}
    }
    event.stopPropagation();
  };
  const outside=event=>{if(accentPicker?.expanded&&!accentPicker.root.contains(event.target))accentPicker.close(false);};
  toggle.addEventListener('click',open);doc.addEventListener('keydown',key,true);doc.addEventListener('click',outside,true);
  return {open,close,get theme(){return theme;},get appearance(){return appearance;},get pollIntervalMs(){return pollIntervalMs;},get modalOpen(){return Boolean(overlay);},
    setConnected(){/* Browser preferences are independent of connection state. */},
    destroy(){disposed=true;close();toggle.remove();doc.removeEventListener('keydown',key,true);doc.removeEventListener('click',outside,true);}};
}
