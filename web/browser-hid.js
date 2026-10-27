/* SPDX-License-Identifier: GPL-3.0-or-later
 * WebHID I/O only. The shared Rust session determines all update commands.
 * Constructing this adapter never enumerates, opens or writes a device.
 */
export const UPDATE_HID_FILTER = Object.freeze({ vendorId: 0x0333, productId: 0x0333, usagePage: 0xffab, usage: 0x0200 });
const INPUT_LIMIT = 64;
const transportError = text => new Error(text);

function reports(collection, direction) {
  return [...(collection[direction] || []), ...(collection.children || []).flatMap(child => reports(child, direction))];
}
function reportBytes(report) {
  if (!Array.isArray(report?.items) || !report.items.length) return -1;
  let bits = 0;
  for (const item of report.items) {
    if (!Number.isSafeInteger(item.reportSize) || !Number.isSafeInteger(item.reportCount) || item.reportSize < 1 || item.reportCount < 1) return -1;
    bits += item.reportSize * item.reportCount;
    if (!Number.isSafeInteger(bits) || bits > 512) return -1;
  }
  return bits === 512 ? 64 : -1;
}
export function matchesUpdateDevice(device) {
  if (device?.vendorId !== UPDATE_HID_FILTER.vendorId || device?.productId !== UPDATE_HID_FILTER.productId || !Array.isArray(device.collections)) return false;
  const collection = device.collections.filter(item => item.usagePage === UPDATE_HID_FILTER.usagePage && item.usage === UPDATE_HID_FILTER.usage);
  if (collection.length !== 1) return false;
  for (const direction of ['inputReports', 'outputReports']) {
    const values = reports(collection[0], direction).filter(report => report.reportId === 0);
    if (values.length !== 1 || reportBytes(values[0]) !== 64) return false;
  }
  return true;
}

