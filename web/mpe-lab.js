// UI and command dispatch only. Channel allocation, pressure/glide conversion,
// note ownership and MIDI ordering belong to the shared C implementation.
const integer = (value, low, high) => Number.isInteger(value) && value >= low && value <= high;
const invalid = () => ({ ok: false, code: 1, status: 'invalid' });
const bounds = { members: [1, 15], bendRange: [1, 96], managerBendRange: [1, 96], padWindow: [1, 11], semitoneWindow: [1, 24] };
const flags = new Set(['enabled', 'upperZone', 'glide']);

export function createMpeLabModel({ expression, rhythm, getProfile = () => null, onEvents = () => {}, now = () => Math.floor(performance.now()) >>> 0 }) {
  if (!expression || !rhythm) throw new TypeError('Shared C expression and rhythm APIs are required.');
  let selectedPad = null;
  let notice = 'Enable MPE while quiet, then hold pads on the controller above.';
  const operation = (label, command) => {
    const advanced = rhythm.advance(now());
    const result = advanced?.ok === false ? advanced : command();
    onEvents();
    notice = result?.ok === false ? `${label}: ${result.status || result.code || 'rejected'}` : label;
    return result;
  };
  return {
    snapshot: () => ({ config: expression.config(), voices: expression.voices(), stats: expression.stats(), selectedPad, notice }),
    selectPad(pad) {
      if (!integer(pad, 0, 95)) return invalid();
      selectedPad = pad;
      return { ok: true, code: 0, status: 'ok' };
    },
    configure(changes) {
      if (!changes || typeof changes !== 'object' || Array.isArray(changes) ||
          Object.entries(changes).some(([key, value]) => flags.has(key) ? typeof value !== 'boolean' :
            !Object.hasOwn(bounds, key) || !integer(value, ...bounds[key]))) return invalid();
      return operation('MPE configuration updated', () => expression.configure(changes));
    },
    sendSetup() {
      return operation('Receiver setup generated in the preview MIDI timeline', () => expression.sendSetup());
    },
    pressure(value, pad = selectedPad) {
      if (!integer(pad, 0, 95) || !integer(value, 0, 127)) return invalid();
      const voice=expression.voices().find(item=>item.pad===pad&&(item.state===1||item.state==='held'));
      if(getProfile(pad)?.enabled && voice) {
        if(!expression.axes)return invalid();
        return operation('Primary pressure updated',()=>expression.axes(pad,value,voice.timbre));
      }
      return operation('Pad pressure updated', () => expression.pressure(pad, value));
    },
    aftertouch(value,pad=selectedPad) {
      if(!integer(pad,0,95)||!integer(value,0,127)||!getProfile(pad)?.enabled||!expression.axes)return invalid();
      const voice=expression.voices().find(item=>item.pad===pad&&(item.state===1||item.state==='held'));
      if(!voice)return invalid();
      return operation('Aftertouch updated',()=>expression.axes(pad,voice.pressure,value));
    },
    bend(value, pad = selectedPad) {
      if (!integer(pad, 0, 95) || !integer(value, 0, 16383)) return invalid();
      return operation('Pad pitch bend updated', () => expression.bend(pad, value));
    },
  };
}

const node = (document, tag, text) => {
  const element = document.createElement(tag);
  if (text !== undefined) element.textContent = text;
  return element;
};
const padLabel = logical => `${logical < 48 ? 'Left' : 'Right'} · row ${logical % 4 + 1}, pad ${Math.floor((logical % 48) / 4) + 1}`;
const noteLabel = pitch => `${['C', 'C♯', 'D', 'E♭', 'E', 'F', 'F♯', 'G', 'A♭', 'A', 'B♭', 'B'][pitch % 12]}${Math.floor(pitch / 12) - 1}`;
const destinationLabel = mask => ['USB 1', 'USB 2', 'DIN'].filter((_, index) => mask & (1 << index)).join(' + ');

