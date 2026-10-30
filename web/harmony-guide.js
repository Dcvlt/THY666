/* SPDX-License-Identifier: GPL-3.0-or-later
 * Device projection only: ranking, maps, note ownership and key changes stay in C. */
export function initializeHarmonyGuide(mount,{families=[],action=async()=>{},control=async()=>{},set=async()=>{},scaleLabel=v=>`Scale ${v+1}`}={}) {
  const doc=mount.ownerDocument,names=['C','C♯','D','E♭','E','F','F♯','G','A♭','A','B♭','B'];
  const notes=mask=>names.filter((_,n)=>mask&(1<<n)).join(' · ')||'—';
  const el=(tag,text='',cls='')=>{const n=doc.createElement(tag);n.textContent=text;n.className=cls;return n;};
  let live=null,connected=false,supported=false,writable=false,controlsReady=false,busy=false;
  const buttons=[],settings=[];
  const button=(label,code,value=0)=>{
    const b=el('button',label,'button hud-action');b.type='button';
    if(typeof code==='string')b.dataset.setting=code;
    if(typeof code==='function')b.dataset.harmonyControl='true';
    b.addEventListener('click',()=>void invoke(code,typeof value==='function'?value():value));buttons.push(b);return b;
  };
  const picker=(label,options,id,key,maximum)=>{
    const field=el('label',label,'harmony-field'),input=el(options?'select':'input','','hud-field');input.setAttribute('aria-label',label);
    if(options)for(const [value,text] of options){const option=el('option',text);option.value=String(value);input.append(option);}
    else {input.type='number';input.min=0;input.max=maximum;input.step=1;}
    if(typeof id==='string')input.dataset.setting=id;
    input.addEventListener('change',()=>void invoke(id,Number(input.value)));field.append(input);settings.push({input,key});return field;
  };
  mount.classList.add('harmony-guide');mount.dataset.view='1';
  const head=el('div','','companion-page-head'),title=el('div'),enter=button('Enter Harmony',0,()=>live?.active?0:1);
  title.append(el('p','HARMONIC WORKSPACE','eyebrow'),el('h1','Harmony'));head.append(title,enter);
  const state=el('p','','harmony-caption');state.setAttribute('role','status');
  const content=el('div','','harmony-content'),intro=el('p','Play a chord to explore what could come next.','harmony-intro');
  const workspace=el('div','','harmony-live-workspace'),overview=el('section','','harmony-card hud-frame'),map=el('section','','harmony-card hud-frame');
  const key=el('p','','harmony-key'),current=el('h2'),tones=el('p','','harmony-notes'),next=el('p'),reason=el('p'),recent=el('p','','harmony-caption'),preference=el('p','','harmony-caption');
  const ring=el('div','','harmony-fifths');ring.setAttribute('role','img');const ringNotes=[];
  for(let n=0;n<12;n++){const pitch=n*7%12,node=el('span',names[pitch],'harmony-fifth');node.style.setProperty('--fifth',n);ring.append(node);ringNotes.push({pitch,node});}
  const capture=el('div','','harmony-tabs'),pin=button('Pin input',9,()=>live?.pinned?0:1);
  capture.append(button('Capture selected pad',7),button('Capture held chord',8),pin,button('Clear input',6));
  enter.classList.add('hud-action--primary');
  const captures=el('details','','harmony-capture hud-disclosure');captures.append(el('summary','Capture & pin'),capture);
  overview.append(key,current,tones,ring,next,reason,recent,preference,captures);
  const halves=el('div','','harmony-mini-halves'),pads=[],halfLabels=[];
  map.append(el('h2','Playing surface'),el('p','Outlined: current · Filled: selected · Underlined: suggested. All chord pads remain playable.'),halves);
  for(let side=0;side<2;side++){
    const half=el('div'),label=el('p','','harmony-caption'),grid=el('div','','harmony-live-pads');half.append(label,grid);halfLabels.push(label);halves.append(half);
    for(let n=0;n<48;n++){const cell=el('span');cell.setAttribute('aria-label',`${side?'RIGHT':'LEFT'} row ${n%4+1}, pad ${Math.floor(n/4)+1}`);cell.style.setProperty('grid-column',Math.floor(n/4)+1);cell.style.setProperty('grid-row',n%4+1);grid.append(cell);pads.push(cell);}
  }
  workspace.append(overview,map);
  const options=el('section','','harmony-card harmony-options hud-frame hud-frame--task'),grid=el('div','','harmony-controls'),playing=el('div','','harmony-controls'),appearance=el('div','','harmony-controls'),toggle=[[0,'Off'],[1,'On']];
  grid.append(
    picker('Direction',[[0,'Balanced'],[1,'Resolve'],[2,'Colour'],[3,'Travel']],'harmony.direction','direction'),
    picker('Outside key · %',null,'harmony.outside','outside',100),
    picker('Stay in key',toggle,'harmony.stayInKey','stayInKey'),
    picker('Lock map',toggle,'harmony.lockMap','lockMap'),
    picker('Chord bank',[[0,'Adaptive'],[1,'Major / minor / 7 / maj7'],[2,'m7 / sus2 / sus4 / dim'],[3,'dim7 / half-dim / aug / 6'],[4,'m6 / maj9 / 9 / m9']],'harmony.palette','palette'));
  playing.append(
    picker('Note surface',[[0,'Chromatic'],[1,'Chord tones']],'harmony.rightMode','rightMode'),
    picker('Melody input',[[0,'Live additions'],[1,'Melody only'],[2,'Manual capture']],'harmony.analysis','analysis'),
    picker('Voicing',[[0,'Inversions'],[1,'Drop voices'],[2,'Auto leading'],[3,'Octave fold'],[4,'Guitar register']],'harmony.voicing','voicing'),
    picker('Leading bass',[[0,'Smooth'],[1,'Root bass']],'harmony.bass','bassRoot'),
    picker('Register low · MIDI',null,'harmony.low','lowNote',127),picker('Register high · MIDI',null,'harmony.high','highNote',127));
  appearance.append(
    picker('Pad hints',toggle,'harmony.highlights','highlights'),picker('Hint contrast · %',null,'harmony.contrast','hintContrast',100),picker('Motion',toggle,'harmony.motion','motion'));
  const fine=el('details','','harmony-fine hud-disclosure');fine.append(el('summary','Pad feedback & motion'),appearance);
  options.append(el('h2','Shape the exploration'),el('p','Direction chooses the musical intent. Stay in key restricts suggestions; turn it off to explore outside the key.'),grid,el('h3','Playing & voicing'),playing,fine);
  const shortcuts=el('details','','harmony-card harmony-shortcuts hud-frame hud-disclosure'),bank=button('',()=>control(3,0)),mode=button('',()=>control(3,1)),target=button('',()=>control(3,11));
  const top=el('div','','harmony-tabs'),lower=el('div','','harmony-tabs'),scales=el('div','','harmony-tabs'),roots=el('div','','harmony-tabs');
  top.append(bank,mode,button('Octave −',()=>control(3,9)),button('Octave +',()=>control(3,10)),target);
  const lowerButtons=[],scaleButtons=[];
  for(let n=0;n<7;n++){const l=button('',()=>control(1,n+7)),s=button('',()=>control(3,n+2));lower.append(l);scales.append(s);lowerButtons.push(l);scaleButtons.push(s);}
  for(let n=0;n<12;n++)roots.append(button(names[n],()=>control(2,n)));
  shortcuts.append(el('summary','Physical shortcuts'),top,el('h3','Lower controls'),lower,el('h3','Scale preview · M1–M7'),scales,el('h3','Root preview'),roots);
  const tabs=el('div','','harmony-tabs');for(const [id,label] of [[0,'Chord suggestions'],[1,'Scale discovery'],[2,'Key comparison']])tabs.append(button(label,1,id));
  const results=el('section','','harmony-card hud-frame'),resultTitle=el('h2'),table=el('table','','harmony-live-table'),tableHead=el('thead'),body=el('tbody');table.append(tableHead,body);results.append(resultTitle,table);
  const destination=el('section','','harmony-card hud-frame'),destinationControls=el('div','','harmony-controls'),comparison=el('p'),apply=button('Apply destination key',5,1);
  apply.classList.add('hud-action--primary');
  destinationControls.append(picker('Destination root',names.map((v,n)=>[n,v]),3,'targetRoot'),picker('Destination scale',families.map(({id,name})=>[id,name]),4,'targetFamily'));
  destination.append(el('h2','Compare a key'),destinationControls,comparison,apply,el('p','Only Apply changes the home key.'));
  const feedback=el('p','','harmony-feedback hud-notice');feedback.setAttribute('role','status');
  const error=el('p');error.setAttribute('role','alert');content.append(intro,workspace,tabs,results,destination,options,shortcuts);mount.append(head,state,feedback,content,error);
  async function invoke(code,value){
    if(!connected||!supported||!writable||busy||typeof code!=='string'&&code!==0&&!live?.active||typeof code==='function'&&!controlsReady)return;
    busy=true;error.textContent='';feedback.textContent='Applying…';render();
    try{if(typeof code==='function')await code();else if(typeof code==='string')await set(code,value);else await action(code,value);feedback.textContent='Change sent · values follow the board.';}catch(e){feedback.textContent='';error.textContent=e.message;}finally{busy=false;render();}
  }
  function render(){
    const available=connected&&supported,ready=available&&live,canAct=ready&&live.active&&writable&&!busy;
    state.textContent=!connected?'Connect a board to open Harmony.':!supported?'This firmware does not expose live Harmony v4. Update to use this page.':!live?'Reading Harmony…':!writable?'Monitoring only: the Client and firmware menus differ.':live.active?`Harmony active · ${live.clusterSwapped?'RIGHT chords / LEFT notes':'LEFT chords / RIGHT notes'}`:'Enter Harmony to explore chords.';
    content.hidden=!ready;enter.textContent=live?.active?'Return to Play':'Enter Harmony';
    for(const b of buttons)b.disabled=!ready||!writable||busy||!b.dataset.setting&&!live.active||Boolean(b.dataset.harmonyControl)&&!controlsReady;
    enter.disabled=!available||!writable||busy;
    for(const {input} of settings)input.disabled=!ready||!writable||busy||!input.dataset.setting&&!live.active;
    intro.hidden=Boolean(live?.inputMask);
    if(!ready)return;
    [...tabs.children].forEach((tab,index)=>{tab.setAttribute('aria-pressed',String(index===live.page));tab.classList.toggle('is-selected',index===live.page);});
    for(const {input,key} of settings){if(doc.activeElement===input&&!busy)continue;const v=live[key];input.value=String(typeof v==='boolean'?Number(v):v??0);}
    key.textContent=`HOME / ${live.keyLabel}`;current.textContent=live.inputMask?live.inputLabel:'Play a chord';tones.textContent=notes(live.inputMask);
    next.textContent=live.selectedLabel?`NEXT / ${live.selectedLabel}`:'Choose a chord';reason.textContent=live.reason||'';
    recent.textContent=(live.history||[]).slice().reverse().map(h=>`${names[h.root]} (${notes(h.mask)})`).join(' → ');
    preference.textContent=`${['Balanced','Resolve','Colour','Travel'][live.direction]||'Balanced'} · ${live.lockMap?'Map locked':live.palette?'Fixed bank':'Adaptive map'} · ${live.stayInKey?'In-key only':`Outside key ${live.outside??35}%`}${live.pending?' · Updates after release':''}`;
    pin.textContent=live.pinned?'Follow playing':'Pin input';pin.setAttribute('aria-pressed',String(live.pinned));
    ring.setAttribute('aria-label',`Circle of fifths: ${live.inputLabel}; next ${live.selectedLabel||'none'}.`);
    const nextMask=live.page===0?live.rows[live.cursor]?.mask||0:0;
    for(const {pitch,node} of ringNotes){node.classList.toggle('is-input',Boolean(live.inputMask&(1<<pitch)));node.classList.toggle('is-next',Boolean(nextMask&(1<<pitch)));node.classList.toggle('is-root',pitch===live.sourceRoot);}
    halfLabels.forEach((label,side)=>label.textContent=`${side?'RIGHT':'LEFT'} / ${(side===0)!==live.clusterSwapped?'CHORDS':'NOTES'}`);
    for(let n=0;n<96;n++){const p=n%48,chord=(n<48)!==live.clusterSwapped;pads[n].dataset.role=live.physicalCurrentPads[n]?'current':live.physicalSelectedPads[n]?'selected':live.physicalSuggestedPads[n]?'suggested':'other';pads[n].textContent=chord?names[Math.floor(p/4)]:'';pads[n].title=chord?notes(live.padMasks?.[p]||0):'Note pad';}
    bank.textContent=`5 · ${live.controlBank?'Octave layers':'Suggestions'}`;mode.textContent=`12 · ${live.rightMode?'Chord tones':'Chromatic'}`;target.textContent=`U3 · ${live.octaveTarget?'Note':'Chord'} octave`;
    for(let n=0;n<7;n++){
      lowerButtons[n].textContent=live.controlBank?`${n>3?'+':''}${n-3} oct`:`Suggestion ${n+1}`;
      lowerButtons[n].setAttribute('aria-pressed',String(live.controlBank?Boolean(live.chordLayers&1<<n):live.selectedSuggestion===n));
      lowerButtons[n].disabled=!canAct||!controlsReady||!live.controlBank&&n>=live.suggestionCount;
      scaleButtons[n].textContent=`M${n+1} · ${scaleLabel(live.scaleButtons[n])}`;scaleButtons[n].disabled=!canAct||!controlsReady||live.scaleButtons[n]>=70;
    }
    results.hidden=live.page===2;destination.hidden=live.page!==2;resultTitle.textContent=live.page===1?'Scales containing the input':'Next chords';
    const heading=el('tr');(live.page===1?['Scale','Notes','Extra tones']:['Chord pad','Notes','Relationship']).forEach(t=>heading.append(el('th',t)));tableHead.replaceChildren(heading);body.replaceChildren();
    live.rows.forEach((row,index)=>{
      const tr=el('tr'),pick=el('button',live.page===1?`Compare ${names[row.root]} ${families.find(f=>f.id===row.family)?.name||'Scale'}`:`Row ${row.pad%4+1} · pad ${Math.floor(row.pad/4)+1}`,'button hud-action hud-action--compact');pick.type='button';pick.disabled=!canAct;pick.addEventListener('click',()=>void invoke(live.page===1?10:2,index));pick.setAttribute('aria-pressed',String(index===live.cursor));
      const cell=el('td');cell.append(pick);tr.className=index===live.cursor?'is-selected':'';tr.append(cell,el('td',notes(row.mask)),el('td',live.page===1?String(row.extra):['Shared tones','Next step','In home key','Outside key','Extension','Resolution','Secondary dominant','Parallel major/minor'][row.reason]));body.append(tr);
    });
    comparison.textContent=`${live.keyLabel} → ${live.targetLabel}. Shared: ${notes(live.common)}. Introduce: ${notes(live.added)}. Leave: ${notes(live.removed)}.`;apply.disabled=!canAct||!live.canApply;
  }
  let lastProjection='';
  render();return {update(snapshot,connection={}){const projection=JSON.stringify([snapshot,connection.connected,connection.supported,connection.writable,connection.controlsReady]);if(projection===lastProjection)return;lastProjection=projection;live=snapshot;connected=Boolean(connection.connected);supported=Boolean(connection.supported);writable=Boolean(connection.writable);controlsReady=Boolean(connection.controlsReady);render();},refresh:render};
}
