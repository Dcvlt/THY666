/* Read-only graphical telemetry. No engine, MIDI, serial or timers. */
export function initializeCockpit({ mount } = {}) {
  if (!mount?.ownerDocument) throw new TypeError('Cockpit requires a DOM mount.');
  const doc = mount.ownerDocument, hadClass = mount.classList.contains('cockpit'), hadActive = mount.classList.contains('cockpit-active');
  const instrument = mount.querySelector('.instrument');
  let disposed = false, state = 'offline', session = 0;
  const values = new Map(), pressurePaths = [], pressureNumbers = [], contactNodes = [], pitchNodes = [];
  const el = (tag, className = '', text) => {
    const node = doc.createElement(tag); if (className) node.className = className;
    if (text !== undefined) node.textContent = String(text); return node;
  };
  const svg = (tag, attributes = {}) => {
    const node = doc.createElementNS ? doc.createElementNS('http://www.w3.org/2000/svg', tag) : doc.createElement(tag);
    for (const [key, value] of Object.entries(attributes)) node.setAttribute(key, String(value)); return node;
  };
  const field = (id, text = '—', className = '') => {
    const node = el('span', `cockpit-value ${className}`, text); node.dataset.cockpitValue = id; values.set(id, node); return node;
  };
  const setText = (id, text) => {
    const node = values.get(id), value = String(text);
    if (node && node.textContent !== value) node.textContent = value;
  };
  const number = (value, low, high) => typeof value === 'number' && Number.isFinite(value) && value >= low && value <= high ? value : null;
  const integer = (value, low, high) => Number.isInteger(value) ? number(value, low, high) : null;
  const setAttribute = (node, key, value) => { if (node.getAttribute?.(key) !== String(value)) node.setAttribute(key, String(value)); };
  const panel = (index, title, className) => {
    const node = el('section', `cockpit-panel ${className}`), head = el('div', 'cockpit-panel-head');
    head.append(el('span', 'cockpit-panel-index', index), el('h2', '', title), el('span', 'cockpit-head-mark', '⌁'));
    node.append(head); return node;
  };
  const reading = (label, id, className = '') => {
    const row = el('div', `cockpit-reading ${className}`); row.append(el('dt', '', label), field(id)); return row;
  };

  mount.classList.add('cockpit'); mount.dataset.cockpitState = 'offline'; mount.dataset.cockpitSession = '0';
  const deck = el('div', 'cockpit-deck');
  if (instrument) { instrument.before(deck); deck.append(instrument); } else mount.append(deck);
  const frame = el('div', 'cockpit-deck-frame'); frame.setAttribute('aria-hidden', 'true');
  const drawing = svg('svg', { viewBox: '0 0 1440 410', preserveAspectRatio: 'none', fill: 'none' });
  drawing.append(svg('path', { class: 'cockpit-frame-outline', d: 'M2 103V23L23 2H301L318 16H1122L1139 2H1417L1438 23V103M2 307V387L23 408H270L288 394H1152L1170 408H1417L1438 387V307' }));
  drawing.append(svg('path', { class: 'cockpit-frame-inner', d: 'M10 91V31L31 10H284M1156 10H1409L1430 31V91M10 319V379L31 400H252M1188 400H1409L1430 379V319' }));
  drawing.append(svg('path', { class: 'cockpit-frame-rich cockpit-circuit', d: 'M30 18H192L205 31H252M1410 18H1248L1235 31H1188M30 392H172L185 379H244M1410 392H1268L1255 379H1196M4 115V154L13 163V247L4 256V295M1436 115V154L1427 163V247L1436 256V295' }));
  for (let n = 0; n < 52; n++) {
    const x = 344 + n * 14.75, height = n % 4 ? 4 : 8;
    drawing.append(svg('path', { class: 'cockpit-frame-rich cockpit-ruler', d: `M${x} 5v${height}M${x} 405v-${height}` }));
  }
  for (const x of [21, 1419]) for (const y of [44, 366])
    drawing.append(svg('path', { class: 'cockpit-rivet', d: `M${x-3} ${y}h6M${x} ${y-3}v6` }));
  for (const x of [5, 1431]) for (let n = 0; n < 7; n++)
    drawing.append(svg('rect', { class: 'cockpit-frame-rich cockpit-rail-segment', x, y: 176 + n * 8, width: 4, height: 4 }));
  frame.append(drawing); deck.append(frame);
  const deckLabel = el('div', 'cockpit-deck-label'); deckLabel.append(el('span', '', 'THY666'), el('span', '', 'INSTRUMENT SURFACE'));
  deck.append(deckLabel);

  const hud = el('div', 'cockpit-hud'); hud.hidden = true; hud.setAttribute('aria-label', 'Instrument telemetry');
  const sourceBar = el('div', 'cockpit-source-bar'), source = field('source', 'OFFLINE', 'cockpit-source');
  source.setAttribute('role', 'status'); source.setAttribute('aria-live', 'polite');
  sourceBar.append(source, el('span', 'cockpit-signal-rule'), field('scale'), el('span', 'cockpit-signal-rule'), el('span', 'cockpit-signal-label', 'MUSICAL TELEMETRY'));
  hud.append(sourceBar);
  const panels = el('div', 'cockpit-panels');

  const contacts = panel('01', 'CONTACT MATRIX', 'cockpit-contacts');
  const map = svg('svg', { class: 'cockpit-contact-map', viewBox: '0 0 304 70', 'aria-hidden': 'true' });
  for (const [side, x] of [['L', 0], ['R', 164]]) {
    const label = svg('text', { x, y: 9, class: 'cockpit-axis-label' }); label.textContent = side; map.append(label);
  }
  for (let logical = 0; logical < 96; logical++) {
    const local = logical % 48, row = local % 4, column = Math.floor(local / 4), x = (logical < 48 ? 0 : 164) + column * 11.5;
    const cell = svg('rect', { x, y: 19 + row * 11.5, width: 8, height: 7, class: 'cockpit-contact-cell' });
    cell.dataset.cockpitPad = String(logical); contactNodes.push(cell); map.append(cell);
  }
  const contactStats = el('dl', 'cockpit-contact-stats'); contactStats.append(reading('HELD PADS', 'pads'), reading('SOUNDING', 'notes'));
  const inspection = el('div', 'cockpit-inspection'); inspection.append(el('span', '', 'INSPECT'), field('inspect'));
  contacts.append(map, contactStats, inspection);

  const pressure = panel('02', 'PRESSURE VECTOR', 'cockpit-pressure');
  const pressurePair = el('div', 'cockpit-pressure-pair');
  for (const [index, label] of ['L', 'R'].entries()) {
    const wrap = el('div', 'cockpit-pressure-gauge'), gauge = svg('svg', { viewBox: '0 0 100 93', 'aria-hidden': 'true' });
    gauge.append(svg('path', { d: 'M24.544 70.456A36 36 0 1 1 75.456 70.456', class: 'cockpit-gauge-track', pathLength: 127 }));
    const path = svg('path', { d: 'M24.544 70.456A36 36 0 1 1 75.456 70.456', class: 'cockpit-gauge-fill', pathLength: 127, 'stroke-dasharray': 127, 'stroke-dashoffset': 127 });
    gauge.append(path); pressurePaths.push(path);
    gauge.append(svg('circle', { cx: 50, cy: 45, r: 25, class: 'cockpit-gauge-inner' }));
    const side = svg('text', { x: 50, y: 24, class: 'cockpit-gauge-side' }); side.textContent = label;
    const value = svg('text', { x: 50, y: 53, class: 'cockpit-gauge-number' }); value.textContent = '—'; pressureNumbers.push(value);
    gauge.append(side, value);
    wrap.append(gauge, field(index ? 'pressure-right' : 'pressure-left')); pressurePair.append(wrap);
  }
  const express = el('div', 'cockpit-expression'); express.append(el('span', '', 'EXPRESSION'), field('expression'));
  pressure.append(pressurePair, express);

  const harmony = panel('03', 'HARMONIC FIELD', 'cockpit-harmony');
  const harmonicBody = el('div', 'cockpit-harmonic-body'), circle = svg('svg', { class: 'cockpit-harmonic-ring', viewBox: '0 0 128 128', 'aria-hidden': 'true' });
  circle.append(svg('circle', { cx: 64, cy: 64, r: 44, class: 'cockpit-harmonic-guide' }), svg('circle', { cx: 64, cy: 64, r: 33, class: 'cockpit-harmonic-inner' }));
  const polygon = svg('polyline', { class: 'cockpit-harmonic-link', points: '' }); circle.append(polygon);
  const pitchNames = ['C','C♯','D','E♭','E','F','F♯','G','A♭','A','B♭','B'], pitchPositions = [];
  for (let n = 0; n < 12; n++) {
    const pitch = (n * 7) % 12, angle = (n * 30 - 90) * Math.PI / 180;
    const x = 64 + Math.cos(angle) * 44, y = 64 + Math.sin(angle) * 44;
    const node = svg('circle', { cx: x, cy: y, r: 2.7, class: 'cockpit-pitch-node' });
    node.dataset.cockpitPitch = String(pitch); pitchNodes[pitch] = node; pitchPositions[pitch] = `${x.toFixed(2)},${y.toFixed(2)}`;
    const label = svg('text', { x: 64 + Math.cos(angle) * 57, y: 66.4 + Math.sin(angle) * 57, class: 'cockpit-pitch-label' });
    label.textContent = pitchNames[pitch]; circle.append(node, label);
  }
  const center = svg('path', { class: 'cockpit-harmonic-cross', d: 'M59 64h10M64 59v10' }); circle.append(center);
  const chord = el('div', 'cockpit-chord'); chord.append(el('span', 'cockpit-field-caption', 'PLAYED MATERIAL'), field('harmony'), el('span', 'cockpit-field-caption', 'ROOT'), field('root'));
  harmonicBody.append(circle, chord); harmony.append(harmonicBody);

  const clock = panel('04', 'CLOCK / ENGINES', 'cockpit-clock');
  const tempo = el('div', 'cockpit-tempo'); tempo.append(field('tempo'), el('span', '', 'BPM')); clock.append(tempo);
  const clockStats = el('dl', 'cockpit-clock-stats'); clockStats.append(reading('SOURCE', 'clock-source'), reading('SYNC', 'clock-status'), reading('OLED AGE', 'oled-age')); clock.append(clockStats);
  const modes = el('div', 'cockpit-modes'), flags = [];
  for (const [key, label] of [['mpe','MPE'],['arp1','ARP 1'],['arp2','ARP 2'],['running','PLAY'],['recording','REC']]) {
    const chip = el('span', 'cockpit-mode', label); chip.dataset.cockpitFlag = key; modes.append(chip); flags.push([key, label, chip]);
  }
  clock.append(modes); panels.append(contacts, pressure, harmony, clock);
  const context=el('div','cockpit-context-panels');
  const mpePanel=panel('05','MPE EXPRESSION','cockpit-context'),arpPanel=panel('06','ARPEGGIATORS','cockpit-context'),seqPanel=panel('07','SEQUENCER','cockpit-context'),harmonyPanel=panel('08','HARMONY','cockpit-context');
  mpePanel.append(reading('OUTPUT','mpe-output'),reading('AFTERTOUCH L / R','mpe-stage'),el('p','cockpit-context-note','Per-note channels. Pressure gauges above show each half’s peak.'));
  arpPanel.append(reading('ENABLED','arp-engines'),reading('NOTE POOL','arp-pool'),reading('SOUNDING','arp-sounding'));
  seqPanel.append(reading('TRACK','seq-track'),reading('TRANSPORT','seq-state'),reading('CAPTURE','seq-capture'));
  harmonyPanel.append(reading('HOME','harmony-home'),reading('PLAYED','harmony-played'),el('p','cockpit-context-note','Open Harmony to refine the map and explore suggestions.'));
  context.append(mpePanel,arpPanel,seqPanel,harmonyPanel);hud.append(panels,context); mount.append(hud);

  function update(snapshot, { connected = false, preview = false } = {}) {
    if (disposed) return null;
    const next = connected ? 'device' : preview ? 'preview' : 'offline';
    if (next !== state) {
      state = next; mount.dataset.cockpitState = state;
      if (state !== 'offline') { session++; mount.dataset.cockpitSession = String(session); }
    }
    const active = state !== 'offline'; hud.hidden = !active; mount.classList.toggle('cockpit-active', active);
    setText('source', state === 'device' ? 'LIVE DEVICE' : state === 'preview' ? 'DESIGN MODE / BROWSER ENGINE' : 'OFFLINE');
    if (!active) return { state, session };
    const data = snapshot && typeof snapshot === 'object' ? snapshot : {};
    mpePanel.hidden=data.mpe!==true;arpPanel.hidden=!(data.arp1||data.arp2);seqPanel.hidden=!(data.engine===2||data.sequenceOpen||data.running||data.recording);harmonyPanel.hidden=data.engine!==6;
    setText('mpe-output','Per-note expression');setText('mpe-stage',Array.isArray(data.aftertouchStage)?data.aftertouchStage.map(v=>v?'On':'Off').join(' / '):'—');
    setText('arp-engines',[data.arp1?'Arp 1':'',data.arp2?'Arp 2':''].filter(Boolean).join(' + '));
    setText('arp-pool',Array.isArray(data.pool)?data.pool.length:'—');setText('arp-sounding',Array.isArray(data.sounding)?data.sounding.length:'—');
    setText('seq-track',Number.isInteger(data.selectedTrack)&&data.selectedTrack>=0&&data.selectedTrack<21?`T${data.selectedTrack+1}`:'—');
    setText('seq-state',data.running?'Playing':'Stopped');setText('seq-capture',['Off','Armed','Recording','Overdub'][data.capture]||'—');
    setText('harmony-home',data.scale||'—');setText('harmony-played',data.harmony||'—');
    const padCount = integer(data.heldPads, 0, 96), notes = integer(data.noteCount, 0, 128), bpm = number(data.tempo, 1, 1000);
    setText('pads', padCount ?? '—'); setText('notes', notes ?? '—'); setText('tempo', bpm === null ? '—' : bpm.toFixed(1));
    setText('scale', typeof data.scale === 'string' && data.scale ? data.scale : '—');
    setText('harmony', typeof data.harmony === 'string' && data.harmony ? data.harmony : '—');
    const root = integer(data.root, 0, 11); setText('root', root === null ? '—' : pitchNames[root]);
    setText('clock-source', ['INTERNAL','USB'][data.clockSource] || '—');
    setText('clock-status', ['INTERNAL','WAITING','LOCKED','LOST'][data.clockStatus] || '—');
    const now = integer(data.now, 0, 0xffffffff), frameMs = integer(data.frameMs, 1, 0xffffffff);
    const age = now !== null && frameMs !== null ? (now - frameMs) >>> 0 : null;
    setText('oled-age', age !== null && age < 0x80000000 ? `${age} ms` : '—');
    const selected = integer(data.inspect?.logical, 0, 95);
    setText('inspect', selected === null ? '—' : `${selected < 48 ? 'LEFT' : 'RIGHT'} R${selected % 4 + 1} C${Math.floor(selected % 48 / 4) + 1}`);
    for (let n = 0; n < 2; n++) {
      const value = integer(data.pressure?.[n], 0, 127);
      setText(n ? 'pressure-right' : 'pressure-left', value === null ? '—' : `${value} / 127`);
      if (pressureNumbers[n].textContent !== String(value ?? '—')) pressureNumbers[n].textContent = String(value ?? '—');
      setAttribute(pressurePaths[n], 'stroke-dashoffset', 127 - (value ?? 0));
    }
    const stage = Array.isArray(data.aftertouchStage) ? data.aftertouchStage : null;
    setText('expression', data.mpe === true ? 'MPE / PER VOICE' : data.mpe === false ? (stage?.some(Boolean) ? 'MIDI / UPPER STAGE' : 'MIDI / PRESSURE') : '—');
    for (const [key, label, chip] of flags) {
      chip.classList.toggle('is-on', data[key] === true); chip.classList.toggle('is-unknown', typeof data[key] !== 'boolean');
      setAttribute(chip, 'aria-label', `${label}: ${typeof data[key] === 'boolean' ? data[key] ? 'on' : 'off' : 'unavailable'}`);
    }
    const held = Array.isArray(data.held) ? data.held : [];
    for (let n = 0; n < 96; n++) {
      contactNodes[n].classList.toggle('is-held', held[n] === true); contactNodes[n].classList.toggle('is-inspected', n === selected);
    }
    const pitches = list => new Set((Array.isArray(list) ? list : []).filter(value => integer(value, 0, 127) !== null).map(value => value % 12));
    const sounding = pitches(data.sounding), pool = pitches(data.pool);
    for (let n = 0; n < 12; n++) {
      pitchNodes[n].classList.toggle('is-pool', pool.has(n)); pitchNodes[n].classList.toggle('is-sounding', sounding.has(n));
      pitchNodes[n].classList.toggle('is-root', root === n);
    }
    const ordered = [...sounding].sort((a, b) => (a * 7) % 12 - (b * 7) % 12);
    const points = ordered.map(value => pitchPositions[value]); if (points.length > 2) points.push(points[0]);
    setAttribute(polygon, 'points', points.length > 1 ? points.join(' ') : '');
    return { state, session };
  }
  return {
    update,
    destroy() {
      if (disposed) return; disposed = true;
      if (instrument) deck.before(instrument); deck.remove(); hud.remove();
      if (!hadClass) mount.classList.remove('cockpit');
      mount.classList.toggle('cockpit-active', hadActive);
      delete mount.dataset.cockpitState; delete mount.dataset.cockpitSession;
    },
  };
}
/** Read-only diagnostics: bounded history, explicit units, no transport or timer. */
export function initializeDiagnostics(mount) {
  const doc=mount.ownerDocument,el=(tag,cls='',text)=>{const n=doc.createElement(tag);n.className=cls;if(text!==undefined)n.textContent=String(text);return n;};
  const root=el('section','diagnostics-dashboard'),head=el('div','diagnostics-heading');
  const freshness=el('p','diagnostics-freshness','Waiting for a device snapshot');freshness.setAttribute('role','status');
  head.append(el('h2','','◈  Device health'),freshness);
  const resources=el('div','diagnostics-resources'),meters=[];
  for(const [key,label,max,description] of [['heldPads','Held pads',96,'Physical pad activity'],['generated','Generated notes',128,'Shared Arp / sequencer owner pool']]){
    const card=el('section','diagnostics-resource'),value=el('strong','','—'),bar=el('meter');bar.min=0;bar.max=max;bar.value=0;bar.setAttribute('aria-label',label);
    card.append(el('h3','',label),value,bar,el('p','',description));resources.append(card);meters.push({key,max,value,bar});
  }
  const unavailable=el('details','diagnostics-unavailable'),resourceNote=el('p','','Read Activity to check firmware resource reporting.');
  unavailable.append(el('summary','','About resources'),resourceNote);
  const targetResources=el('div','diagnostics-resources'),targetMeters=[];
  for(const [key,label,detail] of [['cpu','CPU active','Includes polling; 100% does not mean overload.'],['ram','Static RAM','Stack reservation is separate; no dynamic heap.'],['ccm','CCM RAM','CPU-only memory.'],['stack','Observed stack peak','Since boot; not a worst-case guarantee.'],['flash','Firmware flash','Application partition; separate from saved data.']]){
    const card=el('section','diagnostics-resource'),value=el('strong','','—'),bar=el('meter');bar.min=0;bar.value=0;bar.hidden=true;bar.setAttribute('aria-label',label);
    card.append(el('h3','',label),value,bar,el('p','',detail));targetResources.append(card);targetMeters.push({key,value,bar});
  }
  const resourceAge=el('p','diagnostics-freshness','Resources · read Activity to update');
  targetResources.hidden=true;

  const report=el('section','diagnostics-report'),reportTitle=el('h3','','Report summary'),metrics=el('dl','diagnostics-metrics');
  const graph=doc.createElementNS('http://www.w3.org/2000/svg','svg');graph.setAttribute('viewBox','0 0 600 100');graph.setAttribute('preserveAspectRatio','none');graph.setAttribute('role','img');graph.setAttribute('aria-label','No report samples');graph.setAttribute('class','diagnostics-trend');
  const line=doc.createElementNS('http://www.w3.org/2000/svg','polyline');line.setAttribute('fill','none');line.setAttribute('stroke','currentColor');line.setAttribute('stroke-width','2');graph.append(line);
  const caption=el('p','diagnostics-chart-caption','Read a report or start a live session.');report.append(reportTitle,metrics,graph,caption);
  root.append(head,resources,targetResources,resourceAge,unavailable,report);mount.append(root);
  let history=[],lastKind='',connected=false,previousIO=null;
  const fields={status:[['uptime_ms','Uptime','ms'],['errors','Error flags',''],['voices','Sounding voices',''],['presses','Presses',''],['releases','Releases',''],['loop_us','Mean loop','µs'],['loop_peak_us','Peak loop (last window)','µs']],io:[['din_tx','DIN transmitted','bytes'],['din_rate','DIN output','bytes/s'],['din_accept','DIN accepted','messages'],['usb_accepted','USB accepted','messages'],['din_pending','DIN backlog','events'],['usb_pending','USB backlog','events'],['din_errors','DIN errors',''],['usb_dropped','USB dropped / suppressed',''],['blocked','Blocked pads','']],settings:[['generation','Saved generation',''],['saves','Saves',''],['errors','Save errors','']],display:[['last_render_us','Render time','µs'],['last_duration_ms','Transfer time','ms'],['dirty_bytes','Pending display data','bytes'],['failed_chunks','Failed transfers','']]};
  const trend={status:['voices','Sounding voices',''],io:['din_rate','DIN output','bytes/s'],settings:['saves','Save count',''],display:['last_render_us','Render time','µs']};
  return {
    snapshot(data,isConnected){
      connected=Boolean(isConnected);freshness.textContent=connected?'Device snapshot · updated '+new Date().toLocaleTimeString():'Disconnected';
      for(const m of meters){const n=data?.[m.key],valid=connected&&Number.isInteger(n)&&n>=0&&n<=m.max;m.value.textContent=valid?`${n} / ${m.max}`:'—';m.bar.hidden=!valid;m.bar.value=valid?n:0;}
      if(!connected){history=[];lastKind='';previousIO=null;targetResources.hidden=true;resourceAge.textContent='Resources · disconnected';metrics.replaceChildren();line.setAttribute('points','');graph.setAttribute('aria-label','No report samples');caption.textContent='Connect to read device health.';}
    },
    report(kind,text){
      if(!connected||!fields[kind]||typeof text!=='string'||text.length>8192||!text.startsWith(kind.toUpperCase()+' '))return false;
      const values=new Map();for(const m of text.matchAll(/(?:^|\s)([a-z_]+)=(\d+)(?=\s|$)/g)){const n=Number(m[2]);if(Number.isSafeInteger(n))values.set(m[1],n);}
      if(kind==='status'){
        const supported=values.get('resources')===1;
        targetResources.hidden=!supported;
        resourceNote.textContent=supported?'CPU active includes the continuous polling loop and interrupts. Use loop timing and MIDI backlogs to assess responsiveness. RAM is fixed allocation; stack peak is observed since boot. Saved-state usage remains under System → Storage.':'This firmware does not expose resources. Update firmware to enable CPU timing, RAM, stack and application-flash reporting.';
        resourceAge.textContent=supported?'Resources · sampled at device uptime '+(values.get('resource_ms')??'—')+' ms':'Resources unavailable on this firmware';
        for(const m of targetMeters){
          const total=m.key==='cpu'?100:values.get(m.key+'_total');
          const used=m.key==='cpu'?values.get('cpu_active'):values.get(m.key+'_used');
          const valid=supported&&Number.isInteger(total)&&total>0&&Number.isInteger(used)&&used>=0&&used<=total&&
            (m.key!=='cpu'||values.get('timing_valid')===1)&&(m.key!=='stack'||values.get('stack_valid')===1);
          m.bar.hidden=!valid;m.bar.max=total||1;m.bar.value=valid?used:0;
          const reserve=m.key==='ram'?values.get('stack_total')||0:0;
          m.value.textContent=!valid?'Measuring…':m.key==='cpu'?used+'%':`${(used/1024).toFixed(1)} / ${(total/1024).toFixed(0)} KiB · ${(Math.max(0,total-used-reserve)/1024).toFixed(1)} KiB free`;
        }
      }
      if(kind==='status'&&values.get('resources')===1&&values.get('timing_valid')!==1){values.delete('loop_us');values.delete('loop_peak_us');}
      if(kind==='io'){
        const now=values.get('uptime_ms')??Date.now(),sent=values.get('din_tx');
        if(previousIO&&now>previousIO.time&&Number.isInteger(sent)&&sent>=previousIO.sent)
          values.set('din_rate',Math.round((sent-previousIO.sent)*1000/(now-previousIO.time)));
        previousIO=Number.isInteger(sent)?{time:now,sent}:null;
      }else previousIO=null;
      reportTitle.textContent={status:'Activity',io:'MIDI & pad scanning',settings:'Saved settings',display:'Display timing'}[kind];metrics.replaceChildren();
      for(const [key,label,unit] of fields[kind]){metrics.append(el('dt','',label),el('dd','',values.has(key)?`${values.get(key)} ${unit}`.trim():'Unavailable'));}
      if(lastKind!==kind)history=[];lastKind=kind;
      const [key,label,unit]=trend[kind],n=values.get(key);if(n!==undefined){history.push(n);if(history.length>60)history.shift();}else history=[];
      const max=Math.max(1,...history);line.setAttribute('points',history.map((v,i)=>`${10+i*580/59},${90-v/max*80}`).join(' '));
      graph.setAttribute('aria-label',`${label}: ${n??'unavailable'} ${unit}; ${history.length} samples; scale 0 to ${max}`);
      caption.textContent=history.length?`${label} · last ${history.length} samples · scale 0–${max} ${unit} · ${new Date().toLocaleTimeString()}`:kind==='io'?'DIN activity appears after two reports. Backlog counts messages waiting, not messages sent.':'This report has no trend measurement.';
      return true;
    },
    reset(){history=[];lastKind='';previousIO=null;line.setAttribute('points','');graph.setAttribute('aria-label','No report samples');caption.textContent='Session started · waiting for reports.';},
    destroy(){history=[];root.remove();}
  };
}
