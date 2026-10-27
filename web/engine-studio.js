import { StudioModel } from './studio-model.js';

const studioText = (tag, text, className = '') => {
  const element = document.createElement(tag);
  element.textContent = text;
  element.className = className;
  return element;
};

/* Device drafts are host-side transactions. They never enter the preview's C
 * editor, and keep the displayed instance even if a physical target changes. */
export class ConnectedParameterEditor {
  constructor(encode) { this.encode = encode; this.bridge = null; this.draft = null; this.pending = false; }
  get connected() { return Boolean(this.bridge?.connected); }
  get compatible() { return this.connected && Boolean(this.bridge.compatible); }
  setBridge(bridge) { if (bridge !== this.bridge) { this.draft = null; this.pending = false; } this.bridge = bridge; }
  get(id) {
    if (!this.compatible) return null;
    const record = this.bridge.get(id);
    return record && Number.isInteger(record.raw) && record.raw >= -2147483648 && record.raw <= 2147483647 && Number.isInteger(record.instance) && record.instance >= 0 && record.instance < 255
      ? { value: record.value, raw: record.raw, instance: record.instance } : null;
  }
  select(id) { if (id && this.compatible) this.bridge.select?.(id); }
  cancel() { if (!this.pending) this.draft = null; }
  check() {
    if (!this.connected) throw new Error('The device is disconnected. Reconnect before applying.');
    if (!this.compatible) throw new Error('Device menu schema differs. Parameters are read-only.');
    if (this.pending) throw new Error('Wait for the current device operation to finish.');
  }
  change(row, value, displayed) {
    this.check();
    if (row.status !== 'live' || ['action', 'info', 'text'].includes(row.type)) throw new Error('This device parameter is not editable.');
    const source = this.draft?.id === row.id ? this.draft : displayed;
    if (!source || !Number.isInteger(source.instance) || source.instance < 0 || source.instance >= 255)
      throw new Error('Reading the device value. Wait before editing.');
    const raw = this.encode(row, value);
    if (!Number.isInteger(raw) || raw < -2147483648 || raw > 2147483647)
      throw new Error('Choose a valid device value.');
    if (row.id === 'sensitivity.velocityMode' && raw === 2) throw new Error('Mixed describes the current pads. Choose Fixed or Strike.');
    this.draft = { id: row.id, value, raw, instance: source.instance };
    return this.draft;
  }
  async commit(row, value, displayed) {
    const draft = { ...this.change(row, value, displayed) }, bridge = this.bridge;
    this.pending = true;
    try {
      const result = await bridge.commit(draft.id, draft.instance, draft.raw);
      if (result === false || result?.ok === false) throw new Error(result?.message || 'The device rejected this setting.');
      if (this.bridge === bridge) this.draft = null;
      return result;
    } finally { if (this.bridge === bridge) this.pending = false; }
  }
  actionInstance(row) {
    if (!this.compatible || row.status !== 'live' || row.type !== 'action') return null;
    const instance = this.bridge.instance?.(row.id);
    return Number.isInteger(instance) && instance >= 0 && instance < 255 ? instance : null;
  }
  async action(row, instance) {
    this.check();
    if (row.status !== 'live' || row.type !== 'action' || !Number.isInteger(instance) || instance < 0 || instance >= 255)
      throw new Error('Reading the device action target. Wait before running it.');
    const bridge = this.bridge;
    this.pending = true;
    try {
      const result = await bridge.action(row.id, instance);
      if (result === false || result?.ok === false) throw new Error(result?.message || 'The device rejected this action.');
      return result;
    } finally { if (this.bridge === bridge) this.pending = false; }
  }
  error(error) { this.bridge?.error?.(error); }
}

function deviceFormat(model, row, record) {
  if (row.type === 'action') return 'Run';
  if (row.type === 'info') return 'Info';
  if (!record) return 'Reading…';
  if (row.type === 'toggle') return record.value ? 'ON' : 'OFF';
  if (row.id === 'sensitivity.velocityMode' && record.raw === 2) return 'Mixed';
  if (row.dynamic) return model.scales.find(scale => scale.index === record.raw)?.name || String(record.value ?? record.raw);
  if (row.type === 'select') return row.options.find(option => option.value === record.value)?.label || String(record.value ?? record.raw);
  return String(record.value ?? record.raw);
}

