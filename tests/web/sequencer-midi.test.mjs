import test from 'node:test';
import assert from 'node:assert/strict';
import { parseMidiFile, encodeMidiFile, midiToSongBytes } from '../../web/sequencer-midi.js';
import { crc32 } from 'node:zlib';

const song = () => ({ ppqn: 960, bpm: 123.5, meter: { numerator: 7, denominator: 8 }, tracks: [
  { name: 'Chords', channel: 2, length: 3840, notes: [60, 64, 67].map(note => ({ start: 0, duration: 960, note, velocity: 94 })),
    events: [{ tick: 0, status: 0xb2, data1: 64, data2: 127 }, { tick: 1200, status: 0xc2, data1: 11, data2: 0 },
      { tick: 1210, status: 0xe2, data1: 127, data2: 127 }, { tick: 1220, status: 0xd2, data1: 42, data2: 0 }] },
  { name: 'Melody', channel: 3, length: 1920, notes: [{ start: 240, duration: 480, note: 74, velocity: 98 }] },
] });
test('MIDI roundtrip preserves chord durations, routes, controller messages and musical meter', () => {
  const original = song(), decoded = parseMidiFile(encodeMidiFile(original));
  assert.equal(decoded.tracks.length, 2);
  assert.ok(Math.abs(decoded.bpm - original.bpm) < .001);
  assert.deepEqual(decoded.meter, original.meter);
  assert.deepEqual(decoded.tracks[0].notes, original.tracks[0].notes.map(note => ({ ...note, channel: 2 })));
  assert.deepEqual(decoded.tracks[0].events, original.tracks[0].events);
  assert.equal(decoded.tracks[0].length, 3840);
  assert.deepEqual(decoded.warnings, []);
});
test('MIDI resolution scaling preserves musical time rather than raw source ticks', () => {
  const source = song(); source.ppqn = 480;
  const decoded = parseMidiFile(encodeMidiFile(source), { ppqn: 960 });
  assert.equal(decoded.tracks[1].notes[0].start, 480);
  assert.equal(decoded.tracks[1].notes[0].duration, 960);
  assert.equal(decoded.tracks[0].length, 7680);
});
test('same-tick note off precedes retrigger and overlapping identical note lifetimes pair FIFO', () => {
  const source = { tracks: [{ notes: [{ start: 0, duration: 960, note: 60, velocity: 20 },
    { start: 480, duration: 960, note: 60, velocity: 40 }, { start: 1440, duration: 480, note: 60, velocity: 60 }] }] };
  const decoded = parseMidiFile(encodeMidiFile(source));
  assert.deepEqual(decoded.tracks[0].notes.map(note => [note.start, note.duration, note.velocity]), [[0, 960, 20], [480, 960, 40], [1440, 480, 60]]);
});
test('format zero running status and velocity-zero note off decode correctly', () => {
  const bytes = Uint8Array.from([77,84,104,100,0,0,0,6,0,0,0,1,1,224,
    77,84,114,107,0,0,0,15,0,0x90,60,99,0,64,80,120,60,0,0,64,0,0,255,47,0]);
  bytes[21] = 17;
  const decoded = parseMidiFile(bytes);
  assert.deepEqual(decoded.tracks[0].notes.map(note => [note.note, note.duration]), [[60,240], [64,240]]);
});
test('truncation, invalid quantities, SMPTE and capacity are rejected before publication', () => {
  const valid = encodeMidiFile(song());
  for (const length of [0, 1, 13, valid.length - 1]) assert.throws(() => parseMidiFile(valid.slice(0, length)), RangeError);
  const smpte = valid.slice(); smpte[12] = 0x80;
  assert.throws(() => parseMidiFile(smpte), /SMPTE/);
  assert.throws(() => parseMidiFile(valid, { maxNotes: 2 }), /no data was imported/);
  assert.throws(() => parseMidiFile(valid, { maxTracks: 1 }), /no data was imported/);
});
test('writer refuses invalid notes, nonfinite tempo and impossible meter', () => {
  assert.throws(() => encodeMidiFile({ bpm: NaN }), RangeError);
  assert.throws(() => encodeMidiFile({ meter: { numerator: 0, denominator: 4 } }), RangeError);
  assert.throws(() => encodeMidiFile({ tracks: [{ notes: [{ start: 0, duration: 0, note: 60, velocity: 96 }] }] }), RangeError);
});

test('native conversion uses v2 created tracks and fits long loops into 48 steps without quantizing notes',()=>{
  const midi=parseMidiFile(encodeMidiFile({tracks:[{channel:5,length:96000,notes:[{start:95003,duration:997,note:74,velocity:88}]}]}));
  const bytes=midiToSongBytes(midi),data=new DataView(bytes.buffer);
  assert.equal(data.getUint16(4,true),2);assert.equal(bytes.length,636);
  assert.equal(bytes[55],128);assert.equal(bytes[83],0);assert.equal(data.getUint32(56,true),2000);
  assert.equal(data.getUint32(620,true),95003);assert.equal(data.getUint32(624,true),997);
  assert.equal(data.getUint32(12,true),crc32(bytes.subarray(16))>>>0);
});
