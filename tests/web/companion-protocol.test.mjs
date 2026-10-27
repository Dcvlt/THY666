import test from 'node:test';
import assert from 'node:assert/strict';
import { COMPANION_SNAPSHOT_BYTES, decodeCompanionSnapshot, companionNoteName, paintDeviceOLED } from '../../web/companion-protocol.js';

const identity = { uid: '0123456789abcdef01234567', build: '90d128d90324c978', schema: 'ab'.repeat(32) };
const ascii = (bytes, offset, value) => bytes.set(new TextEncoder().encode(value), offset);
function snapshot() {
  const bytes = new Uint8Array(COMPANION_SNAPSHOT_BYTES), data = new DataView(bytes.buffer);
  ascii(bytes, 0, 'THYCMP1'); data.setUint16(8, 1, true); data.setUint16(10, bytes.length, true);
  data.setUint32(12, 17, true); data.setUint32(16, 123456, true); data.setUint32(20, 16, true);
  data.setUint32(24, 25, true); data.setUint32(28, 42, true);
  ascii(bytes, 32, identity.build); ascii(bytes, 48, identity.uid); ascii(bytes, 72, identity.schema);
  bytes[136] = 1; bytes[1159] = 128;
  bytes.set([17, 34, 51], 1160); bytes.set([254, 253, 252], 1160 + 95 * 3);
  bytes[1448] = 11; bytes[1466] = 19;
  bytes[1467] = 1; bytes[1478] = 128; // first and final pads
  bytes[1479] = 2; bytes[1484] = 4; // controls 1 and 42
  bytes[1485] = 1; bytes[1500] = 128; // sounding MIDI 0 and 127
  bytes[1501 + (60 >>> 3)] |= 1 << (60 & 7);
  bytes[1501 + (64 >>> 3)] |= 1 << (64 & 7);
  ascii(bytes, 1517, 'C MAJ'); ascii(bytes, 1549, 'MAJOR C');
  bytes.set([2, 2, 1, 3, 0, 0, 0, 1, 1, 1], 1581);
  data.setUint16(1591, 143, true); data.setUint16(1593, 146, true);
  bytes.set([2, 3, 49, 2, 8], 1595); data.setInt32(1600, -12, true);
  bytes[1604] = 31; bytes[1605] = 11; data.setUint16(1606, 69, true);
  bytes.set([65, 127, 1, 0, 80, 94, 0, 1, 0, 117], 1608);
  bytes[1618] = 2; bytes[1619] = 2; data.setUint16(1620, 7, true); data.setUint16(1622, 9, true);
  data.setUint32(1624, 123456, true); bytes.set([1, 2, 3, 4], 1628);
  bytes[1632] = 95; bytes[1633] = 27; data.setUint16(1634, 2048, true); data.setInt16(1636, -19, true);
  bytes.set([98, 3, 77, 1], 1638); data.setUint16(1642, 1377, true); data.setUint16(1644, 1354, true);
  data.setUint32(1646, 123450, true); data.setUint32(1650, 88, true); data.setUint32(1654, 23, true);
  bytes.set([1, 4, 20, 8, 1], 1658); data.setInt32(1664, -7, true);
  return bytes;
}

test('1680-byte snapshot decodes exact offsets, signed values and first/last bit masks', () => {
  const bytes = snapshot(), value = decodeCompanionSnapshot(bytes, identity);
  assert.equal(value.sequence, 17); assert.equal(value.now, 123456); assert.equal(value.frameMs, 16);
  assert.equal(value.instrumentRevision, 25); assert.equal(value.stateRevision, 42);
  assert.equal(value.frame.length, 1024); assert.equal(value.frame[0], 1); assert.equal(value.frame[1023], 128);
  assert.deepEqual(value.rgb[0], [17, 34, 51]); assert.deepEqual(value.rgb[95], [254, 253, 252]);
  assert.equal(value.center.length, 19); assert.equal(value.center[0], 11); assert.equal(value.center[18], 19);
  assert.deepEqual(value.held.flatMap((held, n) => held ? [n] : []), [0, 95]);
  assert.deepEqual(value.controlHeld.flatMap((held, n) => held ? [n] : []), [1, 42]);
  assert.deepEqual(value.sounding, [0, 127]); assert.deepEqual(value.pool, [60, 64]);
  assert.equal(value.harmony, 'C MAJ'); assert.equal(value.scale, 'MAJOR C');
  assert.equal(value.currentRow, 143); assert.equal(value.editRow, 146); assert.equal(value.editInstance, 2);
  assert.equal(value.currentInstance, 3); assert.equal(value.sensitivityTarget, 49);
  assert.equal(value.velocityTarget, 2); assert.equal(value.routeTarget, 8); assert.equal(value.editValue, -12);
  assert.equal(value.mpe, true); assert.equal(value.arp1, true); assert.equal(value.arp2, true);
  assert.equal(value.running, true); assert.equal(value.recording, true); assert.equal(value.tempo, 123.456);
  assert.deepEqual(value.pressure, [65, 127]); assert.deepEqual(value.aftertouchStage, [true, false]);
  assert.deepEqual(value.fixedVelocity, [80, 94]); assert.deepEqual(value.velocityMode, [0, 1]);
  assert.equal(value.inspect.logical, 95); assert.equal(value.inspect.electrical, 27); assert.equal(value.inspect.delta, -19);
  assert.equal(value.inspect.trigger, 1377); assert.equal(value.inspect.release, 1354);
  assert.equal(value.layoutRevision, 88); assert.equal(value.savedGeneration, 23); assert.equal(value.panelVisible, true);
  assert.equal(value.cursor, 4); assert.equal(value.selectedTrack, 20); assert.equal(value.view, 8);
  assert.equal(value.engine, 1); assert.equal(value.currentValue, -7);
  bytes[136] = 255;
  assert.equal(value.frame[0], 1, 'Published OLED bytes are a detached snapshot, not a mutable transfer view.');
});

