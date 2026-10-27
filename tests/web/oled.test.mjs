import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { OLEDFramebuffer, OLEDTransferModel, InstrumentOLEDTransferModel, InstrumentOLEDDirtyScheduler } from '../../web/oled.js';

const profile = JSON.parse(await readFile(new URL('../../web/generated/oled-profile.json', import.meta.url)));
const epsilon = 0.0000001;

test('OLED page bits retain native 128x64 coordinates', () => {
  const frame = new OLEDFramebuffer(profile);
  frame.pixel(0, 0); frame.pixel(127, 63); frame.pixel(3, 8); frame.pixel(0, 0, false);
  assert.equal(frame.data.length, 1024);
  assert.equal(frame.data[0], 0);
  assert.equal(frame.data[1023], 128);
  assert.equal(frame.data[131], 1);
  const before = frame.data.slice();
  frame.pixel(128, 0); frame.pixel(0, 64); frame.pixel(-1, 0);
  assert.deepEqual(frame.data, before);
});

test('independent C glyph projection preserves bit orientation, flats and scaling', () => {
  assert.equal(profile.provenance.glyph_source, 'core/thy_ui.c');
  assert.ok(profile.provenance.source_sha256.match(/^[a-f0-9]{64}$/));
  const upper = new OLEDFramebuffer(profile);
  upper.text('B', 'small', 0, 0);
  assert.deepEqual([...upper.data.slice(0, 4)], [31, 21, 21, 10]);
  const flat = new OLEDFramebuffer(profile);
  flat.text('b', 'small', 0, 0);
  assert.deepEqual([...flat.data.slice(0, 4)], [31, 20, 20, 8]);
  const large = new OLEDFramebuffer(profile);
  large.text('B', 'large', 0, 0);
  for (let y = 0; y < 10; y++) for (let x = 0; x < 8; x++) {
    const largePixel = Boolean(large.data[Math.floor(y / 8) * 128 + x] & (1 << (y % 8)));
    const smallPixel = Boolean(upper.data[Math.floor(x / 2)] & (1 << Math.floor(y / 2)));
    assert.equal(largePixel, smallPixel, `${x},${y}`);
  }
});

test('SPI pages appear separately before the complete frame, at source-derived timing', () => {
  const model = new OLEDTransferModel(profile);
  model.setFrame(new Uint8Array(1024).fill(255), 0);
  assert.equal(model.advance(model.pageMs - epsilon), false);
  assert.equal(model.stats().pages, 0);
  assert.ok(model.visible.every(byte => byte === 0));
  model.advance(model.pageMs + epsilon);
  assert.equal(model.stats().pages, 1);
  assert.ok(model.visible.slice(0, 128).every(byte => byte === 255));
  assert.ok(model.visible.slice(128).every(byte => byte === 0));
  model.advance(profile.timing.minimum_cycle_ms + epsilon);
  assert.equal(model.stats().frames, 1);
  assert.equal(model.stats().pages, 8);
  assert.ok(model.visible.every(byte => byte === 255));
  assert.equal(model.stats().page, null);
  model.advance(profile.timing.firmware_service_interval_ms + epsilon);
  assert.equal(model.stats().page, 0);
  assert.equal(model.stats().frames, 1);
  assert.ok(Math.abs(model.periodMs - 41.6875) < epsilon);
});

test('live framebuffer changes can tear within a transmitting page', () => {
  const model = new OLEDTransferModel(profile);
  model.setFrame(new Uint8Array(1024).fill(255), 0);
  model.setFrame(new Uint8Array(1024), (3 + 63.5) * model.byteMs);
  model.advance(model.pageMs + epsilon);
  assert.ok(model.visible.slice(0, 64).every(byte => byte === 255));
  assert.ok(model.visible.slice(64, 128).every(byte => byte === 0));
  assert.equal(model.stats().pages, 1);
});

test('a redraw between pages retains already transmitted pages until the next burst', () => {
  const model = new OLEDTransferModel(profile);
  model.setFrame(new Uint8Array(1024).fill(255), 0);
  model.setFrame(new Uint8Array(1024), model.pageMs + epsilon);
  model.advance(profile.timing.minimum_cycle_ms + epsilon);
  assert.ok(model.visible.slice(0, 128).every(byte => byte === 255));
  assert.ok(model.visible.slice(128).every(byte => byte === 0));
  model.advance(profile.timing.firmware_service_interval_ms + model.pageMs + epsilon);
  assert.ok(model.visible.every(byte => byte === 0));
});

