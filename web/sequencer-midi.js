/* SPDX-License-Identifier: GPL-3.0-or-later
 * Standard MIDI File adapters. Scheduling, storage and ownership remain in C.
 */
const integer = (value, min, max) => Number.isInteger(value) && value >= min && value <= max;
const fail = message => { throw new RangeError(`MIDI file: ${message}`); };
const textBytes = text => [...text].map(char => char.charCodeAt(0));
const uint32 = value => [(value >>> 24) & 255, (value >>> 16) & 255, (value >>> 8) & 255, value & 255];
const uint16 = value => [(value >>> 8) & 255, value & 255];
const variable = value => {
  if (!integer(value, 0, 0x0fffffff)) fail('event delta exceeds the file format.');
  const bytes = [value & 127];
  while ((value = Math.floor(value / 128))) bytes.unshift((value & 127) | 128);
  return bytes;
};
const chunk = (type, bytes) => [...textBytes(type), ...uint32(bytes.length), ...bytes];

export function parseMidiFile(source, { ppqn = 960, maxTracks = 21, maxNotes = 4096 } = {}) {
  if (!(source instanceof Uint8Array) || !integer(ppqn, 1, 32767) || !integer(maxTracks, 1, 256) || !integer(maxNotes, 1, 1000000)) fail('invalid reader input.');
  const bytes = source, view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let at = 0, end = bytes.length;
  const need = size => { if (at + size > end) fail('truncated event or chunk.'); };
  const byte = () => { need(1); return bytes[at++]; };
  const u16 = () => { need(2); const value = view.getUint16(at); at += 2; return value; };
  const u32 = () => { need(4); const value = view.getUint32(at); at += 4; return value; };
  const type = () => String.fromCharCode(byte(), byte(), byte(), byte());
  const vlq = () => {
    let value = 0;
    for (let n = 0; n < 4; n++) { const next = byte(); value = value * 128 + (next & 127); if (!(next & 128)) return value; }
    fail('variable-length quantity exceeds four bytes.');
  };
  if (type() !== 'MThd') fail('missing MThd header.');
  const headerLength = u32();
  if (headerLength < 6) fail('short header.');
  need(headerLength);
  const headerEnd = at + headerLength, format = u16(), trackCount = u16(), division = u16();
  if (format > 1 || !trackCount || (format === 0 && trackCount !== 1)) fail('only synchronous formats 0 and 1 are supported.');
  if (!division || (division & 0x8000)) fail('SMPTE time division is not supported; use PPQN.');
  at = headerEnd;
  const tracks = [], warnings = [], tempo = [], meters = [];
  let noteCount = 0;
  const scaled = tick => Math.round(tick * ppqn / division);
  for (let index = 0; index < trackCount; index++) {
    end = bytes.length;
    if (type() !== 'MTrk') fail(`missing track ${index + 1}.`);
    const length = u32(); need(length); end = at + length;
    let tick = 0, running = 0, name = `Track ${index + 1}`, ended = false;
    const channels = new Map(), held = new Map();
    const channelTrack = channel => {
      if (!channels.has(channel)) channels.set(channel, { name, channel, notes: [], events: [], length: 0 });
      return channels.get(channel);
    };
    const close = (channel, note) => {
      const key = channel * 128 + note, queue = held.get(key);
      if (!queue?.length) { warnings.push(`Track ${index + 1}: unmatched note-off ${note}.`); return; }
      const onset = queue.shift();
      const start = scaled(onset.tick), duration = Math.max(1, scaled(tick) - start);
      channelTrack(channel).notes.push({ start, duration, note, velocity: onset.velocity, channel });
      if (++noteCount > maxNotes) fail(`more than ${maxNotes} notes; no data was imported.`);
    };
    while (at < end) {
      tick += vlq();
      if (!Number.isSafeInteger(tick) || scaled(tick) > 0xffffffff) fail('song length exceeds the device tick range.');
      let status = byte(), first;
      if (status < 0x80) { if (!running) fail('running status without a channel status.'); first = status; status = running; }
      if (status === 0xff) {
        running = 0;
        const meta = byte(), size = vlq(); need(size);
        if (meta === 0x2f) { if (size !== 0) fail('invalid end-of-track event.'); ended = true; at = end; break; }
        if (meta === 0x51 && size === 3) tempo.push({ tick: scaled(tick), microseconds: bytes[at] * 65536 + bytes[at + 1] * 256 + bytes[at + 2] });
        if (meta === 0x58 && size >= 2) meters.push({ tick: scaled(tick), numerator: bytes[at], denominator: 2 ** bytes[at + 1] });
        if (meta === 3) name = new TextDecoder().decode(bytes.subarray(at, at + size)).replace(/\0/g, '').slice(0, 80);
        at += size; continue;
      }
      if (status === 0xf0 || status === 0xf7) { running = 0; const size = vlq(); need(size); at += size; warnings.push(`Track ${index + 1}: SysEx omitted.`); continue; }
      if (status < 0x80 || status >= 0xf0) fail('unsupported system event.');
      running = status;
      const kind = status & 0xf0, channel = status & 15;
      const data1 = first ?? byte(), data2 = kind === 0xc0 || kind === 0xd0 ? 0 : byte();
      if (data1 >= 128 || data2 >= 128) fail('status byte inside channel data.');
      if (kind === 0x90 && data2) {
        const key = channel * 128 + data1;
        if (!held.has(key)) held.set(key, []);
        held.get(key).push({ tick, velocity: data2 });
      } else if (kind === 0x80 || kind === 0x90) close(channel, data1);
      else channelTrack(channel).events.push({ tick: scaled(tick), status, data1, data2 });
    }
    if (!ended) warnings.push(`Track ${index + 1}: missing end-of-track marker.`);
    for (const [key, queue] of held) while (queue.length) { close(Math.floor(key / 128), key % 128); warnings.push(`Track ${index + 1}: held note closed at track end.`); }
    for (const track of channels.values()) {
      track.name = name;
      track.length = Math.max(1, scaled(tick), ...track.notes.map(note => note.start + note.duration));
      track.notes.sort((a, b) => a.start - b.start || a.note - b.note);
      tracks.push(track);
      if (tracks.length > maxTracks) fail(`more than ${maxTracks} channel tracks; no data was imported.`);
    }
  }
  if (at !== bytes.length) warnings.push('Trailing file data ignored.');
  tempo.sort((a, b) => a.tick - b.tick); meters.sort((a, b) => a.tick - b.tick);
  const initialTempo = tempo.find(event => event.tick === 0)?.microseconds ?? 500000;
  if (!initialTempo) fail('invalid zero tempo.');
  if (tempo.some(event => event.tick !== 0)) warnings.push('Tempo changes require a tempo map; the device uses the initial tempo.');
  const meter = meters.find(event => event.tick === 0) ?? { numerator: 4, denominator: 4 };
  return { ppqn, bpm: 60000000 / initialTempo, meter: { numerator: meter.numerator, denominator: meter.denominator }, tracks, tempo, meters, warnings: [...new Set(warnings)] };
}

