/* SPDX-License-Identifier: GPL-3.0-or-later
 * Direct browser updater. Rust owns package validation and protocol policy;
 * JavaScript owns only the explicit chooser, asynchronous I/O and UI status.
 */
import { createBrowserHID } from './browser-hid.js';
import { UPDATE_HELPER_PROTOCOL, UPDATE_PACKAGE_MAX_BYTES } from './updater-client.js';

const CORE_MAX = 4 * 1024 * 1024, JSON_MAX = 65536, ABI = 1;
const encoder = new TextEncoder(), decoder = new TextDecoder('utf-8', { fatal: true });
const clone = value => JSON.parse(JSON.stringify(value));
const lowerHex = (value, length) => typeof value === 'string' && new RegExp(`^[0-9a-f]{${length}}$`).test(value);
const sha256 = async (bytes, subtle) => Array.from(new Uint8Array(await subtle.digest('SHA-256', bytes)), n => n.toString(16).padStart(2, '0')).join('');
function localURL(value, base) {
  const result = new URL(value, base);
  if (!['http:', 'https:'].includes(result.protocol) || result.origin !== new URL(base).origin || result.username || result.password || result.hash || result.search)
    throw new Error('Updater assets must use the same secure site without credentials or URL parameters.');
  return result;
}
async function boundedDownload(url, limit, fetcher, { optional = false } = {}) {
  if (typeof fetcher !== 'function') throw new Error('This browser cannot load the updater.');
  const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 15000);
  try {
    const response = await fetcher(url.href, { cache: 'no-store', credentials: 'omit', redirect: 'error', referrerPolicy: 'no-referrer', signal: controller.signal });
    if (optional && response.status === 404) return null;
    if (!response.ok) throw new Error('An updater asset is unavailable. Refresh the page or use the desktop app.');
    const declared = response.headers?.get?.('Content-Length');
    if (declared !== null && declared !== undefined && (!/^\d+$/.test(declared) || Number(declared) > limit)) throw new Error('An updater download exceeded its size limit.');
    if (!response.body?.getReader) {
      const bytes = new Uint8Array(await response.arrayBuffer());
      if (bytes.length > limit) throw new Error('An updater download exceeded its size limit.');
      return bytes;
    }
    const reader = response.body.getReader(), pieces = []; let size = 0;
    try {
      while (true) {
        const { done, value } = await reader.read(); if (done) break;
        if (!(value instanceof Uint8Array) || (size += value.length) > limit) { await reader.cancel(); throw new Error('An updater download exceeded its size limit.'); }
        pieces.push(value);
      }
    } finally { reader.releaseLock(); }
    const bytes = new Uint8Array(size); let at = 0;
    for (const piece of pieces) { bytes.set(piece, at); at += piece.length; }
    return bytes;
  } catch (error) {
    if (error?.name === 'AbortError') throw new Error('The updater download timed out. No USB commands were sent.');
    throw error;
  } finally { clearTimeout(timer); }
}
export async function loadBrowserUpdaterCore({ wasmURL = new URL('./generated/browser-updater.wasm', import.meta.url),
  coreManifestURL = new URL('./generated/browser-updater.json', import.meta.url), baseURL = globalThis.location?.href || import.meta.url,
  fetch: fetcher = globalThis.fetch, subtle = globalThis.crypto?.subtle, instantiate = globalThis.WebAssembly?.instantiate } = {}) {
  if (!subtle?.digest || typeof instantiate !== 'function') throw new Error('WebAssembly and secure cryptography are required for browser installation.');
  const wasmLocation = localURL(wasmURL, baseURL), manifestLocation = localURL(coreManifestURL, baseURL);
  const manifest = JSON.parse(decoder.decode(await boundedDownload(manifestLocation, JSON_MAX, fetcher)));
  if (manifest?.format !== 'thy666.browser-updater' || manifest.version !== 1 || manifest.abi !== ABI ||
      !Number.isInteger(manifest.artifact?.bytes) || manifest.artifact.bytes < 8 || manifest.artifact.bytes > CORE_MAX || !lowerHex(manifest.artifact.sha256, 64))
    throw new Error('The browser updater manifest is invalid.');
  const bytes = await boundedDownload(wasmLocation, CORE_MAX, fetcher);
  if (bytes.length !== manifest.artifact.bytes || await sha256(bytes, subtle) !== manifest.artifact.sha256) throw new Error('The browser updater differs from its build manifest.');
  const module = await instantiate(bytes, {}), exports = (module.instance || module).exports;
  if (!(exports?.memory instanceof WebAssembly.Memory) || ['thy_update_abi_version', 'thy_update_input_ptr', 'thy_update_input_capacity',
    'thy_update_output_ptr', 'thy_update_request', 'thy_update_reset'].some(name => typeof exports[name] !== 'function') || exports.thy_update_abi_version() !== ABI)
    throw new Error('The browser updater uses an unsupported WebAssembly interface.');
  return {
    reset() { if (exports.thy_update_reset() !== 0) throw new Error('The Rust update session is still active.'); },
    request(metadata, raw = new Uint8Array(), now = 0) {
      const json = encoder.encode(JSON.stringify(metadata));
      if (json.length > 4096 || !(raw instanceof Uint8Array) || raw.length > UPDATE_PACKAGE_MAX_BYTES || !Number.isFinite(now) || now < 0)
        throw new Error('Invalid browser updater request.');
      const input = exports.thy_update_input_ptr(), capacity = exports.thy_update_input_capacity(), length = 4 + json.length + raw.length;
      if (!Number.isSafeInteger(input) || input < 0 || !Number.isSafeInteger(capacity) || length > capacity || input + capacity > exports.memory.buffer.byteLength)
        throw new Error('The browser updater input buffer is invalid.');
      new DataView(exports.memory.buffer).setUint32(input, json.length, true);
      const memory = new Uint8Array(exports.memory.buffer); memory.set(json, input + 4); memory.set(raw, input + 4 + json.length);
      const size = exports.thy_update_request(length, now), output = exports.thy_update_output_ptr();
      if (!Number.isSafeInteger(size) || size < 1 || size > JSON_MAX || !Number.isSafeInteger(output) || output < 0 || output + size > exports.memory.buffer.byteLength)
        throw new Error('The browser updater returned an invalid response.');
      const result = JSON.parse(decoder.decode(new Uint8Array(exports.memory.buffer, output, size)));
      if (typeof result?.ok !== 'boolean') throw new Error('The Rust updater returned an unsupported response.');
      return result;
    },
  };
}

