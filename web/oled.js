/* Independent THY666 glyph masks and native page geometry.
 * Logical bus timing is deterministic. Browser scheduling and optical OLED
 * scanning are separate from the firmware/SPI transfer model below.
 */

export const OLED_WIDTH = 128;
export const OLED_HEIGHT = 64;
export const OLED_FRAME_BYTES = 1024;

function validateProfile(profile) {
  if (profile?.format !== 'thy333.oled-profile' || profile.version !== 1) throw new Error('Unsupported OLED preview profile.');
  if (profile.geometry?.width !== OLED_WIDTH || profile.geometry?.height !== OLED_HEIGHT || profile.geometry?.page_bytes !== 128 || profile.geometry?.pages !== 8) throw new Error('OLED geometry does not match the recovered 128×64 page buffer.');
  if (!Array.isArray(profile.fonts) || profile.fonts.length === 0) throw new Error('Independent OLED font masks are missing.');
  for (const font of profile.fonts) {
    const first = font.first_codepoint ?? 32;
    const last = font.last_codepoint ?? 126;
    if (!Number.isInteger(font.width) || font.width < 1 || font.width > 16 || !Number.isInteger(font.height) || font.height < 1 || font.height > 64 || !Array.isArray(font.rows) || font.rows.length !== (last - first + 1) * font.height || !font.rows.every((row) => Number.isInteger(row) && row >= 0 && row <= 65535)) throw new Error('Invalid OLED font mask.');
    if (font.advances && (!Array.isArray(font.advances) || font.advances.length !== last - first + 1)) throw new Error('Invalid OLED font advances.');
  }
  return profile;
}

export class OLEDFramebuffer {
  constructor(profile) {
    this.profile = validateProfile(profile);
    this.data = new Uint8Array(OLED_FRAME_BYTES);
  }

  pixel(x, y, enabled = true) {
    if (!Number.isInteger(x) || !Number.isInteger(y) || x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    const index = Math.floor(y / 8) * OLED_WIDTH + x;
    const bit = 1 << (y % 8);
    this.data[index] = enabled ? this.data[index] | bit : this.data[index] & ~bit;
  }

  line(x1, y1, x2, y2) {
    if (x1 === x2) for (let y = y1; y <= y2; y++) this.pixel(x1, y);
    else if (y1 === y2) for (let x = x1; x <= x2; x++) this.pixel(x, y1);
    else throw new Error('OLED line helper accepts horizontal or vertical lines.');
  }

  rectangle(x, y, width, height, filled = false) {
    if (filled) for (let row = y; row < y + height; row++) for (let column = x; column < x + width; column++) this.pixel(column, row);
    else {
      this.line(x, y, x + width - 1, y);
      this.line(x, y + height - 1, x + width - 1, y + height - 1);
      this.line(x, y, x, y + height - 1);
      this.line(x + width - 1, y, x + width - 1, y + height - 1);
    }
  }

  text(text, fontId, x, y) {
    const font = this.profile.fonts.find((candidate) => candidate.id === fontId);
    if (!font) throw new Error(`OLED font '${fontId}' is unavailable.`);
    const first = font.first_codepoint ?? 32;
    const last = font.last_codepoint ?? 126;
    for (const character of text) {
      const code = character.codePointAt(0);
      if (code < first || code > last) throw new Error('OLED font only supports the supported character range.');
      if (x + font.width > OLED_WIDTH || y + font.height > OLED_HEIGHT) break;
      const glyph = code - first;
      for (let row = 0; row < font.height; row++) {
        const mask = font.rows[glyph * font.height + row];
        for (let column = 0; column < font.width; column++) this.pixel(x + column, y + row, Boolean(mask & (0x8000 >> column)));
      }
      x += font.advances ? font.advances[glyph] : font.width;
    }
    return x;
  }
}

export class OLEDTransferModel {
  constructor(profile) {
    validateProfile(profile);
    this.timing = profile.timing;
    this.spiHz = this.timing.spi_hz;
    this.periodMs = this.timing.firmware_service_interval_ms;
    if (!Number.isFinite(this.spiHz) || this.spiHz <= 0 || !Number.isFinite(this.periodMs) || this.periodMs <= 0 || this.timing.page_command_bytes !== 3 || this.timing.page_data_bytes !== 128) throw new Error('OLED profile does not contain a verified firmware service interval and page bus timing.');
    this.byteMs = 8 * 1000 / this.spiHz;
    this.pageMs = 131 * this.byteMs;
    this.visible = new Uint8Array(OLED_FRAME_BYTES);
    this.history = [{ time: -Infinity, frame: new Uint8Array(OLED_FRAME_BYTES) }];
    this.nextRequest = 0;
    this.pageStart = Infinity;
    this.page = 0;
    this.pending = false;
    this.frames = 0;
    this.pages = 0;
    this.coalesced = 0;
    this.lastAdvance = 0;
  }