export function encodeMidiFile({ ppqn = 960, bpm = 120, meter = { numerator: 4, denominator: 4 }, tracks = [] } = {}) {
  if (!integer(ppqn, 1, 32767) || !Number.isFinite(bpm) || bpm <= 0 || !Array.isArray(tracks) || tracks.length > 255 ||
      !integer(meter.numerator, 1, 255) || ![1, 2, 4, 8, 16, 32, 64, 128].includes(meter.denominator)) fail('invalid song.');
  const microseconds = Math.round(60000000 / bpm);
  if (!integer(microseconds, 1, 0xffffff)) fail('tempo is outside the MIDI file range.');
  const conductor = [0, 0xff, 0x51, 3, (microseconds >>> 16) & 255, (microseconds >>> 8) & 255, microseconds & 255,
    0, 0xff, 0x58, 4, meter.numerator, Math.log2(meter.denominator), 24, 8, 0, 0xff, 0x2f, 0];
  const chunks = [chunk('MTrk', conductor)];
  for (let index = 0; index < tracks.length; index++) {
    const track = tracks[index], events = [];
    const name = new TextEncoder().encode(track.name || `Track ${index + 1}`);
    events.push({ tick: 0, order: -2, data: [0xff, 3, ...variable(name.length), ...name] });
    for (const note of track.notes || []) {
      const channel = note.channel ?? track.channel ?? 0;
      if (!integer(note.start, 0, 0xffffffff) || !integer(note.duration, 1, 0xffffffff - note.start) || !integer(note.note, 0, 127) ||
          !integer(note.velocity, 1, 127) || !integer(channel, 0, 15)) fail('invalid note.');
      events.push({ tick: note.start, order: 1, data: [0x90 | channel, note.note, note.velocity] },
        { tick: note.start + note.duration, order: 0, data: [0x80 | channel, note.note, 0] });
    }
    for (const event of track.events || []) {
      if (!integer(event.tick, 0, 0xffffffff) || !integer(event.status, 0xa0, 0xef) || !integer(event.data1, 0, 127) || !integer(event.data2, 0, 127)) fail('invalid controller event.');
      const kind = event.status & 0xf0;
      events.push({ tick: event.tick, order: -1, data: kind === 0xc0 || kind === 0xd0 ? [event.status, event.data1] : [event.status, event.data1, event.data2] });
    }
    events.sort((a, b) => a.tick - b.tick || a.order - b.order);
    let lastTick = 0; const output = [];
    for (const event of events) { output.push(...variable(event.tick - lastTick), ...event.data); lastTick = event.tick; }
    const endTick = Math.max(lastTick, track.length || 0);
    output.push(...variable(endTick - lastTick), 0xff, 0x2f, 0);
    chunks.push(chunk('MTrk', output));
  }
  return Uint8Array.from([...chunk('MThd', [0, 1, ...uint16(chunks.length), ...uint16(ppqn)]), ...chunks.flat()]);
}

