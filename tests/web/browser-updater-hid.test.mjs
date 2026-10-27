import test from 'node:test';
import assert from 'node:assert/strict';
import { createBrowserHID, matchesUpdateDevice, UPDATE_HID_FILTER } from '../../web/browser-hid.js';

const report = () => ({ reportId: 0, items: [{ reportSize: 8, reportCount: 64 }] });
class Device extends EventTarget {
  constructor() {
    super(); this.vendorId = 0x333; this.productId = 0x333; this.opened = false;
    this.collections = [{ usagePage: 0xffab, usage: 0x200, inputReports: [report()], outputReports: [report()] }];
    this.calls = [];
  }
  async open() { this.calls.push('open'); this.opened = true; }
  async close() { this.calls.push('close'); this.opened = false; }
  async sendReport(id, payload) { this.calls.push({ id, payload: payload.slice() }); }
  input(payload = new Uint8Array(64), id = 0) {
    const event = new Event('inputreport'); Object.assign(event, { device: this, reportId: id, data: new DataView(payload.buffer, payload.byteOffset, payload.byteLength) });
    this.dispatchEvent(event);
  }
}
function fixture(options = {}) {
  const device = new Device(), hid = new EventTarget(), calls = [];
  hid.requestDevice = async filters => { calls.push(filters); return [device]; };
  const adapter = createBrowserHID({ hid, secureContext: true, randomUUID: () => 'opaque', ...options });
  return { device, hid, calls, adapter, async open() { const [selected] = await adapter.requestDevices(); return adapter.open(selected.id); } };
}

test('WebHID is inert until an explicit chooser and opens only the selected matching reports', async () => {
  const f = fixture(); assert.deepEqual(f.calls, []); assert.deepEqual(f.device.calls, []);
  assert.equal(f.adapter.available, true); const [selected] = await f.adapter.requestDevices();
  assert.deepEqual(f.calls, [{ filters: [{ ...UPDATE_HID_FILTER }] }]);
  assert.deepEqual(f.device.calls, []); assert.ok(f.adapter.has(selected.id));
  assert.equal(selected.simulation, false); assert.equal(selected.mode, 'bootloader');
  assert.equal('serial' in selected, false);
  const transport = await f.adapter.open(selected.id); assert.deepEqual(f.device.calls, ['open']);
  const bytes = new Uint8Array(65); bytes[1] = 7;
  assert.equal(await transport.writeReport(bytes), 65);
  assert.equal(f.device.calls[1].id, 0); assert.equal(f.device.calls[1].payload.length, 64); assert.equal(f.device.calls[1].payload[0], 7);
  await transport.close(); assert.equal(f.device.opened, false);
});

test('USB identity, usage and exact report descriptors are checked before any open', async () => {
  for (const mutate of [d => { d.vendorId = 2; }, d => { d.collections[0].usage = 1; },
    d => { d.collections[0].outputReports[0].reportId = 1; }, d => { d.collections[0].inputReports[0].items[0].reportCount = 65; }]) {
    const f = fixture(); mutate(f.device); assert.equal(matchesUpdateDevice(f.device), false);
    await assert.rejects(f.adapter.requestDevices(), /does not match/); assert.deepEqual(f.device.calls, []);
  }
  assert.equal(createBrowserHID({ hid: {}, secureContext: true }).available, false);
  assert.equal(createBrowserHID({ hid: { requestDevice() {} }, secureContext: false }).available, false);
});

test('permission denial and cancellation leave no staged target or commands', async () => {
  const f = fixture(); f.hid.requestDevice = async () => { throw Object.assign(new Error(), { name: 'NotAllowedError' }); };
  await assert.rejects(f.adapter.requestDevices(), /permission was not granted/); assert.deepEqual(f.device.calls, []);
  f.hid.requestDevice = async () => { throw Object.assign(new Error(), { name: 'NotFoundError' }); };
  assert.deepEqual(await f.adapter.requestDevices(), []);
});

test('queued input is copied with its DataView offset; timeout never replays a write', async () => {
  const f = fixture(), transport = await f.open(), window = new Uint8Array(70); window[3] = 42;
  f.device.input(window.subarray(3, 67)); window.fill(0);
  assert.equal((await transport.readReport(10))[0], 42);
  assert.equal((await transport.readReport(2)).length, 0); assert.deepEqual(f.device.calls, ['open']);
  const pending = transport.readReport(50); f.device.input(new Uint8Array(64).fill(5)); assert.equal((await pending)[0], 5);
  await transport.close();
});

test('malformed reports and bounded input overflow fail closed', async () => {
  for (const bad of [f => f.device.input(new Uint8Array(65)), f => f.device.input(new Uint8Array(64), 1)]) {
    const f = fixture(), transport = await f.open(), pending = transport.readReport(100); bad(f);
    await assert.rejects(pending, /unexpected HID report/);
    await assert.rejects(transport.writeReport(new Uint8Array(65)), /unexpected HID report/); await transport.close();
  }
  const f = fixture({ queueLimit: 1 }), transport = await f.open(); f.device.input(); f.device.input();
  await assert.rejects(transport.readReport(10), /overflow/); await transport.close();
});

test('disconnect rejects a pending read and close removes the old listeners', async () => {
  const f = fixture(), transport = await f.open(), pending = transport.readReport(100);
  const event = new Event('disconnect'); event.device = f.device; f.hid.dispatchEvent(event);
  await assert.rejects(pending, /disconnected/); await transport.close();
  const again = await f.adapter.open((await f.adapter.requestDevices())[0].id);
  f.device.input(new Uint8Array(64).fill(8)); assert.equal((await again.readReport(10))[0], 8); await again.close();
});

test('overlapping reads, numbered outputs and stalled writes cannot start a second command', async () => {
  const f = fixture({ writeTimeoutMs: 3 }), transport = await f.open();
  const reading = transport.readReport(3); await assert.rejects(transport.readReport(3), /sequential/); await reading;
  const bad = new Uint8Array(65); bad[0] = 1; await assert.rejects(transport.writeReport(bad), /zero report ID/);
  f.device.sendReport = async () => { f.device.calls.push('stalled'); return new Promise(() => {}); };
  await assert.rejects(transport.writeReport(new Uint8Array(65)), /timed out/);
  await assert.rejects(transport.writeReport(new Uint8Array(65)), /timed out/);
  assert.equal(f.device.calls.filter(value => value === 'stalled').length, 1); await transport.close();
});

test('a late successful open after timeout is closed rather than orphaned', async () => {
  const f = fixture({ writeTimeoutMs: 3 }); let finish;
  f.device.open = () => new Promise(resolve => { finish = () => { f.device.opened = true; resolve(); }; });
  await assert.rejects(f.open(), /Opening.*timed out/); finish(); await new Promise(resolve => setImmediate(resolve));
  assert.equal(f.device.opened, false); assert.ok(f.device.calls.includes('close'));
});

test('a reboot during a host delay is handled by the next protocol I/O action', async () => {
  const f = fixture(), transport = await f.open(), delay = transport.sleep(2);
  const event = new Event('disconnect'); event.device = f.device; f.hid.dispatchEvent(event);
  await delay;
  await assert.rejects(transport.writeReport(new Uint8Array(65)), /disconnected/);
  await transport.close();
});