  setFrame(frame, now = this.lastAdvance) {
    if (!(frame instanceof Uint8Array) || frame.length !== OLED_FRAME_BYTES || !Number.isFinite(now) || now < this.lastAdvance) throw new Error('OLED writes require a complete 1024-byte frame and monotonic timestamp.');
    const changed = this.advance(now);
    this.history.push({ time: now, frame: frame.slice() });
    return changed;
  }

  sourceAt(time) {
    for (let index = this.history.length - 1; index >= 0; index--) if (this.history[index].time <= time) return this.history[index].frame;
    return this.history[0].frame;
  }

  advance(now) {
    if (!Number.isFinite(now) || now < this.lastAdvance) throw new Error('OLED clock must advance monotonically.');
    let changed = false;
    while (Math.min(this.nextRequest, this.pageStart + this.pageMs) <= now) {
      const completion = this.pageStart + this.pageMs;
      if (this.nextRequest <= completion) {
        const request = this.nextRequest;
        this.nextRequest += this.periodMs;
        if (Number.isFinite(this.pageStart)) {
          if (this.pending) this.coalesced++;
          this.pending = true;
        } else {
          this.page = 0;
          this.pageStart = request;
        }
      } else {
        for (let byte = 0; byte < 128; byte++) {
          // Transmit_IT reads a byte while servicing TXE; use the byte-start time.
          const readTime = this.pageStart + (3 + byte) * this.byteMs;
          const offset = this.page * 128 + byte;
          this.visible[offset] = this.sourceAt(readTime)[offset];
        }
        changed = true;
        this.pages++;
        this.page++;
        if (this.page < 8) this.pageStart = completion;
        else {
          this.frames++;
          if (this.pending) {
            this.pending = false;
            this.page = 0;
            this.pageStart = completion;
          } else this.pageStart = Infinity;
        }
      }
    }
    this.lastAdvance = now;
    // Retain the source at the beginning of the currently transmitting page.
    const cutoff = Number.isFinite(this.pageStart) ? this.pageStart : now;
    let keep = 0;
    while (keep + 1 < this.history.length && this.history[keep + 1].time <= cutoff) keep++;
    if (keep) this.history.splice(0, keep);
    return changed;
  }

  restart(now) {
    if (!Number.isFinite(now) || now < this.lastAdvance) throw new Error('OLED clock must restart monotonically.');
    const latest = this.history[this.history.length - 1].frame;
    this.history = [{ time: now, frame: latest }];
    this.nextRequest = now;
    this.pageStart = Infinity;
    this.pending = false;
    this.page = 0;
    this.lastAdvance = now;
  }

  nextDeadline() { return Math.min(this.nextRequest, this.pageStart + this.pageMs); }
  stats() { return { frames: this.frames, pages: this.pages, coalesced: this.coalesced, page: Number.isFinite(this.pageStart) ? this.page : null, spiHz: this.spiHz, firmwarePeriodMs: this.periodMs }; }
}

/* Mirror of core/thy_display.c for standalone bus-model tests. The workbench
 * injects engine.display and executes that C scheduler through WebAssembly.
 * Targets coalesce against bytes actually committed to the panel; only the
 * current <=16-byte addressed transfer stays frozen. */
export class InstrumentOLEDDirtyScheduler {
  constructor() { this.reset(); }