export function createBrowserHID({ hid = globalThis.navigator?.hid, secureContext = globalThis.isSecureContext === true,
  randomUUID = () => globalThis.crypto.randomUUID(), setTimer = globalThis.setTimeout, clearTimer = globalThis.clearTimeout,
  writeTimeoutMs = 5000, queueLimit = INPUT_LIMIT } = {}) {
  let chosen = new Map(), sequence = 0, requesting = false, active = null;
  const known = new WeakMap();
  const available = secureContext && typeof hid?.requestDevice === 'function';
  if (!Number.isInteger(queueLimit) || queueLimit < 1 || queueLimit > 256) throw new TypeError('Invalid HID queue limit.');
  if (!Number.isInteger(writeTimeoutMs) || writeTimeoutMs < 1 || writeTimeoutMs > 60000) throw new TypeError('Invalid HID write timeout.');
  return {
    available,
    async requestDevices() {
      if (!available) throw transportError('Direct USB installation requires WebHID on a secure page. Use desktop Chrome or Edge, or the desktop app.');
      if (requesting || active) throw transportError('Another USB selection or update already owns this browser adapter.');
      requesting = true; chosen.clear();
      try {
        // Keep this chooser before the first await: callers must invoke it from
        // the Detect button's user activation, not a timer or automatic retry.
        const devices = await hid.requestDevice({ filters: [{ ...UPDATE_HID_FILTER }] });
        if (!Array.isArray(devices) || devices.length > 16) throw transportError('The device chooser returned an invalid selection.');
        const selected = [...new Set(devices)];
        if (selected.some(device => !matchesUpdateDevice(device))) throw transportError('The selected USB interface does not match the supported TheoryBoard update reports.');
        return selected.map(device => {
          let id = known.get(device);
          if (!id) { id = `webhid-${++sequence}-${randomUUID()}`; known.set(device, id); }
          chosen.set(id, device);
          return { id, label: `TheoryBoard update interface ${chosen.size}`, mode: 'bootloader', simulation: false };
        });
      } catch (error) {
        chosen.clear();
        if (error?.name === 'NotFoundError') return [];
        if (error?.name === 'NotAllowedError' || error?.name === 'SecurityError') throw transportError('USB permission was not granted. No update commands were sent.');
        throw error;
      } finally { requesting = false; }
    },
    has(id) { return chosen.has(id); },
    clear() { if (active || requesting) throw transportError('The USB interface is busy.'); chosen.clear(); },
    async open(id) {
      if (active || requesting) throw transportError('Another operation already owns this USB interface.');
      const device = chosen.get(id);
      if (!device || !matchesUpdateDevice(device)) throw transportError('Select the update-mode board again before installing.');
      if (device.opened) throw transportError('The selected USB interface is already open. Close other clients first.');
      let closed = false, fault = null, queue = [], reader = null, writing = false;
      const waits = new Set();
      const fail = message => {
        if (!fault) fault = message instanceof Error ? message : transportError(message);
        queue = [];
        if (reader) { const pending = reader; reader = null; clearTimer(pending.timer); pending.reject(fault); }
        for (const pending of waits) pending.reject(fault);
        return fault;
      };
      const check = () => { if (fault) throw fault; if (closed || !device.opened) throw transportError('The update-mode USB interface is closed.'); };
      const input = event => {
        if (closed || fault) return;
        if (event.device !== device || event.reportId !== 0 || !(event.data instanceof DataView) || event.data.byteLength !== 64) {
          fail('An unexpected HID report interrupted the update. No retry was sent.'); return;
        }
        const bytes = new Uint8Array(event.data.buffer, event.data.byteOffset, event.data.byteLength).slice();
        if (reader) { const pending = reader; reader = null; clearTimer(pending.timer); pending.resolve(bytes); }
        else if (queue.length < queueLimit) queue.push(bytes);
        else fail('The USB input queue overflowed. The update was stopped without replaying commands.');
      };
      const disconnected = event => { if (event.device === device) fail('The selected USB board disconnected. Verify its state before another update.'); };
      const bounded = async (promise, milliseconds, message) => {
        let timer, pending;
        const failure = new Promise((resolve, reject) => {
          pending = { reject }; waits.add(pending);
          timer = setTimer(() => reject(fail(message)), milliseconds);
        });
        try { return await Promise.race([promise, failure]); }
        finally { clearTimer(timer); waits.delete(pending); }
      };
      const transport = {
        async writeReport(report) {
          check();
          if (writing || reader) throw transportError('HID report operations must be sequential.');
          if (!(report instanceof Uint8Array) || report.length !== 65 || report[0] !== 0) throw transportError('Expected a zero report ID and 64 output payload bytes.');
          writing = true;
          try {
            await bounded(Promise.resolve().then(() => device.sendReport(0, report.slice(1))), writeTimeoutMs,
              'USB write timed out; delivery is uncertain. Do not retry the command.');
            check(); return 65;
          } catch (error) { throw fail(error); }
          finally { writing = false; }
        },
        async readReport(timeoutMs) {
          check();
          if (reader || writing) throw transportError('HID report operations must be sequential.');
          if (!Number.isInteger(timeoutMs) || timeoutMs < 1 || timeoutMs > 60000) throw transportError('Invalid HID read timeout.');
          if (queue.length) return queue.shift();
          return new Promise((resolve, reject) => {
            const pending = { resolve, reject, timer: null };
            pending.timer = setTimer(() => { if (reader === pending) { reader = null; resolve(new Uint8Array()); } }, timeoutMs);
            reader = pending;
          });
        },
        async sleep(ms) {
          // Delay is a host scheduling action, not a USB read. Preserve the
          // native protocol's behavior: a reboot during activation's delay is
          // reported by the next write, where Rust can retain its CRC result
          // and request application verification instead of replaying it.
          if (closed) throw transportError('The update-mode USB interface is closed.');
          if (!Number.isInteger(ms) || ms < 0 || ms > 60000) throw transportError('Invalid update delay.');
          await new Promise(resolve => setTimer(resolve, ms));
        },
        async close() {
          if (closed) return;
          closed = true; fail('The update USB handle was closed.');
          device.removeEventListener('inputreport', input); hid.removeEventListener?.('disconnect', disconnected);
          try { if (device.opened) await bounded(device.close(), writeTimeoutMs, 'Closing the USB handle timed out.'); }
          finally { if (active === transport) active = null; }
        },
      };
      active = transport;
      device.addEventListener('inputreport', input); hid.addEventListener?.('disconnect', disconnected);
      try {
        const opening = Promise.resolve().then(() => device.open());
        // A timed-out native open cannot be cancelled. If it resolves later,
        // release that handle instead of leaving an invisible USB owner behind.
        opening.then(() => { if (closed && device.opened) void device.close().catch(() => {}); }, () => {});
        await bounded(opening, writeTimeoutMs, 'Opening the USB interface timed out.');
        if (!matchesUpdateDevice(device)) throw transportError('The opened interface no longer matches the selected board.');
        check(); return transport;
      } catch (error) { await transport.close().catch(() => {}); throw error; }
    },
  };
}
