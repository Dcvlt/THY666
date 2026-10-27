// Browser controls only. Input admission, grouping, pitch generation, gates,
// swing, note ownership and the playhead all come from the shared C engine.
const arpInteger = (value, low, high) => Number.isInteger(value) && value >= low && value <= high;
const arpInvalid = () => ({ ok: false, code: 1, status: 'invalid' });

export function createArpLabModel({ arp, rhythm, onEvents = () => {}, now = () => Math.floor(performance.now()) >>> 0 }) {
  if (!arp || !rhythm) throw new TypeError('The shared C Arp and rhythm APIs are required.');
  let selected = arp.quickState?.().selected ?? 0, notice = 'Hold pads to contribute notes. Saved Arp settings restore without sounding held notes.';
  const operation = (label, command) => {
    const advanced = rhythm.advance(now());
    const result = advanced?.ok === false ? advanced : command();
    onEvents();
    notice = result?.ok === false ? `${label}: ${result.status || result.code || 'rejected'}` : label;
    return result;
  };
  const model = {
    snapshot() {
      if (arp.quickState) selected = arp.quickState().selected;
      const instances = [arp.snapshot(0), arp.snapshot(1)];
      return { selected, arp: instances[selected], instances, clock: rhythm.snapshot(), quick:arp.quickState?.(), notice };
    },
    select(index) {
      if (!arpInteger(index, 0, 1)) return arpInvalid();
      const response = arp.select?.(index);
      if (response?.ok === false) return response;
      selected = index;
      return { ok: true, code: 0, status: 'ok' };
    },
    openQuick() { return operation('OLED Arp page opened; CENTER returns to Play', () => arp.openQuick(selected)); },
    quickKeys(enabled) { return operation('Temporary M-key controls updated', () => arp.quickKeys(enabled)); },
    configure(config, index = selected) {
      if (!arpInteger(index, 0, 1) || !config || typeof config !== 'object' || Array.isArray(config)) return arpInvalid();
      return operation(`Arp ${index + 1} updated`, () => arp.configure(index, config));
    },
    clear(index = selected) {
      if (!arpInteger(index, 0, 1)) return arpInvalid();
      return operation(`Arp ${index + 1} cleared; settings retained`, () => arp.clear(index));
    },
    toggleStep(step) {
      if (!arpInteger(step, 0, 7)) return arpInvalid();
      return model.configure({ stepMask: arp.snapshot(selected).stepMask ^ (1 << step) });
    },
    setOffset(step, value) {
      if (!arpInteger(step, 0, 7) || !Number.isInteger(value)) return arpInvalid();
      const offsets = [...arp.snapshot(selected).offsets];
      offsets[step] = value;
      return model.configure({ offsets });
    },
    setZone(zone, enabled) {
      if (!arpInteger(zone, 0, 5) || typeof enabled !== 'boolean') return arpInvalid();
      const mask = arp.snapshot(selected).sourceZones;
      return model.configure({ sourceZones: enabled ? mask | (1 << zone) : mask & ~(1 << zone) });
    },
  };
  return model;
}

const arpNode = (document, tag, text, className = '') => {
  const element = document.createElement(tag);
  if (text !== undefined) element.textContent = text;
  element.className = className;
  return element;
};