  reset() {
    this.target = new Uint8Array(OLED_FRAME_BYTES);
    this.shadow = new Uint8Array(OLED_FRAME_BYTES);
    this.valid = new Uint8Array(OLED_FRAME_BYTES);
    this.dirty = new Uint8Array(OLED_FRAME_BYTES);
    this.started = false;
    this.transfer = null;
    this.priorityCursor = this.otherCursor = this.priorityStreak = this.densePhase = 0;
    this.submittedMs = 0;
    this.sceneControl = false;
    this.scenePhase = 0;
    this.queuedScene = null;
    this.report = Object.fromEntries(['frames', 'chunks', 'commandBytes', 'dataBytes', 'submitted', 'coalesced', 'unchanged', 'generation', 'dirtyBytes', 'validBytes', 'initialComplete', 'active', 'lastGenerationAgeMs', 'maxGenerationAgeMs', 'failedChunks', 'sceneRequests', 'sceneCoalesced', 'sceneDeferred', 'sceneCompleted', 'panelVisible', 'sceneActive'].map(key => [key, 0]));
  }

  enableSceneControl(enabled) {
    if (this.started || this.transfer) return;
    this.sceneControl = Boolean(enabled);
    this.report.panelVisible = 0;
  }

  refresh(offset) {
    const dirty = Number(!this.valid[offset] || this.target[offset] !== this.shadow[offset]);
    this.report.dirtyBytes += dirty - this.dirty[offset];
    this.dirty[offset] = dirty;
  }

  finish() {
    if (this.report.active && !this.transfer && !this.report.dirtyBytes) {
      if (this.scenePhase) {
        if (this.scenePhase === 2 && this.report.initialComplete) this.scenePhase = 3;
        return;
      }
      this.report.active = 0;
      this.report.frames++;
    }
  }

  submitTarget(frame, now) {
    const changed = !this.started || frame.some((byte, offset) => byte !== this.target[offset]);
    const wasActive = this.report.active && this.started;
    this.started = true;
    this.report.submitted++;
    if (changed) {
      if (wasActive) this.report.coalesced++;
      this.report.generation++;
      this.submittedMs = now;
      this.target.set(frame);
    } else this.report.unchanged++;
    for (let offset = 0; offset < OLED_FRAME_BYTES; offset++) this.refresh(offset);
    if (this.report.dirtyBytes || this.transfer) this.report.active = 1;
    this.finish();
    return changed;
  }

  submit(frame, now) {
    if (this.report.sceneActive) { this.report.sceneDeferred++; return false; }
    if (this.sceneControl && !this.started) {
      this.scenePhase = 2;
      this.report.sceneActive = this.report.active = 1;
    }
    return this.submitTarget(frame, now);
  }

  submitScene(frame, now) {
    if (!this.sceneControl) return this.submit(frame, now);
    this.report.sceneRequests++;
    if (this.transfer?.kind === 2) {
      const changed = frame.some((byte, offset) => byte !== this.target[offset]);
      if (this.queuedScene) this.report.sceneCoalesced++;
      this.queuedScene = changed ? { frame: frame.slice(), submittedMs: now } : null;
      return changed;
    }
    const changed = !this.started || frame.some((byte, offset) => byte !== this.target[offset]);
    if (!changed) return false;
    if (this.report.sceneActive) this.report.sceneCoalesced++;
    this.report.sceneActive = this.report.active = 1;
    this.scenePhase = this.report.panelVisible ? 1 : 2;
    return this.submitTarget(frame, now);
  }

  find(priority) {
    const length = priority ? 384 : 640;
    const cursor = priority ? this.priorityCursor : this.otherCursor;
    const pages = [1, 5, 0, 6, 7]; // Match C: outer note rows before titles.
    for (let scanned = 0; scanned < length; scanned++) {
      const index = (cursor + scanned) % length;
      const page = priority ? 2 + Math.floor(index / 128) : pages[Math.floor(index / 128)];
      const offset = page * 128 + index % 128;
      if (this.dirty[offset]) return { offset, index };
    }
    return null;
  }