test('snapshot uses byteOffset correctly and accepts the unavailable inspector sentinel', () => {
  const bytes = snapshot(); bytes[1632] = 255; bytes[1633] = 255;
  const padded = new Uint8Array(bytes.length + 19); padded.set(bytes, 7);
  const value = decodeCompanionSnapshot(padded.subarray(7, 7 + bytes.length), identity);
  assert.equal(value.inspect.logical, 255); assert.equal(value.inspect.electrical, 255);
  assert.equal(value.editValue, -12); assert.equal(value.currentValue, -7);
});

test('snapshot rejects bad framing, identities and bounded live fields', () => {
  assert.throws(() => decodeCompanionSnapshot(new Uint8Array(1679)), /size/);
  assert.throws(() => decodeCompanionSnapshot(new Array(1680).fill(0)), /size/);
  for (const mutate of [
    b => b[0] = 0, b => b[8] = 2, b => b[10] = 0,
    b => b[1605] = 12, b => b[1618] = 97, b => b[1619] = 129, b => b[1632] = 96,
  ]) {
    const bytes = snapshot(); mutate(bytes); assert.throws(() => decodeCompanionSnapshot(bytes, identity));
  }
  for (const changed of [
    { ...identity, uid: 'ff'.repeat(12) }, { ...identity, build: 'ff'.repeat(8) },
    { ...identity, schema: 'ff'.repeat(32) },
  ]) assert.throws(() => decodeCompanionSnapshot(snapshot(), changed), /identity/);
  assert.doesNotThrow(() => decodeCompanionSnapshot(snapshot(), { ...identity, uid: identity.uid.toUpperCase() }));
});

test('note labels preserve MIDI octaves and readable accidentals', () => {
  assert.equal(companionNoteName(0), 'C-1'); assert.equal(companionNoteName(60), 'C4');
  assert.equal(companionNoteName(61), 'C♯4'); assert.equal(companionNoteName(63), 'E♭4');
  assert.equal(companionNoteName(127), 'G9');
});

test('OLED painting uses vertical page bits at native resolution and rejects incomplete frames', () => {
  const frames = [], canvas = { getContext(type) {
    assert.equal(type, '2d');
    return { createImageData(width, height) { assert.equal(width, 128); assert.equal(height, 64); return { data: new Uint8ClampedArray(width * height * 4) }; },
      putImageData(pixels, x, y) { assert.equal(x, 0); assert.equal(y, 0); frames.push(pixels.data); } };
  } };
  const bytes = new Uint8Array(1024); bytes[0] = 1; bytes[127] = 128; bytes[1023] = 128;
  paintDeviceOLED(canvas, bytes); assert.equal(frames.length, 1);
  const pixel = (x, y) => [...frames[0].subarray((y * 128 + x) * 4, (y * 128 + x) * 4 + 4)];
  assert.deepEqual(pixel(0, 0), [207, 232, 225, 255]);
  assert.deepEqual(pixel(0, 1), [0, 0, 0, 255]);
  assert.deepEqual(pixel(127, 7), [207, 232, 225, 255]);
  assert.deepEqual(pixel(127, 63), [207, 232, 225, 255]);
  paintDeviceOLED(canvas, bytes.subarray(1)); paintDeviceOLED(null, bytes);
  assert.equal(frames.length, 1);
});