export function initializeArpLab({ engine, onEvents, now, document = globalThis.document }) {
  const model = createArpLabModel({ arp: engine.arp, rhythm: engine.rhythm, onEvents, now });
  const panel = arpNode(document, 'section', undefined, 'panel arp-lab');
  panel.id = 'arp-lab';
  panel.setAttribute('aria-label', 'Shared C dual arpeggiators');
  panel.innerHTML = `
    <div class="engine-heading"><div><p class="eyebrow">THY666 / SHARED C MUSICAL ENGINE</p><h2>Arpeggiators</h2><p>Two independent Arps from the playable pads, using the shared musical clock.</p></div><span class="engine-scope-badge">C / WebAssembly</span></div>
    <p class="arp-scope">Browser preview · no MIDI hardware connected. Arp configuration saves with the shared settings; notes and playback state never restore. Hold input only; Latch, Arp-to-Strum links and release tails are not implemented.</p>
    <div class="arp-instance-bar"><div class="arp-tabs" role="group" aria-label="Edited Arpeggiator"><button type="button" data-arp="0" aria-pressed="true">Arp 1 <span data-instance-state="0">OFF</span></button><button type="button" data-arp="1" aria-pressed="false">Arp 2 <span data-instance-state="1">OFF</span></button></div><button data-action="enable" type="button" class="button" aria-pressed="false">Enable Arp 1</button><button data-action="clear" type="button" class="button button-small">Clear held inputs</button><span class="arp-hold-badge">HOLD</span></div>
    <div class="arp-instance-bar"><button data-action="quick-open" type="button" class="button button-small">Open OLED quick page</button><label><input type="checkbox" data-field="quick-keys"> Temporary M controls</label><span class="arp-help">M1/2 enable · M5/6 clear · M7 edit Arp · CENTER Play. M3/4: Hold only.</span></div>
    <div class="arp-step-heading"><span>Eight-step mask</span><span data-readout="step">No active phrase</span></div>
    <div class="arp-steps" role="group" aria-label="Arpeggiator step mask"></div>
    <p class="arp-help">Lit steps play; dark steps rest. The outline follows the selected phrase in C; other chord phrases may have different phases.</p>
    <div class="arp-main-fields">
      <label>Type<select data-field="type"><option value="notes">Notes</option><option value="chords">Chord phrases</option></select></label>
      <label>Input grouping<select data-field="grouping"><option value="hybrid">Chords separate + joined notes</option><option value="combined">All inputs combined</option><option value="each">Each input separately</option></select></label>
      <label>Rate<select data-field="rateTicks"></select></label>
      <label>Gate %<input data-field="gatePercent" type="number" min="1" max="100" step="1"></label>
      <label>Swing %<input data-field="swingPercent" type="number" min="0" max="50" step="1"></label>
      <label data-control="pattern">Note pattern<select data-field="pattern"><option value="up">Up</option><option value="down">Down</option><option value="up-down">Up / down</option><option value="down-up">Down / up</option><option value="as-played">As played</option></select></label>
    </div>
    <div data-control="offsets" class="arp-offsets" hidden><p>Chord steps · signed scale-degree offsets, not semitone transposition.</p><div class="arp-offset-fields"></div></div>
    <details class="arp-options"><summary>Sources, behavior & output</summary><div class="arp-advanced-fields">
      <label>Input<select data-field="source"><option value="63">Both sides</option><option value="7">Left</option><option value="56">Right</option><option value="zones">Selected zones</option></select></label>
      <label>Phrase start<select data-field="start"><option value="immediate">Immediate</option><option value="grid">On clock grid</option></select></label>
      <label>When input changes<select data-field="restartOnChange"><option value="false">Continue phrase</option><option value="true">Restart phrase</option></select></label>
      <label>Original notes<select data-field="layer"><option value="false">Layer OFF · generated only</option><option value="true">Layer ON · originals + generated</option></select></label>
      <label>Output channel<input data-field="channel" type="number" min="1" max="16" step="1"></label>
      <fieldset class="arp-destinations"><legend>Output destinations</legend><label><input type="checkbox" data-destination="1">USB 1</label><label><input type="checkbox" data-destination="2">USB 2</label><label><input type="checkbox" data-destination="4">DIN</label></fieldset>
    </div><fieldset class="arp-zones" hidden><legend>Six 16-pad input zones · four columns each</legend></fieldset><p class="arp-help">Source zones select inputs independently of the output route. Source-zone and Layer edits affect future presses; already-admitted held contributions remain until release or Clear. Enabling both Arps on the same sources deliberately layers their generated parts. Disable and Clear remove Arp inputs without re-adopting held pads: release and press again to contribute them again. Clear retains settings.</p></details>
    <dl class="arp-runtime"><div><dt>Inputs</dt><dd data-readout="inputs">0</dd></div><div><dt>Phrases</dt><dd data-readout="phrases">0</dd></div><div><dt>Owned notes</dt><dd data-readout="owners">0</dd></div><div><dt>Selected chord</dt><dd data-readout="chord">None</dd></div><div><dt>Errors / skipped steps</dt><dd data-readout="errors">0 / 0</dd></div></dl>
    <p data-readout="notice" class="arp-notice" role="status" aria-live="polite"></p>`;
  (document.querySelector('#rhythm-lab') || document.querySelector('#engine-studio') || document.querySelector('.instrument')).insertAdjacentElement('afterend', panel);
  const find = selector => panel.querySelector(selector);
  const field = name => find(`[data-field="${name}"]`);
  const zoneEditing = [false, false];
  let rateIdentity = '';
  for (let step = 0; step < 8; step++) {
    const button = arpNode(document, 'button', String(step + 1));
    button.type = 'button'; button.dataset.step = step;
    button.setAttribute('aria-label', `Arp step ${step + 1}`);
    button.addEventListener('click', () => { model.toggleStep(step); refresh(true); });
    find('.arp-steps').append(button);
    const label = arpNode(document, 'label', String(step + 1));
    const input = document.createElement('input');
    input.type = 'number'; input.step = '1'; input.min = '-24'; input.max = '24';
    input.dataset.offset = step; input.setAttribute('aria-label', `Chord step ${step + 1} scale-degree offset`);
    input.addEventListener('change', () => { if (input.reportValidity()) { model.setOffset(step, Number(input.value)); refresh(true); } });
    label.append(input); find('.arp-offset-fields').append(label);
  }
  for (let zone = 0; zone < 6; zone++) {
    const label = arpNode(document, 'label');
    const input = document.createElement('input'); input.type = 'checkbox'; input.dataset.zone = zone;
    input.addEventListener('change', () => { model.setZone(zone, input.checked); refresh(true); });
    label.append(input, document.createTextNode(`${zone < 3 ? 'Left' : 'Right'} ${zone % 3 + 1}`));
    find('.arp-zones').append(label);
  }
  const syncValue = (input, value, force) => {
    if (force || document.activeElement !== input) input.value = String(value);
  };
  function refresh(force = false) {
    const snapshot = model.snapshot(), current = snapshot.arp;
    field('quick-keys').checked = Boolean(snapshot.quick?.keys);
    panel.querySelectorAll('[data-arp]').forEach(button => button.setAttribute('aria-pressed', String(Number(button.dataset.arp) === snapshot.selected)));
    snapshot.instances.forEach((instance, index) => {
      const clockReady = ['internal', 'locked'].includes(snapshot.clock.clockState);
      const externalClock = snapshot.clock.clockSource === 'usb';
      find(`[data-instance-state="${index}"]`).textContent = !instance.enabled ? 'OFF'
        : externalClock && snapshot.clock.clockState === 'waiting' ? 'WAITING CLOCK'
        : externalClock && snapshot.clock.clockState === 'lost' ? 'CLOCK LOST'
        : instance.step !== null && clockReady ? 'PLAYING' : 'ON';
    });
    const enable = find('[data-action="enable"]');
    enable.textContent = `${current.enabled ? 'Disable' : 'Enable'} Arp ${snapshot.selected + 1}`;
    enable.setAttribute('aria-pressed', String(current.enabled));
    panel.querySelectorAll('[data-step]').forEach(button => {
      const step = Number(button.dataset.step);
      button.setAttribute('aria-pressed', String(Boolean(current.stepMask & (1 << step))));
      button.dataset.playhead = String(current.step === step);
    });
    find('[data-readout="step"]').textContent = current.step === null ? 'No active phrase' : `Selected phrase · step ${current.step + 1}`;
    const ppqn = snapshot.clock.ppqn || 960;
    const rates = [[ppqn * 4, 'Whole note'], [ppqn * 2, 'Half note'], [ppqn, 'Quarter note'],
      [ppqn / 2, 'Eighth note'], [ppqn / 4, 'Sixteenth note'], [ppqn / 8, 'Thirty-second note'],
      [ppqn / 3, 'Eighth-note triplet'], [ppqn / 6, 'Sixteenth-note triplet']];
    if (!rates.some(([ticks]) => ticks === current.rateTicks)) rates.push([current.rateTicks, `${current.rateTicks} ticks`]);
    const ratesKey = JSON.stringify(rates);
    if (ratesKey !== rateIdentity) {
      rateIdentity = ratesKey;
      field('rateTicks').replaceChildren(...rates.map(([ticks, label]) => {
        const option = arpNode(document, 'option', label); option.value = ticks; return option;
      }));
    }
    for (const name of ['type', 'grouping', 'rateTicks', 'gatePercent', 'swingPercent', 'pattern', 'start', 'restartOnChange', 'layer'])
      syncValue(field(name), current[name], force);
    syncValue(field('channel'), current.channel + 1, force);
    const preset = [63, 7, 56].includes(current.sourceZones) && !zoneEditing[snapshot.selected];
    syncValue(field('source'), preset ? current.sourceZones : 'zones', force);
    find('.arp-zones').hidden = Boolean(preset);
    panel.querySelectorAll('[data-zone]').forEach(input => { input.checked = Boolean(current.sourceZones & (1 << Number(input.dataset.zone))); });
    panel.querySelectorAll('[data-destination]').forEach(input => { input.checked = Boolean(current.mask & Number(input.dataset.destination)); });
    panel.querySelectorAll('[data-offset]').forEach(input => syncValue(input, current.offsets[Number(input.dataset.offset)], force));
    find('[data-control="pattern"]').hidden = current.type !== 'notes';
    find('[data-control="offsets"]').hidden = current.type !== 'chords';
    for (const name of ['inputs', 'phrases', 'owners']) find(`[data-readout="${name}"]`).textContent = current[name];
    find('[data-readout="chord"]').textContent = current.selectedChord === null ? 'None' : `Input ${current.selectedChord + 1}`;
    find('[data-readout="errors"]').textContent = `${current.errors} / ${current.skippedSteps}`;
    find('[data-readout="notice"]').textContent = snapshot.notice;
    return snapshot;
  }
  panel.querySelectorAll('[data-arp]').forEach(button => button.addEventListener('click', () => { model.select(Number(button.dataset.arp)); refresh(true); }));
  find('[data-action="enable"]').addEventListener('click', () => { model.configure({ enabled: !model.snapshot().arp.enabled }); refresh(true); });
  find('[data-action="clear"]').addEventListener('click', () => { model.clear(); refresh(true); });
  find('[data-action="quick-open"]').addEventListener('click', () => { model.openQuick(); refresh(true); });
  field('quick-keys').addEventListener('change', () => { model.quickKeys(field('quick-keys').checked); refresh(true); });
  for (const name of ['type', 'grouping', 'pattern', 'start']) field(name).addEventListener('change', () => { model.configure({ [name]: field(name).value }); refresh(true); });
  for (const name of ['layer', 'restartOnChange']) field(name).addEventListener('change', () => { model.configure({ [name]: field(name).value === 'true' }); refresh(true); });
  for (const name of ['rateTicks', 'gatePercent', 'swingPercent', 'channel']) field(name).addEventListener('change', () => {
    if (!field(name).reportValidity()) return;
    model.configure({ [name]: Number(field(name).value) - (name === 'channel' ? 1 : 0) }); refresh(true);
  });
  field('source').addEventListener('change', () => {
    const selected = model.snapshot().selected;
    zoneEditing[selected] = field('source').value === 'zones';
    if (!zoneEditing[selected]) model.configure({ sourceZones: Number(field('source').value) });
    refresh(true);
  });
  panel.querySelectorAll('[data-destination]').forEach(input => input.addEventListener('change', () => {
    const mask = [...panel.querySelectorAll('[data-destination]')].reduce((value, item) => value | (item.checked ? Number(item.dataset.destination) : 0), 0);
    model.configure({ mask }); refresh(true);
  }));
  refresh(true);
  return { model, refresh, element: panel };
}