  prepare() {
    if (!this.report.active) return null;
    if (this.transfer) return this.transfer;
    if (this.scenePhase === 1 || this.scenePhase === 3) {
      const kind = this.scenePhase === 1 ? 1 : 2;
      this.transfer = { offset: 0, count: 0, generation: this.report.generation, submittedMs: this.submittedMs,
        kind, commandCount: 1, command: Uint8Array.of(kind === 1 ? 0xae : 0xaf), data: new Uint8Array(0) };
      return this.transfer;
    }
    if (!this.report.dirtyBytes) { this.finish(); return null; }
    const dense = this.report.dirtyBytes > 256;
    let priority = dense ? this.densePhase < 3 : this.priorityStreak < 3;
    let found = this.find(priority);
    if (!found) { priority = !priority; found = this.find(priority); }
    if (!found) return null;
    const { offset, index } = found;
    let count = 1;
    const capacity = Math.min(16, 128 - offset % 128);
    for (let byte = 1; byte < capacity; byte++) if (this.dirty[offset + byte]) count = byte + 1;
    this.transfer = { offset, count, generation: this.report.generation, submittedMs: this.submittedMs, kind: 0, commandCount: 3,
      command: Uint8Array.of(0xb0 | Math.floor(offset / 128), offset & 15, 0x10 | ((offset % 128) >> 4)),
      data: this.target.slice(offset, offset + count) };
    if (priority) {
      this.priorityCursor = (index + count) % 384;
      this.priorityStreak = Math.min(3, this.priorityStreak + 1);
    } else {
      this.otherCursor = (index + count) % 640;
      this.priorityStreak = 0;
    }
    if (dense) this.densePhase = (this.densePhase + 1) % 8;
    return this.transfer;
  }

  commit(success, now) {
    if (!this.transfer) return;
    if (!success) { this.report.failedChunks++; return; }
    const transfer = this.transfer;
    if (transfer.kind !== 0) {
      this.report.chunks++;
      this.report.commandBytes += transfer.commandCount;
      this.transfer = null;
      if (transfer.kind === 1) {
        this.report.panelVisible = 0;
        this.scenePhase = 2;
        this.finish();
      } else {
        this.report.panelVisible = 1;
        this.report.sceneActive = 0;
        this.scenePhase = 0;
        this.report.sceneCompleted++;
        this.finish();
        if (this.queuedScene) {
          const queued = this.queuedScene;
          this.queuedScene = null;
          this.report.sceneActive = this.report.active = 1;
          this.scenePhase = 1;
          this.submitTarget(queued.frame, queued.submittedMs);
        }
      }
      return;
    }
    for (let byte = 0; byte < transfer.count; byte++) {
      const offset = transfer.offset + byte;
      this.shadow[offset] = transfer.data[byte];
      if (!this.valid[offset]) { this.valid[offset] = 1; this.report.validBytes++; }
      this.refresh(offset);
    }
    this.report.initialComplete = Number(this.report.validBytes === OLED_FRAME_BYTES);
    this.report.chunks++;
    this.report.commandBytes += transfer.commandCount;
    this.report.dataBytes += transfer.count;
    this.report.lastGenerationAgeMs = now - transfer.submittedMs;
    this.report.maxGenerationAgeMs = Math.max(this.report.maxGenerationAgeMs, this.report.lastGenerationAgeMs);
    this.transfer = null;
    this.finish();
  }

  stats() { return { ...this.report, panelVisible: this.sceneControl ? this.report.panelVisible : 1 }; }
}

export class InstrumentOLEDTransferModel {
  constructor(profile, { foregroundGapMs = 0, scheduler = new InstrumentOLEDDirtyScheduler(), sceneControl = false } = {}) {
    validateProfile(profile);
    if (!Number.isFinite(foregroundGapMs) || foregroundGapMs < 0) throw new Error('OLED foreground gap must be nonnegative.');
    this.spiHz = 328125;
    this.periodMs = 16;
    this.byteMs = 8000 / this.spiHz;
    this.foregroundGapMs = foregroundGapMs;
    this.scheduler = scheduler;
    this.scheduler.reset();
    this.sceneControl = Boolean(sceneControl);
    this.scheduler.enableSceneControl(this.sceneControl);
    this.sceneRevision = undefined;
    this.visible = new Uint8Array(OLED_FRAME_BYTES);
    this.history = [{ time: -Infinity, frame: new Uint8Array(OLED_FRAME_BYTES) }];
    this.chunkStart = Infinity;
    this.transfer = null;
    this.readyAt = this.lastAdvance = 0;
    this.frames = this.pages = this.chunks = 0;
    this.lastDurationMs = this.maxDurationMs = this.frameStarted = 0;
    this.sharedScheduler = scheduler instanceof InstrumentOLEDDirtyScheduler ? 'javascript-mirror' : 'shared-c';
  }

