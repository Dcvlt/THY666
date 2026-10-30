import { createCore } from './core.js';
import { SCALE_CATALOG } from './generated/menu-schema.js';
import { OLEDPreview } from './oled.js';
import { ROOT_KEYS, MODE_KEYS } from './controller.js';
import { initializeFirmwareStudio } from './firmware.js';
import { initializeEngineStudio } from './engine-studio.js';
import { PreviewSettings } from './preview-settings.js';
import { initializeRhythmLab } from './rhythm-lab.js';
import { initializeArpLab } from './arp-lab.js';
import { initializeSequencerLab } from './sequencer-lab.js';
import { initializeMpeLab } from './mpe-lab.js';
import { initializeLibraryLab } from './library-lab.js';
import { initializeCompanionShell } from './companion-shell.js';
import { initializeStartup } from './startup.js';
import { connectDesktopRuntime } from './desktop-runtime.js';
import { initializeDesktopChrome } from './desktop-chrome.js';
import { initializeUIComponents } from './ui-system.js';

const startup=initializeStartup();
let desktopChrome=null;
const desktopBindingsPromise=connectDesktopRuntime().then(bindings=>{
  if(bindings){document.documentElement.dataset.host='desktop';desktopChrome=initializeDesktopChrome({runtime:bindings.runtime});}
  return bindings;
});
window.addEventListener('pagehide',()=>desktopChrome?.destroy(),{once:true});

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];
const ROOT_ORDER = ['A', 'Ab', 'B', 'Bb', 'C', 'D', 'Db', 'E', 'Eb', 'F', 'G', 'Gb'];
const NOTE_NAMES = ['C', 'C♯', 'D', 'E♭', 'E', 'F', 'F♯', 'G', 'A♭', 'A', 'B♭', 'B'];
const DESTINATIONS = ['USB 1', 'USB 2', 'DIN'];
// IDs follow the shared C instrument enum; no pitch rules are duplicated here.
const MELODY_LAYOUTS = ['stored', 'chromatic', 'thirds', 'fourths', 'condensed', 'guitar', 'piano', 'in-key-lit', 'all-lit'];
const CONTEXT_LABELS = ['', 'L octave −', 'L octave +', 'Inversion', 'Bass octave', 'R octave −', 'R octave +'];
const COLORS = [
  ['#30363c', '#89959f', '#434c55'],
  ['#554b4b', '#eccec4', '#76635e'],
  ['#525143', '#e1dbad', '#73715b'],
  ['#425449', '#bed4bd', '#5f7564'],
  ['#405357', '#b9d6d8', '#59757a'],
  ['#465067', '#c6d1ed', '#626f8a'],
  ['#514862', '#d7c9e7', '#746385'],
  ['#62494d', '#e9c3ca', '#8a636b'],
  ['#5e5143', '#e9d2b9', '#84725c'],
];
const KEY_PADS = new Map();
'1234567890'.split('').forEach((key, column) => KEY_PADS.set(key, column * 4));
'qwertyuiop'.split('').forEach((key, column) => KEY_PADS.set(key, 48 + column * 4));
const KEY_LABELS = new Map([...KEY_PADS].map(([key, pad]) => [pad, key.toUpperCase()]));

let engine;
let data;
let selectedScale;
let selectedPad = 0;
let transpose = 0;
let latch = false;
let sustain = false;
let manualSustain = false;
let keyboardSustain = false;
let oledProfile;
let oledPreview;
let scenarioRunning = false;
let eventCount = 0;
let startedAt = performance.now();
let strumTimer;
let melodyLayout = 'chromatic';
const performanceSettings = { inversion: 0, spread: 0, strum: false, delay: 24, order: 0, chordOctave: 0, melodyOctave: 0, upper: 0 };
const chordEdits = new Map();
const physicalOwners = new Map();
const pointerPads = new Map();
const downKeys = new Map();
const navigationOwners = new Map();
const pointerNavigation = new Map();
const navigationKeys = new Map();
const sustainGates = new Map();
const bendTargets = new Map();
const padButtons = [];
let firmwareStudio;
let engineStudio;
let previewSettings;
let rhythmLab;
let arpLab;
let sequencerLab;
let mpeLab;
let libraryLab;
let companionShell;
let firmwareScale = -1;
let lastStudioState = '';
let lastRouteControls = '';
let lastBenchTranspose = null;

function syncFirmware() {
  if (!engine?.firmware || !selectedScale) return;
  if (firmwareScale !== selectedScale.index) {
    const pads = Array.from({ length: 96 }, (_, index) => {
      const pair = selectedScale.pads[index];
      const record = pair ? data.pad_records[pair[1]] : null;
      return { notes: record?.midi_slots ?? [0, 0, 0, 0, 0, 0, 0],
        mask: record?.notes?.reduce((mask, note) => mask | (1 << note.slot), 0) ?? 0, color: record?.color_id ?? 0 };
    });
    const definition = melodyDefinition() ?? { anchor: 60, degrees: [0, 2, 4, 5, 7, 9, 11] };
    checkResult(engine.firmware.layout(pads, { ...definition, root: Math.max(0, ROOT_KEYS.indexOf(selectedScale.root_label)) }), 'Firmware catalogue');
    engine.state.catalog(selectedScale.group_index, data.groups.length);
    if(lastBenchTranspose!==null && engineStudio?.model){
      transpose=engineStudio.model.get('performance.transpose');
      lastBenchTranspose=transpose;
      $('#transpose-value').textContent=transpose>0?`+${transpose}`:transpose;
    }
    firmwareScale = selectedScale.index;
    for (let id = 0; id < 48; id++) engine.firmware.slot(id, chordEdits.get(`${selectedScale.index}:${id}`) ?? 0);
  }
  engine.firmware.performance({ ...performanceSettings, melodyLayout: MELODY_LAYOUTS.indexOf(melodyLayout), selectedPad });
  const routeSignature = JSON.stringify([route('chord'), route('melody')]);
  if (routeSignature !== lastRouteControls) {
    for (const [side, name] of ['chord', 'melody'].entries()) {
      const settings = route(name);
      if (!lastRouteControls) engine.firmware.route(side, settings.channel, settings.mask || 1, transpose);
      else engine.firmware.editSideRoute(side, settings.channel, settings.mask || 1);
    }
    lastRouteControls = routeSignature;
  }
  if (lastBenchTranspose !== transpose) {
    engine.firmware.transpose(transpose);
    lastBenchTranspose = transpose;
  }
  engine.firmware.expression(Number($('#velocity').value), Boolean(engine.firmware.state().pressureEnabled));
}

function paintFirmwareLights() {
  if(companionShell?.connected)return;
  if (!selectedScale || !engine?.firmware) return;
  const colors = engine.firmware.colors(Math.floor(performance.now()) >>> 0);
  const sequencerLights=engine.sequencer?.colors();
  for (let id = 0; id < padButtons.length; id++) {
    if (sequencerLights?.[id*4]) {
      const rgb = Array.from(sequencerLights.subarray(id*4+1,id*4+4)), peak = Math.max(...rgb);
      padButtons[id].style.setProperty('--note-light', rgb.map(value => peak ? Math.round(value*255/peak) : 0).join(' '));
      padButtons[id].style.setProperty('--note-level', Math.min(1,peak/4095));
      padButtons[id].dataset.light = rgb.map(value=>value.toString(16).padStart(3,'0')).join('');
      continue;
    }
    const color = colors[id];
    // The MCU word retains wire-order GRB; the owner's board photo confirms
    // C red / F green / G blue. CSS consumes RGB instead.
    const components = [(color >>> 8) & 255, color >>> 16, color & 255];
    const peak = Math.max(...components);
    const hue = components.map(value => peak ? Math.round(value * 255 / peak) : 0).join(' ');
    padButtons[id].style.setProperty('--note-light', hue);
    padButtons[id].style.setProperty('--note-level', Math.min(1, peak / 8));
    padButtons[id].dataset.light = color.toString(16).padStart(6, '0');
  }
  {
    const levels = engine.state.centerLevels();
    const controls = [...$$('[data-control="nav"]'), ...$$('[data-control="function"]')];
    controls.forEach((button, index) => {
      button.style.setProperty('--control-level', levels[index] / 4095);
      button.dataset.level = levels[index];
    });
  }
}