function checkedInfo(value) {
  if (!value || !['thy666', 'stock'].includes(value.kind) || typeof value.installable !== 'boolean' || !lowerHex(value.file_sha256, 64) ||
      !lowerHex(value.application_sha256, 64) || !Number.isInteger(value.application_bytes) || value.application_bytes < 1 || value.application_bytes > 0x80000 ||
      !Number.isInteger(value.file_bytes) || value.file_bytes > UPDATE_PACKAGE_MAX_BYTES || value.file_bytes < 142 ||
      value.kind === 'thy666' && value.build_id !== null && value.build_id !== undefined && !lowerHex(value.build_id, 16))
    throw new Error('The Rust updater returned incomplete package information.');
  return clone(value);
}
function rustFailure(result) {
  if (result?.ok === true) return result;
  const details = result?.error;
  const error = new Error(typeof details?.message === 'string' ? details.message : 'The Rust updater rejected this operation.');
  error.protocol = { phase: details?.phase || 'preflight', mutation_started: details?.mutation_started === true, indeterminate: details?.indeterminate === true };
  throw error;
}
function entry(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) || Object.keys(value).some(key => !['id', 'label', 'kind', 'file', 'sha256', 'build_id'].includes(key)) ||
      !/^[A-Za-z0-9_-]{1,64}$/.test(value.id || '') || typeof value.label !== 'string' || !value.label.length || value.label.length > 128 || /[\x00-\x1f\x7f]/.test(value.label) ||
      !['thy666', 'stock'].includes(value.kind) || !lowerHex(value.sha256, 64) || typeof value.file !== 'string' || value.file.length > 255 ||
      value.file.startsWith('.') || !value.file.endsWith('.333') || /[\\/:*?"<>|\x00-\x1f\x7f]/.test(value.file) || /[. ]$/.test(value.file) ||
      /^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)/i.test(value.file) ||
      (value.kind === 'thy666' ? !lowerHex(value.build_id, 16) : value.build_id != null)) throw new Error('A published firmware entry is invalid.');
  return { ...value };
}
function checkedInstall(request) {
  const keys = ['operation_id', 'device_id', 'package_sha256', 'expected_uid', 'backup_disposition', 'confirmed', 'preparation_acknowledged', 'physical_target_confirmed'];
  if (!request || typeof request !== 'object' || Array.isArray(request) || Object.keys(request).some(key => !keys.includes(key)) ||
      !/^[A-Za-z0-9_-]{16,64}$/.test(request.operation_id || '') || typeof request.device_id !== 'string' || !request.device_id || request.device_id.length > 128 ||
      !lowerHex(request.package_sha256, 64) || ['confirmed', 'preparation_acknowledged', 'physical_target_confirmed'].some(key => request[key] !== true) ||
      !['downloaded', 'unavailable'].includes(request.backup_disposition)) throw new Error('Explicit board, firmware, backup and preparation confirmation is required.');
  if (request.expected_uid != null && !/^[0-9a-f]{24}$/i.test(request.expected_uid) || request.backup_disposition === 'downloaded' && !request.expected_uid)
    throw new Error('A downloaded backup requires its valid 24-digit board UID.');
  const checked = Object.fromEntries(Object.keys(request).sort().map(key => [key, request[key]]));
  if (checked.expected_uid) checked.expected_uid = checked.expected_uid.toLowerCase();
  return checked;
}

