import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createCore } from '../../web/core.js';
import { InstrumentOLEDDirtyScheduler, InstrumentOLEDTransferModel } from '../../web/oled.js';

const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));
const profile = JSON.parse(await readFile(new URL('../../web/generated/oled-profile.json', import.meta.url)));
const fresh = () => createCore({ bytes });

test('WASM dirty scheduler validates native image and timestamp inputs', async () => {
  const { display } = await fresh();
  for (const frame of [[], new Uint8Array(1023), new Uint8Array(1025), null]) assert.throws(() => display.submit(frame, 0), /native OLED/);
  for (const now of [-1, 0.5, NaN, Infinity, 4294967296]) {
    assert.throws(() => display.submit(new Uint8Array(1024), now), /timestamp/);
    assert.throws(() => display.prepare(now), /timestamp/);
    assert.throws(() => display.commit(true, now), /timestamp/);
  }
  assert.throws(() => display.commit(1, 0), /result/);
  assert.equal(display.prepare(0), null);
});

test('JavaScript standalone mirror matches shared C transactions under churn, cancellation and retries', async () => {
  const { display } = await fresh();
  const mirror = new InstrumentOLEDDirtyScheduler();
  let random = 0x6d2b79f5;
  const byte = () => { random = Math.imul(random, 1664525) + 1013904223 | 0; return random >>> 24; };
  let now = 0;
  const frame = new Uint8Array(1024);
  const submit = () => {
    assert.equal(display.submit(frame, now), mirror.submit(frame, now));
    assert.deepEqual(display.stats(), mirror.stats());
  };
  submit();
  for (let action = 0; action < 900; action++, now++) {
    if (action % 7 === 0) {
      for (let change = 0; change < 12; change++) frame[(byte() << 2 | byte() & 3) % 1024] = byte();
      submit();
    }
    if (action % 53 === 0) { frame.fill(action & 255); submit(); }
    const actual = display.prepare(now), expected = mirror.prepare(now);
    assert.deepEqual(actual, expected, `transfer ${action}`);
    if (actual) {
      assert.ok(actual.count >= 1 && actual.count <= 16);
      assert.equal(Math.floor(actual.offset / 128), Math.floor((actual.offset + actual.count - 1) / 128));
      if (action % 17 === 0) {
        frame[actual.offset] ^= 255;
        submit();
        assert.deepEqual(display.prepare(now), actual, 'a newer image cannot rewrite the transfer');
        display.commit(false, now); mirror.commit(false, now);
        assert.deepEqual(display.prepare(now), actual, 'retry retains address and payload');
      }
      display.commit(true, now); mirror.commit(true, now);
    }
    assert.deepEqual(display.stats(), mirror.stats(), `report ${action}`);
  }
  for (let count = 0; display.stats().active && count < 1024; count++, now++) {
    assert.deepEqual(display.prepare(now), mirror.prepare(now));
    display.commit(true, now); mirror.commit(true, now);
  }
  assert.equal(display.stats().dirtyBytes, 0);
  assert.deepEqual(display.stats(), mirror.stats());
});

test('instrument bus preview executes C scheduler and matches standalone mirror at wire boundaries', async () => {
  const { display } = await fresh();
  const shared = new InstrumentOLEDTransferModel(profile, { scheduler: display, foregroundGapMs: 1.25 });
  const standalone = new InstrumentOLEDTransferModel(profile, { foregroundGapMs: 1.25 });
  const frame = new Uint8Array(1024);
  for (let now = 0; now < 1200; now += 16) {
    if (now < 160) frame.fill(now & 255);
    else {
      frame.fill(0, 256, 512);
      frame[256 + (now / 16 | 0) % 128] = 255;
      frame[900] = now & 255;
    }
    shared.setFrame(frame, now); standalone.setFrame(frame, now);
    assert.deepEqual(shared.visible, standalone.visible);
    const { scheduler: actualScheduler, ...actual } = shared.stats();
    const { scheduler: expectedScheduler, ...expected } = standalone.stats();
    assert.equal(actualScheduler, 'shared-c');
    assert.equal(expectedScheduler, 'javascript-mirror');
    assert.deepEqual(actual, expected);
  }
  shared.advance(2000); standalone.advance(2000);
  assert.deepEqual(shared.visible, standalone.visible);
  assert.deepEqual(shared.visible, frame);
  assert.equal(shared.stats().dirtyBytes, 0);
});

test('dirty display service leaves held ownership and queued MIDI untouched', async () => {
  const engine = await fresh();
  engine.press({ pad: 0, notes: [60, 64, 67], channel: 3, mask: 5, velocity: 96 });
  const before = engine.stats();
  const voices = engine.voices();
  engine.display.submit(new Uint8Array(1024).fill(255), 0);
  for (let now = 0; now < 100; now++) {
    engine.display.prepare(now);
    engine.display.commit(true, now);
  }
  assert.deepEqual(engine.stats(), before);
  assert.deepEqual(engine.voices(), voices);
  assert.equal(engine.drain().length, 6);
  engine.release(0);
  assert.equal(engine.drain().length, 6);
  assert.equal(engine.stats().activeVoices, 0);
});