function applyFirmwareState() {
  const state = engine.firmware.state();
  if(engineStudio?.model){transpose=engineStudio.model.get('performance.transpose');
    $('#transpose-value').textContent=transpose>0?`+${transpose}`:transpose;}
  Object.assign(performanceSettings, { inversion: state.inversion, spread: state.spread, strum: Boolean(state.strum), delay: state.delay,
    order: state.order, chordOctave: state.chordOctave, melodyOctave: state.melodyOctave, upper: state.upper });
  melodyLayout = MELODY_LAYOUTS[state.melodyLayout];
  $('#melody-layout').value = melodyLayout;
  $('#velocity').value = String(state.velocity);
  $('#velocity-value').textContent = state.velocity;
  $('#strum-delay').value = state.delay;
  $('#strum-direction').value = state.order;
  // Project committed routes back into the legacy bench controls. Rendering
  // must not write old DOM channels over the shared menu or reseed zone routes.
  for (const [side, name] of ['chord', 'melody'].entries()) {
    const current = engine.firmware.padRoute(side + 1);
    $(`#${name}-channel`).value = String(current.channel);
    $$(`input[data-route="${name}"]`).forEach(input => { input.checked = Boolean(current.destinations & Number(input.value)); });
  }
  lastRouteControls = JSON.stringify([route('chord'), route('melody')]);
  lastBenchTranspose = transpose;
  for (const control of $$('input[data-route], #chord-channel, #melody-channel')) {
    control.disabled = state.routingProfile !== 1;
    control.title = state.routingProfile === 1 ? '' : 'Configure the active profile in MIDI & Routing.';
  }
  firmwareStudio?.refresh();
  for (let id = 0; id < 48; id++) chordEdits.set(`${selectedScale.index}:${id}`, engine.firmware.slotMask(id));
}

function navigateController(action) { engineStudio?.navigate(action); }

function heldNavigation(action, owner, pressed) {
  if (!engine || !['up','down'].includes(action)) return false;
  let owners=navigationOwners.get(action);
  if (pressed) {
    if (!owners) {owners=new Set();navigationOwners.set(action,owners);}
    if (owners.has(owner)) return true;
    if (!owners.size) {
      advanceCoreClock();
      checkResult(engine.firmware.control(0,action==='up'?1:3,true),'Navigation press');
    }
    owners.add(owner);
  } else {
    if (!owners?.delete(owner)) return false;
    if (!owners.size) {
      navigationOwners.delete(action);
      checkResult(engine.firmware.control(0,action==='up'?1:3,false),'Navigation release');
    }
  }
  afterOperation();engineStudio?.refresh();renderScreen();renderPhysicalControls();return true;
}

function clearNavigationHolds() {
  if (engine) for (const action of navigationOwners.keys())
    engine.firmware.control(0,action==='up'?1:3,false);
  navigationOwners.clear();pointerNavigation.clear();navigationKeys.clear();
  $$('[data-control="nav"]').forEach(button=>{delete button.dataset.navigationPointer;});
}

function releaseNavigationPointer(event) {
  const held=pointerNavigation.get(event.pointerId);
  if (!held) return;
  pointerNavigation.delete(event.pointerId);
  heldNavigation(held.action,held.owner,false);
}

function textElement(tag, text, className) {
  const element = document.createElement(tag);
  if (className) element.className = className;
  element.textContent = text;
  return element;
}

function midiName(note) {
  return `${NOTE_NAMES[note % 12]}${Math.floor(note / 12) - 1}`;
}

function currentScaleName() {
  const slot=engine?.firmware.state().userScale;
  return slot ? engine.firmware.library.record(0,slot-1)?.name || 'Custom scale' : selectedScale.name;
}

function getPad(index) {
  if(engine?.state.pickingPad())
    return {index,name:'Pick pad',midi_slots:[],used_slot_count:0,note_status:'control',color_id:0,color_token:'sensor-pick',generated:'sensitivity-pick'};
  const seqPad=engine?.sequencer?.pad(index);
  if(seqPad&&['editor','track','command','separator'].includes(seqPad.role)) {
    const name=seqPad.role==='editor'?`Step ${index%4*12+Math.floor(index%48/4)+1}`:seqPad.role==='track'?`Track ${seqPad.track+1}`:seqPad.role==='command'?'Seq control':'—';
    return{index,name,midi_slots:[],used_slot_count:0,note_status:'control',color_id:0,color_token:'sequencer',generated:'sequencer'};
  }
  if(seqPad&&['notes','chords'].includes(seqPad.role)) {
    const preview=engine.sequencer.previewPad(index);
    return{index,name:preview.ok&&preview.notes.length?preview.notes.map(midiName).join(' '):'RANGE',
      midi_slots:preview.notes,used_slot_count:preview.notes.length,note_status:preview.notes.length?'active':'range',
      color_id:0,color_token:'sequencer-input',generated:seqPad.role};
  }
  const context = engine?.firmware.contextPad(index) ?? 0;
  if (context) return { index, name: CONTEXT_LABELS[context], midi_slots: [], used_slot_count: 0,
    note_status: 'control', color_id: 0, color_token: 'context', context, generated: 'context control' };
  const pair = selectedScale?.pads[index];
  const shared=engine?.firmware.state();
  if(index<48 && shared?.chordLayout) {
    const cell=engine.firmware.chordCell(index),preview=engine.firmware.previewPad(index);
    const custom=shared.chordLayout===5?engine.firmware.library.record(1,Math.floor(index/4)*2+index%2):null;
    return {index,name:cell?.mask ? custom?.used?custom.name:`${NOTE_NAMES[((cell.root%12)+12)%12]} chord`:'Empty',
      midi_slots:cell?.notes||[],valid_mask:cell?.mask||0,used_slot_count:preview.notes.length,
      note_status:cell?.mask?'active':'empty',color_id:0,color_token:'shared-chord',generated:'chord layout'};
  }
  if (index >= 48 && (melodyLayout !== 'stored' || shared?.liveUpdate || shared?.userScale) && engine && selectedScale) {
    const preview = engine.firmware.previewPad(index);
    const source = preview.ok && preview.notes.length ? selectedScale.pads.slice(48).filter(Boolean).map(pair => data.pad_records[pair[1]])
      .find(record => padNotes(record).some(note => note % 12 === preview.notes[0] % 12)) : null;
    return { index, name: preview.ok && preview.notes.length ? midiName(preview.notes[0]) :
      preview.ok && melodyLayout === 'piano' ? '—' : 'RANGE',
      midi_slots: preview.notes, used_slot_count: preview.notes.length,
      note_status: preview.ok && preview.notes.length ? 'active' :
        preview.ok && melodyLayout === 'piano' ? 'empty' : 'range', color_id: source?.color_id ?? 0,
      color_token: source?.color_token ?? 'generated', generated: melodyLayout };
  }
  if (!pair) return null;
  return { ...data.pad_records[pair[1]], index, source_offset: pair[0] };
}

function melodyDefinition() {
  const first = selectedScale.pads[48];
  if (!first) return null;
  const record = data.pad_records[first[1]];
  if (!record.used_slot_count) return null;
  const anchor = record.midi_slots[0];
  const degrees = new Set([0]);
  for (let index = 48; index < 80; index += 4) {
    const pair = selectedScale.pads[index];
    if (pair) for (const note of padNotes(data.pad_records[pair[1]])) degrees.add((note - anchor + 1200) % 12);
  }
  return { anchor, degrees: [...degrees].sort((a, b) => a - b) };
}

function shapeFor(index) {
  const chord = index < 48;
  return { spread: chord ? performanceSettings.spread : 0, inversion: chord ? performanceSettings.inversion : 0,
    octave: chord ? performanceSettings.chordOctave : performanceSettings.melodyOctave,
    upper: chord ? performanceSettings.upper : 0, order: chord ? performanceSettings.order : 0,
    delay: chord && performanceSettings.strum ? performanceSettings.delay : 0,
    excluded: chord ? chordEdits.get(`${selectedScale.index}:${index}`) ?? 0 : 0 };
}

function advanceCoreClock() {
  checkResult(engine.advance(Math.floor(performance.now()) >>> 0), 'Core clock');
}

function scheduleStrum() {
  if (strumTimer !== undefined || !engine.stats().pending || document.hidden) return;
  strumTimer = window.setTimeout(() => {
    strumTimer = undefined;
    advanceCoreClock();
    if (engine.stats().queued) afterOperation();
    else scheduleStrum();
  }, 4);
}

function padNotes(pad) {
  if (!pad) return [];
  if(pad.valid_mask!==undefined)return pad.midi_slots.filter((_,n)=>pad.valid_mask&(1<<n));
  return pad.midi_slots.slice(0, pad.used_slot_count);
}

function route(side) {
  const mask = $$(`input[data-route="${side}"]:checked`).reduce((mask, input) => mask | Number(input.value), 0);
  return { mask, channel: Number($(`#${side}-channel`).value) };
}

function showError(message) {
  $('#error-banner').textContent = message;
  $('#error-banner').hidden = false;
}

