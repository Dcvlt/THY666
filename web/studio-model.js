/* Projection/serialization only. C owns values, validation, transactions,
 * instance selection, navigation, history and animation transition state. */
import { STUDIO_MENU } from './studio-menu.js';
import { MENU_SCHEMA_SHA256 } from './generated/menu-schema.js';

export class StudioModel {
  constructor({ engine, groups = [] } = {}) {
    if (!engine?.state) throw new Error('The shared C state engine is required.');
    this.engine = engine;
    this.shared = engine.state;
    this.groups = STUDIO_MENU.groups;
    this.scales = groups;
    this.rows = this.groups.flatMap(group => group.rows);
    this.definitions = new Map(this.rows.map(row => [row.id, row]));
    this.indices = new Map(this.rows.map((row, index) => [row.id, index]));
    if (this.shared.rowCount !== this.rows.length || this.shared.schemaHash() !== MENU_SCHEMA_SHA256)
      throw new Error('The menu contract and C engine differ. Rebuild the shared core.');
  }
  get groupIndex() { return this.shared.info().group; }
  get cursors() { return new Map(this.groups.map((group, index) => [group.id, this.shared.cursor(index)])); }
  get edit() {
    const info = this.shared.info();
    if (!info.editing) return null;
    const row = this.rows[info.editRow];
    return { id: row.id, value: this.decode(row, row.type === 'text' ? info.editText : info.editValue) };
  }
  get events() {
    return this.shared.events().map(event => ({ ...event, id: this.rows[event.row]?.id,
      type: ['', 'setting.applied', 'config.changed', 'edit.cancelled'][event.kind],
      value: this.decode(this.rows[event.row], this.rows[event.row]?.type === 'text' ? event.text : event.value) }));
  }
  decode(row, value) {
    if (row?.type === 'toggle') return Boolean(value);
    if (row?.type === 'select' && !row.dynamic) return row.options[value]?.value;
    return value;
  }
  encode(row, value) {
    if (row?.type === 'toggle') return typeof value === 'boolean' ? Number(value) : -2147483648;
    if (row?.type === 'select' && !row.dynamic) return row.options.findIndex(option => option.value === value);
    return value;
  }
  get(id) { const row = this.definitions.get(id); return row ? this.decode(row, this.shared.get(this.indices.get(id))) : undefined; }
  bounds(id) {return this.shared.bounds(this.indices.get(typeof id==='string'?id:id.id));}
  set(id, value) { const row = this.definitions.get(id); return this.shared.set(this.indices.get(id), this.encode(row, value)); }
  beginEdit(id) { return this.shared.begin(this.indices.get(id)).ok; }
  setEditValue(value) { const edit = this.edit; return edit ? this.shared.stage(this.encode(this.definitions.get(edit.id), value)).ok : false; }
  adjust(delta) { return this.shared.adjust(delta).ok; }
  commitEdit() { return this.shared.commit(); }
  cancelEdit() { return this.shared.cancel().ok; }
  focus(groupId, rowId) {
    const group = this.groups.findIndex(item => item.id === groupId);
    if (group < 0 || rowId !== undefined && !this.indices.has(rowId)) return false;
    const info = this.shared.info();
    if (info.trackOptionsOpen && (groupId !== 'sequencer' ||
        rowId !== undefined && !this.shared.visible(this.indices.get(rowId)))) return false;
    return this.shared.focus(group, rowId === undefined ? -1 : this.indices.get(rowId)).ok;
  }
  focusQuick(rowId) {
    const info = this.shared.info();
    if (!info.quickOpen || !this.engine.arp?.quickRows) return false;
    const rows = this.engine.arp.quickRows();
    const destination = rows.indexOf(this.indices.get(rowId));
    if (destination < 0) return false;
    if (destination === info.quickCursor) return true;
    if (info.editing) this.cancelEdit();
    const forward = (destination - info.quickCursor + rows.length) % rows.length;
    const backward = rows.length - forward;
    const action = forward <= backward ? 'down' : 'up';
    for (let moves = Math.min(forward, backward); moves > 0; moves--)
      if (!this.navigate(action)) return false;
    return true;
  }
  open() { this.shared.open(); }
  play() { this.shared.play(); }
  navigate(action) { return this.shared.navigate(action).ok; }
  options(row) {
    const index = this.indices.get(row.id);
    if (row.dynamic === 'scale-shortcuts') return [...this.scales.map(group=>({value:group.index,label:group.name})),{value:70,label:'Cycle layout'},
      ...Array.from({length:8},(_,n)=>({value:71+n,label:this.engine.firmware.library.record(0,n)?.name||`Scale ${n+1}`}))].filter(option=>this.shared.allowed(index,option.value));
    if (row.dynamic) return this.scales.filter(group => this.shared.allowed(index, group.index)).map(group => ({ value: group.index, label: group.name }));
    if (row.id === 'chords.userScale' || row.id === 'chords.sixBank') return (row.options || []).map((option, ordinal) => ({...option,
      label: row.id === 'chords.userScale' && ordinal === 0 ? 'Built-in' :
        this.engine.firmware.library.record(row.id === 'chords.userScale' ? 0 : 2,
          row.id === 'chords.userScale' ? ordinal-1 : ordinal)?.name || option.label
    })).filter((_,ordinal)=>this.shared.allowed(index,ordinal));
    return (row.options || []).filter((_, ordinal) => this.shared.allowed(index, ordinal));
  }
  visibleRows(group, includeFuture = false) {
    return group.rows.filter(row => (includeFuture || row.status === 'live' || group.id === 'engines' && row.type === 'info') && this.shared.visible(this.indices.get(row.id)));
  }
  format(row, value = this.get(row.id)) {
    if (row.type === 'info') return String(typeof value === 'string' ? value : row.default ?? '');
    if (row.type === 'toggle') return value ? 'ON' : 'OFF';
    if (row.id === 'sensitivity.target') return this.shared.format(this.indices.get(row.id),Boolean(this.edit?.id===row.id));
    if (row.id === 'chords.userScale' || row.id === 'chords.sixBank') return this.options(row).find(option=>option.value===value)?.label || String(value);
    if (row.dynamic === 'scale-shortcuts' && value === 70) return 'Cycle layout';
    if (row.dynamic === 'scale-shortcuts' && value > 70) return this.engine.firmware.library.record(0,value-71)?.name || String(value);
    if (row.dynamic) return this.scales.find(group => group.index === value)?.name || String(value);
    if (row.type === 'select') return row.options.find(option => option.value === value)?.label ?? String(value ?? '');
    if (row.type === 'action' && row.id.startsWith('engines.')) return this.shared.compact(this.indices.get(row.id),false);
    if (row.type === 'action') return row.status === 'live' ? 'Run' : 'Not implemented';
    return String(value ?? '');
  }
  compact(row) { return this.shared.compact(this.indices.get(row.id), Boolean(this.edit?.id === row.id)); }
  snapshot() {
    const info = this.shared.info(), quick = Boolean(info.quickOpen && info.level === 0);
    const trackOptions = Boolean(info.trackOptionsOpen && info.level === 2);
    const group = quick ? this.groups.find(item => item.id === 'arps') : this.groups[info.group], edit = this.edit;
    const definitions = quick ? this.engine.arp.quickRows().map(index => this.rows[index]) :
      trackOptions ? this.visibleRows(group, true) : group.rows;
    const cursor = quick ? info.quickCursor : trackOptions ?
      Math.max(0, definitions.findIndex(row => row === group.rows[info.cursor])) : info.cursor;
    const rows = !quick && info.level === 1 ? this.groups.map((item, index) => ({ id: item.id, label: item.shortLabel || item.label, value: '>', selected: index === cursor, status: 'group' })) : definitions.map((row, index) => ({
      id: row.id, label: row.shortLabel || row.label, value: this.format(row, edit?.id === row.id ? edit.value : this.get(row.id)),
      compactValue: this.shared.compact(this.indices.get(row.id), Boolean(edit?.id === row.id)), selected: index === cursor, status: row.status,
    }));
    return { level: quick ? 'arp' : ['play', 'groups', 'rows'][info.level], about: Boolean(info.about), infoOpen: Boolean(info.infoOpen),
      transitioning: Boolean(info.transitioning), transitionElapsedMs: info.transitionElapsedMs, groupId: group.id,
      groupLabel: quick ? `Arp ${info.arpTarget + 1}` : trackOptions ? `T${info.trackTarget + 1} settings` : group.label, quickOpen: quick,
      trackOptionsOpen: trackOptions, trackTarget: info.trackTarget,
      sensitivityTarget: info.sensitivityTarget, sensitivityPick: Boolean(info.sensitivityPick),
      sensitivityPicked: Boolean(info.sensitivityPicked),
      arpTarget: info.arpTarget, quickKeys: Boolean(info.quickKeys),
      selectedId: rows[cursor]?.id, editing: Boolean(info.editing), edit, cursor,
      rowCursor: !quick && info.level === 1 ? null : cursor, count: rows.length, rows, notice: info.message,
      revision: info.revision, transitionRevision: info.transitionRevision, direction: info.direction, draftChanges: info.changes };
  }
  exportDraft() {
    const padScopes = Array.from({length:98},()=>({})); // 0..95 logical pads, 96 Left, 97 Right.
    const instances = { velocitySides:{both:{},left:{},right:{}}, pads: padScopes.slice(0,96), padClusters:{left:padScopes[96],right:padScopes[97]}, shortcuts: Array.from({length:12},()=>({})), views: Array.from({ length: 9 }, () => ({})), arps: Array.from({ length: 2 }, () => ({})), strummers: Array.from({ length: 2 }, () => ({})), tracks: Array.from({ length: 21 }, () => ({})),
      routes: Object.fromEntries(['whole-board', 'left', 'right', 'left-1', 'left-2', 'left-3', 'right-1', 'right-2', 'right-3'].map(id => [id, {}])) };
    const routeIds = Object.keys(instances.routes);
    for (const [index, row] of this.rows.entries()) {
      const count = this.shared.instanceCount(index);
      if (count <= 1) continue;
      const family = row.id === 'sensitivity.velocityMode' ? Object.values(instances.velocitySides) : count === 98 ? padScopes : row.id === 'engines.assignment' ? instances.shortcuts : row.id.startsWith('views.') ? instances.views : row.id.startsWith('arps.') ? instances.arps : row.id.startsWith('chords.') ? instances.strummers : row.id.startsWith('sequencer.') ? instances.tracks : null;
      for (let i = 0; i < count; i++) (family ? family[i] : instances.routes[routeIds[i]])[row.id] = this.decode(row, this.shared.instance(index, i));
    }
    return { format: 'thy666.ui-design-draft', version: 2, menuVersion: STUDIO_MENU.version, schema: this.shared.schemaHash(),
      scope: 'Shared C configuration preview; not a device profile, song, backup or firmware package.',
      values: Object.fromEntries(this.rows.filter(row => !['action', 'info'].includes(row.type)).map(row => [row.id, this.get(row.id)])), instances };
  }
  frame(options) { return this.shared.frame(options); }
}