export function createBrowserUpdaterClient({ hid = globalThis.navigator?.hid, secureContext = globalThis.isSecureContext === true,
  loadCore = loadBrowserUpdaterCore, wasmURL, coreManifestURL, releasesURL = null, baseURL = globalThis.location?.href || import.meta.url,
  fetch: fetcher = globalThis.fetch, subtle = globalThis.crypto?.subtle, locks = globalThis.navigator?.locks,
  now = () => globalThis.performance.now(), randomUUID = () => globalThis.crypto.randomUUID(), transport: suppliedTransport,
  onProgress = () => {} } = {}) {
  const usb = suppliedTransport || createBrowserHID({ hid, secureContext, randomUUID });
  let core = null, connected = false, opening = false, pending = false, epoch = 0, image = null, devices = new Set();
  let operation = null, lastRequest = '', catalog = null, catalogLoaded = false;
  const used = new Set();
  const isRunning = () => operation?.state === 'running';
  const requireConnected = () => { if (!connected || !core) throw new Error('Connect the browser updater first.'); };
  const idle = () => { requireConnected(); if (pending || opening || isRunning()) throw new Error('An update or request already owns the browser updater.'); };
  const status = () => ({ protocol: UPDATE_HELPER_PROTOCOL, version: '0.1.0', mode: 'browser-direct', simulation: false, operation: operation ? clone(operation) : null });
  function update(change) { Object.assign(operation, change); operation.sequence++; try { onProgress(clone(operation)); } catch { /* Presentation callbacks never control USB delivery. */ } }
  async function stage(bytes, { name = '', expected_build = '' } = {}) {
    image = null;
    if (!(bytes instanceof Uint8Array) || bytes.length < 142 || bytes.length > UPDATE_PACKAGE_MAX_BYTES) throw new Error('Invalid .333 package size.');
    if (expected_build && !lowerHex(expected_build, 16)) throw new Error('Expected build must contain exactly 16 lowercase hexadecimal characters.');
    name = String(name).split(/[\\/]/).at(-1);
    if (name.length > 255) throw new Error('Firmware filename is too long.');
    core.reset();
    const copy = bytes.slice(), info = checkedInfo(rustFailure(core.request({ cmd: 'inspect', name, expected_build: expected_build || undefined }, copy, now())).package);
    image = { bytes: copy, name, expected_build, info }; return clone(info);
  }
  async function loadCatalog() {
    if (catalogLoaded) return;
    catalog = null;
    if (!releasesURL) { catalogLoaded = true; return; }
    const url = localURL(releasesURL, baseURL), bytes = await boundedDownload(url, JSON_MAX, fetcher, { optional: true });
    if (bytes === null) { catalogLoaded = true; return; }
    const manifest = JSON.parse(decoder.decode(bytes));
    if (manifest?.format !== 'thy666.release-catalog' || manifest.version !== 1 || Object.keys(manifest).some(key => !['format', 'version', 'releases'].includes(key)) ||
        !Array.isArray(manifest.releases) || manifest.releases.length > 16) throw new Error('The firmware catalog is invalid.');
    const entries = manifest.releases.map(entry);
    if (new Set(entries.map(value => value.id)).size !== entries.length) throw new Error('The firmware catalog has duplicate entries.');
    catalog = { url, entries }; catalogLoaded = true;
  }
  async function run(request, staged) {
    let transport = null, started = false, lastProgress = '';
    try {
      if (typeof locks?.request !== 'function') throw new Error('This browser cannot reserve exclusive update access. Use the desktop app.');
      await locks.request('thy666-bootloader-update', { mode: 'exclusive', ifAvailable: true }, async lock => {
        if (!lock) throw new Error('Another tab owns the updater. No USB commands were sent.');
        transport = await usb.open(request.device_id);
        // The package is rechecked by the same Rust parser immediately before
        // beginning, even though the JS staging copy is private to this client.
        const info = checkedInfo(rustFailure(core.request({ cmd: 'inspect', name: staged.name,
          expected_build: staged.info.build_id || staged.expected_build || undefined }, staged.bytes, now())).package);
        if (info.file_sha256 !== request.package_sha256 || !info.installable) throw new Error('The staged firmware changed before transfer.');
        let result = core.request({ cmd: 'begin', expected_uid: request.expected_uid || undefined }, new Uint8Array(), now());
        while (true) {
          rustFailure(result);
          const progress = result.progress;
          if (progress && typeof progress.phase === 'string' && typeof progress.message === 'string') {
            const key = JSON.stringify(progress);
            if (key !== lastProgress) { lastProgress = key; update({ phase: progress.phase, completed: progress.completed, total: progress.total, message: progress.message }); }
          }
          const action = result.action;
          if (!action || typeof action.kind !== 'string') throw new Error('The Rust update session returned an invalid action.');
          if (action.kind === 'done') {
            const outcome = action.outcome;
            if (!/^[0-9a-f]{24}$/i.test(outcome?.target_uid || '') || outcome.awaiting_application_verification !== true) throw new Error('The transfer result is incomplete; verify the board manually.');
            await transport.close(); transport = null;
            update({ state: 'awaiting-verification', phase: 'application-verification', completed: staged.info.application_bytes,
              total: staged.info.application_bytes, target_uid: outcome.target_uid.toLowerCase(), mutation_started: true,
              message: outcome.warning || 'Transfer CRCs match. Reconnect the application to verify its build and board identity.' });
            return;
          }
          if (!Number.isSafeInteger(action.id) || action.id < 0) throw new Error('The Rust update action identifier is invalid.');
          const feed = { cmd: 'feed', id: action.id, kind: action.kind };
          try {
            if (action.kind === 'write') {
              if (!Array.isArray(action.report) || action.report.length !== 65 || action.report.some(n => !Number.isInteger(n) || n < 0 || n > 255)) throw new Error('The Rust output report is malformed.');
              started = true; operation.mutation_started = true;
              feed.count = await transport.writeReport(Uint8Array.from(action.report));
            } else if (action.kind === 'read') feed.data = Array.from(await transport.readReport(action.timeout_ms));
            else if (action.kind === 'sleep') await transport.sleep(action.ms);
            else throw new Error('The Rust update action is unsupported.');
          } catch { feed.error = true; }
          result = core.request(feed, new Uint8Array(), now());
        }
      });
    } catch (error) {
      let closeFailed = false;
      if (transport) { try { await transport.close(); } catch { closeFailed = true; } }
      update({ state: 'failed', phase: error.protocol?.phase || operation.phase,
        mutation_started: started || error.protocol?.mutation_started === true,
        indeterminate: started || error.protocol?.indeterminate === true || closeFailed,
        message: error.message || 'The browser update stopped. No retry was sent.' });
    }
  }
  return {
    kind: 'browser-direct',
    get available() { return usb.available === true; }, get connected() { return connected; }, get busy() { return opening || pending || isRunning(); },
    async connect() {
      if (connected || opening || pending || isRunning()) throw new Error('The browser updater is already connected or busy.');
      if (!usb.available) throw new Error('Direct USB installation requires desktop Chrome or Edge on a secure page. The desktop app remains available.');
      const ticket = ++epoch; opening = true;
      try {
        const loaded = await loadCore({ wasmURL, coreManifestURL, baseURL, fetch: fetcher, subtle });
        if (ticket !== epoch) throw new Error('The browser updater connection was closed.');
        core = loaded; connected = true; return status();
      } finally { opening = false; }
    },
    async disconnect() {
      if (isRunning()) throw new Error('An update is active. Keep the tab and board connected until it finishes.');
      if (pending) throw new Error('Wait for the current browser request before disconnecting.');
      ++epoch; connected = false; image = null; devices.clear(); usb.clear(); core?.reset(); core = null;
    },
    async status() { requireConnected(); return status(); },
    async inspectPackage(bytes, options) { idle(); pending = true; try { return await stage(bytes, options); } finally { pending = false; } },
    async listDevices() {
      idle(); pending = true; devices.clear();
      try { const result = await usb.requestDevices(); devices = new Set(result.map(value => value.id)); return result; }
      finally { pending = false; }
    },
    async releases() {
      requireConnected(); if (pending || opening) throw new Error('Another updater request is pending.');
      pending = true;
      try {
        await loadCatalog();
        return { protocol: UPDATE_HELPER_PROTOCOL, simulation: false, configured: catalog !== null,
          releases: (catalog?.entries || []).map(value => ({ ...value, available: true, disabled_reason: null })) };
      } finally { pending = false; }
    },
    async selectRelease(id) {
      idle(); pending = true; image = null;
      try {
        await loadCatalog();
        const selected = catalog?.entries.find(value => value.id === id);
        if (!selected) throw new Error('This published firmware choice is unavailable. Choose a checked .333 file instead.');
        const url = localURL(encodeURIComponent(selected.file), catalog.url);
        if (url.pathname.slice(0, url.pathname.lastIndexOf('/') + 1) !== catalog.url.pathname.slice(0, catalog.url.pathname.lastIndexOf('/') + 1))
          throw new Error('The firmware file leaves its catalog directory.');
        const bytes = await boundedDownload(url, UPDATE_PACKAGE_MAX_BYTES, fetcher);
        const info = await stage(bytes, { name: selected.file, expected_build: selected.build_id || '' });
        if (info.file_sha256 !== selected.sha256 || info.kind !== selected.kind || info.installable !== true) { image = null; throw new Error('The firmware differs from its reviewed catalog entry.'); }
        return info;
      } finally { pending = false; }
    },
    async install(value) {
      requireConnected();
      const request = checkedInstall(value), fingerprint = JSON.stringify(request);
      if (operation?.operation_id === request.operation_id) {
        if (lastRequest !== fingerprint) throw new Error('This operation identifier belongs to a different request.');
        return clone(operation);
      }
      idle();
      if (used.has(request.operation_id)) throw new Error('A previous operation identifier cannot be replayed.');
      if (used.size >= 64) throw new Error('This updater session reached its operation limit. Close it while idle.');
      if (!devices.has(request.device_id) || !usb.has(request.device_id)) throw new Error('Explicitly detect and select the update-mode board first.');
      if (!image || image.info.file_sha256 !== request.package_sha256 || image.info.installable !== true || image.info.kind === 'thy666' && !image.info.build_identity_verified)
        throw new Error('Inspect and bind this exact supported firmware package before installing.');
      const staged = image; image = null; devices.clear(); used.add(request.operation_id); lastRequest = fingerprint;
      operation = { operation_id: request.operation_id, sequence: 1, state: 'running', phase: 'opening', completed: 0, total: staged.info.application_bytes,
        message: 'Opening the explicitly selected browser USB interface.', expected_build: staged.info.build_id || null, firmware_kind: staged.info.kind,
        expected_uid: request.expected_uid || null, target_uid: null, simulation: false, mutation_started: false, indeterminate: false };
      const initial = clone(operation);
      void run(request, staged);
      return initial;
    },
    async progress(id) {
      requireConnected();
      if (operation?.operation_id !== id) throw new Error('Unknown update operation. Do not start a replacement automatically.');
      return clone(operation);
    },
  };
}