function clearError() {
  $('#error-banner').hidden = true;
}

function checkResult(result, context) {
  if (result && result.ok === false) {
    showError(`${context}: ${result.status === 'queue-full' ? 'The event queue is full; drain events before trying again.' : result.status === 'invalid' ? 'The selected notes, transpose or route are outside the core’s valid range.' : result.status}`);
    return false;
  }
  return true;
}

function collectEvents() {
  const events = engine.drain();
  events.forEach((event) => {
    addTimelineEvent(event);
  });
  return events;
}

function afterOperation() {
  const events = collectEvents();
  renderCoreState();
  scheduleStrum();
  return events;
}

function selectPad(index) {
  selectedPad = index;
  padButtons.forEach((button, pad) => button.classList.toggle('is-selected', pad === index));
  renderInspector();
  renderPhysicalControls();
  syncFirmware();
  firmwareStudio?.refresh();
}

function pressPad(index, owner) {
  if (!engine || scenarioRunning || companionShell) return;
  if (firmwareStudio?.isSampling(index)) { feedback('This pad is being played by the raw sensor simulator. Stop sampling to use pointer or keyboard input.'); return; }
  selectPad(index);
  if (latch && engine.isHeld(index)) {
    checkResult(engine.release(index), 'Pad release');
    physicalOwners.delete(index);
    afterOperation();
    return;
  }
  const pad = getPad(index);
  if (!pad || !['active', 'control'].includes(pad.note_status)) return;
  let owners = physicalOwners.get(index);
  if (!owners) {
    const settings = route(index < 48 ? 'chord' : 'melody');
    if (!settings.mask && !pad.context) { showError('Select at least one destination for this pad bank.'); return; }
    advanceCoreClock();
    const result = engine.firmware.pressPad(index);
    if (!checkResult(result, 'Pad press')) return;
    owners = new Set();
    physicalOwners.set(index, owners);
    const pressure = Number($('#pressure').value);
    if (pressure > 0 && !pad.context) checkResult(engine.firmware.pressurePad(index, pressure), 'Pad pressure');
    if (pad.context) applyFirmwareState();
  }
  owners.add(owner);
  afterOperation();
  if (pad.context) renderPads();
}

function releasePad(index, owner) {
  if (!engine) return;
  const owners = physicalOwners.get(index);
  if (!owners) return;
  owners.delete(owner);
  if (owners.size === 0 && (!latch || engine.firmware.contextPad(index))) {
    if (checkResult(engine.release(index), 'Pad release')) physicalOwners.delete(index);
  }
  afterOperation();
}

function panic() {
  if (!engine) return;
  clearNavigationHolds();
  checkResult(engine.panic(), 'Panic');
  if (strumTimer !== undefined) window.clearTimeout(strumTimer);
  strumTimer = undefined;
  physicalOwners.clear();
  pointerPads.clear();
  downKeys.clear();
  sustainGates.clear();
  sustain = false;
  manualSustain = false;
  keyboardSustain = false;
  $('#sustain-toggle').setAttribute('aria-pressed', 'false');
  $('#sustain-toggle span').textContent = 'OFF';
  afterOperation();
  resetBend();
}

function controllerChannels() {
  const channels = new Map();
  for (const settings of engine.firmware.activeRoutes())
    channels.set(settings.channel, (channels.get(settings.channel) || 0) | settings.destinations);
  // Include our still-sounding owners from an earlier route edit. Cleanup is
  // retained separately in sustainGates/bendTargets until release/recentering.
  for (const voice of engine.voices())
    channels.set(voice.channel, (channels.get(voice.channel) || 0) | (1 << voice.destination));
  return channels;
}

function setBend(value) {
  if (!engine) return;
  $('#bend-value').textContent = value;
  const channels = controllerChannels();
  for (const [channel, mask] of channels) {
    if (mask && checkResult(engine.bend(mask, channel, value), 'Pitch bend')) bendTargets.set(channel, (bendTargets.get(channel) || 0) | mask);
  }
  afterOperation();
}

function resetBend() {
  if (!engine || !bendTargets.size) return;
  for (const [channel, mask] of bendTargets) {
    if (checkResult(engine.bend(mask, channel, 8192), 'Pitch bend center')) bendTargets.delete(channel);
    collectEvents();
  }
  $('#bend').value = '8192';
  $('#bend-value').textContent = '8192';
  afterOperation();
}

function applySustainRoutes() {
  if (!engine || !sustain) return;
  const channels = controllerChannels();
  for (const [channel, mask] of channels) {
    if (!mask) continue;
    if (checkResult(engine.sustain(mask, channel, true), 'Sustain')) {
      sustainGates.set(channel, (sustainGates.get(channel) || 0) | mask);
    }
  }
  afterOperation();
}

function setSustain(enabled) {
  if (!engine) return;
  if (sustain === enabled) return;
  sustain = enabled;
  $('#sustain-toggle').setAttribute('aria-pressed', String(enabled));
  $('#sustain-toggle span').textContent = enabled ? 'ON' : 'OFF';
  if (enabled) applySustainRoutes();
  else {
    for (const [channel, mask] of sustainGates) checkResult(engine.sustain(mask, channel, false), 'Sustain release');
    sustainGates.clear();
    afterOperation();
  }
}

function buildPads() {
  for (let index = 0; index < 96; index++) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'pad';
    button.dataset.pad = index;
    button.dataset.testid = `pad-${index}`;
    button.setAttribute('aria-description', 'Press Enter to audition this pad. Space holds sustain; Escape releases all notes.');
    button.style.gridColumn = String(Math.floor((index % 48) / 4) + 1);
    button.style.gridRow = String((index % 48) % 4 + 1);
    button.addEventListener('pointerdown', (event) => {
      if (event.button !== 0 && event.pointerType === 'mouse') return;
      event.preventDefault();
      button.focus({ preventScroll: true });
      try { button.setPointerCapture(event.pointerId); } catch { /* Document release also handles surfaces without capture. */ }
      pointerPads.set(event.pointerId, index);
      pressPad(index, `pointer:${event.pointerId}`);
    });
    const releasePointer = (event) => {
      const pad = pointerPads.get(event.pointerId);
      if (pad === undefined) return;
      pointerPads.delete(event.pointerId);
      releasePad(pad, `pointer:${event.pointerId}`);
    };
    button.addEventListener('pointerup', releasePointer);
    button.addEventListener('pointercancel', releasePointer);
    button.addEventListener('lostpointercapture', releasePointer);
    button.addEventListener('keydown', (event) => { if (event.key === 'Enter' && event.repeat) event.preventDefault(); });
    // Native keyboard activation has no pointerdown. A click auditions a short note.
    button.addEventListener('click', (event) => {
      if (event.detail !== 0) return;
      pressPad(index, `accessible:${index}`);
      window.setTimeout(() => releasePad(index, `accessible:${index}`), 240);
    });
    padButtons.push(button);
    $(index < 48 ? '#chord-pads' : '#melody-pads').append(button);
  }
}

function renderPads() {
  syncFirmware();
  padButtons.forEach((button, index) => {
    const pad = getPad(index);
    const unassigned = !pad;
    const control = pad?.note_status === 'control';
    const empty = !control && pad?.note_status !== 'active';
    const silentKey = empty && pad?.generated === 'piano' && pad?.note_status === 'empty';
    button.disabled = companionShell ? !companionShell.connected : (unassigned || empty) && !engine?.isHeld(index);
    button.classList.toggle('is-null', !unassigned && empty);
    button.classList.toggle('is-selected', index === selectedPad);
    button.replaceChildren();
    button.append(textElement('span', unassigned || silentKey ? '—' : empty ? 'NULL' : pad.name, 'pad-name'));
    const preview = pad && !empty ? ['notes','chords'].includes(pad.generated) ?
      engine.sequencer.previewPad(index) : engine.firmware.previewPad(index) : null;
    button.append(textElement('span', control ? 'CONTROL' : unassigned ? 'NO DATA' : silentKey ? '' : empty ? pad?.note_status === 'range' ? 'RANGE' : 'EMPTY' : !preview.ok ? 'OUT OF RANGE' : preview.notes.length === 1 ? midiName(preview.notes[0]) : `${preview.notes.length} notes`, 'pad-note'));
    if (KEY_LABELS.has(index) && !silentKey) button.append(textElement('span', KEY_LABELS.get(index), 'pad-key'));
    const color = COLORS[Math.abs(pad?.color_id || 0) % COLORS.length];
    button.style.setProperty('--pad-bg', color[0]);
    button.style.setProperty('--pad-text', color[1]);
    button.style.setProperty('--pad-line', color[2]);
    button.style.setProperty('--pad-bottom', color[0]);
    button.style.setProperty('--pad-glow', `${color[1]}25`);
    const side = pad?.generated === 'sequencer' ? 'Sequence' : pad?.generated === 'chords' ?
      'Chord input' : pad?.generated === 'notes' ? 'Note input' : index < 48 ? 'Chord' : 'Melody';
    const location = `column ${Math.floor((index % 48) / 4) + 1}, row ${index % 4 + 1}`;
    button.setAttribute('aria-label', control ? `${side} pad ${location}: ${pad.name}, context control, no MIDI note` : unassigned ? `${side} pad ${location}: no stored assignment` : `${side} pad ${location}: ${pad.name}, ${preview?.ok ? preview.notes.map(midiName).join(', ') || 'silent' : empty ? 'empty' : 'outside MIDI range'}, color ID ${pad.color_id}`);
  });
  renderInspector();
  renderScreen();
  renderCoreState();
  paintFirmwareLights();
}

