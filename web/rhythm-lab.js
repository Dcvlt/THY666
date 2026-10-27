// This panel sends commands to the shared C rhythm service. It deliberately
// contains no musical clock, note scheduler, arp or sequencer implementation.
const midiMessages = Object.freeze({ pulse: 0xf8, start: 0xfa, continue: 0xfb, stop: 0xfc, position: 0xf2 });
const integer = (value, min, max) => Number.isInteger(value) && value >= min && value <= max;

export function createRhythmLabModel({ rhythm, onEvents = () => {}, now = () => Math.floor(performance.now()) >>> 0 }) {
  if (!rhythm) throw new TypeError('The shared C rhythm API is required.');
  const handles = new Map();
  let selectedTrack = 0, bank = 0, notice = 'Track transport and ownership tests. Use the Arp panel for live phrases.';
  const invalid = () => ({ ok: false, code: 'invalid', status: 'invalid' });
  const operation = (label, command) => {
    const timestamp = now();
    // Synchronize the C timeline at the input edge. The normal render service
    // can be one frame behind an exact beat/bar boundary when a button lands.
    const advanced = rhythm.advance?.(timestamp);
    const result = advanced?.ok === false ? advanced : command(timestamp);
    // Drain real C events even on a rejected operation; never synthesize MIDI.
    onEvents();
    notice = result?.ok === false ? `${label}: ${result.status || result.code || 'rejected'}` : label;
    return result;
  };
  return {
    snapshot() {
      const state = rhythm.snapshot();
      // Source changes, track stop/reset and panic can invalidate C handles.
      // Retire only the local labels; ownership itself is always managed in C.
      if (state.generatedCount === 0) handles.clear();
      const runs = new Map();
      for (const [handle, owner] of handles) {
        const track = state.tracks?.[owner.track];
        if (owner.source === 'track' && track && (!track.running || owner.generation !== track.generation)) handles.delete(handle);
        if (owner.runGeneration !== undefined && rhythm.generation) {
          const key = `${owner.source}:${owner.track}`;
          if (!runs.has(key)) runs.set(key, rhythm.generation(owner.source, owner.track));
          if (owner.runGeneration !== runs.get(key)) handles.delete(handle);
        }
      }
      return { ...state, selectedTrack, bank, previewOwners: [...handles.values()], notice };
    },
    selectTrack(track) {
      if (!integer(track, 0, 20)) return invalid();
      selectedTrack = track; bank = Math.floor(track / 7);
      notice = `Track ${track + 1} selected. Selection does not change playback.`;
      return { ok: true };
    },
    selectBank(value) {
      if (!integer(value, 0, 2)) return invalid();
      bank = value; selectedTrack = bank * 7 + selectedTrack % 7;
      return { ok: true };
    },
    configure(config) {
      return operation('Clock configuration applied', () => {
        const before = rhythm.snapshot().clockSource;
        const result = rhythm.configure(config);
        if (result?.ok && before !== rhythm.snapshot().clockSource) handles.clear();
        return result;
      });
    },
    start() { return operation('Transport started', () => rhythm.start()); },
    stop() { return operation('Stopped and reset; live pads and independent Arps are preserved', () => rhythm.stop()); },
    toggleTrack(track = selectedTrack) {
      if (!integer(track, 0, 20)) return invalid();
      return operation(`Track ${track + 1} launch/stop requested`, () => rhythm.toggleTrack(track));
    },
    setTrackLoop(startTick, endTick) {
      return operation(`Track ${selectedTrack + 1} loop applied`, () => rhythm.setTrackLoop(selectedTrack, startTick, endTick));
    },
    midi(command, position = 0) {
      if (!Object.hasOwn(midiMessages, command) || !integer(position, 0, 16383)) return invalid();
      const status = midiMessages[command];
      return operation(`Synthetic USB ${command} sent to C`, timestamp => rhythm.midi(status,
        command === 'position' ? position & 127 : 0, command === 'position' ? position >> 7 : 0, timestamp));
    },
    acquire(options) {
      return operation('Generated owner acquired', () => {
        const result = rhythm.acquire(options);
        if (result?.ok && result.handle !== undefined) handles.set(result.handle, { ...options, handle: result.handle,
          generation: rhythm.snapshot().tracks?.[options.track]?.generation,
          runGeneration: rhythm.generation?.(options.source, options.track) });
        return result;
      });
    },
    release(handle) {
      return operation('Generated owner released', () => {
        const result = rhythm.release(handle);
        if (result?.ok) handles.delete(handle);
        return result;
      });
    },
    clearSource(source, track = selectedTrack) {
      return operation('Generated source cleared', () => {
        const result = rhythm.clearSource(source, track);
        if (result?.ok) for (const [handle, owner] of handles) {
          if (owner.source === source && (source !== 'track' || owner.track === track)) handles.delete(handle);
        }
        return result;
      });
    },
    forgetOwners() { handles.clear(); },
  };
}

