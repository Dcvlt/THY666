/* Read-only graphical telemetry. No engine, MIDI, serial or timers. */
export function initializeCockpit({ mount } = {}) {
  if (!mount?.ownerDocument) throw new TypeError('Cockpit requires a DOM mount.');
  const doc = mount.ownerDocument, hadClass = mount.classList.contains('cockpit'), hadActive = mount.classList.contains('cockpit-active');
  const instrument = mount.querySelector('.instrument');
  let disposed = false, state = 'offline', session = 0, hostActive = true;
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
  const setClass = (node, name, on) => { if (node.classList.contains(name) !== Boolean(on)) node.classList.toggle(name, Boolean(on)); };
  const setHidden = (node, hidden) => { if (node.hidden !== Boolean(hidden)) node.hidden = Boolean(hidden); };
  const reducedMotion = globalThis.matchMedia?.('(prefers-reduced-motion: reduce)');
  const finishAssembly = () => setClass(mount, 'cockpit-assembling', false);
  const assemblyEnd = event => {
    if (event.target === mount && event.animationName === 'cockpit-assembly-lifetime') finishAssembly();
  };
  const motionPreference = () => { if (reducedMotion?.matches) finishAssembly(); };
  mount.addEventListener('animationend', assemblyEnd);
  reducedMotion?.addEventListener?.('change', motionPreference);
  const label = value => typeof value === 'string' && value ? value.slice(0, 96) : '—';
  const axisName = value => ['Pressure', 'CC74', 'CC1'][integer(value, 0, 2)] ?? '—';
  const stageName = value => value === true ? 'On' : value === false ? 'Off' : value === -1 ? 'Mixed' : '—';
  const syncMotion = () => {
    const paused = !hostActive || Boolean(doc.hidden) || state === 'offline';
    if (paused) finishAssembly();
    setClass(mount, 'cockpit-motion-paused', paused);
    if (mount.dataset.cockpitMotion !== (paused ? 'paused' : 'running')) mount.dataset.cockpitMotion = paused ? 'paused' : 'running';
  };
  doc.addEventListener?.('visibilitychange', syncMotion);
  const panel = (index, title, className) => {
    const node = el('section', `cockpit-panel hud-frame ${className}`), head = el('div', 'cockpit-panel-head');
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
  drawing.append(svg('path', { class: 'cockpit-frame-outline', pathLength: 100, d: 'M2 103V23L23 2H301L318 16H1122L1139 2H1417L1438 23V103M2 307V387L23 408H270L288 394H1152L1170 408H1417L1438 387V307' }));
  drawing.append(svg('path', { class: 'cockpit-frame-inner', pathLength: 100, d: 'M10 91V31L31 10H284M1156 10H1409L1430 31V91M10 319V379L31 400H252M1188 400H1409L1430 379V319' }));
  drawing.append(svg('path', { class: 'cockpit-frame-rich cockpit-circuit', pathLength: 100, d: 'M30 18H192L205 31H252M1410 18H1248L1235 31H1188M30 392H172L185 379H244M1410 392H1268L1255 379H1196M4 115V154L13 163V247L4 256V295M1436 115V154L1427 163V247L1436 256V295' }));
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
    const velocity = el('div', 'cockpit-input-velocity'); velocity.append(el('span', '', 'VELOCITY'), field(index ? 'velocity-right' : 'velocity-left'));
    wrap.append(gauge, field(index ? 'pressure-right' : 'pressure-left'), velocity); pressurePair.append(wrap);
  }
  const express = el('div', 'cockpit-expression'); express.append(el('span', '', 'EXPRESSION'), field('expression'));
  pressure.append(pressurePair, express);

  const harmony = panel('03', 'HARMONIC FIELD', 'cockpit-harmony');
  const harmonicBody = el('div', 'cockpit-harmonic-body'), circle = svg('svg', { class: 'cockpit-harmonic-ring', viewBox: '0 0 128 128', 'aria-hidden': 'true' });
  circle.append(svg('circle', { cx: 64, cy: 64, r: 44, class: 'cockpit-harmonic-guide' }), svg('circle', { cx: 64, cy: 64, r: 33, class: 'cockpit-harmonic-inner cockpit-ambient-orbit' }));
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
  const harmonyDetail = el('div', 'cockpit-harmony-detail'); harmonyDetail.hidden = true;
  const transition = el('div', 'cockpit-harmony-transition');
  transition.append(field('harmony-played'), el('span', 'cockpit-transition-arrow', '→'), field('harmony-next'));
  const harmonyReason = field('harmony-reason', '—', 'cockpit-harmony-reason');
  const toneMap = svg('svg', { class: 'cockpit-tone-map', viewBox: '0 0 360 50', role: 'img', 'aria-label': 'Harmony tones unavailable' }), toneNodes = [];
  for (let n = 0; n < 12; n++) {
    const cell = svg('rect', { x: 3 + n * 30, y: 4, width: 24, height: 21, class: 'cockpit-tone-cell' });
    const text = svg('text', { x: 15 + n * 30, y: 42, class: 'cockpit-tone-name' }); text.textContent = pitchNames[n];
    toneMap.append(cell, text); toneNodes.push(cell);
  }
  const toneLegend = el('div', 'cockpit-tone-legend'); toneLegend.append(el('span', 'is-common', 'SHARED'), el('span', 'is-added', 'NEW'), el('span', 'is-removed', 'LEAVING'));
  const history = el('div', 'cockpit-harmony-history'), historyNodes = [];
  for (let n = 0; n < 4; n++) {
    const item = el('div', 'cockpit-history-item'), ring = svg('svg', { viewBox: '0 0 56 56', 'aria-hidden': 'true' }), dots = [];
    ring.append(svg('circle', { cx: 28, cy: 28, r: 18, class: 'cockpit-history-orbit' }));
    for (let pitch = 0; pitch < 12; pitch++) {
      const angle = (((pitch * 7) % 12) * 30 - 90) * Math.PI / 180;
      const dot = svg('circle', { cx: 28 + Math.cos(angle) * 18, cy: 28 + Math.sin(angle) * 18, r: 1.8, class: 'cockpit-history-tone' }); ring.append(dot); dots.push(dot);
    }
    const name = el('span', 'cockpit-history-root', '—'); item.append(ring, name); item.hidden = true; history.append(item); historyNodes.push({item, dots, name});
  }
  const harmonyFooter = el('div', 'cockpit-harmony-footer'); harmonyFooter.append(field('harmony-home'), field('harmony-status'));
  harmonyDetail.append(transition, harmonyReason, toneMap, toneLegend, history, harmonyFooter); harmony.append(harmonyDetail);

  const clock = panel('04', 'CLOCK / ENGINES', 'cockpit-clock');
  const tempo = el('div', 'cockpit-tempo'); tempo.append(field('tempo'), el('span', '', 'BPM')); clock.append(tempo);
  const clockStats = el('dl', 'cockpit-clock-stats'); clockStats.append(reading('SOURCE', 'clock-source'), reading('SYNC', 'clock-status'), reading('OLED AGE', 'oled-age')); clock.append(clockStats);
  const modes = el('div', 'cockpit-modes'), flags = [];
  for (const [key, label] of [['mpe','MPE'],['arp1','ARP 1'],['arp2','ARP 2'],['running','PLAY'],['recording','REC']]) {
    const chip = el('span', 'cockpit-mode', label); chip.dataset.cockpitFlag = key; modes.append(chip); flags.push([key, label, chip]);
  }
  clock.append(modes); panels.append(contacts, pressure, harmony, clock);
  const context = el('div', 'cockpit-context-panels');
  const mpePanel = panel('02', 'MPE / EXPRESSION ROUTING', 'cockpit-context cockpit-mpe-context');
  const arpPanel = panel('05', 'ARP / COMBINED PITCHES', 'cockpit-context cockpit-arp-context');
  const seqPanel = panel('06', 'SEQUENCER / TRANSPORT', 'cockpit-context cockpit-seq-context');
  const expressionLanes = [], pressureBars = [], velocityBars = [];
  for (let side = 0; side < 2; side++) {
    const lane = el('div', 'cockpit-expression-lane');
    const header = el('div', 'cockpit-lane-heading'); header.append(el('span', '', side ? 'RIGHT' : 'LEFT'), field(`mpe-primary-${side}`));
    const graph = svg('svg', { viewBox: '0 0 300 70', class: 'cockpit-expression-bars', 'aria-hidden': 'true' });
    for (const [row, name] of ['PRESSURE', 'VELOCITY'].entries()) {
      const y = 12 + row * 32, text = svg('text', { x: 0, y: y + 8, class: 'cockpit-axis-label' }); text.textContent = name;
      graph.append(text, svg('path', { d: `M76 ${y + 5}H296`, class: 'cockpit-input-track' }));
      const bar = svg('rect', { x: 76, y, width: 0, height: 10, class: `cockpit-input-fill${row ? ' is-velocity' : ''}` }); graph.append(bar);
      (row ? velocityBars : pressureBars).push(bar);
    }
    const footer = el('div', 'cockpit-lane-footer'); footer.append(el('span', '', 'UPPER AFTERTOUCH'), field(`mpe-upper-${side}`));
    lane.append(header, graph, footer); mpePanel.append(lane); expressionLanes.push(lane);
  }
  const expressionSummary = el('div', 'cockpit-context-footer'); expressionSummary.append(el('span', '', 'PHYSICAL INPUT PEAKS'), field('mpe-output')); mpePanel.append(expressionSummary);
  const ribbon = svg('svg', { viewBox: '0 0 448 112', class: 'cockpit-note-ribbon', role: 'img', 'aria-label': 'Combined Arp pitch pool and sounding output' }), ribbonPool = [], ribbonSounding = [], ribbonLabels = [];
  const noteName = note => `${pitchNames[note % 12]}${Math.floor(note / 12) - 1}`;
  for (const [row, text] of ['POOL', 'OUTPUT'].entries()) {
    const name = svg('text', { x: 1, y: row * 37 + 10, class: 'cockpit-axis-label' }); name.textContent = text; ribbon.append(name);
    for (let note = 0; note < 128; note++) {
      const cell = svg('rect', { x: 3 + note * 3.45, y: row * 37 + 16, width: 2.1, height: 13, class: 'cockpit-ribbon-note' });
      cell.dataset.cockpitRibbon = `${row ? 'output' : 'pool'}-${note}`; ribbon.append(cell); (row ? ribbonSounding : ribbonPool).push(cell);
    }
  }
  for (let note = 0; note < 128; note++) {
    const text = svg('text', { x: 0, y: 96, class: 'cockpit-ribbon-label', 'text-anchor': 'middle' }); text.textContent = noteName(note); ribbon.append(text); ribbonLabels.push(text);
  }
  const arpStats = el('div', 'cockpit-context-footer'); arpStats.append(field('arp-engines'), field('arp-range'), field('arp-pool'), field('arp-sounding'));
  arpPanel.append(ribbon, arpStats);
  const transport = svg('svg', { viewBox: '0 0 300 96', class: 'cockpit-transport-visual', role: 'img', 'aria-label': 'Transport unavailable' });
  const playSymbol = svg('path', { d: 'M25 23L55 42L25 61Z', class: 'cockpit-transport-play' });
  const stopSymbol = svg('path', { d: 'M27 27H55V55H27Z', class: 'cockpit-transport-stop' });
  const pauseSymbol = svg('path', { d: 'M28 26V56M48 26V56', class: 'cockpit-transport-pause' });
  transport.append(svg('path', { d: 'M7 15L20 2H280L293 15V76L280 89H20L7 76Z', class: 'cockpit-transport-frame' }), playSymbol, stopSymbol, pauseSymbol);
  const transportTrack = svg('text', { x: 93, y: 37, class: 'cockpit-transport-track' }); transportTrack.textContent = '—';
  const captureSymbol = svg('circle', { cx: 106, cy: 65, r: 6, class: 'cockpit-capture-dot' });
  const captureText = svg('text', { x: 124, y: 69, class: 'cockpit-capture-label' }); captureText.textContent = '—'; transport.append(transportTrack, captureSymbol, captureText);
  const seqStats = el('div', 'cockpit-context-footer'); seqStats.append(field('seq-track'), field('seq-state'), field('seq-capture')); seqPanel.append(transport, seqStats);
  const voiceRows=[],trackRows=[];
  const voiceHeading=el('div','cockpit-detail-heading');voiceHeading.append(el('h3','','Member voices'),field('voice-detail-status'));
  const voiceList=el('div','cockpit-voice-list hud-scroll-region');voiceList.setAttribute('role','region');voiceList.setAttribute('aria-label','MPE member voice meters');voiceList.tabIndex=0;
  for(let slot=0;slot<15;slot++){
    const row=el('div','cockpit-voice-row');row.dataset.cockpitVoice=String(slot);row.hidden=true;
    const heading=el('div','cockpit-voice-heading');heading.append(field(`voice-name-${slot}`),field(`voice-state-${slot}`));
    const graph=svg('svg',{viewBox:'0 0 300 49',class:'cockpit-voice-graph','aria-hidden':'true'}),bars=[];
    for(const [index,label] of ['PRIMARY','UPPER'].entries()){
      const y=7+index*16,name=svg('text',{x:0,y:y+5,class:'cockpit-axis-label'});name.textContent=label;
      const bar=svg('rect',{x:72,y,width:0,height:7,class:`cockpit-output-fill${index?' is-upper':''}`});
      graph.append(name,svg('path',{d:`M72 ${y+3.5}H296`,class:'cockpit-input-track'}),bar);bars.push(bar);
    }
    const bendLabel=svg('text',{x:0,y:45,class:'cockpit-axis-label'});bendLabel.textContent='BEND';
    const bend=svg('path',{d:'M184 35V47',class:'cockpit-output-bend'});graph.append(bendLabel,svg('path',{d:'M72 41H296M184 37V45',class:'cockpit-input-track'}),bend);
    const footer=el('div','cockpit-voice-footer');footer.append(field(`voice-route-${slot}`),field(`voice-values-${slot}`));row.append(heading,graph,footer);voiceList.append(row);voiceRows.push({row,bars,bend});
  }
  const voiceEmpty=el('p','cockpit-detail-empty','No member voices');
  mpePanel.append(voiceHeading,el('p','cockpit-detail-note','Controller values accepted into the board’s MIDI queue.'),voiceList,voiceEmpty);
  const trackHeading=el('div','cockpit-detail-heading');trackHeading.append(el('h3','','Track positions'),field('track-detail-status'));
  const trackList=el('div','cockpit-track-list hud-scroll-region');trackList.setAttribute('role','region');trackList.setAttribute('aria-label','Sequencer track playheads');trackList.tabIndex=0;
  for(let index=0;index<21;index++){
    const row=el('div','cockpit-track-row');row.dataset.cockpitTrack=String(index);row.hidden=true;
    const heading=el('div','cockpit-track-heading');heading.append(field(`track-name-${index}`,`T${index+1}`),field(`track-state-${index}`));
    const graph=svg('svg',{viewBox:'0 0 300 16',class:'cockpit-track-graph','aria-hidden':'true'});
    const fill=svg('rect',{x:1,y:6,width:0,height:4,class:'cockpit-playhead-fill'}),needle=svg('path',{d:'M1 1V15',class:'cockpit-playhead-needle'});
    graph.append(svg('path',{d:'M1 4V12H299V4Z',class:'cockpit-input-track'}),fill,needle);
    const footer=el('div','cockpit-track-footer');footer.append(field(`track-step-${index}`),field(`track-range-${index}`));row.append(heading,graph,footer);trackList.append(row);trackRows.push({row,fill,needle});
  }
  const trackEmpty=el('p','cockpit-detail-empty','No tracks');
  seqPanel.append(trackHeading,trackList,trackEmpty);
  const destinations=value=>[['USB 1',1],['USB 2',2],['DIN',4]].filter(([,bit])=>value&bit).map(([name])=>name).join(' / ');
  function performanceDetail(context){
    const sample=context?.available===true&&!context.stale?context.snapshot:null;
    const status=sample?'Sampled on board':context?.stale?'Waiting for fresh data':context?.supported?'Reading device detail':'Update firmware for detail';
    setText('voice-detail-status',status);setText('track-detail-status',status);
    let voices=0,tracks=0;
    for(let slot=0;slot<15;slot++){
      const v=sample?.voices?.[slot],nodes=voiceRows[slot],present=Boolean(v&&v.state);setHidden(nodes.row,!present);
      if(!present)continue;voices++;
      const state=v.pendingAttack?'Reserved':v.state===2?(v.sustained?'Sustained':'Releasing'):v.gliding?'Gliding':'Held';
      setText(`voice-name-${slot}`,`${noteName(v.note)} · Ch ${v.channel+1}`);setText(`voice-state-${slot}`,`${state}${v.pendingOutput?' · pending':''}`);
      setText(`voice-route-${slot}`,`${v.logicalPad<48?'L':'R'}${v.logicalPad%48+1} · ${destinations(v.destinations)}`);
      setText(`voice-values-${slot}`,`${axisName(v.output)} ${v.pressure} · Upper ${v.upper} · Bend ${v.bend}`);
      setAttribute(nodes.bars[0],'width',(v.pressure*224/127).toFixed(2));setAttribute(nodes.bars[1],'width',(v.upper*224/127).toFixed(2));
      const bendX=72+v.bend*224/16383;setAttribute(nodes.bend,'d',`M${bendX.toFixed(2)} 35V47`);
      setClass(nodes.row,'is-reserved',v.pendingAttack);setClass(nodes.row,'is-retiring',v.state===2);
      setAttribute(nodes.row,'aria-label',`MPE member channel ${v.channel+1}, ${noteName(v.note)}, ${state}. ${destinations(v.destinations)}; active references ${destinations(v.soundingDestinations)||'none'}. Queued ${axisName(v.output)} ${v.pressure}, upper ${v.upper}, bend ${v.bend}; target ${v.desiredPressure}, ${v.desiredUpper}, ${v.desiredBend}. Gesture ${v.gesture}.`);
    }
    for(let index=0;index<21;index++){
      const track=sample?.tracks?.[index],nodes=trackRows[index],present=Boolean(track?.created);setHidden(nodes.row,!present);if(!present)continue;tracks++;
      const state=track.running?(sample.transport===2?'Paused':sample.suspended||!sample.clockReady?(sample.clockStatus===3?'Clock lost':'Waiting for clock'):sample.transport===1?'Playing':'Stopped'):'Stopped',capture=['','Armed','Queued','Recording','Finishing','Failed'][track.capture];
      const modifiers=[track.muted?'Muted':'',!track.enabled?'Disabled':'',track.oneShot?'One shot':'',track.pending===1?'Start queued':track.pending===2?'Stop queued':'',capture].filter(Boolean);
      setText(`track-state-${index}`,[state,...modifiers].join(' · '));setClass(nodes.row,'is-selected',sample.selectedTrack===index);setClass(nodes.row,'is-muted',track.muted);setClass(nodes.row,'is-recording',track.capture===3||track.capture===4);
      setAttribute(nodes.needle,'display',track.validPosition?'inline':'none');setAttribute(nodes.fill,'display',track.validPosition?'inline':'none');
      if(track.validPosition){
        const relative=track.playhead-track.loopStart,fraction=relative/track.loopLength,x=1+fraction*298;
        setAttribute(nodes.fill,'width',(fraction*298).toFixed(2));setAttribute(nodes.needle,'d',`M${x.toFixed(2)} 1V15`);
        setText(`track-step-${index}`,`Step ${Math.floor(relative/track.gridTicks)+1} / ${Math.ceil(track.loopLength/track.gridTicks)}`);
        setText(`track-range-${index}`,`${track.playhead} / ${track.loopStart+track.loopLength} ticks`);
      }else{setText(`track-step-${index}`,'Position unavailable');setText(`track-range-${index}`,'—');}
      setAttribute(nodes.row,'aria-label',`Track ${index+1}; ${state}${modifiers.length?', '+modifiers.join(', '):''}; ${track.validPosition?`position ${track.playhead}, loop start ${track.loopStart}, length ${track.loopLength}, grid ${track.gridTicks}`:'position unavailable'}; generation ${track.generation}.`);
    }
    setHidden(voiceEmpty,voices>0||!sample);setHidden(trackEmpty,tracks>0||!sample);
  }
  panels.insertBefore(mpePanel, pressure); mpePanel.hidden = true;
  context.append(arpPanel, seqPanel); hud.append(panels, context); mount.append(hud); syncMotion();

  function update(snapshot, { connected = false, preview = false } = {}) {
    if (disposed) return null;
    const present = Boolean(snapshot && typeof snapshot === 'object' && !Array.isArray(snapshot));
    const next = present && connected ? 'device' : present && preview ? 'preview' : 'offline';
    if (next !== state) {
      state = next; mount.dataset.cockpitState = state;
      if (state !== 'offline') { session++; mount.dataset.cockpitSession = String(session); }
      setClass(mount, 'cockpit-assembling', state !== 'offline' && !reducedMotion?.matches);
    }
    const active = state !== 'offline'; setHidden(hud, !active); setClass(mount, 'cockpit-active', active); syncMotion();
    setText('source', state === 'device' ? 'LIVE DEVICE' : state === 'preview' ? 'DESIGN MODE / BROWSER ENGINE' : 'OFFLINE');
    if (!active) {performanceDetail(null);return { state, session };}
    const data = snapshot, expressionContext = data.context?.expression;
    const expression = expressionContext?.available === false ? null : expressionContext;
    const harmonyData = data.context?.harmony ?? data.harmonyContext;
    performanceDetail(data.context?.performance);
    const harmonyActive = data.engine === 6;
    // The physical board already shows contacts. Give Harmony that slot so
    // its wider explanation and the clock remain in one balanced overview.
    setHidden(contacts, harmonyActive);
    setHidden(mpePanel, data.mpe !== true); setHidden(arpPanel, !(data.arp1 === true || data.arp2 === true));
    setHidden(pressure, data.mpe === true);
    setHidden(seqPanel, !(data.engine === 2 || data.sequenceOpen === true || data.running === true || data.recording === true));
    setHidden(harmonyDetail, !harmonyActive); setClass(harmony, 'has-harmony-context', harmonyActive); setClass(panels, 'has-harmony-context', harmonyActive);
    setText('mpe-output', expressionContext?.pending === true ? 'Reading routing' : expressionContext?.available === true || expression?.mpeOutput?.some(value => integer(value, 0, 2) !== null) ? 'Per-note routing' : 'Routing unavailable');
    const stages = expression?.aftertouchStage ?? data.aftertouchStage;
    for (let n = 0; n < 2; n++) {
      const output = integer(expression?.mpeOutput?.[n], 0, 2), pressure = integer(data.pressure?.[n], 0, 127);
      const velocity = integer(data.heldVelocity?.[n], 1, 127), upper = output === null ? '—' : output === 0 ? 'CC74' : 'Pressure';
      setText(`mpe-primary-${n}`, `Pressure → ${axisName(output)}`);
      setText(`mpe-upper-${n}`, `${stageName(stages?.[n])}${stages?.[n] === true ? ` → ${upper}` : ''}`);
      setAttribute(pressureBars[n], 'width', pressure === null ? 0 : pressure * 220 / 127);
      setAttribute(velocityBars[n], 'width', velocity === null ? 0 : velocity * 220 / 127);
      setAttribute(expressionLanes[n], 'aria-label', `${n ? 'Right' : 'Left'}: pressure ${pressure ?? 'unavailable'}, velocity ${velocity ?? 'unavailable'}; primary ${axisName(output)}; upper Aftertouch ${stageName(stages?.[n])}`);
    }
    setText('arp-engines',[data.arp1?'Arp 1':'',data.arp2?'Arp 2':''].filter(Boolean).join(' + '));
    const validNotes = list => new Set((Array.isArray(list) ? list : []).filter(note => integer(note, 0, 127) !== null));
    const poolNotes = validNotes(data.pool), outputNotes = validNotes(data.sounding);
    const heldPitches = validNotes(data.heldPitches);
    const rangeNotes = [...poolNotes, ...heldPitches];
    // Derive the view from sustained inputs, never the changing sounding gate.
    const low = rangeNotes.length ? Math.max(0, Math.floor((Math.min(...rangeNotes) - 2) / 12) * 12) : 0;
    const high = rangeNotes.length ? Math.min(127, Math.ceil((Math.max(...rangeNotes) + 3) / 12) * 12 - 1) : 127;
    const cellWidth = 442 / (high - low + 1);
    let outside = 0; for (const note of outputNotes) if (note < low || note > high) outside++;
    setText('arp-range', `${noteName(low)}–${noteName(high)}`);
    setText('arp-pool', Array.isArray(data.pool) ? `${poolNotes.size} pooled` : '—');
    setText('arp-sounding', Array.isArray(data.sounding) ? `${outputNotes.size} sounding${outside ? ` · ${outside} outside range` : ''}` : '—');
    for (let n = 0; n < 128; n++) {
      setClass(ribbonPool[n], 'is-present', poolNotes.has(n)); setClass(ribbonSounding[n], 'is-present', outputNotes.has(n));
      const visible = n >= low && n <= high;
      for (const cell of [ribbonPool[n], ribbonSounding[n]]) {
        setAttribute(cell, 'display', visible ? 'inline' : 'none');
        if (visible) { setAttribute(cell, 'x', 3 + (n - low) * cellWidth); setAttribute(cell, 'width', Math.max(1, cellWidth - 2)); }
      }
      const showLabel = visible && (n % 12 === 0 || high - low < 48 && (poolNotes.has(n) || heldPitches.has(n)));
      setAttribute(ribbonLabels[n], 'display', showLabel ? 'inline' : 'none');
      if (showLabel) setAttribute(ribbonLabels[n], 'x', 3 + (n - low + .5) * cellWidth);
    }
    setAttribute(ribbon, 'aria-label', `Combined Arp pool: ${Array.isArray(data.pool) ? poolNotes.size : 'unavailable'} pitches; all sounding output: ${Array.isArray(data.sounding) ? outputNotes.size : 'unavailable'} pitches; range ${noteName(low)} to ${noteName(high)}${outside ? `; ${outside} sounding outside range` : ''}`);
    const track = integer(data.selectedTrack, 0, 20), transportState = integer(data.transport, 0, 2), capture = integer(data.capture, 0, 5);
    const trackText = track === null ? '—' : `T${track + 1}`;
    const transportText = transportState === null ? typeof data.running === 'boolean' ? data.running ? 'Playing' : 'Stopped' : '—' : ['Stopped', 'Playing', 'Paused'][transportState];
    const captureState = capture === null ? '—' : ['Off', 'Armed', 'Queued', 'Recording', 'Finishing', 'Failed'][capture];
    setText('seq-track', trackText); setText('seq-state', transportText); setText('seq-capture', captureState);
    if (transportTrack.textContent !== trackText) transportTrack.textContent = trackText;
    if (captureText.textContent !== captureState) captureText.textContent = captureState;
    setAttribute(playSymbol, 'display', transportText === 'Playing' ? 'inline' : 'none');
    setAttribute(stopSymbol, 'display', transportText === 'Stopped' ? 'inline' : 'none');
    setAttribute(pauseSymbol, 'display', transportText === 'Paused' ? 'inline' : 'none');
    setClass(captureSymbol, 'is-armed', capture === 1 || capture === 2);
    setClass(captureSymbol, 'is-recording', capture === 3 || capture === 4); setClass(captureSymbol, 'is-failed', capture === 5);
    setAttribute(transport, 'aria-label', `Selected track ${trackText}; transport ${transportText}; capture ${captureState}`);
    const padCount = integer(data.heldPads, 0, 96), notes = integer(data.noteCount, 0, 128), bpm = number(data.tempo, 1, 1000);
    setClass(mount, 'cockpit-has-input', (padCount ?? 0) > 0 || (notes ?? 0) > 0);
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
      const velocity = integer(data.heldVelocity?.[n], 1, 127);
      setText(n ? 'velocity-right' : 'velocity-left', velocity ?? '—');
    }
    const stage = Array.isArray(data.aftertouchStage) ? data.aftertouchStage : null;
    setText('expression', data.mpe === true ? 'MPE / PER VOICE' : data.mpe === false ? (stage?.some(Boolean) ? 'MIDI / UPPER STAGE' : 'MIDI / PRESSURE') : '—');
    for (const [key, label, chip] of flags) {
      setClass(chip, 'is-on', data[key] === true); setClass(chip, 'is-unknown', typeof data[key] !== 'boolean');
      setAttribute(chip, 'aria-label', `${label}: ${typeof data[key] === 'boolean' ? data[key] ? 'on' : 'off' : 'unavailable'}`);
    }
    const held = Array.isArray(data.held) ? data.held : [];
    for (let n = 0; n < 96; n++) {
      setClass(contactNodes[n], 'is-held', held[n] === true); setClass(contactNodes[n], 'is-inspected', n === selected);
    }
    const pitches = list => new Set((Array.isArray(list) ? list : []).filter(value => integer(value, 0, 127) !== null).map(value => value % 12));
    const sounding = pitches(data.sounding), pool = pitches(data.pool);
    for (let n = 0; n < 12; n++) {
      setClass(pitchNodes[n], 'is-pool', pool.has(n)); setClass(pitchNodes[n], 'is-sounding', sounding.has(n));
      setClass(pitchNodes[n], 'is-root', root === n);
    }
    const harmonicCurrent = integer(harmonyData?.inputMask, 0, 4095);
    // The extension's comparison masks describe key previews. A chord step
    // compares its actual selected row mask with the captured chord instead.
    const selectedRow = harmonyData?.page === 0 && Array.isArray(harmonyData.rows) ? harmonyData.rows[harmonyData.cursor] : null;
    const harmonicNext = integer(selectedRow?.mask, 0, 4095);
    const tonesAvailable = harmonicCurrent !== null && harmonicNext !== null;
    const shared = tonesAvailable ? harmonicCurrent & harmonicNext : 0;
    const added = tonesAvailable ? harmonicNext & ~harmonicCurrent : 0;
    const removed = tonesAvailable ? harmonicCurrent & ~harmonicNext : 0;
    setText('harmony-home', label(harmonyData?.keyLabel ?? data.scale));
    setText('harmony-played', label(harmonyData?.inputLabel ?? data.harmony));
    setText('harmony-next', label(harmonyData?.selectedLabel)); setText('harmony-reason', label(harmonyData?.reason));
    setText('harmony-status', harmonyData?.pending === true ? 'Updating choices' : harmonyData?.cacheReady === true ? 'Selected next chord' : 'Context unavailable');
    const maskNames = mask => pitchNames.filter((_, n) => mask & (1 << n)).join(' ') || 'none';
    setAttribute(toneMap, 'aria-label', tonesAvailable ? `Shared: ${maskNames(shared)}. New: ${maskNames(added)}. Leaving: ${maskNames(removed)}.` : 'Harmony tones unavailable');
    for (let n = 0; n < 12; n++) {
      setClass(toneNodes[n], 'is-common', shared & (1 << n)); setClass(toneNodes[n], 'is-added', added & (1 << n)); setClass(toneNodes[n], 'is-removed', removed & (1 << n));
      setClass(pitchNodes[n], 'is-current', harmonyActive && harmonicCurrent !== null && (harmonicCurrent & (1 << n)));
      setClass(pitchNodes[n], 'is-next', harmonyActive && harmonicNext !== null && (harmonicNext & (1 << n)));
    }
    const historyData = Array.isArray(harmonyData?.history) ? harmonyData.history.slice(0, 4) : [];
    for (let n = 0; n < 4; n++) {
      const entry = historyData[n], mask = integer(entry?.mask, 0, 4095), historyRoot = integer(entry?.root, 0, 11), item = historyNodes[n];
      setHidden(item.item, mask === null || historyRoot === null);
      const name = historyRoot === null ? '—' : pitchNames[historyRoot]; if (item.name.textContent !== name) item.name.textContent = name;
      setAttribute(item.item, 'aria-label', mask === null ? 'History unavailable' : `History chord ${n + 1}: ${maskNames(mask)}`);
      for (let pitch = 0; pitch < 12; pitch++) setClass(item.dots[pitch], 'is-present', mask !== null && (mask & (1 << pitch)));
    }
    const ordered = [...sounding].sort((a, b) => (a * 7) % 12 - (b * 7) % 12);
    const points = ordered.map(value => pitchPositions[value]); if (points.length > 2) points.push(points[0]);
    setAttribute(polygon, 'points', points.length > 1 ? points.join(' ') : '');
    return { state, session };
  }
  return {
    update,
    setActive(active) { if (!disposed) { hostActive = active === true; syncMotion(); } },
    destroy() {
      if (disposed) return; disposed = true;
      mount.removeEventListener('animationend', assemblyEnd);
      reducedMotion?.removeEventListener?.('change', motionPreference);
      finishAssembly(); mount.classList.remove('cockpit-has-input');
      doc.removeEventListener?.('visibilitychange', syncMotion);
      if (instrument) deck.before(instrument); deck.remove(); hud.remove();
      if (!hadClass) mount.classList.remove('cockpit');
      mount.classList.toggle('cockpit-active', hadActive);
      delete mount.dataset.cockpitState; delete mount.dataset.cockpitSession;
      delete mount.dataset.cockpitMotion; mount.classList.remove('cockpit-motion-paused');
    },
  };
}
/** Read-only diagnostics: bounded history, explicit units, no transport or timer. */
export function initializeDiagnostics(mount) {
  const doc=mount.ownerDocument,el=(tag,cls='',text)=>{const n=doc.createElement(tag);n.className=cls;if(text!==undefined)n.textContent=String(text);return n;};
  const root=el('section','diagnostics-dashboard'),head=el('div','diagnostics-heading');
  const freshness=el('p','diagnostics-freshness','Waiting for a device snapshot');freshness.setAttribute('role','status');
  head.append(el('h2','','Device health'),freshness);
  const resources=el('div','diagnostics-resources'),meters=[];
  for(const [key,label,max,description] of [['heldPads','Held pads',96,'Physical pad activity'],['generated','Generated notes',128,'Shared Arp / sequencer owner pool']]){
    const card=el('section','diagnostics-resource hud-frame'),value=el('strong','','—'),bar=el('meter','hud-meter');bar.min=0;bar.max=max;bar.value=0;bar.setAttribute('aria-label',label);
    card.append(el('h3','',label),value,bar,el('p','',description));resources.append(card);meters.push({key,max,value,bar});
  }
  const unavailable=el('details','diagnostics-unavailable hud-disclosure'),resourceNote=el('p','','Read Activity to check firmware resource reporting.');
  unavailable.append(el('summary','','About resources'),resourceNote);
  const targetResources=el('div','diagnostics-resources'),targetMeters=[];
  for(const [key,label,detail] of [['cpu','CPU active','Includes polling; 100% does not mean overload.'],['ram','Static RAM','Stack reservation is separate; no dynamic heap.'],['ccm','CCM RAM','CPU-only memory.'],['stack','Observed stack peak','Since boot; not a worst-case guarantee.'],['flash','Firmware flash','Application partition; separate from saved data.']]){
    const card=el('section','diagnostics-resource hud-frame'),value=el('strong','','—'),bar=el('meter','hud-meter');bar.min=0;bar.value=0;bar.hidden=true;bar.setAttribute('aria-label',label);
    card.append(el('h3','',label),value,bar,el('p','',detail));targetResources.append(card);targetMeters.push({key,value,bar});
  }
  const resourceAge=el('p','diagnostics-freshness','Resources · read Activity to update');
  targetResources.hidden=true;

  const report=el('section','diagnostics-report hud-frame'),reportTitle=el('h3','','Report summary'),metrics=el('dl','diagnostics-metrics');
  const graph=doc.createElementNS('http://www.w3.org/2000/svg','svg');graph.setAttribute('viewBox','0 0 600 100');graph.setAttribute('preserveAspectRatio','none');graph.setAttribute('role','img');graph.setAttribute('aria-label','No report samples');graph.setAttribute('class','diagnostics-trend');
  const line=doc.createElementNS('http://www.w3.org/2000/svg','polyline');line.setAttribute('fill','none');line.setAttribute('stroke','currentColor');line.setAttribute('stroke-width','2');graph.append(line);
  const caption=el('p','diagnostics-chart-caption','Read a report or start a live session.');report.append(reportTitle,metrics,graph,caption);
  root.append(head,resources,targetResources,resourceAge,unavailable,report);mount.append(root);
  let history=[],lastKind='',connected=false,previousIO=null;
  const fields={status:[['uptime_ms','Uptime','ms'],['errors','Error flags',''],['voices','Sounding voices',''],['presses','Presses',''],['releases','Releases',''],['loop_us','Mean loop','µs'],['loop_peak_us','Peak loop (last window)','µs']],io:[['din_tx','DIN transmitted','bytes'],['din_rate','DIN output','bytes/s'],['din_accept','DIN accepted','messages'],['usb_accepted','USB accepted','messages'],['din_pending','DIN backlog','events'],['usb_pending','USB backlog','events'],['din_errors','DIN errors',''],['usb_dropped','USB dropped / suppressed',''],['blocked','Blocked pads','']],settings:[['generation','Saved generation',''],['saves','Saves',''],['errors','Save errors','']],display:[['last_render_us','Render time','µs'],['last_duration_ms','Transfer time','ms'],['dirty_bytes','Pending display data','bytes'],['failed_chunks','Failed transfers','']]};
  const trend={status:['voices','Sounding voices',''],io:['din_rate','DIN output','bytes/s'],settings:['saves','Save count',''],display:['last_render_us','Render time','µs']};
  fields.status.unshift(['ready','Instrument ready','']);
  fields.settings.unshift(['status','Save state','']);
  fields.pads=[['frames','Complete scans',''],['scans','Mux samples',''],['errors','Acquisition errors','']];
  fields['backup status']=[['state','Transfer state',''],['restore_phase','Restore stage',''],['restore_active','Playing locked',''],['restore_verified','Device save verified',''],['restore_completed','Stage completed','bytes'],['restore_total','Stage total','bytes'],['received','Received','bytes'],['bytes','File size','bytes'],['generation','Saved generation',''],['payload_crc32','Payload checksum','']];
  trend.pads=['frames','Complete scans',''];trend['backup status']=['received','Received','bytes'];
  return {
    snapshot(data,isConnected){
      connected=Boolean(isConnected);freshness.textContent=connected?'Device snapshot · updated '+new Date().toLocaleTimeString():'Disconnected';
      for(const m of meters){const n=data?.[m.key],valid=connected&&Number.isInteger(n)&&n>=0&&n<=m.max;m.value.textContent=valid?`${n} / ${m.max}`:'—';m.bar.hidden=!valid;m.bar.value=valid?n:0;}
      if(!connected){history=[];lastKind='';previousIO=null;targetResources.hidden=true;resourceAge.textContent='Resources · disconnected';metrics.replaceChildren();line.setAttribute('points','');graph.setAttribute('aria-label','No report samples');caption.textContent='Connect to read device health.';}
    },
    report(kind,text){
      const prefix=kind==='backup status'?'BACKUP':typeof kind==='string'?kind.toUpperCase():'';
      if(!connected||!fields[kind]||typeof text!=='string'||text.length>8192||!text.startsWith(prefix+' '))return false;
      const values=new Map();for(const m of text.matchAll(/(?:^|\s)([a-z_]+)=(\d+)(?=\s|$)/g)){const n=Number(m[2]);if(Number.isSafeInteger(n))values.set(m[1],n);}
      const textValues=new Map();
      for(const m of text.matchAll(/(?:^|\s)(state|status|payload_crc32|restore_phase)=([a-z0-9-]+)(?=\s|$)/g))textValues.set(m[1],m[2]);
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
      reportTitle.textContent={status:'Activity',io:'MIDI & pad scanning',pads:'Pad scanner',settings:'Saved settings','backup status':'Backup service',display:'Display timing'}[kind];metrics.replaceChildren();
      for(const [key,label,unit] of fields[kind]){metrics.append(el('dt','',label),el('dd','',textValues.has(key)?textValues.get(key):values.has(key)?`${values.get(key)} ${unit}`.trim():'Unavailable'));}
      if(lastKind!==kind)history=[];lastKind=kind;
      const [key,label,unit]=trend[kind],n=values.get(key);if(n!==undefined){history.push(n);if(history.length>60)history.shift();}else history=[];
      const max=Math.max(1,...history);line.setAttribute('points',history.map((v,i)=>`${10+i*580/59},${90-v/max*80}`).join(' '));
      graph.setAttribute('aria-label',`${label}: ${n??'unavailable'} ${unit}; ${history.length} samples; scale 0 to ${max}`);
      caption.textContent=history.length?`${label} · last ${history.length} samples · scale 0–${max} ${unit} · ${new Date().toLocaleTimeString()}`:kind==='io'?'DIN activity appears after two reports. Backlog counts messages waiting, not messages sent.':'This report has no trend measurement.';
      if(kind==='backup status')caption.textContent=['applied','queued'].includes(textValues.get('state'))?
        'Settings were applied. Saving is not verified; inspect Saved settings before continuing.':
        textValues.get('restore_phase')==='error'&&values.get('restore_verified')===1?'Device save verified. The Client comparison is unfinished; keep your backup.':
        'Read-only transfer status. Reading it does not upload, restore or cancel a backup.';
      return true;
    },
    reset(){history=[];lastKind='';previousIO=null;line.setAttribute('points','');graph.setAttribute('aria-label','No report samples');caption.textContent='Session started · waiting for reports.';},
    destroy(){history=[];root.remove();}
  };
}