function renderInspector() {
  const pad = getPad(selectedPad);
  const side = selectedPad < 48 ? 'A' : 'B';
  $('#inspector-location').textContent = `${side} · ${String(selectedPad % 48 + 1).padStart(2, '0')}`;
  const body = $('#pad-inspector');
  body.replaceChildren();
  body.append(textElement('div', pad?.name || 'Unassigned', 'inspector-chord'));
  if (!pad) {
    body.append(textElement('p', 'This physical pad has no record in the recovered dump. No pitch is inferred.', 'inspector-notice'));
    return;
  }
  if (pad.context) {
    body.append(textElement('p', `${pad.name}: applies once per press, without sending a MIDI note. Context placement and visible actions are configurable in Performance; hidden controls return to their musical assignments.`, 'inspector-notice'));
    return;
  }
  if (pad.generated === 'sequencer') {
    body.append(textElement('p', 'Sequence steps select an edit target without sounding notes. Track settings keep this surface active; LEFT returns directly to the track editor.', 'inspector-notice'));
    return;
  }
  if(pad.generated==='sensitivity-pick') {
    body.append(textElement('p','Tap and release a pad to select its response settings. Picking a pad sends no note.','inspector-notice'));return;
  }
  const chips = textElement('div', '', 'note-chips');
  padNotes(pad).forEach((note) => {
    const chip = textElement('div', midiName(note), 'note-chip');
    chip.append(textElement('span', String(note)));
    chips.append(chip);
  });
  if (chips.childElementCount === 0) chips.append(textElement('p', 'Original NULL record · no active notes', 'inspector-notice'));
  body.append(chips);
  const details = textElement('div', '', 'inspector-details');
  const settings = engine.firmware.routeForPad(selectedPad);
  for (const [label, value] of [
    ['Color ID', `${pad.color_id} / ${pad.color_token}`],
    ['Source', pad.generated ? `Generated ${pad.generated} preview` : `0x${pad.source_offset.toString(16).toUpperCase()}`],
    ['Original slots', pad.midi_slots.join(' · ')],
    ['Next press', `Ch ${settings.channel + 1} · ${transpose > 0 ? '+' : ''}${transpose} st`],
  ]) {
    const field = textElement('span', `${label}: `);
    field.append(textElement('b', value));
    details.append(field);
  }
  body.append(details);
  const preview = ['notes','chords'].includes(pad.generated) ?
    engine.sequencer.previewPad(selectedPad) : engine.firmware.previewPad(selectedPad);
  body.append(textElement('p', `Next press: ${preview.ok ? preview.notes.map(midiName).join(' · ') || 'All notes disabled' : 'Outside MIDI range; the press will be rejected.'}`, 'inspector-notice'));
  if (engine?.isHeld(selectedPad)) {
    const owners = engine.owner(selectedPad);
    const captured = new Map();
    for (const voice of owners) {
      const key = `${DESTINATIONS[voice.destination]} / Ch ${voice.channel + 1}`;
      if (!captured.has(key)) captured.set(key, []);
      captured.get(key).push(midiName(voice.note));
    }
    body.append(textElement('p', `Held snapshot: ${[...captured].map(([route, notes]) => `${route}: ${notes.join(', ')}`).join(' · ')}`, 'inspector-notice'));
  }
}

function renderCoreState() {
  if (!engine) return;
  const stats = engine.stats();
  const voices = engine.voices();
  $('#voice-count').textContent = `${stats.activeVoices} ${stats.activeVoices === 1 ? 'voice' : 'voices'}`;
  $('#held-count').textContent = stats.heldPads;
  $('#reserved-count').textContent = stats.reserved;
  $('#engine-health').textContent = stats.pending ? `${stats.pending} PENDING ROUTED NOTES` : 'OWNERSHIP TRACKED';
  padButtons.forEach((button, index) => {
    const held = engine.isHeld(index) || engine.firmware.contextHeld(index);
    const pad = getPad(index);
    button.disabled = companionShell ? !companionShell.connected : (!pad || !['active', 'control'].includes(pad.note_status)) && !held;
    button.classList.toggle('is-held', held);
    button.setAttribute('aria-pressed', String(held));
  });
  const list = $('#voice-list');
  list.replaceChildren();
  if (!voices.length) {
    const empty = textElement('div', '', 'empty-state');
    empty.append(textElement('span', '⌁', 'empty-icon'), textElement('p', 'Every note has an owner.'), textElement('span', 'Press a pad to inspect voices by destination and channel.'));
    list.append(empty);
  } else voices.forEach((voice) => {
    const row = textElement('div', '', `voice-row${voice.sustained ? ' is-sustained' : ''}`);
    row.dataset.voice = `${voice.destination}:${voice.channel}:${voice.note}`;
    row.append(textElement('span', midiName(voice.note), 'voice-pitch'), textElement('span', `${DESTINATIONS[voice.destination]} · CH ${voice.channel + 1} · MIDI ${voice.note}`, 'voice-route'), textElement('span', voice.sustained ? 'sustained' : `${voice.owners} ${voice.owners === 1 ? 'owner' : 'owners'}`, 'voice-owner'));
    list.append(row);
  });
  renderScreen();
  renderInspector();
  renderPhysicalControls();
  paintFirmwareLights();
}

function renderScreen() {
  if(companionShell){companionShell.render();engineStudio?.refresh();return;}
  if (!selectedScale || !oledPreview) return;
  syncFirmware();
  const animations=Boolean(engine.firmware.state().animationsEnabled)&&!matchMedia('(prefers-reduced-motion: reduce)').matches;
  const frame=engineStudio?engineStudio.frame(oledProfile,animations):engine.firmware.frame({scaleName:`${currentScaleName()} ${selectedScale.root_label}`,now:Math.floor(performance.now())>>>0,animations});
  oledPreview.setFrame(frame,{sceneRevision:engineStudio?engine.state.sceneKey():undefined});
  $('#oled-canvas').setAttribute('aria-label','128 by 64 shared C instrument renderer');
  engineStudio?.refresh();
}

async function loadOLEDProfile() {
  const embedded = document.getElementById('thy-oled-profile');
  if (embedded) return JSON.parse(embedded.textContent);
  const response = await fetch('./generated/oled-profile.json');
  if (!response.ok) throw new Error(`Extracted OLED profile is missing (${response.status}). Rebuild the workbench.`);
  return response.json();
}

function initializeOLED(profile) {
  if (!profile || profile.error) {
    $('#oled-canvas').hidden = true;
    $('#oled-error').hidden = false;
    $('#oled-error').textContent = profile?.error || 'Extracted OLED profile unavailable. Rebuild the workbench.';
    return;
  }
  try {
    profile = { ...profile, timing: { ...profile.timing, firmware_service_interval_ms: 42 } };
    oledPreview = new OLEDPreview($('#oled-canvas'), profile, { instrumentScheduler: engine.display });
    oledProfile = profile;
    oledPreview.setTransport('instrument');
  } catch (error) {
    $('#oled-canvas').hidden = true;
    $('#oled-error').hidden = false;
    $('#oled-error').textContent = `Hardware preview unavailable: ${error.message}`;
  }
}

function populateSelect(element, records, valueKey = 'index', labelKey = 'name') {
  element.replaceChildren(...records.map((record) => {
    const option = document.createElement('option');
    option.value = record[valueKey];
    option.textContent = record[labelKey];
    return option;
  }));
  element.disabled = false;
}

function chooseScale() {
  const groupIndex = Number($('#group-select').value);
  const root = $('#root-select').value;
  selectedScale = data.scales.find((scale) => scale.group_index === groupIndex && scale.root_label === root);
  if (!selectedScale) throw new Error('No preserved layout matches the selected scale and root.');
  $('#layout-source').textContent = `Original layout #${selectedScale.index} · ${selectedScale.name}`;
  clearError();
  renderPads();
  renderPhysicalControls();
}