/* THYS v2 keeps track existence and the complete 48-step editor extent. C validates this complete
 * candidate, including CRC, identities, routes and bounds, before publication.
 * There is no temporary mutation of the playing song during MIDI conversion. */
export function midiToSongBytes(midi, { mask = 5, eventCapacity = 512 } = {}) {
  if (!midi || midi.ppqn !== 960 || !Array.isArray(midi.tracks) || midi.tracks.length > 21 || !integer(mask,1,7)) fail('song is not compatible with the device.');
  const numerator = integer(midi.meter?.numerator,1,16) ? midi.meter.numerator : 4;
  const denominator = [1,2,4,8,16,32].includes(midi.meter?.denominator) ? midi.meter.denominator : 4;
  const bar = numerator * 960 * 4 / denominator, entries = [], lengths = [], kinds = { 0xa0:2,0xb0:1,0xc0:3,0xd0:4,0xe0:5 };
  let identity = 1, group = 1;
  for (let track = 0; track < midi.tracks.length; track++) {
    const source = midi.tracks[track], onsets = new Map();
    for (const note of source.notes) {
      if (!onsets.has(note.start)) onsets.set(note.start,[]);
      onsets.get(note.start).push(note);
    }
    for (const [tick, notes] of onsets) {
      const chord = notes.length > 1;
      for (const note of notes) entries.push({tick,duration:note.duration,id:identity++,group,track,kind:chord?128:0,pitch:note.note,value:note.velocity});
      group++;
    }
    for (const event of source.events) {
      const kind = kinds[event.status & 0xf0];
      if (kind === undefined) fail('unsupported channel event.');
      entries.push({tick:event.tick,duration:1,id:identity++,group:group++,track,kind,pitch:event.data1,value:event.data2});
    }
    const content = Math.max(1,source.length,...entries.filter(event=>event.track===track).map(event=>event.tick+event.duration));
    lengths.push(Math.max(bar,Math.ceil(content/bar)*bar));
  }
  if (entries.length > eventCapacity) fail(`${entries.length} events exceed the ${eventCapacity}-event device capacity; no data was imported.`);
  if (lengths.some(length=>length>0x3fffffff) || entries.some(event=>event.tick<0 || event.duration<1 || event.tick+event.duration>0x3fffffff)) fail('song exceeds the device tick range.');
  entries.sort((a,b)=>a.tick-b.tick || a.track-b.track || a.kind-b.kind || a.pitch-b.pitch);
  const bytes = new Uint8Array(32+21*28+entries.length*16), data = new DataView(bytes.buffer);
  bytes.set(textBytes('THYS')); data.setUint16(4,2,true);data.setUint16(6,32,true);data.setUint32(8,bytes.length,true);
  data.setUint16(16,entries.length,true);data.setUint16(18,21,true);data.setUint16(20,identity,true);data.setUint16(22,group,true);data.setUint32(24,1,true);
  for(let track=0;track<21;track++) {
    const offset=32+track*28,length=lengths[track]??bar;
    data.setUint32(offset,length,true);data.setUint32(offset+8,length,true);data.setUint32(offset+12,1,true);
    bytes[offset+16]=midi.tracks[track]?.channel??track%16;bytes[offset+17]=mask;
    bytes[offset+23]=track<midi.tracks.length?128:0;
    data.setUint32(offset+24,Math.max(240,Math.ceil(length/48)),true);
  }
  entries.forEach((event,index)=>{
    const offset=32+21*28+index*16;data.setUint32(offset,event.tick,true);data.setUint32(offset+4,event.duration,true);
    data.setUint16(offset+8,event.id,true);data.setUint16(offset+10,event.group,true);bytes[offset+12]=event.track;bytes[offset+13]=event.kind;bytes[offset+14]=event.pitch;bytes[offset+15]=event.value;
  });
  let crc=0xffffffff;
  for(let index=16;index<bytes.length;index++){crc^=bytes[index];for(let bit=0;bit<8;bit++)crc=(crc>>>1)^((-(crc&1))&0xedb88320);}
  data.setUint32(12,(~crc)>>>0,true);
  return bytes;
}

export function sequencerToMidi(sequencer, clock) {
  const kinds = {cc:0xb0,pressure:0xa0,program:0xc0,'channel-pressure':0xd0,bend:0xe0};
  const tracks = Array.from({length:21},(_,index)=>{
    const config=sequencer.track(index), events=sequencer.events(index);
    return {name:`Track ${index+1}`,channel:config.channel,length:config.contentEnd,
      notes:events.filter(event=>event.type==='note').map(event=>({start:event.tick,duration:event.duration,note:event.pitch,velocity:event.value})),
      events:events.filter(event=>event.type!=='note').map(event=>({tick:event.tick,status:kinds[event.type]|config.channel,data1:event.pitch,data2:event.value}))};
  }).filter(track=>track.notes.length||track.events.length);
  return encodeMidiFile({ppqn:clock.ppqn,bpm:clock.bpm,meter:{numerator:clock.meterNumerator,denominator:clock.meterDenominator},tracks});
}
