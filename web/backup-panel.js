/* Explicit applied-state files only. No connection, firmware update or note input. */
import { BACKUP_HEADER_BYTES, BACKUP_MAX_BYTES, inspectDeviceBackup } from './device-client.js';

let panelSequence = 0;

export function initializeBackupPanel({ mount, client, getEngine, onApplied = () => {}, notify = () => {},runtime=null } = {}) {
  if (!mount?.ownerDocument || !client?.downloadBackup || !client?.restoreBackup)
    throw new TypeError('Backups require a DOM mount and the shared device client.');
  const doc = mount.ownerDocument, prefix = `thy-backup-${++panelSequence}`, listeners = [];
  let disposed = false, working = false, selection = null, fileTicket = 0, dialog = null, returnFocus = null;
  let notice = '';
  const el = (tag, className = '', text) => {
    const node = doc.createElement(tag); if (className) node.className = className;
    if (text !== undefined) node.textContent = String(text); return node;
  };
  const listen = (node, type, handler, options) => {
    node.addEventListener(type, handler, options);
    listeners.push(() => node.removeEventListener(type, handler, options));
  };
  const button = (text, action) => {
    const node = el('button', 'button', text); node.type = 'button'; node.dataset.backupAction = action; return node;
  };
  const root = el('section', 'panel backup-panel'); root.setAttribute('aria-labelledby', `${prefix}-title`);
  const heading = el('div', 'panel-heading'), title = el('h2', '', 'Save or restore'); title.id = `${prefix}-title`;
  const help = button('?', 'help'); help.dataset.help = 'backup'; help.setAttribute('aria-label', 'Help with backups');
  heading.append(title, help);
  const coverage = el('p', 'panel-copy', 'Save your settings, pad adjustments, custom library and current song.');
  const body = el('div', 'pad-inspector backup-body');
  const identity = el('p', 'backup-identity'); identity.dataset.backupReadout = 'identity';
  const actions = el('div', 'backup-actions');
  const read = button('Read backup', 'read'); actions.append(read);
  const choose = el('label', 'library-field backup-file-label', 'Choose a .thyb backup file');
  const input = el('input', 'backup-file'); input.type = 'file'; input.accept = '.thyb,application/octet-stream';
  input.dataset.backupField = 'file'; input.setAttribute('aria-label', 'Choose a THY666 backup file'); choose.append(input);
  const fileSummary = el('div', 'backup-summary'); fileSummary.dataset.backupReadout = 'file';
  fileSummary.setAttribute('aria-live', 'polite');
  const restore = button('Restore backup', 'restore');
  const status = el('p', 'library-notice backup-notice'); status.dataset.backupReadout = 'notice';
  status.setAttribute('role', 'status'); status.setAttribute('aria-live', 'polite');
  const preview = el('details', 'backup-limits');
  preview.append(el('summary', '', 'Backup limits'), el('p', 'inspector-notice', 'Backups contain saved configuration, not firmware, the bootloader or unsaved edits. Restore requires the same board.'));
  body.append(identity, actions, choose, fileSummary, restore, status, preview); root.append(heading, coverage, body);
  mount.replaceChildren(root);

  function selectionStatus() {
    if (!selection) return { valid: false, info: null, error: '' };
    try {
      const info = inspectDeviceBackup(selection.bytes, client.connected ? client.backupIdentity : null);
      if (client.connected && !client.backupIdentity) throw new Error('The connected device has no verified backup identity.');
      return { valid: true, info, error: '' };
    } catch (error) { return { valid: false, info: selection.info, error: error.message }; }
  }
  function refresh() {
    if (disposed) return null;
    const connected = Boolean(client.connected), busy = working || Boolean(client.busy), current = selectionStatus();
    const board = client.identity;
    identity.textContent = connected && board ? '' : 'Connect your board to save or restore a backup.';
    identity.hidden = connected && Boolean(board);
    read.disabled = !connected || busy || !client.backupIdentity;
    input.disabled = busy;
    restore.disabled = !connected || busy || !current.valid;
    fileSummary.replaceChildren();
    if (selection) {
      fileSummary.append(el('h3', '', selection.name));
      if (current.info) {
        const info = current.info;
        fileSummary.append(el('p', '', current.error || (connected ? 'Verified backup for this board.' : 'Valid backup. Connect the matching board to restore.')));
        const details = el('details', 'backup-file-details');
        details.append(el('summary', '', 'File details'),
          el('p', '', `THYB ${info.fileVersion} · settings V${info.settingsVersion} · ${info.bytes} bytes`),
          el('p', '', `Board ${info.uid} · source build ${info.build}`));
        fileSummary.append(details);
      } else fileSummary.append(el('p', '', current.error || 'File rejected. Choose a complete THY666 backup.'));
    } else fileSummary.append(el('p', 'inspector-notice', 'Choose a backup to restore.'));
    status.textContent = working && client.status?.message ? client.status.message : notice;
    status.hidden = !status.textContent;
    if (!connected && dialog) closeConfirmation();
    return { connected, busy, selected: selection?.name || null, canRestore: !restore.disabled, confirmationOpen: Boolean(dialog), notice: status.textContent };
  }
  function say(message, kind = 'info') {
    if (disposed) return;
    notice = String(message); notify(notice, kind); refresh();
  }
  function download(bytes, name) {
    if(runtime?.kind==='desktop')return runtime.saveFile(bytes,name);
    const platform = doc.defaultView || globalThis, BlobType = platform.Blob || globalThis.Blob;
    const urlAPI = platform.URL || globalThis.URL;
    if (!BlobType || !urlAPI?.createObjectURL) throw new Error('This browser cannot download the backup file.');
    const blob = new BlobType([bytes], { type: 'application/octet-stream' }), url = urlAPI.createObjectURL(blob);
    try {
      const link = el('a'); link.href = url; link.download = name; link.hidden = true;
      doc.body.append(link); link.click(); link.remove();
    } finally { queueMicrotask(() => urlAPI.revokeObjectURL(url)); }
  }
  async function readBackup() {
    if (disposed || read.disabled || working) return;
    working = true; notice = 'Reading backup from the connected board…'; refresh();
    try {
      const board = client.backupIdentity;
      if (!board) throw new Error('Connect a board with a verified backup identity first.');
      const bytes = await client.downloadBackup();
      if (disposed) return;
      const info = inspectDeviceBackup(bytes, board);
      const name = `THY666-${info.uid}-${info.build}.thyb`;
      await download(bytes, name);
      say('Backup downloaded · checksums and board identity verified. Keep this file.', 'success');
    } catch (error) { say(`Backup was not downloaded: ${error.message}`, 'error'); }
    finally { working = false; refresh(); }
  }
  async function selectFile() {
    const ticket = ++fileTicket, file = input.files?.[0]; selection = null;
    if (!file) { refresh(); return; }
    const name = String(file.name || 'Selected backup');
    if (!Number.isInteger(file.size) || file.size < BACKUP_HEADER_BYTES + 1 || file.size > BACKUP_MAX_BYTES) {
      say(`Choose a complete .thyb file between ${BACKUP_HEADER_BYTES + 1} and ${BACKUP_MAX_BYTES} bytes.`, 'error'); input.value = ''; return;
    }
    try {
      const bytes = new Uint8Array(await file.arrayBuffer());
      if (disposed || ticket !== fileTicket) return;
      if (bytes.length !== file.size) throw new Error('The selected file size changed while reading it.');
      const info = inspectDeviceBackup(bytes);
      selection = { name, bytes, info };
      const checked = selectionStatus();
      say(checked.error || '', checked.error ? 'error' : 'info');
    } catch (error) {
      if (!disposed && ticket === fileTicket) say(`File rejected: ${error.message}`, 'error');
    } finally { if (!disposed && ticket === fileTicket) { input.value = ''; refresh(); } }
  }
  function closeConfirmation({ focus = true } = {}) {
    if (!dialog) return;
    dialog.remove(); dialog = null;
    if (focus && returnFocus?.isConnected !== false) returnFocus?.focus?.();
    returnFocus = null;
  }
  function requestRestore() {
    if (disposed || restore.disabled || working || dialog || !client.connected || client.busy) return;
    const current = selectionStatus(); if (!current.valid) { say(current.error, 'error'); return; }
    returnFocus = doc.activeElement || restore;
    dialog = el('div', 'thy-help-overlay backup-confirmation'); dialog.dataset.backupDialog = '';
    const card = el('section', 'thy-help-dialog'); card.setAttribute('role', 'dialog'); card.setAttribute('aria-modal', 'true');
    card.setAttribute('aria-labelledby', `${prefix}-confirm-title`); card.setAttribute('aria-describedby', `${prefix}-confirm-copy`);
    const h = el('h2', '', 'Restore this backup?'); h.id = `${prefix}-confirm-title`;
    const copy = el('p', '', 'Stop playback and release all pads and controls. This replaces the board’s whole applied configuration, per-pad settings, custom library and current song. It does not restore firmware or the bootloader.'); copy.id = `${prefix}-confirm-copy`;
    card.append(h, copy, el('p', '', `File: ${selection.name} · source build ${current.info.build}`));
    const cancel = button('Cancel', 'cancel'), confirm = button('Restore backup', 'confirm');
    cancel.className += ' backup-cancel'; confirm.className += ' backup-confirm';
    card.append(cancel, confirm); dialog.append(card); doc.body.append(dialog); cancel.focus();
  }
  async function restoreBackup() {
    if (disposed || working || !dialog) return;
    const current = selectionStatus(), bytes = selection?.bytes;
    if (!client.connected || client.busy || !current.valid || !bytes) {
      closeConfirmation(); say(current.error || 'Connect the matching board and wait for current work to finish.', 'error'); return;
    }
    closeConfirmation({ focus: false }); working = true; notice = 'Restoring applied state · waiting for saved readback verification…'; refresh();
    try {
      const result = await client.restoreBackup(bytes);
      if (disposed) return;
      if (result?.verified !== true) throw new Error('Saved readback verification was not confirmed. Keep the backup file.');
      say('Restore complete. Settings saved and verified.', 'success');
      try { await onApplied(result); } catch (error) { say(`Restore saved and verified; refreshing the view failed: ${error.message}`, 'error'); }
    } catch (error) { say(`Restore not verified: ${error.message} Keep the backup file and inspect device state before retrying.`, 'error'); }
    finally { working = false; refresh(); if (!disposed) restore.focus(); }
  }
  listen(root, 'click', event => {
    const target = event.target.closest?.('[data-backup-action]'); if (!target || target.disabled) return;
    if (target.dataset.backupAction === 'read') void readBackup();
    else if (target.dataset.backupAction === 'restore') requestRestore();
  });
  listen(input, 'change', () => { if (!working) void selectFile(); });
  listen(doc, 'click', event => {
    if (!dialog?.contains(event.target)) return;
    const target = event.target.closest?.('[data-backup-action]');
    if (!target) return;
    event.preventDefault(); event.stopPropagation();
    if (target.dataset.backupAction === 'cancel') closeConfirmation();
    else if (target.dataset.backupAction === 'confirm') void restoreBackup();
  }, true);
  listen(doc, 'keydown', event => {
    if (!dialog) return;
    if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); closeConfirmation(); }
    if (event.key === 'Tab') {
      const cancel = dialog.querySelector('[data-backup-action="cancel"]'), confirm = dialog.querySelector('[data-backup-action="confirm"]');
      if (event.shiftKey && (doc.activeElement === cancel || !dialog.contains(doc.activeElement))) { event.preventDefault(); confirm.focus(); }
      else if (!event.shiftKey && (doc.activeElement === confirm || !dialog.contains(doc.activeElement))) { event.preventDefault(); cancel.focus(); }
    }
  }, true);
  // getEngine is deliberately not used to manufacture a device identity or
  // apply physical-device files to the independent local preview.
  void getEngine;
  refresh();
  return {
    refresh,
    destroy() {
      if (disposed) return;
      closeConfirmation(); disposed = true; fileTicket++; selection = null;
      for (const remove of listeners.splice(0)) remove(); mount.replaceChildren();
    },
  };
}