function chooseCategory(groupPreference) {
  const category = data.categories.find((record) => record.index === Number($('#category-select').value));
  const groups = category.group_indices.map((index) => data.groups.find((group) => group.index === index));
  populateSelect($('#group-select'), groups);
  if (groupPreference !== undefined && groups.some((group) => group.index === groupPreference)) $('#group-select').value = groupPreference;
  chooseScale();
}

function feedback(message) {
  const output = $('#control-feedback');
  if (output) output.textContent = message;
}

function chooseGroup(index) {
  const group = data.groups.find(group => group.index === index);
  if (!group) return;
  $('#category-select').value = group.category_index;
  chooseCategory(group.index);
}

function changePerformance(values, message) {
  Object.assign(performanceSettings, values);
  if ($('#strum-delay')) $('#strum-delay').value = String(performanceSettings.delay);
  if ($('#strum-direction')) $('#strum-direction').value = String(performanceSettings.order);
  clearError();
  renderPads();
  if (message) feedback(message);
}

function setMelodyLayout(layout) {
  if (!MELODY_LAYOUTS.includes(layout)) return;
  melodyLayout = layout;
  if ($('#melody-layout')) $('#melody-layout').value = layout;
  renderPads();
  feedback(layout === 'stored' ? 'Melody: original stored assignments.' : `Melody: generated ${layout} layout. Original data stays available in Stored.`);
}


function renderPhysicalControls() {
  if(companionShell)return;
  if (!selectedScale || !engine) return;
  $$('[data-control="root"]').forEach((button, index) => button.setAttribute('aria-pressed', String(ROOT_KEYS[index] === selectedScale.root_label)));
  const sequence=engine.sequencer?.snapshot();
  $$('[data-control="mode"]').forEach(button => {
    const mode = button.dataset.mode;
    if (!button.dataset.normalLabel) button.dataset.normalLabel = button.getAttribute('aria-label') || mode;
    const quick = engine.arp.quickState();
    const m = MODE_KEYS.indexOf(mode);
    if(sequence?.open){
      const labels=['Play selected track / stop','Clear step / delete track','Overdub','Notes / Chords','Undo','Redo','Sequencer help'];
      const label=mode==='5'?'Play all / stop':mode==='12'?'Record / finish':mode==='U1'?'Input octave down':mode==='U2'?'Input octave up':mode==='U3'?'Next scale / note layout':m>=0?labels[m]:'Sequencer control';
      button.setAttribute('aria-label',`${mode}: ${label}`);button.title=label;button.setAttribute('aria-pressed',String(m===2?sequence.overdub:m===3?sequence.inputLayout==='chords':m===6?sequence.helpOpen:false));return;
    }
    if (quick?.open && quick.keys && m >= 0) {
      const labels = ['Enable Arp 1', 'Enable Arp 2', 'Arp 1 Hold only', 'Arp 2 Hold only', 'Clear Arp 1', 'Clear Arp 2', 'Switch edited Arp'];
      button.setAttribute('aria-label', `M${m + 1}: ${labels[m]}`);
      button.title = `${labels[m]} · temporary Arp quick-page control`;
      button.setAttribute('aria-pressed', String(m < 2 && engine.arp.snapshot(m).enabled));
      return;
    }
    const key = ['5', '12', ...MODE_KEYS, 'U1', 'U2', 'U3'].indexOf(mode);
    const assignment = engine.firmware.scaleShortcut(key);
    const family = data.groups.find(group => group.index === assignment);
    const custom=assignment>70?engine.firmware.library.record(0,assignment-71):null;
    const label = family ? family.name : custom?.used ? custom.name : 'Cycle note layout';
    button.setAttribute('aria-label', `${mode}: ${label}`);
    button.title = `${mode}: ${label} · configurable in Engines → Scale shortcuts`;
    const customSlot=engine.firmware.state().userScale;
    button.setAttribute('aria-pressed', String(custom?.used ? customSlot===assignment-70 :
      family ? !customSlot&&selectedScale.group_index === assignment : melodyLayout !== 'stored'));
  });
  $$('[data-control="function"]').forEach(button => {
    const action = button.dataset.function;
    const match = /^(?:voicing|inversion|spread)-(\d)$/.exec(action);
    if (match) {
      const current=engine.firmware.state(),voice=Number(match[1])-1;
      if(!action.startsWith('spread') && current.voicingMode>=2){
        button.disabled=true;
        button.setAttribute('aria-pressed','false');
        button.setAttribute('aria-label','Automatic voicing mode · manual inversion unavailable');
      } else if(!action.startsWith('spread') && current.voicingMode===1){
        button.disabled=!(current.dropAvailable&(1<<voice));
        button.setAttribute('aria-pressed',String(!button.disabled&&!!(current.dropMask&(1<<voice))));
        button.setAttribute('aria-label',`Lower voice ${voice+2}${button.disabled?' · unavailable in selected chord':''}`);
      } else {button.disabled=false;
        if(!action.startsWith('spread'))button.setAttribute('aria-label',`Inversion ${Number(match[1])}`);
        button.setAttribute('aria-pressed', String(performanceSettings[action.startsWith('spread') ? 'spread' : 'inversion'] === Number(match[1])));}
    }
    else if (action === 'strum') button.setAttribute('aria-pressed', String(performanceSettings.strum));
    else if (action.startsWith('note-slot-')) {
      const slot = Number(action.slice(-1)) - 1;
      const current = engine.firmware.state();
      const disabled = !(current.lowerAvailable & (1 << slot));
      button.disabled = disabled;
      button.setAttribute('aria-pressed', String(!disabled && !!(current.lowerMask & (1 << slot))));
      const octave = slot - 3;
      const label = current.lowerMode === 1 ? octave === 0 ? 'Original octave layer' : `Octave layer ${octave > 0 ? '+' : ''}${octave}` : `Stored note ${slot + 1}`;
      const detail = disabled ? 'unavailable in this chord selection' : `${current.slotScope ? 'selected chord' : 'whole left grid'}; next chord press`;
      button.title = `${label}: ${detail}`;
      button.setAttribute('aria-label', button.title);
    }
  });

}

function handlePhysicalFunction(action) {
  const index=$$('[data-control="function"]').findIndex(button=>button.dataset.function===action);
  if(index<0)return;
  engine.firmware.control(1,index,true);engine.firmware.control(1,index,false);
  applyFirmwareState();renderPads();renderScreen();renderPhysicalControls();
}

function eventDescription(event) {
  const type = event.status & 0xf0;
  if (type === 0x90 && event.data2 !== 0) return ['Note on', `${midiName(event.data1)} · ${event.data1} · vel ${event.data2}`, 'event-on'];
  if (type === 0x80 || (type === 0x90 && event.data2 === 0)) return ['Note off', `${midiName(event.data1)} · ${event.data1}`, 'event-off'];
  if (type === 0xb0) return ['Control change', `CC ${event.data1} · ${event.data2}`, ''];
  if (type === 0xe0) return ['Pitch bend', `${event.data1 | (event.data2 << 7)} / 16383`, ''];
  if (type === 0xa0) return ['Poly pressure', `${midiName(event.data1)} · ${event.data2}`, ''];
  if (type === 0xd0) return ['Channel pressure', `${event.data1} / 127`, ''];
  return [`0x${event.status.toString(16).toUpperCase()}`, `${event.data1} · ${event.data2}`, ''];
}

function addTimelineEvent(event) {
  eventCount++;
  $('#event-count').textContent = `${eventCount} events`;
  const [name, value, kind] = eventDescription(event);
  const row = textElement('tr', '', `event-row ${kind}`);
  row.dataset.destination = event.destination;
  row.dataset.channel = event.channel;
  row.dataset.status = event.status;
  row.dataset.note = event.data1;
  const bytes = event.destination === 2 ? event.din : event.usb;
  for (const [text, className] of [
    [`${((performance.now() - startedAt) / 1000).toFixed(3)}s`, ''],
    [DESTINATIONS[event.destination], ''],
    [String(event.channel + 1), ''],
    [name, 'event-kind'],
    [value, ''],
    [(bytes || []).map((byte) => byte.toString(16).padStart(2, '0').toUpperCase()).join(' '), 'wire-bytes'],
  ]) row.append(textElement('td', text, className));
  const list = $('#event-list');
  list.querySelector('.timeline-empty')?.remove();
  list.prepend(row);
  while (list.childElementCount > 100) list.lastElementChild.remove();
}