test('shared C scene handoff pins one image while black and reveals only committed pixels', async () => {
  const { display } = await fresh();
  const mirror = new InstrumentOLEDDirtyScheduler();
  display.enableSceneControl(true); mirror.enableSceneControl(true);
  const initial = new Uint8Array(1024).fill(17);
  assert.equal(display.submit(initial, 0), mirror.submit(initial, 0));
  assert.equal(display.stats().panelVisible, 0);
  const shadow = new Uint8Array(1024);
  let now = 1;
  const step = (success = true) => {
    const actual = display.prepare(now), expected = mirror.prepare(now);
    assert.deepEqual(actual, expected);
    if (actual) {
      assert.ok(actual.kind === 0 ? actual.count >= 1 && actual.count <= 16 && actual.commandCount === 3 : actual.count === 0 && actual.commandCount === 1);
      if (success && actual.kind === 0) shadow.set(actual.data, actual.offset);
      display.commit(success, now); mirror.commit(success, now);
    }
    assert.deepEqual(display.stats(), mirror.stats());
    now++;
    return actual;
  };
  for (let count = 0; display.stats().sceneActive && count < 100; count++) {
    const animation = new Uint8Array(1024).fill(count);
    assert.equal(display.submit(animation, now), false, 'ordinary animation cannot replace or prolong a pinned handoff');
    assert.equal(mirror.submit(animation, now), false);
    const transfer = step();
    if (transfer?.kind === 2) assert.deepEqual(shadow, initial, 'all pixels match before AF reveals them');
    else assert.equal(display.stats().panelVisible, 0);
  }
  assert.equal(display.stats().sceneCompleted, 1);
  assert.equal(display.stats().panelVisible, 1);
  assert.equal(display.stats().dataBytes, 1024);
  assert.equal(display.stats().commandBytes, 193, '64 addressed chunks and one AF; no black framebuffer upload');

  const target = new Uint8Array(1024).fill(34);
  display.submitScene(target, now); mirror.submitScene(target, now);
  const off = display.prepare(now);
  assert.equal(off.kind, 1);
  assert.deepEqual(off.command, Uint8Array.of(0xae));
  step(false);
  assert.deepEqual(display.prepare(now), off, 'failed OFF remains the immutable transfer');
  const newer = new Uint8Array(1024).fill(51);
  display.submitScene(newer, now); mirror.submitScene(newer, now);
  step();
  assert.equal(display.stats().panelVisible, 0);
  while (display.stats().dirtyBytes) step();
  const on = display.prepare(now);
  assert.deepEqual(mirror.prepare(now), on);
  assert.equal(on.kind, 2);
  assert.deepEqual(shadow, newer);
  const queued = new Uint8Array(1024).fill(68);
  display.submitScene(target, now); mirror.submitScene(target, now);
  display.submitScene(queued, now); mirror.submitScene(queued, now);
  assert.deepEqual(display.prepare(now), on, 'AF and its complete underlying image are frozen');
  step();
  assert.equal(display.stats().panelVisible, 1);
  assert.deepEqual(shadow, newer, 'an already in-flight AF can only reveal its original complete image');
  assert.equal(display.prepare(now).kind, 1, 'queued replacement sends AE before any new pixel data');
  while (display.stats().sceneActive) {
    const transfer = step();
    if (transfer?.kind === 2) assert.deepEqual(shadow, queued);
  }
  assert.equal(display.stats().sceneCompleted, 3);
  assert.deepEqual(display.stats(), mirror.stats());
});

test('semantic scene revisions blank hierarchy changes while live updates remain visible', async () => {
  const { display } = await fresh();
  const panel = new InstrumentOLEDTransferModel(profile, { scheduler: display, sceneControl: true, foregroundGapMs: 1.4 });
  const first = new Uint8Array(1024).fill(17);
  panel.setFrame(first, 0, { sceneRevision: 0 });
  for (let now = 0; now <= 200; now++) panel.advance(now);
  assert.equal(panel.stats().panelVisible, 1);
  const second = new Uint8Array(1024).fill(34);
  panel.setFrame(second, 201, { sceneRevision: 1 });
  let sawBlack = false;
  for (let now = 202; now <= 400; now++) {
    panel.advance(now);
    if (!panel.stats().panelVisible) sawBlack = true;
    else assert.deepEqual(panel.visible, second, 'the new hierarchy is never partially visible');
  }
  assert.ok(sawBlack);
  const completed = panel.stats().sceneCompleted;
  const noteChange = second.slice(); noteChange[260] ^= 255;
  panel.setFrame(noteChange, 401, { sceneRevision: 1 });
  for (let now = 402; now <= 450; now++) {
    panel.advance(now);
    assert.equal(panel.stats().panelVisible, 1, 'same-scene live notes and selection never blank');
  }
  assert.deepEqual(panel.visible, noteChange);
  assert.equal(panel.stats().sceneCompleted, completed);
  assert.equal(panel.stats().sceneRequests, 1);
});