  setFrame(frame, now = this.lastAdvance, { sceneRevision } = {}) {
    if (!(frame instanceof Uint8Array) || frame.length !== OLED_FRAME_BYTES || !Number.isFinite(now) || now < this.lastAdvance) throw new Error('OLED writes require a complete 1024-byte frame and monotonic timestamp.');
    const changed = this.advance(now);
    const wasActive = this.scheduler.stats().active;
    this.history = [{ time: now, frame: frame.slice() }];
    const sceneRequested = sceneRevision !== undefined && this.sceneRevision !== undefined && sceneRevision !== this.sceneRevision;
    if (sceneRevision !== undefined) this.sceneRevision = sceneRevision;
    if (sceneRequested) this.scheduler.submitScene(frame, Math.floor(now) >>> 0);
    else this.scheduler.submit(frame, Math.floor(now) >>> 0);
    if (!wasActive && this.scheduler.stats().active) this.frameStarted = now;
    if (!this.transfer) this.readyAt = Math.max(this.readyAt, now);
    return this.advance(now) || changed;
  }

  chunkMs() { return this.transfer ? (this.transfer.commandCount + this.transfer.count) * this.byteMs : 0; }

  advance(now) {
    if (!Number.isFinite(now) || now < this.lastAdvance) throw new Error('OLED clock must advance monotonically.');
    let changed = false;
    // A delayed browser callback can drain at most one full-panel-sized batch.
    // New target submissions replace old work; no render-request queue exists.
    for (let serviced = 0; serviced < 64; serviced++) {
      if (!this.transfer) {
        if (this.readyAt > now) break;
        this.transfer = this.scheduler.prepare(Math.floor(this.readyAt) >>> 0);
        if (!this.transfer) break;
        this.chunkStart = this.readyAt;
      }
      const complete = this.chunkStart + this.chunkMs();
      if (complete > now) break;
      if (this.transfer.kind === 0) {
        this.visible.set(this.transfer.data, this.transfer.offset);
        if ((this.transfer.offset + this.transfer.count) % 128 === 0) this.pages++;
      }
      this.scheduler.commit(true, Math.floor(complete) >>> 0);
      this.transfer = null;
      this.chunkStart = Infinity;
      this.readyAt = complete + this.foregroundGapMs;
      changed = true;
      const report = this.scheduler.stats();
      if (report.frames !== this.frames) {
        this.frames = report.frames;
        this.lastDurationMs = complete - this.frameStarted;
        this.maxDurationMs = Math.max(this.maxDurationMs, this.lastDurationMs);
      }
    }
    this.lastAdvance = now;
    const report = this.scheduler.stats();
    this.frames = report.frames;
    this.chunks = report.chunks;
    return changed;
  }

  restart(now) {
    if (!Number.isFinite(now) || now < this.lastAdvance) throw new Error('OLED clock must restart monotonically.');
    // Preserve committed pixels and at most one immutable transfer. Resume its
    // wire time now instead of replaying the hidden tab's elapsed interval.
    if (this.transfer) this.chunkStart = now;
    this.readyAt = now;
    this.lastAdvance = now;
  }

  nextDeadline() {
    if (this.transfer) return this.chunkStart + this.chunkMs();
    return this.scheduler.stats().active ? this.readyAt : Infinity;
  }