async function runScenario(name) {
  if (!engine || scenarioRunning) return;
  const result = $('#scenario-result');
  scenarioRunning = true;
  $$('[data-scenario]').forEach((button) => button.disabled = true);
  result.className = 'scenario-result';
  result.textContent = 'Running against the shared musical core…';
  const originalChannel = $('#chord-channel').value;
  const originalRoutes = $$('input[data-route="chord"]').map((input) => input.checked);
  try {
    panic();
    const require = (condition, message) => { if (!condition) throw new Error(message); };
    const invoke = (operation) => {
      const response = operation();
      require(response?.ok !== false, `Core rejected operation: ${response?.status}`);
      return afterOperation();
    };
    if (name === 'shared') {
      invoke(() => engine.press({ pad: 0, notes: [60, 64], mask: 1, channel: 0, velocity: 96 }));
      const second = invoke(() => engine.press({ pad: 1, notes: [60, 67], mask: 1, channel: 0, velocity: 96 }));
      require(!second.some((event) => (event.status & 0xf0) === 0x90 && event.data1 === 60), 'Shared C4 was incorrectly retriggered.');
      const firstRelease = invoke(() => engine.release(0));
      require(firstRelease.length === 1 && firstRelease[0].data1 === 64 && (firstRelease[0].status & 0xf0) === 0x80, 'First release should stop E4 and retain shared C4.');
      require(engine.voices().some((voice) => voice.note === 60 && voice.owners === 1), 'C4 lost its remaining owner.');
      const lastRelease = invoke(() => engine.release(1));
      require(lastRelease.filter((event) => (event.status & 0xf0) === 0x80).length === 2, 'Final release did not stop both remaining notes.');
      result.textContent = 'PASS · Shared C4 stays on until its final pad releases.';
    } else if (name === 'route') {
      invoke(() => engine.press({ pad: 0, notes: [60], mask: 1, channel: 0, velocity: 96 }));
      $('#chord-channel').value = '2';
      $$('input[data-route="chord"]').forEach((input) => input.checked = input.value === '4');
      const release = invoke(() => engine.release(0));
      require(release.length === 1 && release[0].destination === 0 && release[0].channel === 0 && release[0].data1 === 60 && (release[0].status & 0xf0) === 0x80, 'Release used the edited route instead of its frozen ownership.');
      result.textContent = 'PASS · Release returns to USB 1 / channel 1 after the route changes.';
    } else if (name === 'bend') {
      // The 14-bit pressure path must saturate at 16383.
      const events = invoke(() => engine.bend(5, 0, 16384));
      require(events.length === 2 && events.every((event) => event.data1 === 127 && event.data2 === 127 && (event.status & 0xf0) === 0xe0), 'Maximum bend wrapped or produced invalid data bytes.');
      const usb = events.find((event) => event.destination === 0);
      const din = events.find((event) => event.destination === 2);
      require(usb?.usb.join(',') === '14,224,127,127' && din?.din.join(',') === '224,127,127', 'USB or DIN encoding differs from the expected maximum bend.');
      invoke(() => engine.bend(5, 0, 8192));
      result.textContent = 'PASS · Maximum bend emits 127 / 127 on USB and DIN.';
    }
    require(engine.stats().heldPads === 0 && engine.stats().activeVoices === 0, 'Scenario left notes sounding.');
    result.classList.add('pass');
    result.dataset.result = 'pass';
  } catch (error) {
    result.textContent = `FAIL · ${error.message}`;
    result.classList.add('fail');
    result.dataset.result = 'fail';
    panic();
  } finally {
    $('#chord-channel').value = originalChannel;
    $$('input[data-route="chord"]').forEach((input, index) => input.checked = originalRoutes[index]);
    scenarioRunning = false;
    $$('[data-scenario]').forEach((button) => button.disabled = false);
    renderInspector();
    renderCoreState();
  }
}

