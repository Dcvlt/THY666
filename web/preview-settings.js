/* Browser storage wraps the exact C settings payload. It is a local preview
 * preference, never a device backup or an operation on connected hardware. */
export const PREVIEW_SETTINGS_KEY = 'thy666-preview-settings-v1';
export class PreviewSettings {
  constructor(engine, storage, onStatus = () => {}) {
    this.engine = engine; this.storage = storage; this.onStatus = onStatus;
    this.saved = null; this.observed = null; this.changedAt = 0; this.blocked = false;
  }
  status(value) { this.onStatus(value); return value; }
  restore(loadCatalog) {
    try {
      const text = this.storage.getItem(PREVIEW_SETTINGS_KEY);
      if (!text) return this.status('Defaults');
      const record = JSON.parse(text);
      if (!Number.isInteger(record.version) || record.version < 1 || record.version > this.engine.settings.version) {
        this.blocked = true; return this.status('Unsupported settings');
      }
      if (record.format !== 'thy666.preview-settings' || !Array.isArray(record.payload) ||
          !record.payload.every(byte => Number.isInteger(byte) && byte >= 0 && byte <= 255)) throw new Error('Invalid settings');
      const bytes = Uint8Array.from(record.payload);
      if (this.engine.settings.payloadVersion(bytes) !== record.version ||
          !this.engine.settings.validate(bytes) || this.engine.settings.crc(bytes) !== record.crc) throw new Error('Corrupt settings');
      const catalog = this.engine.settings.catalog(bytes);
      if (!catalog || !this.engine.settings.canApply() || !loadCatalog(catalog) || !this.engine.settings.apply(bytes)) throw new Error('Settings restore failed');
      // An older payload must be rewritten even if its CRC happens to match
      // the new encoding. The normal idle debounce still owns that write.
      this.saved = record.version === this.engine.settings.version ? record.crc : null;
      this.observed = null;
      return this.status('Saved locally');
    } catch {
      this.blocked = true;
      return this.status('Storage error');
    }
  }
  tick(now, idle = true) {
    if (this.blocked) return;
    const payload = this.engine.settings.encode();
    if (!payload) return this.status('Settings error');
    const crc = this.engine.settings.crc(payload);
    if (crc === this.saved) { this.observed = crc; return this.status('Saved locally'); }
    if (crc !== this.observed) { this.observed = crc; this.changedAt = now; }
    this.status('Save pending');
    if (!idle || now - this.changedAt < 1500) return;
    try {
      const record = JSON.stringify({ format: 'thy666.preview-settings', version: this.engine.settings.version, crc, payload: Array.from(payload) });
      this.storage.setItem(PREVIEW_SETTINGS_KEY, record);
      if (this.storage.getItem(PREVIEW_SETTINGS_KEY) !== record) throw new Error('Storage verification failed');
      this.saved = crc; this.status('Saved locally');
    } catch { this.blocked = true; this.status('Storage error'); }
  }
}