const node = (document, tag, content, className = '') => {
  const element = document.createElement(tag);
  if (content !== undefined) element.textContent = content;
  element.className = className;
  return element;
};

export function initializeRhythmLab({ engine, onEvents, now, document = globalThis.document }) {
  const model = createRhythmLabModel({ rhythm: engine.rhythm, onEvents, now });
  const panel = node(document, 'section', undefined, 'panel rhythm-lab');
  panel.id = 'rhythm-lab';
  panel.setAttribute('aria-label', 'Shared clock, transport and generated-note ownership foundation');
  panel.innerHTML = `
    <div class="engine-heading"><div><p class="eyebrow">THY666 / SHARED C FOUNDATION</p><h2>Clock & transport</h2><p>Shared timing for live Arps, independent note owners and future recorded patterns.</p></div><span class="engine-scope-badge">C / WebAssembly</span></div>
    <p class="rhythm-scope">Preview only · no MIDI hardware connected. Track controls change transport state; there is no pattern recording or playback engine yet.</p>
    <div class="rhythm-columns">
      <div class="rhythm-clock">
        <form data-form="clock" class="rhythm-fields">
          <label>Clock<select data-field="source"><option value="internal">Internal</option><option value="usb">USB MIDI input</option></select></label>
          <label>Internal tempo<input data-field="bpm" type="number" min="20" max="300" step="1" value="120"></label>
          <label>Meter<div class="rhythm-meter"><input data-field="numerator" type="number" min="1" max="16" value="4" aria-label="Meter numerator"><select data-field="denominator" aria-label="Meter denominator"><option>1</option><option>2</option><option selected>4</option><option>8</option><option>16</option><option>32</option></select></div></label>
          <label>Track boundary<select data-field="quantization"><option value="bar">Next bar</option><option value="beat">Next beat</option><option value="immediate">Immediate</option></select></label>
          <button class="button button-small" type="submit">Apply clock</button>
        </form>
        <div class="rhythm-transport"><button type="button" data-action="start" class="button">5 · Play</button><button type="button" data-action="stop" class="button">5 · Stop/reset</button></div>
        <dl class="rhythm-readout"><div><dt>Transport / clock</dt><dd data-readout="transport">Stopped</dd></div><div><dt>Position</dt><dd data-readout="position">Bar 1 · beat 1</dd></div><div><dt>Timeline</dt><dd data-readout="ticks">0 ticks</dd></div><div><dt>Owners</dt><dd data-readout="owners">0 generated owners</dd></div></dl>
        <details class="rhythm-usb"><summary>Synthetic USB clock input</summary><p>These buttons pass actual MIDI messages to C. Pulse sends one F8; there is no JavaScript MIDI-clock generator. Stop sending pulses to observe the C timeout.</p><div class="rhythm-actions"><button type="button" data-midi="start" class="button button-small">Start FA</button><button type="button" data-midi="continue" class="button button-small">Continue FB</button><button type="button" data-midi="stop" class="button button-small">Stop FC</button><button type="button" data-midi="pulse" class="button button-small">Pulse F8</button></div><form data-form="position" class="rhythm-inline-form"><label>Song position<input type="number" data-field="song-position" min="0" max="16383" value="0"></label><button type="submit" class="button button-small">Send F2</button></form><p>Song position uses MIDI sixteenth-note units.</p></details>
      </div>
      <div class="rhythm-tracks"><div class="rhythm-track-heading"><h3>Tracks</h3><label>Bank<select data-field="bank"><option value="0">1 · Tracks 1–7</option><option value="1">2 · Tracks 8–14</option><option value="2">3 · Tracks 15–21</option></select></label></div><p class="rhythm-help">M selects. U3+M queues launch/stop at the selected boundary.</p><div data-list="tracks" class="rhythm-track-list"></div><form data-form="loop" class="rhythm-inline-form"><label>Loop start (ticks)<input data-field="loop-start" type="number" min="0" value="0"></label><label>Loop end (ticks)<input data-field="loop-end" type="number" min="1" value="3840"></label><button type="submit" class="button button-small">Apply to selected track</button></form><p class="rhythm-help">Loop editing tests state only; no recorded notes are present.</p></div>
    </div>
    <details class="rhythm-generated"><summary>Generated-note ownership test</summary><p>Acquire overlapping test notes in Arp or track owner contexts; this does not run an Arp. Matching pitches share the C note ledger with playable pads. A track must be running to acquire its owner. Release owners separately; Stop/reset clears track owners and preserves independent Arp owners. USB waiting/lost rejects generated attacks.</p><form data-form="owner" class="rhythm-fields"><label>Owner context<select data-field="owner-source"><option value="arp1">Arp 1 test context</option><option value="arp2">Arp 2 test context</option><option value="track">Selected track</option></select></label><label>Note<input data-field="note" type="number" min="0" max="127" value="60"></label><label>Channel<input data-field="channel" type="number" min="1" max="16" value="1"></label><label>Destination<select data-field="mask"><option value="5">USB 1 + DIN</option><option value="1">USB 1</option><option value="2">USB 2</option><option value="4">DIN</option><option value="7">All</option></select></label><label>Velocity<input data-field="velocity" type="number" min="1" max="127" value="96"></label><button type="submit" class="button button-small">Add owner</button><button type="button" data-action="clear-source" class="button button-small">Clear source</button></form><div data-list="owners" class="rhythm-owner-list"></div></details>
    <p data-readout="notice" class="rhythm-notice" role="status" aria-live="polite"></p>`;
  (document.querySelector('#engine-studio') || document.querySelector('.instrument')).insertAdjacentElement('afterend', panel);
  const find = selector => panel.querySelector(selector);
  const field = key => find(`[data-field="${key}"]`);
  const numeric = key => Number(field(key).value);
  let lastTracks = '', lastOwners = '', initial = true;
  function refresh() {
    const snapshot = model.snapshot();
    const tracks = snapshot.tracks || [];
    const transport = snapshot.transport || 'stopped';
    const clock = snapshot.clockState || snapshot.state || (typeof snapshot.clock === 'object' ? snapshot.clock.state : '') || 'waiting';
    const source = snapshot.clockSource || 'internal';
    const timing = source === 'usb' ? 'external pulse timing' : `${snapshot.bpm ?? 120} BPM`;
    find('[data-readout="transport"]').textContent = `${transport} · ${source} · ${clock} · ${timing}`;
    find('[data-readout="position"]').textContent = `Bar ${snapshot.bar ?? 1} · beat ${snapshot.beat ?? 1}`;
    find('[data-readout="ticks"]').textContent = `${snapshot.songTick ?? snapshot.tick ?? 0} song ticks · ${snapshot.ppqn ?? snapshot.PPQN ?? 960} PPQN`;
    find('[data-readout="owners"]').textContent = `${snapshot.generatedCount ?? snapshot.generated ?? 0} generated owners`;
    find('[data-readout="notice"]').textContent = snapshot.notice;
    field('bank').value = String(snapshot.bank);
    if (initial) {
      if (snapshot.bpm) field('bpm').value = snapshot.bpm;
      field('source').value = source;
      if (snapshot.meterNumerator) field('numerator').value = snapshot.meterNumerator;
      if (snapshot.meterDenominator) field('denominator').value = snapshot.meterDenominator;
      if (snapshot.quantization) field('quantization').value = snapshot.quantization;
      field('loop-start').value = String(tracks[0]?.loopStart ?? 0);
      field('loop-end').value = String(tracks[0]?.loopEnd ?? (snapshot.ppqn ?? snapshot.PPQN ?? 960) * 4);
      initial = false;
    }
    const identity = JSON.stringify([snapshot.bank, snapshot.selectedTrack, tracks.map(track =>
      [track.enabled, track.running, track.pending, track.pendingTick, track.generation, track.loopStart, track.loopEnd])]);
    if (identity !== lastTracks) {
      lastTracks = identity;
      find('[data-list="tracks"]').replaceChildren(...Array.from({ length: 7 }, (_, offset) => {
        const index = snapshot.bank * 7 + offset;
        const track = tracks[index] || {};
        const row = node(document, 'div', undefined, 'rhythm-track-row');
        const select = node(document, 'button', `M${offset + 1} · Track ${index + 1}`, 'rhythm-track-select');
        select.type = 'button'; select.setAttribute('aria-pressed', String(index === snapshot.selectedTrack));
        select.addEventListener('click', () => {
          model.selectTrack(index);
          if (track.loopStart !== undefined) field('loop-start').value = track.loopStart;
          if (track.loopEnd !== undefined) field('loop-end').value = track.loopEnd;
          refresh();
        });
        const state = node(document, 'span', `${track.running ? 'Running' : track.enabled ? 'Ready · stopped' : 'Stopped'}${track.pending ? ` · queued ${track.pending} @ ${track.pendingTick}` : ''}`, 'rhythm-track-status');
        const toggle = node(document, 'button', 'U3+M · Launch/stop', 'button button-small');
        toggle.type = 'button'; toggle.setAttribute('aria-label', `Launch or stop track ${index + 1}`);
        toggle.addEventListener('click', () => { model.toggleTrack(index); refresh(); });
        row.append(select, state, toggle); return row;
      }));
    }
    const owners = JSON.stringify(snapshot.previewOwners);
    if (owners !== lastOwners) {
      lastOwners = owners;
      find('[data-list="owners"]').replaceChildren(...snapshot.previewOwners.map(owner => {
        const row = node(document, 'div', undefined, 'rhythm-owner-row');
        row.append(node(document, 'span', `${owner.source === 'track' ? `Track ${owner.track + 1}` : owner.source} · note ${owner.note} · ch ${owner.channel + 1} · handle ${owner.handle}`));
        const release = node(document, 'button', 'Release', 'button button-small');
        release.type = 'button'; release.addEventListener('click', () => { model.release(owner.handle); refresh(); }); row.append(release); return row;
      }));
    }
    return snapshot;
  }
  find('[data-form="clock"]').addEventListener('submit', event => {
    event.preventDefault();
    if (!event.target.reportValidity()) return;
    model.configure({ bpm: numeric('bpm'), source: field('source').value, meterNumerator: numeric('numerator'), meterDenominator: numeric('denominator'), quantization: field('quantization').value }); refresh();
  });
  find('[data-action="start"]').addEventListener('click', () => { model.start(); refresh(); });
  find('[data-action="stop"]').addEventListener('click', () => { model.stop(); refresh(); });
  field('bank').addEventListener('change', () => {
    model.selectBank(numeric('bank'));
    const track = model.snapshot().tracks?.[model.snapshot().selectedTrack];
    if (track?.loopStart !== undefined) field('loop-start').value = track.loopStart;
    if (track?.loopEnd !== undefined) field('loop-end').value = track.loopEnd;
    refresh();
  });
  panel.querySelectorAll('[data-midi]').forEach(button => button.addEventListener('click', () => { model.midi(button.dataset.midi); refresh(); }));
  find('[data-form="position"]').addEventListener('submit', event => { event.preventDefault(); if (event.target.reportValidity()) { model.midi('position', numeric('song-position')); refresh(); } });
  find('[data-form="loop"]').addEventListener('submit', event => { event.preventDefault(); if (event.target.reportValidity()) { model.setTrackLoop(numeric('loop-start'), numeric('loop-end')); refresh(); } });
  find('[data-form="owner"]').addEventListener('submit', event => {
    event.preventDefault(); if (!event.target.reportValidity()) return;
    model.acquire({ source: field('owner-source').value, track: model.snapshot().selectedTrack, note: numeric('note'), channel: numeric('channel') - 1, mask: numeric('mask'), velocity: numeric('velocity') }); refresh();
  });
  find('[data-action="clear-source"]').addEventListener('click', () => { model.clearSource(field('owner-source').value); refresh(); });
  refresh();
  return { model, refresh, element: panel };
}