function wireControls() {
  $$('[data-control="root"]').forEach((button, index) => button.addEventListener('click', () => {
    $('#root-select').value = ROOT_KEYS[index];
    chooseScale();
    feedback(`Root ${button.textContent}. Held notes keep their original pitches.`);
  }));
  $$('[data-control="mode"]').forEach(button => button.addEventListener('click', () => {
    const mode = button.dataset.mode;
    const quick = engine.arp.quickState();
    const m = MODE_KEYS.indexOf(mode);
    if (engine.sequencer?.snapshot().open) {
      // Accessible keyboard clicks issue the same complete edge pair as touch.
      if (button.dataset.seqPointer !== 'true') {
        const key = ['5', '12', ...MODE_KEYS, 'U1', 'U2', 'U3'].indexOf(mode);
        advanceCoreClock();
        checkResult(engine.firmware.control(3,key,true),'Sequencer control');
        checkResult(engine.firmware.control(3,key,false),'Sequencer release');
      }
      delete button.dataset.seqPointer;
      afterOperation();sequencerLab?.refresh(true);engineStudio?.refresh();return;
    }
    if (quick?.open && quick.keys && m >= 0) {
      advanceCoreClock();
      checkResult(engine.firmware.control(3, m + 2, true), 'Arp quick control');
      checkResult(engine.firmware.control(3, m + 2, false), 'Arp quick release');
      afterOperation(); renderPhysicalControls(); renderScreen();
      engineStudio?.refresh(); arpLab?.refresh();
      return;
    }
    const key = ['5', '12', ...MODE_KEYS, 'U1', 'U2', 'U3'].indexOf(mode);
    const assignment = engine.firmware.scaleShortcut(key);
    checkResult(engine.firmware.control(3, key, true), 'Scale shortcut');
    checkResult(engine.firmware.control(3, key, false), 'Scale shortcut release');
    applyFirmwareState();
    const group = engineStudio?.model.get('performance.scale');
    if (group !== undefined && group !== selectedScale.group_index) chooseGroup(group);
    else renderPads();
    const name = data.groups.find(group => group.index === assignment)?.name;
    feedback(name ? `${name} · ${selectedScale.root_label}. Held notes keep their original pitches.` : `Note layout: ${melodyLayout}.`);
    renderPhysicalControls(); renderScreen(); engineStudio?.refresh();
  }));
  $$('[data-control="mode"]').forEach(button => {
    const key = ['5', '12', ...MODE_KEYS, 'U1', 'U2', 'U3'].indexOf(button.dataset.mode);
    button.addEventListener('pointerdown',event=>{
      if (!engine.sequencer?.snapshot().open || (event.pointerType==='mouse'&&event.button!==0)) return;
      button.dataset.seqPointer='true';
      try { button.setPointerCapture(event.pointerId); } catch { /* Pointerup fallback remains active. */ }
      advanceCoreClock();checkResult(engine.firmware.control(3,key,true),'Sequencer control');afterOperation();
    });
    const release=()=>{if(button.dataset.seqPointer==='true'){checkResult(engine.firmware.control(3,key,false),'Sequencer release');afterOperation();}};
    button.addEventListener('pointerup',release);button.addEventListener('pointercancel',release);button.addEventListener('lostpointercapture',release);
  });
  $$('[data-control="function"]').forEach(button => button.addEventListener('click', () => handlePhysicalFunction(button.dataset.function)));
  $$('[data-control="nav"]').forEach(button => {
    button.addEventListener('pointerdown',event=>{
      const action=button.dataset.nav;
      if(!['up','down'].includes(action)||(event.pointerType==='mouse'&&event.button!==0))return;
      event.preventDefault();
      const owner=`pointer:${event.pointerId}`;
      button.dataset.navigationPointer='true';pointerNavigation.set(event.pointerId,{action,owner});
      try{button.setPointerCapture(event.pointerId);}catch{/* Document pointer release remains available. */}
      heldNavigation(action,owner,true);
    });
    for(const event of ['pointerup','pointercancel','lostpointercapture'])button.addEventListener(event,releaseNavigationPointer);
    button.addEventListener('click',event=>{
      const action=button.dataset.nav,handled=button.dataset.navigationPointer==='true'&&event.detail>0;
      delete button.dataset.navigationPointer;
      if(handled)return;
      if(['up','down'].includes(action)) {
        heldNavigation(action,'activation',true);heldNavigation(action,'activation',false);
      } else navigateController(action);
    });
  });
  $('#strum-delay').addEventListener('input', () => {
    const value = Number($('#strum-delay').value);
    if (Number.isInteger(value) && value >= 1 && value <= 1000) {
      performanceSettings.delay = value;
      clearError();
      renderScreen();
      renderInspector();
    }
  });
  $('#strum-delay').addEventListener('change', () => {
    const value = Number($('#strum-delay').value);
    if (!Number.isInteger(value) || value < 1 || value > 1000) { $('#strum-delay').value = performanceSettings.delay; showError('Strum delay must be 1 to 1000 ms.'); return; }
    changePerformance({ delay: value }, `Strum delay: ${value} ms.`);
  });
  $('#strum-direction').addEventListener('change', () => changePerformance({ order: Number($('#strum-direction').value) }));
  $('#melody-layout').addEventListener('change', () => setMelodyLayout($('#melody-layout').value));
  const releasePointer = (event) => {
    const pad = pointerPads.get(event.pointerId);
    if (pad === undefined) return;
    pointerPads.delete(event.pointerId);
    releasePad(pad, `pointer:${event.pointerId}`);
  };
  document.addEventListener('pointerup', releasePointer);
  document.addEventListener('pointercancel', releasePointer);
  document.addEventListener('pointerup',releaseNavigationPointer);
  document.addEventListener('pointercancel',releaseNavigationPointer);
  $('#category-select').addEventListener('change', () => chooseCategory());
  $('#group-select').addEventListener('change', chooseScale);
  $('#root-select').addEventListener('change', chooseScale);
  $('#transpose-down').addEventListener('click', () => changeTranspose(-1));
  $('#transpose-up').addEventListener('click', () => changeTranspose(1));
  $('#velocity').addEventListener('input', () => { $('#velocity-value').textContent = $('#velocity').value; renderScreen(); });
  $('#bend').addEventListener('input', () => setBend(Number($('#bend').value)));
  $('#bend').addEventListener('pointerup', resetBend);
  $('#bend').addEventListener('pointercancel', resetBend);
  $('#bend').addEventListener('keyup', resetBend);
  $('#bend').addEventListener('blur', resetBend);
  $('#pressure').addEventListener('input', () => {
    const value = Number($('#pressure').value);
    $('#pressure-value').textContent = value;
    for (let pad = 0; pad < 96; pad++) if (engine.isHeld(pad)) {
      checkResult(engine.firmware.pressurePad(pad, value), 'Pad pressure');
      collectEvents();
    }
    afterOperation();
  });
  $('#latch-toggle').addEventListener('click', () => {
    latch = !latch;
    $('#latch-toggle').setAttribute('aria-pressed', String(latch));
    $('#latch-toggle span').textContent = latch ? 'ON' : 'OFF';
    if (!latch) for (const [pad, owners] of physicalOwners) if (!owners.size) {
      checkResult(engine.release(pad), 'Latch release');
      physicalOwners.delete(pad);
    }
    afterOperation();
  });
  $('#sustain-toggle').addEventListener('click', () => { manualSustain = !manualSustain; setSustain(manualSustain || keyboardSustain); });
  $('#panic').addEventListener('click', panic);
  for (const control of $$('input[data-route], #chord-channel, #melody-channel')) control.addEventListener('change', () => { applySustainRoutes(); renderInspector(); });
  $$('[data-scenario]').forEach((button) => button.addEventListener('click', () => runScenario(button.dataset.scenario)));
  $('#clear-events').addEventListener('click', () => {
    eventCount = 0;
    startedAt = performance.now();
    $('#event-count').textContent = '0 events';
    const row = textElement('tr', '', 'timeline-empty');
    const cell = textElement('td', 'Your first note starts here.');
    cell.colSpan = 6;
    row.append(cell);
    $('#event-list').replaceChildren(row);
  });
  document.addEventListener('keydown', (event) => {
    if(companionShell)return;
    if (event.key === 'Escape') { event.preventDefault(); panic(); return; }
    if (event.ctrlKey || event.metaKey || event.altKey || event.target.closest('input, select, textarea, [contenteditable="true"]')) return;
    const focusedNav=event.target.closest('[data-control="nav"]')?.dataset.nav;
    const heldAction=event.key==='ArrowUp'?'up':event.key==='ArrowDown'?'down':
      (event.key==='Enter'||event.code==='Space')&&['up','down'].includes(focusedNav)?focusedNav:null;
    if(heldAction) {
      event.preventDefault();
      if(!event.repeat){navigationKeys.set(event.code,heldAction);heldNavigation(heldAction,`key:${event.code}`,true);}
      return;
    }
    if(event.repeat)return;
    const navigation = { ArrowLeft: 'left', ArrowUp: 'up', ArrowRight: 'right', ArrowDown: 'down' }[event.key];
    if (navigation) { event.preventDefault(); navigateController(navigation); return; }
    if (event.key === 'Enter' && !event.target.closest('button, a, summary')) { event.preventDefault(); navigateController('select'); return; }
    // Pad focus remains in performance mode; other controls keep native Space activation.
    if (event.code === 'Space' && (!event.target.closest('button') || event.target.closest('.pad'))) { event.preventDefault(); downKeys.set(' ', 'sustain'); keyboardSustain = true; setSustain(manualSustain || keyboardSustain); return; }
    const key = event.key.toLowerCase();
    const pad = KEY_PADS.get(key);
    if (pad === undefined) return;
    event.preventDefault();
    downKeys.set(key, pad);
    pressPad(pad, `key:${key}`);
  });
  document.addEventListener('keyup', (event) => {
    const navigation=navigationKeys.get(event.code);
    if(navigation){navigationKeys.delete(event.code);event.preventDefault();heldNavigation(navigation,`key:${event.code}`,false);return;}
    const key = event.code === 'Space' ? ' ' : event.key.toLowerCase();
    const pad = downKeys.get(key);
    if (pad === undefined) return;
    downKeys.delete(key);
    event.preventDefault();
    if (pad === 'sustain') { keyboardSustain = false; setSustain(manualSustain || keyboardSustain); }
    else releasePad(pad, `key:${key}`);
  });
  window.addEventListener('blur', panic);
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) { panic(); oledPreview?.pause(); }
    else if(!companionShell)oledPreview?.resume();
  });
  window.addEventListener('pagehide', () => { panic(); engine?.close?.(); oledPreview?.close(); });
}

function changeTranspose(delta) {
  transpose = Math.max(-24, Math.min(24, transpose + delta));
  $('#transpose-value').textContent = `${transpose > 0 ? '+' : ''}${transpose}`;
  $('#transpose-down').disabled = transpose === -24;
  $('#transpose-up').disabled = transpose === 24;
  renderPads();
}

async function loadRuntimeData() {
  const embedded = document.getElementById('thy-runtime-data');
  if (embedded) return JSON.parse(embedded.textContent);
  const response = await fetch('./generated/chords.runtime.json');
  if (!response.ok) throw new Error(`Chord data request failed (${response.status}). Run the workbench build command first.`);
  return response.json();
}

function initializeApprovedStudio() {
  return initializeEngineStudio({ engine, groups: data.groups,
    getContext: () => ({ scaleName: currentScaleName(), voices: engine.voices(), held: engine.stats().heldPads }),
    onChange: () => {
      // Project committed C values and service the validated catalogue request
      // before rendering or accepting the next JS input event.
      const model = engineStudio?.model;
      if (model) {
        const group = model.get('performance.scale');
        const root = ROOT_KEYS[engine.firmware.state().root];
        transpose = model.get('performance.transpose');
        $('#transpose-value').textContent = transpose > 0 ? `+${transpose}` : transpose;
        applyFirmwareState();
        if (sustain) applySustainRoutes();
        $('#root-select').value = root;
        if (group !== selectedScale.group_index) chooseGroup(group);
        else if (root !== selectedScale.root_label) chooseScale();
        else renderPads();
      }
      libraryLab?.refresh(); renderScreen(); renderPhysicalControls();
    },
    onActivate: () => {} });
}