test('busy requests coalesce and restart discards hidden-tab catch-up work', () => {
  const fast = { ...profile, timing: { ...profile.timing, firmware_service_interval_ms: 1 } };
  const model = new OLEDTransferModel(fast);
  model.setFrame(new Uint8Array(1024).fill(255), 0);
  model.advance(100);
  assert.equal(model.stats().frames, 3);
  assert.ok(model.stats().coalesced > 0);
  const pages = model.stats().pages;
  model.restart(10000);
  model.advance(10000);
  assert.equal(model.stats().pages, pages);
  assert.equal(model.stats().page, 0);
  assert.throws(() => model.advance(9999), /monotonic/);
});

test('native framebuffer rejects incompatible geometry and missing fonts', () => {
  assert.throws(() => new OLEDFramebuffer({ ...profile, geometry: { ...profile.geometry, width: 256 } }), /geometry/);
  assert.throws(() => new OLEDFramebuffer({ ...profile, fonts: [] }), /missing/);
});

test('instrument initializes the panel once, then skips unchanged images', () => {
  const model = new InstrumentOLEDTransferModel(profile);
  model.setFrame(new Uint8Array(1024).fill(255), 0);
  const chunkMs = 19 * model.byteMs;
  assert.equal(model.advance(chunkMs - epsilon), false);
  model.advance(chunkMs + epsilon);
  assert.equal(model.stats().chunks, 1);
  assert.equal(model.stats().commandBytes, 3);
  assert.ok(model.visible.slice(256, 272).every(byte => byte === 255), 'note band receives the first chunk');
  assert.ok(model.visible.slice(0, 16).every(byte => byte === 0));
  model.advance(64 * chunkMs + epsilon);
  assert.equal(model.stats().frames, 1);
  assert.equal(model.stats().chunks, 64);
  assert.equal(model.stats().commandBytes, 192);
  assert.equal(model.stats().dataBytes, 1024);
  assert.equal(model.stats().initialComplete, 1);
  assert.ok(model.visible.every(byte => byte === 255));
  const before = model.stats().dataBytes;
  model.setFrame(new Uint8Array(1024).fill(255), 50);
  model.advance(50000);
  assert.equal(model.stats().dataBytes, before);
  assert.equal(model.stats().unchanged, 1);
  assert.equal(model.nextDeadline(), Infinity, 'idle preview schedules no zero-delay polling');
  assert.equal(model.periodMs, 16);
});

test('instrument sparse changes send only dirty runs with complete column addresses', () => {
  const model = new InstrumentOLEDTransferModel(profile);
  model.setFrame(new Uint8Array(1024), 0);
  model.advance(100);
  const target = new Uint8Array(1024);
  target.set([3, 4, 5], 260);
  target.set([7, 8], 804);
  model.setFrame(target, 110);
  assert.deepEqual(model.transfer.command, Uint8Array.of(0xb2, 4, 0x10));
  assert.equal(model.transfer.count, 3);
  model.advance(111);
  assert.deepEqual(model.visible, target);
  assert.equal(model.stats().dataBytes, 1029);
  assert.equal(model.stats().commandBytes, 198);
  assert.equal(model.stats().chunks, 66);
});

test('new targets coalesce during an immutable transfer and rebase against committed bytes', () => {
  const model = new InstrumentOLEDTransferModel(profile);
  const empty = new Uint8Array(1024);
  model.setFrame(empty, 0);
  model.advance(100);
  const target = new Uint8Array(1024).fill(255);
  model.setFrame(target, 110);
  target.fill(85);
  const prepared = model.transfer;
  model.setFrame(empty, 110.1);
  assert.equal(model.transfer, prepared);
  assert.ok(prepared.data.every(byte => byte === 255), 'the wire payload owns its copied bytes');
  model.advance(111);
  assert.deepEqual(model.visible, empty);
  assert.equal(model.stats().dataBytes, 1056, 'only the in-flight old sixteen bytes and their repair cross the bus');
  assert.equal(model.stats().coalesced, 1);
  assert.equal(model.stats().dirtyBytes, 0);
});