export function initializeMpeLab({ engine, onEvents, now, document = globalThis.document }) {
  const model = createMpeLabModel({ expression: engine.expression, rhythm: engine.rhythm,
    getProfile:pad=>engine.firmware.expressionProfile(pad),onEvents,now });
  const panel = node(document, 'section');
  panel.id = 'mpe-lab'; panel.className = 'panel mpe-lab';
  panel.setAttribute('aria-label', 'Shared C MPE expression preview');
  panel.innerHTML = `
    <div class="engine-heading"><div><p class="eyebrow">THY666 / SHARED C EXPRESSION</p><h2>MPE & pressure glide</h2><p>Independent note channels, shared chord pressure and an optional neighboring-pad glide.</p></div><span class="engine-scope-badge">C / WebAssembly</span></div>
    <p class="mpe-scope">Browser preview only · no MIDI hardware connected. This first milestone supports live pads and chords. Arps and sequencer operation remain separate; disable them and release all pads before enabling MPE or changing its configuration.</p>
    <div class="mpe-fields">
      <label class="mpe-check"><input type="checkbox" data-mpe="enabled"> Enable MPE</label>
      <label>Zone<select data-mpe="upperZone"><option value="false">Lower · manager channel 1</option><option value="true">Upper · manager channel 16</option></select></label>
      <label>Member channels<input type="number" data-mpe="members" min="1" max="15" step="1"></label>
      <label>Member bend ± semitones<input type="number" data-mpe="bendRange" min="1" max="96" step="1"></label>
      <label>Manager bend ± semitones<input type="number" data-mpe="managerBendRange" min="1" max="96" step="1"></label>
    </div>
    <div class="mpe-zone-readout" data-readout="zone"></div>
    <div class="mpe-fields mpe-glide">
      <label class="mpe-check"><input type="checkbox" data-mpe="glide"> Neighbor glide</label>
      <label>Neighbor window · pads<input type="number" data-mpe="padWindow" min="1" max="11" step="1"></label>
      <label>Pitch window · semitones<input type="number" data-mpe="semitoneWindow" min="1" max="24" step="1"></label>
    </div>
    <p class="mpe-help">Hold a note or chord, then a nearby pad in the same row. Pressure balance glides the entire original chord; the neighboring pad becomes a modifier. The C engine checks pad distance and musical interval separately. Use the pressure selector below to vary each held contact independently.</p>
    <details class="mpe-receiver"><summary>Receiver setup</summary><p class="mpe-help">A real MPE receiver must use the matching zone and bend ranges. This explicit command generates MPE configuration and pitch-range RPN messages, which change a receiver’s configuration when delivered. Here they appear only in the local MIDI timeline. Selecting a zone or pressing a pad does not send this setup automatically.</p><button type="button" class="button button-small" data-action="setup">Send receiver setup · preview only</button></details>
    <div class="mpe-simulator">
      <label>Held contact<select data-field="pad" aria-label="Held contact for pressure simulation"></select></label>
      <label>Pressure <output data-readout="pressure">0</output><input data-field="pressure" type="range" min="0" max="127" value="0" aria-label="Selected contact pressure"></label>
      <label>Pitch bend <output data-readout="bend">8192</output><input data-field="bend" type="range" min="0" max="16383" value="8192" aria-label="Selected pad pitch bend"></label>
      <label data-field="aftertouch-row" style="grid-column:1/-1" hidden>Upper Aftertouch <output data-readout="aftertouch">0</output><input data-field="aftertouch" type="range" min="0" max="127" value="0" aria-label="Selected source aftertouch"></label>
    </div>
    <p class="mpe-help">Press pads on the controller above. Selecting a contact changes no notes or pressure. Pitch bend springs back to center after release. With a Dual-stage expression profile, primary pressure and aftertouch can be auditioned independently; a glide contact controls pressure only.</p>
    <dl class="mpe-runtime"><div><dt>Member leases</dt><dd data-readout="active">0</dd></div><div><dt>Glide contacts</dt><dd data-readout="consumed">0</dd></div></dl>
    <div class="mpe-table-wrap"><table class="mpe-ledger"><caption>Member-channel ownership · desired / last queued expression</caption><thead><tr><th>Source</th><th>Note</th><th>Channel</th><th>Output</th><th>Primary</th><th>Upper AT</th><th>Bend</th><th>State</th><th>Glide</th></tr></thead><tbody></tbody></table></div>
    <p data-readout="notice" class="mpe-notice" role="status" aria-live="polite"></p>`;
  (document.querySelector('#arp-lab') || document.querySelector('#rhythm-lab') || document.querySelector('.instrument')).insertAdjacentElement('afterend', panel);
  const find = selector => panel.querySelector(selector);
  const field = name => find(`[data-mpe="${name}"]`);
  let contactsKey = '', ledgerKey = '', selectedContact = null;
  const contacts = voices => {
    const result = new Map();
    for (const voice of voices) {
      if (voice.state === 0 || voice.state === 2 || voice.state === 'free' || voice.state === 'retiring') continue;
      result.set(voice.pad, { pad: voice.pad, label: padLabel(voice.logical), pressure: voice.pressure,
        aftertouch:voice.timbre,bend:voice.bend,modifier:false,dualProfile:engine.firmware.expressionProfile(voice.pad).enabled });
      // The bridge exposes C's encoded modifier identity: zero means none;
      // positive values are electrical source ID + 1, never a pitch offset.
      if (voice.modifier) result.set(voice.modifier - 1, { pad: voice.modifier - 1, label: `Glide contact · source ${voice.modifier}`, pressure: voice.modifierPressure, modifier: true });
    }
    return [...result.values()];
  };
  const sync = (element, value, force) => {
    if (force || document.activeElement !== element) element.value = String(value);
  };
  function refresh(force = false) {
    const state = model.snapshot(), config = state.config;
    for (const key of ['enabled', 'glide']) field(key).checked = config[key];
    for (const key of ['upperZone', ...Object.keys(bounds)]) sync(field(key), config[key], force);
    const manager = config.upperZone ? 16 : 1;
    const first = config.upperZone ? 16 - config.members : 2;
    const last = config.upperZone ? 15 : 1 + config.members;
    find('[data-readout="zone"]').textContent = `Manager ${manager} · members ${first}–${last} · ${config.enabled ? 'MPE ON' : 'MPE OFF'}`;
    find('[data-action="setup"]').disabled = !config.enabled;
    const available = contacts(state.voices);
    const newContactsKey = JSON.stringify(available.map(contact => [contact.pad, contact.label]));
    if (newContactsKey !== contactsKey) {
      contactsKey = newContactsKey;
      find('[data-field="pad"]').replaceChildren(...(available.length ? available.map(contact => {
        const option = node(document, 'option', contact.label); option.value = contact.pad; return option;
      }) : [node(document, 'option', 'Hold a pad on the controller') ]));
    }
    let contact = available.find(item => item.pad === state.selectedPad);
    if (!contact && available.length) { contact = available[0]; model.selectPad(contact.pad); }
    const currentPad = contact?.pad ?? null;
    const contactChanged = currentPad !== selectedContact;
    selectedContact = currentPad;
    find('[data-field="pad"]').disabled = !contact;
    if (contact) find('[data-field="pad"]').value = String(contact.pad);
    find('[data-field="pressure"]').disabled = !contact;
    find('[data-field="bend"]').disabled = !contact || contact.modifier;
    const hasAftertouch=Boolean(contact&&!contact.modifier&&contact.dualProfile);
    find('[data-field="aftertouch-row"]').hidden=!hasAftertouch;
    find('[data-field="aftertouch"]').disabled=!hasAftertouch;
    // Read every axis from C. Keeping contact pressure does not infer or reset
    // its independent aftertouch, including when selection changes.
    sync(find('[data-field="pressure"]'),contact?.pressure??0,contactChanged||force);
    sync(find('[data-field="bend"]'),contact?.bend??8192,contactChanged||force);
    sync(find('[data-field="aftertouch"]'),hasAftertouch?contact.aftertouch:0,contactChanged||force);
    find('[data-readout="pressure"]').textContent = find('[data-field="pressure"]').value;
    find('[data-readout="bend"]').textContent = find('[data-field="bend"]').value;
    find('[data-readout="aftertouch"]').textContent=find('[data-field="aftertouch"]').value;
    find('[data-readout="active"]').textContent = state.stats.active;
    find('[data-readout="consumed"]').textContent = state.stats.consumed;
    const nextLedgerKey = JSON.stringify(state.voices);
    if (nextLedgerKey !== ledgerKey) {
      ledgerKey = nextLedgerKey;
      const rows = state.voices.map(voice => {
        const row = node(document, 'tr');
        const values = [padLabel(voice.logical), noteLabel(voice.pitch), voice.channel + 1, destinationLabel(voice.destinations),
          `${voice.pressure} / ${voice.sentPressure}`, `${voice.timbre} / ${voice.sentTimbre}`, `${voice.bend} / ${voice.sentBend}`,
          voice.state === 2 ? 'Retiring' : voice.state === 1 ? 'Held' : String(voice.state),
          voice.modifier ? `${voice.interval > 0 ? '+' : ''}${voice.interval} st` : '—'];
        row.append(...values.map(value => node(document, 'td', value))); return row;
      });
      if (!rows.length) { const row = node(document, 'tr'), cell = node(document, 'td', 'No member channels owned. Hold a pad to inspect its voices.'); cell.colSpan = 9; row.append(cell); rows.push(row); }
      find('tbody').replaceChildren(...rows);
    }
    find('[data-readout="notice"]').textContent = state.notice;
  }
  for (const key of ['enabled', 'glide']) field(key).addEventListener('change', () => { model.configure({ [key]: field(key).checked }); refresh(true); });
  field('upperZone').addEventListener('change', () => { model.configure({ upperZone: field('upperZone').value === 'true' }); refresh(true); });
  for (const key of Object.keys(bounds)) field(key).addEventListener('change', () => {
    if (field(key).reportValidity()) { model.configure({ [key]: Number(field(key).value) }); refresh(true); }
  });
  find('[data-action="setup"]').addEventListener('click', () => { model.sendSetup(); refresh(true); });
  find('[data-field="pad"]').addEventListener('change', () => { model.selectPad(Number(find('[data-field="pad"]').value)); refresh(true); });
  find('[data-field="pressure"]').addEventListener('input', () => { model.pressure(Number(find('[data-field="pressure"]').value)); refresh(); });
  find('[data-field="aftertouch"]').addEventListener('input',()=>{model.aftertouch(Number(find('[data-field="aftertouch"]').value));refresh();});
  find('[data-field="bend"]').addEventListener('input', () => { model.bend(Number(find('[data-field="bend"]').value)); refresh(); });
  const releaseBend = () => { if (!find('[data-field="bend"]').disabled) { model.bend(8192); find('[data-field="bend"]').value = '8192'; refresh(); } };
  find('[data-field="bend"]').addEventListener('pointerup', releaseBend);
  find('[data-field="bend"]').addEventListener('pointercancel', releaseBend);
  refresh(true);
  return { model, refresh, element: panel };
}