// The connected client needs public menu metadata, never the archival pad corpus.
// The separately packaged local workbench retains its embedded-data startup.
async function initializeConnectedCompanion() {
  const loaded = await Promise.all([createCore(), desktopBindingsPromise]);
  engine = loaded[0];
  const bindings = loaded[1];
  engine.state.navigationAnimations(false);
  engine.state.boot.skip();
  for (let index = 0; index < 96; index++) {
    const pad = document.createElement('button');
    pad.type = 'button'; pad.className = 'pad'; pad.dataset.pad = index;
    // Enable the RGB projection styles without assigning any local pad color.
    pad.dataset.light = 'device';
    pad.dataset.testid = `pad-${index}`;
    pad.setAttribute('aria-label', `${index < 48 ? 'Left' : 'Right'} pad ${index % 48 + 1}`);
    pad.setAttribute('aria-description', 'Inspect this pad on the connected board.');
    pad.style.gridColumn = String(Math.floor((index % 48) / 4) + 1);
    pad.style.gridRow = String(index % 4 + 1);
    $(index < 48 ? '#chord-pads' : '#melody-pads').append(pad);
  }
  const context = () => ({ selectedPad: 0, scaleName: '', voices: [], held: 0 });
  firmwareStudio = initializeFirmwareStudio({ engine, getContext: context });
  engineStudio = initializeEngineStudio({ engine, groups: SCALE_CATALOG, getContext: context,
    onChange: () => {}, onActivate: () => {} });
  rhythmLab = initializeRhythmLab({ engine });
  arpLab = initializeArpLab({ engine });
  sequencerLab = initializeSequencerLab({ engine });
  mpeLab = initializeMpeLab({ engine });
  libraryLab = initializeLibraryLab($('#library-lab'), engine);
  companionShell = initializeCompanionShell({ engine, studio: engineStudio, groups: SCALE_CATALOG,
    ...(bindings || {}),
    applyPayload: async payload => {
      if (!engine.settings.apply(payload)) throw new Error('The device configuration could not be decoded.');
      engine.state.boot.skip();
      engineStudio.refresh(); libraryLab.refresh(); sequencerLab.refresh();
    } });
  if (bindings) {
    document.title = 'THY666';
    const runtime = bindings.runtime;
    const activity = active => { document.documentElement.dataset.hostActive = String(active); companionShell.setHostActive(active); };
    activity(runtime.active);
    const unsubscribe = await runtime.subscribeActivity(activity);
    const notice = document.createElement('p'); notice.className = 'desktop-simulation-banner';
    notice.hidden = true; notice.setAttribute('role', 'alert');
    document.querySelector('.companion-topbar').after(notice);
    const unsubscribeNotices = await runtime.subscribeNotices(message => { notice.textContent = message; notice.hidden = false; });
    if (runtime.simulation) {
      const badge = document.createElement('p'); badge.className = 'desktop-simulation-banner';
      badge.textContent = 'SIMULATION · No physical USB access. Physical device health is not verified.';
      notice.before(badge);
    }
    window.addEventListener('pagehide', () => { unsubscribe(); unsubscribeNotices(); companionShell.destroy(); }, { once: true });
  }
  // Both archive-free browser/desktop startup and the local workbench need
  // the same observer for existing controls and later dialogs/page content.
  initializeUIComponents();
  window.thyWorkbench = Object.freeze({ engine, menuDesign: engineStudio, companion: companionShell, desktop: bindings?.runtime || null });
  document.documentElement.dataset.ready = 'true';
  startup.ready();
}

async function initialize() {
  try {
    if (!document.getElementById('thy-runtime-data')) {
      await initializeConnectedCompanion();
      return;
    }
    const loaded = await Promise.all([
      createCore(),
      loadRuntimeData(),
      loadOLEDProfile().catch((error) => ({ error: error.message })),
      desktopBindingsPromise,
    ]);
    [engine, data] = loaded;
    engine.state.navigationAnimations(false);
    if (data.format !== 'thy333.runtime-chords' || data.version !== 1) throw new Error('Unsupported runtime chord data format.');
    for (const id of ['#chord-channel', '#melody-channel']) populateSelect($(id), Array.from({ length: 16 }, (_, index) => ({ index, name: String(index + 1) })));
    $('#melody-channel').value = '1';
    populateSelect($('#category-select'), data.categories);
    populateSelect($('#root-select'), ROOT_ORDER.map((root) => ({ index: root, name: root })));
    $('#root-select').value = 'C';
    const major = data.groups.find((group) => group.name === 'Major') || data.groups[0];
    $('#category-select').value = major.category_index;
    buildPads();
    chooseCategory(major.index);
    $('#melody-layout').value = melodyLayout;
    initializeOLED(loaded[2]);
    wireControls();
    firmwareStudio = initializeFirmwareStudio({ engine,
      getContext: () => ({ selectedPad, scaleName: currentScaleName() }), selectPad,
      onEvents: () => { if (engine.stats().queued) afterOperation(); },
      onChange: () => {
        const signature = JSON.stringify(engine.firmware.state());
        if (signature !== lastStudioState) { lastStudioState = signature; applyFirmwareState(); renderPads(); }
      } });
    engineStudio = initializeApprovedStudio();
    rhythmLab = initializeRhythmLab({ engine, onEvents: afterOperation });
    arpLab = initializeArpLab({ engine, onEvents: afterOperation });
    sequencerLab = initializeSequencerLab({engine,onEvents:afterOperation,onChange:()=>{renderPads();engineStudio?.refresh();}});
    mpeLab = initializeMpeLab({engine,onEvents:afterOperation});
    libraryLab = initializeLibraryLab($('#library-lab'),engine,{onChange:()=>{
      applyFirmwareState();renderPads();engineStudio?.refresh();
    },onError:error=>feedback(error.message)});
    const saveStatus = document.createElement('span');
    saveStatus.id = 'preview-save-status'; saveStatus.className = 'status-pill';
    saveStatus.title = 'Settings saved in this browser only. Device settings use their own flash journal.';
    $('.header-actions').prepend(saveStatus);
    let storage;
    try { storage = localStorage; } catch { storage = { getItem() { throw new Error('Storage unavailable'); } }; }
    previewSettings = new PreviewSettings(engine, storage, text => { saveStatus.textContent = text; });
    previewSettings.restore(catalog => {
      if (!data.groups.some(group => group.index === catalog.group) || !ROOT_KEYS[catalog.root]) return false;
      $('#root-select').value = ROOT_KEYS[catalog.root];
      chooseGroup(catalog.group);
      return true;
    });
    transpose = engineStudio.model.get('performance.transpose');
    applyFirmwareState();
    libraryLab.refresh();
    engineStudio.replayBoot();

    changePerformance({});
    $('#core-status').className = 'status-pill';
    $('#core-status span:last-child').textContent = 'Native core · WebAssembly';
    companionShell=initializeCompanionShell({engine,studio:engineStudio,groups:data.groups,
      ...(loaded[3]||{}),
      onPreviewStart:()=>panic(),onInspect:selectPad,
      applyPayload:async payload=>{
        const catalog=engine.settings.catalog(payload);
        if(!catalog||!data.groups.some(group=>group.index===catalog.group)||!ROOT_KEYS[catalog.root])
          throw new Error('The saved catalogue cannot be loaded by this preview.');
        panic();engineStudio.model.play();
        $('#root-select').value=ROOT_KEYS[catalog.root];chooseGroup(catalog.group);
        if(!engine.settings.apply(payload))throw new Error('The device configuration could not be mirrored into this preview.');
        engine.state.boot.skip();applyFirmwareState();renderPads();libraryLab.refresh();engineStudio.refresh();
      }});
    if(loaded[3]){
      const runtime=loaded[3].runtime;
      document.documentElement.dataset.host='desktop';
      document.title='THY666';
      const hostActivity=active=>{document.documentElement.dataset.hostActive=String(active);companionShell.setHostActive(active);};
      hostActivity(runtime.active);
      if(runtime.simulation){
        const badge=document.createElement('p');badge.className='desktop-simulation-banner';badge.setAttribute('role','status');
        badge.textContent='SIMULATION · No physical USB access. Updates use a fake backend; physical device health is not verified.';
        document.querySelector('.companion-topbar')?.after(badge);
      }
      const unsubscribe=await runtime.subscribeActivity(hostActivity);
      const nativeNotice=document.createElement('p');nativeNotice.className='desktop-simulation-banner';nativeNotice.hidden=true;nativeNotice.setAttribute('role','alert');document.querySelector('.companion-topbar')?.after(nativeNotice);
      const unsubscribeNotices=await runtime.subscribeNotices(message=>{nativeNotice.textContent=message;nativeNotice.hidden=false;});
      window.addEventListener('pagehide',()=>{unsubscribe();unsubscribeNotices();companionShell.destroy();},{once:true});
    }
    initializeUIComponents();
    oledPreview?.pause();
    renderScreen();
    // Read-only hooks support browser integration checks against actual WASM state.
    window.thyWorkbench = Object.freeze({ engine, studio: firmwareStudio, menuDesign: engineStudio, rhythmLab, arpLab, mpeLab, sequencerLab, libraryLab, companion:companionShell, desktop:loaded[3]?.runtime||null, getScale: () => selectedScale, getPad, getTranspose: () => transpose, getOLED: () => oledPreview, panic });
    document.documentElement.dataset.ready = 'true';
    startup.ready();
  } catch (error) {
    $('#core-status').className = 'status-pill failed';
    $('#core-status span:last-child').textContent = 'Core unavailable';
    showError(`The workbench could not load. ${error.message} Serve the web folder over localhost; browser file:// pages cannot load the core and dataset.`);
    document.documentElement.dataset.ready = 'failed';
    startup.fail(error);
  }
}

initialize();