test('instrument contention does not backlog obsolete full frames', () => {
  const model = new InstrumentOLEDTransferModel(profile, { foregroundGapMs: 1 });
  model.setFrame(new Uint8Array(1024).fill(170), 0);
  model.setFrame(new Uint8Array(1024).fill(85), 16);
  model.setFrame(new Uint8Array(1024).fill(51), 32);
  model.setFrame(new Uint8Array(1024).fill(102), 48);
  for (const now of [150, 250]) model.advance(now);
  assert.ok(model.visible.every(byte => byte === 102));
  assert.equal(model.stats().dirtyBytes, 0);
  assert.equal(model.stats().submitted, 4);
  assert.equal(model.stats().coalesced, 3);
  assert.ok(model.stats().dataBytes < 4 * 1024);
  assert.throws(() => new InstrumentOLEDTransferModel(profile, { foregroundGapMs: -1 }), /nonnegative/);
});

test('instrument hidden-tab restart drops elapsed catch-up and preserves committed pixels', () => {
  const model = new InstrumentOLEDTransferModel(profile);
  model.setFrame(new Uint8Array(1024).fill(255), 0);
  model.advance(5);
  const visible = model.visible.slice();
  const chunks = model.stats().chunks;
  model.restart(10000);
  assert.deepEqual(model.visible, visible);
  model.advance(10000);
  assert.equal(model.stats().chunks, chunks);
  assert.notEqual(model.stats().page, null);
  model.advance(10100);
  assert.ok(model.visible.every(byte => byte === 255));
  assert.throws(() => model.advance(9999), /monotonically/);
});

test('instrument delayed callbacks have a bounded transfer batch and no request replay queue', () => {
  const scheduler = new InstrumentOLEDDirtyScheduler();
  const model = new InstrumentOLEDTransferModel(profile, { scheduler });
  model.setFrame(new Uint8Array(1024), 0);
  model.advance(10000);
  assert.equal(model.stats().chunks, 64);
  const target = new Uint8Array(1024);
  for (let offset = 0; offset < 1024; offset += 2) target[offset] = 1;
  model.setFrame(target, 10001);
  const before = model.stats().chunks;
  model.advance(20000);
  assert.ok(model.stats().chunks - before <= 64, 'a foreground callback cannot loop through arbitrary stale work');
  assert.equal(model.history.length, 1);
});

test('instrument scheduler gives note pages priority without starving header or footer', () => {
  const scheduler = new InstrumentOLEDDirtyScheduler();
  scheduler.submit(new Uint8Array(1024), 0);
  while (scheduler.stats().active) { scheduler.prepare(0); scheduler.commit(true, 0); }
  const header = new Uint8Array(1024); header[0] = 1;
  scheduler.submit(header, 1); scheduler.prepare(1); scheduler.commit(true, 1);
  const frame = new Uint8Array(1024);
  frame.fill(255, 256, 448);
  frame.fill(255, 0, 64);
  scheduler.submit(frame, 1);
  const pages = [];
  for (let count = 0; count < 12; count++) {
    const transfer = scheduler.prepare(count);
    pages.push(Math.floor(transfer.offset / 128));
    scheduler.commit(true, count);
  }
  for (let i = 0; i < pages.length; i += 4) {
    assert.ok(pages.slice(i, i + 3).every(page => page >= 2 && page <= 4));
    assert.ok([0, 1, 5, 6, 7].includes(pages[i + 3]));
  }
});

test('instrument packs fragmented glyph bytes and balances dense screen transitions', () => {
  const scheduler = new InstrumentOLEDDirtyScheduler();
  scheduler.submit(new Uint8Array(1024), 0);
  while (scheduler.stats().active) { scheduler.prepare(0); scheduler.commit(true, 0); }
  const frame = new Uint8Array(1024);
  for (let offset = 0; offset < 1024; offset += 3) frame[offset] = 255;
  scheduler.submit(frame, 1);
  let chunks = 0;
  while (scheduler.stats().active) { scheduler.prepare(1); scheduler.commit(true, 1); chunks++; }
  assert.ok(chunks <= 64, 'fragmented glyphs must not become hundreds of foreground transfers');
  assert.deepEqual(scheduler.shadow, frame);
});
