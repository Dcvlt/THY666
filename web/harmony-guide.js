/* SPDX-License-Identifier: GPL-3.0-or-later
 * Advisory drafts and presentation only. All harmonic analysis is shared C.
 * No transport, sound, settings writes or device connection capabilities. */
export function initializeHarmonyGuide(mount, guidance) {
  const doc=mount.ownerDocument;
  const noteNames=['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'];
  const notes=mask=>noteNames.filter((_,n)=>mask&(1<<n)).join(' · ')||'None';
  const el=(tag,text='',className='')=>{const n=doc.createElement(tag);n.textContent=text;n.className=className;return n;};
  const button=(text,action)=>{const n=el('button',text,'button');n.type='button';n.addEventListener('click',action);return n;};
  const select=(label,options,value,onChange)=>{
    const field=el('label',label,'harmony-field'),input=el('select');input.setAttribute('aria-label',label);
    for(const [id,text] of options){const option=el('option',text);option.value=String(id);input.append(option);}
    input.value=String(value);input.addEventListener('change',()=>onChange(Number(input.value)));field.append(input);return {field,input};
  };
  let sourceRoot=0,sourceFamily=0,targetRoot=7,targetFamily=0,selected=0x091;
  mount.classList.add('harmony-guide');
  const head=el('div','','companion-page-head'),titles=el('div');
  titles.append(el('p','EXPLORE THE MUSIC','eyebrow'),el('h1','Harmony'));head.append(titles);
  const intro=el('p','Explore chords, discover scales and compare keys. These are local musical sketches: nothing plays, connects or changes your board.','harmony-intro');
  const source=el('section','','harmony-card'),sourceControls=el('div','','harmony-controls');
  source.append(el('h2','Your working key'),sourceControls);
  const roots=noteNames.map((name,id)=>[id,name]),families=guidance.families.map(({id,name})=>[id,name]);
  const rootPicker=select('Key root',roots,sourceRoot,v=>{sourceRoot=v;render();});
  const familyPicker=select('Key scale',families,sourceFamily,v=>{sourceFamily=v;render();});
  sourceControls.append(rootPicker.field,familyPicker.field);
  const keyNotes=el('p','','harmony-notes');source.append(keyNotes);
  const input=el('section','','harmony-card'),pitchButtons=el('div','','harmony-pitches');
  input.append(el('h2','Notes to explore'),el('p','Start with the C major example, or select your own notes. Octaves and enharmonic spellings are combined.'),pitchButtons);
  const toggles=noteNames.map((name,n)=>{
    const b=button(name,()=>{selected^=1<<n;render();});b.setAttribute('aria-label',`Include ${name}`);pitchButtons.append(b);return b;
  });
  const current=el('p','','harmony-notes');current.setAttribute('aria-live','polite');input.append(current,button('Clear notes',()=>{selected=0;render();}));
  const columns=el('div','','harmony-panels'),tabs=el('div','','harmony-tabs');
  let active='chords';const tabButtons=new Map();
  const chords=el('section','','harmony-card'),chordRows=el('div','','harmony-results');
  chords.append(el('h2','Where next?'),el('p','In-key chord options ranked by shared tones. Tonic and fifth-root options break ties. Select a card to explore its notes; this is not a prescribed progression.'),chordRows);
  const discovery=el('section','','harmony-card'),scaleInfo=el('p'),scaleRows=el('div','','harmony-results');
  discovery.append(el('h2','Scale discovery'),el('p',`${guidance.families.length} scale families in every root. All matches contain every selected note; fewer extra notes rank first. Matches cannot establish your intended key.`),scaleInfo,scaleRows);
  columns.append(chords,discovery);
  const modulation=el('section','','harmony-card'),targetControls=el('div','','harmony-controls'),comparison=el('div','','harmony-comparison');
  modulation.append(el('h2','Modulation guidance'),el('p','Compare a destination with your working key. Shared tones and pivot chords suggest a route; phrasing and resolution establish the new tonal center.'),targetControls,comparison);
  const targetRootPicker=select('Destination root',roots,targetRoot,v=>{targetRoot=v;render();});
  const targetFamilyPicker=select('Destination scale',families,targetFamily,v=>{targetFamily=v;render();});
  targetControls.append(targetRootPicker.field,targetFamilyPicker.field);
  modulation.append(button('Use destination as working key',()=>{
    sourceRoot=targetRoot;sourceFamily=targetFamily;rootPicker.input.value=String(sourceRoot);familyPicker.input.value=String(sourceFamily);render();
  }),el('p','Changes this local sketch only. Use Parameters separately when you choose to change the board.','harmony-caption'));
  const status=el('p','','harmony-caption');status.setAttribute('role','status');status.setAttribute('aria-live','polite');
  columns.append(modulation);
  const inputGrid=el('div','','harmony-input-grid');inputGrid.append(source,input);
  const panels={chords,discovery,modulation};
  for(const [id,label] of [['chords','Chord suggestions'],['discovery','Scale discovery'],['modulation','Modulation']]) {
    const tab=button(label,()=>{active=id;showPanel();});
    tab.setAttribute('aria-controls',`harmony-${id}`);panels[id].id=`harmony-${id}`;
    tabButtons.set(id,tab);tabs.append(tab);
  }
  function showPanel(){for(const [id,panel] of Object.entries(panels)){panel.hidden=id!==active;tabButtons.get(id).setAttribute('aria-pressed',String(id===active));}}
  mount.append(head,intro,inputGrid,tabs,columns,status);
  function render(){
    showPanel();
    const key=guidance.scale(sourceFamily,sourceRoot),target=guidance.scale(targetFamily,targetRoot);
    keyNotes.textContent=notes(key);
    toggles.forEach((b,n)=>b.setAttribute('aria-pressed',String(Boolean(selected&(1<<n)))));
    current.textContent=selected?`${guidance.label(selected)} · ${notes(selected)}`:'Select notes to discover scales.';
    chordRows.replaceChildren();
    const suggestions=guidance.chords(key,sourceRoot,selected);
    for(const chord of suggestions){
      const b=button('',()=>{selected=chord.mask;render();toggles[0].focus();});b.classList.add('harmony-result');
      b.append(el('strong',guidance.label(chord.mask,chord.root)),el('span',notes(chord.mask)),el('small',`${chord.shared} shared tone${chord.shared===1?'':'s'}`));chordRows.append(b);
    }
    if(!suggestions.length)chordRows.append(el('p','No other supported chord fits this key. Try another working scale.'));
    scaleRows.replaceChildren();
    if(selected){
      const {matches,total}=guidance.discover(selected);scaleInfo.textContent=`Showing ${matches.length} of ${total} matches, ranked by fewest extra notes. Mode names with the same notes remain distinct.`;
      for(const match of matches){
        const b=button('',()=>{targetRoot=match.root;targetFamily=match.family;targetRootPicker.input.value=String(targetRoot);targetFamilyPicker.input.value=String(targetFamily);active='modulation';render();targetRootPicker.input.focus();});b.classList.add('harmony-result');
        b.append(el('strong',`${noteNames[match.root]} ${guidance.families[match.family].name}`),el('span',`${match.extra} extra notes · Compare as destination`));scaleRows.append(b);
      }
    } else scaleInfo.textContent='Select at least one note. No key is inferred from silence.';
    const m=guidance.modulation(key,target,selected);comparison.replaceChildren();
    for(const [label,mask] of [['Shared notes',m.common],['Introduce',m.added],['Leave behind',m.removed]]){
      const cell=el('div');cell.append(el('h3',label),el('p',notes(mask)));comparison.append(cell);
    }
    const same=sourceRoot===targetRoot&&sourceFamily===targetFamily;
    comparison.append(el('p',same?'This is already your working key.':key===target?'Same pitch collection, different tonal center. Emphasize the destination root and its characteristic tones.':!selected?'Select notes to check whether your current material fits both keys.':m.currentFits?'Your selected notes fit both keys. They can connect the two collections.':'Your selected notes do not all fit both keys. Change them deliberately when moving to the destination.'));
    comparison.append(el('p',m.pivot?`Possible pivot: ${guidance.label(m.pivot,m.pivotRoot)} (${notes(m.pivot)}). Try it between the two tonal centers.`:'No shared major, minor or diminished triad. Consider a common-note transition or a deliberate direct change.'));
    status.textContent=`Local sketch · ${noteNames[sourceRoot]} ${guidance.families[sourceFamily].name} → ${noteNames[targetRoot]} ${guidance.families[targetFamily].name}`;
  }
  render();return {refresh:render};
}