  stats() {
    return { ...this.scheduler.stats(), transport: 'instrument', dirtyRegions: true, frozen: false,
      immutableChunks: true, scheduler: this.sharedScheduler, pages: this.pages,
      page: this.transfer ? Math.floor(this.transfer.offset / 128) : null,
      spiHz: this.spiHz, firmwarePeriodMs: this.periodMs, foregroundGapMs: this.foregroundGapMs,
      lastDurationMs: this.lastDurationMs, maxDurationMs: this.maxDurationMs };
  }
}

export class OLEDPreview {
  constructor(canvas, profile, { instrumentScheduler } = {}) {
    this.profile = validateProfile(profile);
    this.canvas = canvas;
    canvas.width = OLED_WIDTH;
    canvas.height = OLED_HEIGHT;
    this.context = canvas.getContext('2d', { alpha: false });
    if (!this.context) throw new Error('Canvas pixel rendering is unavailable.');
    this.context.imageSmoothingEnabled = false;
    this.image = this.context.createImageData(OLED_WIDTH, OLED_HEIGHT);
    this.model = new OLEDTransferModel(profile);
    this.instrumentScheduler = instrumentScheduler;
    this.transport = 'reference';
    this.origin = performance.now();
    this.timer = null;
    this.running = false;
    this.paint();
    this.resume();
  }

  setFrame(frame, options) {
    if (this.model.setFrame(frame, performance.now() - this.origin, options)) this.paint();
    if (this.running && this.timer === null) this.pump();
  }

  setTransport(transport) {
    if (!['reference', 'instrument'].includes(transport)) throw new Error('Unknown OLED transport.');
    if (transport === this.transport) return;
    const running = this.running;
    this.pause();
    const source = this.model.history[this.model.history.length - 1].frame;
    const visible = this.model.visible.slice();
    this.model = transport === 'instrument' ? new InstrumentOLEDTransferModel(this.profile, { scheduler: this.instrumentScheduler, sceneControl: true }) : new OLEDTransferModel(this.profile);
    this.model.visible.set(visible);
    this.origin = performance.now();
    this.transport = transport;
    if (transport === 'reference') this.model.setFrame(source, 0);
    this.paint();
    if (running) this.resume();
  }

  paint() {
    const report = this.model.stats();
    for (let y = 0; y < OLED_HEIGHT; y++) for (let x = 0; x < OLED_WIDTH; x++) {
      const on = report.panelVisible !== 0 && Boolean(this.model.visible[Math.floor(y / 8) * OLED_WIDTH + x] & (1 << (y % 8)));
      const pixel = (y * OLED_WIDTH + x) * 4;
      this.image.data[pixel] = this.image.data[pixel + 1] = this.image.data[pixel + 2] = on ? 255 : 0;
      this.image.data[pixel + 3] = 255;
    }
    this.context.putImageData(this.image, 0, 0);
    this.canvas.dataset.pages = this.model.pages;
    this.canvas.dataset.frames = this.model.frames;
    this.canvas.dataset.transport = this.transport;
    this.canvas.dataset.scheduler = report.scheduler || 'historical-reference';
    for (const key of ['dirtyBytes', 'generation', 'dataBytes', 'commandBytes', 'lastGenerationAgeMs', 'maxGenerationAgeMs', 'panelVisible', 'sceneActive', 'sceneRequests', 'sceneCompleted']) {
      if (report[key] === undefined) delete this.canvas.dataset[key];
      else this.canvas.dataset[key] = report[key];
    }
  }

  pump() {
    if (!this.running) return;
    this.timer = null;
    const now = performance.now() - this.origin;
    if (this.model.advance(now)) this.paint();
    const deadline = this.model.nextDeadline();
    if (Number.isFinite(deadline)) this.timer = window.setTimeout(() => this.pump(), Math.max(1, Math.min(60000, deadline - (performance.now() - this.origin))));
  }

  pause() {
    this.running = false;
    if (this.timer !== null) window.clearTimeout(this.timer);
    this.timer = null;
  }

  resume() {
    if (this.running) return;
    this.model.restart(performance.now() - this.origin);
    this.running = true;
    this.pump();
  }

  close() { this.pause(); }
}