function deviceOptions(model, row) {
  if (row.dynamic === 'scale-shortcuts') return [
    ...model.scales.map(scale => ({ value: scale.index, label: scale.name })),
    { value: 70, label: 'Cycle layout' },
    ...Array.from({ length: 8 }, (_, index) => ({ value: 71 + index, label: `User scale ${index + 1}` })),
  ];
  if (row.dynamic) return model.scales.map(scale => ({ value: scale.index, label: scale.name }));
  return row.options.filter(option => row.id !== 'sensitivity.velocityMode' || option.value !== 2);
}

export function initializeEngineStudio({ engine, groups, getContext, onChange, onActivate }) {
  const model = new StudioModel({ engine, groups });
  const device = new ConnectedParameterEditor((row, value) => model.encode(row, value));
  engine.state.designMode(false);
  const panel = document.createElement('section');
  panel.className = 'panel engine-studio';
  panel.id = 'engine-studio';
  panel.setAttribute('aria-label', 'Approved engine and menu preview');
  panel.innerHTML = `
    <div class="engine-heading"><div><p class="eyebrow">THY666 / SHARED C ENGINE</p><h2>Engine & menus</h2><p>One state model and OLED renderer for the browser and STM32.</p></div><span class="engine-scope-badge">C / WebAssembly</span></div>
    <div class="engine-intro"><span><i class="engine-live-dot"></i> Live controls shape the playable core.</span><label class="engine-design-toggle"><input type="checkbox" id="engine-future-controls"> Show future designs & notes</label><button type="button" id="engine-export" class="button button-small">Export UI draft</button></div>
    <nav class="engine-groups" aria-label="Firmware menu categories"></nav>
    <div class="engine-layout"><div><div class="engine-list-heading"><h3 id="engine-group-title"></h3><span id="engine-group-count"></span><button type="button" id="engine-track-back" class="button button-small" hidden>Back to track</button></div><div class="engine-menu-list" aria-label="Settings in selected category"></div></div>
      <div class="engine-editor" aria-label="Selected setting editor"></div>
      <aside class="engine-state" aria-label="Engine state overview"><h3>State boundaries</h3><p>Configuration shapes future events. Held notes keep the pitches and routes captured at their attack.</p><dl><div><dt>Playing</dt><dd id="engine-state-playing">Play</dd></div><div><dt>Transport</dt><dd id="engine-state-transport">Stopped</dd></div><div><dt>Tracks</dt><dd>21 maximum · 48 steps each</dd></div><div><dt>Routing profile</dt><dd id="engine-state-routing">Split Sides</dd></div><div><dt>Active ownership</dt><dd id="engine-state-voices">0 voices</dd></div><div><dt>UI draft</dt><dd id="engine-state-drafts">0 changes</dd></div></dl><p class="engine-state-foot">Settings, performance, clock, tracks and storage have separate lifecycles. No device connection.</p><details><summary>Recent state changes</summary><ol id="engine-event-log"></ol></details></aside>
    </div><div class="engine-footer"><p id="engine-notice" role="status" aria-live="polite">Center toggles Settings / Play. Right opens, edits or applies; up/down moves or changes values; left goes back or cancels.</p><span>Live settings save locally · future designs stay in this session</span></div>`;
  document.querySelector('.instrument').insertAdjacentElement('afterend', panel);
  const find = selector => panel.querySelector(selector);
  let lastIdentity = '';
  let showFuture = false;
  let lastDeviceSelection = '';
  let forceEditor = false;
  let lastDeviceMode = 'offline';
  const activate = () => onActivate?.();
  const changed = (force = false) => { lastIdentity = ''; forceEditor ||= force; refresh(); if (!device.connected) onChange?.(); };
  for (const group of model.groups) {
    const button = studioText('button', group.label);
    const icons = { engines: '⚙', performance: '◇', chords: '♬', arps: '↗', sequencer: '▥', midi: '⇄', sensitivity: '◉', lights: '☼', storage: '▣', system: '⚙', views: '▤', controls: '✥' };
    const icon = studioText('span', icons[group.id] || '◇', 'engine-group-icon');
    icon.setAttribute('aria-hidden', 'true'); button.prepend(icon);
    button.type = 'button';
    button.dataset.group = group.id;
    button.addEventListener('click', () => {
      if (device.pending) return;
      if (model.snapshot().trackOptionsOpen) return;
      if (device.connected) device.cancel();
      activate();
      const current = group.rows[model.cursors.get(group.id) || 0];
      const visible = model.visibleRows(group, showFuture);
      const selected = visible.includes(current) ? current : visible[0];
      model.focus(group.id, selected?.id);
      changed();
    });
    find('.engine-groups').append(button);
  }

  function buildEditor(row, snapshot) {
    const editor = find('.engine-editor');
    editor.replaceChildren();
    if (!row) { editor.append(studioText('h3', 'Choose a setting'), studioText('p', 'Select a category and setting, or open the OLED menu with the center control.')); return; }
    const editable = !['action', 'info'].includes(row.type);
    const connected = device.connected;
    editor.append(studioText('span', row.type === 'info' ? 'FEATURE HELP' : connected ? 'CONNECTED DEVICE' : row.status === 'live' ? 'LIVE / PLAYABLE CORE' : 'FUTURE / CONFIGURATION ONLY', 'engine-capability ' + (row.status === 'live' ? 'is-live' : '')),
      studioText('h3', row.label), studioText('p', row.description || 'A setting from the approved feature set.', 'engine-description'));
    const help = studioText('button', '? Help', 'button button-small');
    help.type = 'button'; help.dataset.help = row.id;
    editor.append(help);
    if (connected && !device.compatible) {
      editor.append(studioText('p', 'Device menu schema differs. Live parameters are read-only; use the matching firmware and companion.', 'engine-draft-note'));
      return;
    }
    if (!showFuture && row.status !== 'live' && row.type !== 'info') {
      editor.append(studioText('p', 'This feature is not implemented. Enable “Show future designs & notes” to review its configuration proposal. Changing that proposal does not enable the feature.', 'engine-draft-note'));
      return;
    }
    if (row.type === 'action' && row.status === 'live') {
      const instance = connected ? device.actionInstance(row) : null;
      const run = studioText('button', (connected ? 'Run on device: ' : row.id.startsWith('engines.') ? 'Select ' : 'Run ') + row.label, 'button engine-apply');
      run.type = 'button';
      run.disabled = connected && (instance === null || device.pending);
      run.addEventListener('click', async () => {
        if (!connected) { activate(); model.beginEdit(row.id); changed(); return; }
        run.disabled = true;
        try { await device.action(row, instance); } catch (error) { device.error(error); }
        changed(true);
      });
      editor.append(run, studioText('p', connected ? instance === null ? 'Reading the device action target…' : 'Runs this action on the connected device only.' : 'Runs the shared C action immediately. Held notes keep their captured pitches and routes.', 'engine-draft-note'));
      return;
    }
    if (!editable) {
      editor.append(studioText('p', row.type === 'info' ? connected ? String(row.default ?? row.description ?? '') : model.format(row) : 'Not implemented. This action cannot run until its engine or storage service exists.', 'engine-draft-note'));
      if (row.type === 'info') editor.append(studioText('p', row.status === 'live' ? 'This describes the current engine. It has no editable value.' : 'This is a design reference. It has no editable value or device command.', 'engine-draft-note'));
      if (row.type === 'info' && !connected) {
        const show = studioText('button','Show on OLED','button button-small');
        show.type='button';show.addEventListener('click',()=>{activate();model.beginEdit(row.id);changed();});editor.append(show);
      }
      return;
    }
    if (connected && row.status !== 'live') {
      editor.append(studioText('p', 'This feature is not implemented on the device.', 'engine-draft-note'));
      return;
    }
    const displayed = connected ? device.get(row.id) : null;
    if (connected && (!displayed || row.type === 'text')) {
      editor.append(studioText('p', row.type === 'text' ? 'Text editing is not available through this device connection yet.' : 'Reading the device value…', 'engine-draft-note'));
      return;
    }
    const form = document.createElement('form');
    form.noValidate=true;
    const label = studioText('label', row.type === 'toggle' ? 'Enabled' : 'Value');
    const input = document.createElement(row.type === 'select' || row.type === 'toggle' ? 'select' : 'input');
    input.id = 'engine-setting-value';
    input.setAttribute('aria-label', row.label + ' value');
    const value = connected ? device.draft?.id === row.id ? device.draft.value : displayed.value : snapshot.edit?.id === row.id ? snapshot.edit.value : model.get(row.id);
    if (input.tagName === 'SELECT') {
      const options = row.type === 'toggle' ? [{ value: false, label: 'OFF' }, { value: true, label: 'ON' }] : connected ? deviceOptions(model, row) : model.options(row);
      for (const option of options) {
        const element = studioText('option', option.label);
        element.value = JSON.stringify(option.value);
        input.append(element);
      }
      if (!options.some(option => JSON.stringify(option.value) === JSON.stringify(value))) {
        const summary = studioText('option', (connected ? deviceFormat(model, row, displayed) : model.format(row)) + ' — choose a value');
        summary.value = JSON.stringify(value) ?? '__summary__';
        summary.disabled = true;
        input.append(summary);
      }
      input.value = JSON.stringify(value) ?? '__summary__';
    } else {
      input.type = row.type === 'number' ? 'number' : 'text';
      if (row.type === 'number') {
        const bounds=(connected ? row : model.bounds(row))||row;input.min=bounds.min;input.max=bounds.max;input.step=row.step||1;
      }
      else input.maxLength = row.maxLength || 24;
      input.value = value ?? '';
    }
    const inputValue = (clamp=false) => {
      if(input.tagName==='SELECT')return input.value === '' ? NaN : JSON.parse(input.value);
      if(row.type!=='number')return input.value;
      if(input.value==='')return NaN;
      const value=Number(input.value),bounds=connected ? row : model.bounds(row);
      if(!Number.isInteger(value)||!bounds||!clamp)return value;
      input.min=bounds.min;input.max=bounds.max;
      const clamped=Math.max(bounds.min,Math.min(bounds.max,value));input.value=clamped;return clamped;
    };
    input.addEventListener('input', () => {
      apply.disabled = input.tagName === 'SELECT' && (!input.selectedOptions[0] || input.selectedOptions[0].disabled);
      if (connected) {
        try { device.change(row, inputValue(), displayed); } catch (error) { apply.disabled = true; device.error(error); }
        return;
      }
      if (model.edit?.id !== row.id) model.beginEdit(row.id);
      model.setEditValue(inputValue());
      onChange?.();
    });
    input.addEventListener('change',()=>{
      if(row.type!=='number')return;
      if (connected) {
        try { device.change(row, inputValue(true), displayed); } catch (error) { apply.disabled = true; device.error(error); }
        return;
      }
      if(model.edit?.id!==row.id)model.beginEdit(row.id);
      model.setEditValue(inputValue(true));onChange?.();
    });
    label.append(input);
    form.append(label);
    const actions = studioText('div', '', 'engine-editor-actions');
    const apply = studioText('button', connected ? 'Apply to device' : row.status === 'live' ? 'Apply to preview' : 'Save draft', 'button engine-apply');
    apply.type = 'submit';
    apply.disabled = device.pending || input.tagName === 'SELECT' && (!input.selectedOptions[0] || input.selectedOptions[0].disabled);
    input.disabled = device.pending;
    const cancel = studioText('button', 'Cancel', 'button'); cancel.type = 'button';
    cancel.disabled = device.pending;
    cancel.addEventListener('click', () => { if (connected) device.cancel(); else model.cancelEdit(); changed(true); });
    actions.append(apply, cancel); form.append(actions);
    form.addEventListener('submit', async event => {
      event.preventDefault();
      if (input.tagName === 'SELECT' && (!input.selectedOptions[0] || input.selectedOptions[0].disabled)) return;
      if (connected) {
        const value = inputValue(true);
        if (!form.reportValidity()) return;
        apply.disabled = input.disabled = cancel.disabled = true;
        try { await device.commit(row, value, displayed); } catch (error) { device.error(error); }
        changed(true);
        return;
      }
      if (model.edit?.id !== row.id) model.beginEdit(row.id);
      const value=inputValue(true);
      if (!form.reportValidity()) return;
      if (!model.setEditValue(value)) { changed(); return; }
      model.commitEdit(); changed();
    });
    editor.append(form, studioText('p', connected ? `Edits device target ${device.draft?.id === row.id ? device.draft.instance : displayed.instance}. Apply sends the change; Cancel sends nothing.` : row.status === 'live' ? 'Applies on commit. Cancel leaves the current setting unchanged; held notes keep their snapshots.' : 'This saves future configuration only. It does not enable this engine or change the notes you hear.', 'engine-draft-note'));
    if (!connected && row.id.startsWith('sensitivity.') && /center/i.test(row.id))
      editor.append(studioText('p', 'This sets physical center-control polling for the next device build. The browser has no capacitive sensors and does not simulate a change to physical touch sensitivity or latency.', 'engine-draft-note'));
    if (row.policy) editor.append(studioText('p', row.policy, 'engine-draft-note'));
    if (showFuture) editor.append(studioText('p', (row.feature_ids || []).join(' · '), 'engine-feature-refs'));
  }

  function refresh() {
    const deviceMode = device.connected ? device.compatible ? 'device' : 'read-only' : 'offline';
    if (deviceMode !== lastDeviceMode) {
      if (deviceMode === 'offline') device.draft = null;
      forceEditor = true;
      lastDeviceMode = deviceMode;
    }
    const snapshot = model.snapshot();
    const context = getContext();
    const group = model.groups.find(item => item.id === snapshot.groupId);
    const row = snapshot.level === 'groups' ? group.rows[model.cursors.get(group.id) || 0] : model.definitions.get(snapshot.selectedId);
    if (!device.compatible) lastDeviceSelection = '';
    if (device.compatible && row?.id !== lastDeviceSelection) {
      lastDeviceSelection = row?.id || '';
      try { device.select(row?.id); } catch (error) { device.error(error); }
    }
    const visibleRows = snapshot.quickOpen ? snapshot.rows.map(item => model.definitions.get(item.id)) : model.visibleRows(group, showFuture);
    const liveCount = visibleRows.filter(item => item.status === 'live').length;
    const identity = JSON.stringify({ level: snapshot.level, group: group.id, selected: row?.id, edit: device.connected ? device.draft : snapshot.edit, connected: device.connected, compatible: device.compatible, pending: device.pending, values: visibleRows.map(item => device.connected ? device.get(item.id) : model.get(item.id)), notice: snapshot.notice, showFuture, target: snapshot.arpTarget, trackOptions: snapshot.trackOptionsOpen, trackTarget: snapshot.trackTarget, sensitivityTarget:snapshot.sensitivityTarget,sensitivityPick:snapshot.sensitivityPick });
    if (identity !== lastIdentity) {
      lastIdentity = identity;
      panel.querySelectorAll('[data-group]').forEach(button => {
        button.setAttribute('aria-pressed', String(button.dataset.group === group.id));
        button.disabled = snapshot.trackOptionsOpen;
      });
      find('#engine-track-back').hidden = !snapshot.trackOptionsOpen;
      find('#engine-track-back').textContent = snapshot.editing ? 'Cancel edit' : 'Back to track';
      find('#engine-group-title').textContent = snapshot.quickOpen ? `${snapshot.groupLabel} · Quick controls` : snapshot.groupLabel;
      find('#engine-group-count').textContent = snapshot.trackOptionsOpen ? `${visibleRows.length} track options · LEFT returns to editor` : snapshot.quickOpen ? `${visibleRows.length} live · ${snapshot.quickKeys ? 'temporary M controls on' : 'M controls normal'}` : `${group.rows.filter(item => item.status === 'live').length} live · ${group.rows.filter(item => item.status !== 'live').length} future / notes`;
      const list = find('.engine-menu-list');
      list.replaceChildren();
      if (!liveCount && !showFuture)
        list.append(studioText('p', 'No live controls in this category yet. Enable “Show future designs & notes” to review the planned features.', 'engine-draft-note'));
      let sawBasic = false;
      let divided = false;
      for (const item of visibleRows) {
        if (!snapshot.quickOpen && sawBasic && item.advanced && !divided) {
          const separator = document.createElement('hr');
          separator.className = 'engine-menu-divider';
          separator.setAttribute('aria-label', 'Advanced options');
          list.append(separator);
          divided = true;
        }
        if (!item.advanced) sawBasic = true;
        const button = studioText('button', '', 'engine-menu-item');
        button.type = 'button'; button.dataset.setting = item.id;
        button.dataset.section = item.advanced ? 'advanced' : 'basic';
        button.setAttribute('aria-pressed', String(item.id === row?.id));
        const workspaceIcons={'engines.play':'♬','engines.sequencer':'▥','engines.drums':'◉','engines.lightboard':'☼','engines.user':'◇'};
        const itemLabel=studioText('span',item.label);
        if(workspaceIcons[item.id]) { const glyph=studioText('span',workspaceIcons[item.id]+' ','engine-workspace-icon'); glyph.setAttribute('aria-hidden','true'); itemLabel.prepend(glyph); }
        button.append(itemLabel, studioText('span', device.connected ? deviceFormat(model, item, device.get(item.id)) : model.format(item), 'engine-menu-value'), studioText('small', item.status === 'live' ? 'LIVE' : item.type === 'info' ? 'INFO' : 'FUTURE', item.status === 'live' ? 'is-live' : ''));
        button.addEventListener('click', () => { if (device.pending) return; if (device.connected) device.cancel(); activate(); if (snapshot.quickOpen) model.focusQuick(item.id); else model.focus(group.id, item.id); changed(); });
        list.append(button);
      }
      const selected = list.querySelector('[aria-pressed="true"]');
      if (selected) {
        const rect = selected.getBoundingClientRect(), bounds = list.getBoundingClientRect();
        if (rect.top < bounds.top) list.scrollTop += rect.top - bounds.top;
        else if (rect.bottom > bounds.bottom) list.scrollTop += rect.bottom - bounds.bottom;
      }
      // Preserve typing and native selection while the display service runs.
      const preserveDraft = device.connected
        ? device.compatible && device.draft?.id === row?.id
        : find('.engine-editor').contains(document.activeElement) && model.edit;
      if (forceEditor || !preserveDraft) buildEditor(row, snapshot);
      forceEditor = false;
    }
    find('#engine-notice').textContent = device.connected
      ? !device.compatible ? 'Device schema differs. Parameter changes are disabled.' : device.pending ? 'Applying on device…' : 'Changes apply to the connected device. Browsing settings here does not move its OLED menu.'
      : snapshot.notice || (snapshot.trackOptionsOpen ? 'Track settings keep the LEFT steps and RIGHT playing surface active. LEFT returns to the track, or cancels an edit. CENTER returns to Play.' : 'Center toggles Settings / Play. Right opens, edits or applies; up/down moves or changes values; left goes back or cancels.');
    find('.engine-state-foot').textContent = device.connected
      ? 'Connected parameters are read from the board. The menu selection in this editor is local.'
      : 'Settings, performance, clock, tracks and storage have separate lifecycles. No device connection.';
    find('.engine-footer > span').textContent = device.connected
      ? 'Apply to device · Cancel keeps its current value'
      : 'Live settings save locally · future designs stay in this session';
    const session = engine.state.session();
    find('#engine-state-playing').textContent = ['Play', 'Play', 'Sequencer', 'Drums', 'Lightboard', 'User'][session.mode] || 'Play';
    const rhythm = engine.rhythm?.snapshot();
    find('#engine-state-transport').textContent = rhythm ? `${rhythm.transport} · ${rhythm.clockSource === 'usb' ? 'external pulse timing' : `${rhythm.bpm} BPM`} · ${rhythm.clockState}` : 'Stopped';
    find('#engine-state-voices').textContent = `${context.voices?.length || 0} voices · ${context.held || 0} pads`;
    find('#engine-state-drafts').textContent = snapshot.draftChanges + ' changed values';
    const routing = [...model.definitions.values()].find(item => item.feature_ids?.includes('midi.bank-route-modes') && item.type === 'select');
    if (routing) find('#engine-state-routing').textContent = device.connected ? deviceFormat(model, routing, device.get(routing.id)) : model.format(routing);
    find('#engine-event-log').replaceChildren(...model.events.slice(-5).reverse().map(event => studioText('li', `${event.type}: ${model.definitions.get(event.id)?.label || event.id}`)));
  }

  find('#engine-future-controls').addEventListener('change', event => {
    showFuture = event.target.checked;
    if (!device.connected) engine.state.designMode(showFuture);
    if (!device.connected && !showFuture && model.edit && model.definitions.get(model.edit.id)?.status !== 'live') model.cancelEdit();
    changed();
  });

  find('#engine-track-back').addEventListener('click', () => {
    activate(); model.navigate('left'); changed();
  });

  find('#engine-export').addEventListener('click', () => {
    const url = URL.createObjectURL(new Blob([JSON.stringify(model.exportDraft(), null, 2) + '\n'], { type: 'application/json' }));
    const link = document.createElement('a'); link.href = url; link.download = 'thy666-ui-design-draft.json'; link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  });
  refresh();
  return {
    model, refresh,
    setDeviceBridge(bridge) { device.setBridge(bridge); lastDeviceSelection = ''; changed(true); },
    navigate(action) { model.navigate(action); changed(); },
    open() { model.open(); changed(); },
    play() { engine.state.boot.skip(); model.play(); changed(); },
    replayBoot(now = Math.floor(performance.now()) >>> 0) {
      model.play();
      engine.state.boot.begin(now);
      // The local core and catalogue have already initialized. Mark this
      // actual preview readiness; C owns the entire animation and handoff.
      // Device milestone timing is supplied separately by STM32 bring-up.
      engine.state.boot.progress(4);
      engine.state.boot.ready();
      changed();
    },
    snapshot: () => model.snapshot(),
    frame(_profile, animations = true) {
      return model.frame({ scaleName: getContext().scaleName, now: Math.floor(performance.now()) >>> 0, animations });
    },
  };
}
