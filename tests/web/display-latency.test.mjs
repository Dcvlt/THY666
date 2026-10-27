import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createCore } from '../../web/core.js';
import { InstrumentOLEDTransferModel } from '../../web/oled.js';
import { MENU_ROWS } from '../../web/studio-menu.js';

const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));
const profile = JSON.parse(await readFile(new URL('../../web/generated/oled-profile.json', import.meta.url)));
const okay = result => { assert.equal(result.ok, true, result.status || result.message); return result; };
const rowId = id => {
  const index = MENU_ROWS.findIndex(row => row.id === id);
  assert.notEqual(index, -1, `Missing shared row ${id}`);
  return index;
};
const noteBand = frame => frame.subarray(2 * 128, 5 * 128);
const wire = events => events.map(({ destination, status, data1, data2 }) => [destination, status, data1, data2]);
const isOn = event => (event.status & 0xf0) === 0x90 && event.data2 > 0;
const isOff = event => (event.status & 0xf0) === 0x80 || (event.status & 0xf0) === 0x90 && event.data2 === 0;

async function freshArp(notes = [60, 64, 67]) {
  const engine = await createCore({ bytes });
  const pads = Array.from({ length: 96 }, () => ({ notes: [...notes, ...Array(7 - notes.length).fill(0)], mask: (1 << notes.length) - 1, color: 1 }));
  okay(engine.firmware.layout(pads, { anchor: 60, degrees: [0, 2, 4, 5, 7, 9, 11], root: 0 }));
  okay(engine.firmware.route(0, 0, 1));
  const view = rowId('lights.view');
  const simple = MENU_ROWS[view].options.findIndex(option => option.value === 'simple');
  assert.notEqual(simple, -1);
  okay(engine.state.set(view, simple));
  okay(engine.rhythm.configure({ source: 'internal', bpm: 120 }));
  okay(engine.arp.configure(0, { enabled: true, type: 'notes', grouping: 'hybrid',
    sourceZones: 63, layer: false, start: 'immediate', restartOnChange: false,
    rateTicks: 240, gatePercent: 50, swingPercent: 0, stepMask: 255,
    pattern: 'up', channel: 2, mask: 1 }));
  okay(engine.advance(0));
  return engine;
}

for (const notes of [[60, 64, 67], [57, 60, 62, 64, 65]]) {
  test(`actual 1/16 Arp ${notes.length}-note glyphs reach the modeled panel within 45 ms, without changing MIDI`, async t => {
  const engine = await freshArp(notes);
  const control = await freshArp(notes);
  const observedBand = notes.length === 5 ? frame => frame.subarray(128, 6 * 128) : noteBand;
  // The additional 1.4 ms foreground gap approximates the previously measured
  // ~115 ms per frozen 64-chunk frame. This is a host service model, not a
  // measurement or promise of optical refresh on the physical OLED.
  const panel = new InstrumentOLEDTransferModel(profile, { scheduler: engine.display, foregroundGapMs: 1.4 });
  assert.equal(panel.stats().scheduler, 'shared-c');
  const gates = [], rests = [];
  const withDisplay = [], withoutDisplay = [];
  let gate = null, rest = null;
  // Release in the ninth rest, before advancing across a tenth onset.
  const pressAt = 512, releaseAt = pressAt + 125 * notes.length * 3 - 1;
  for (let now = 0; now <= releaseAt + 160; now++) {
    okay(engine.advance(now)); okay(control.advance(now));
    if (now === pressAt) {
      assert.equal(panel.stats().initialComplete, 1, 'Finish the initial panel upload before measuring note feedback.');
      assert.equal(panel.stats().dirtyBytes, 0, 'Start the musical observation with no old display backlog.');
      okay(engine.firmware.pressPad(0)); okay(control.firmware.pressPad(0));
    }
    if (now === releaseAt) { okay(engine.release(0)); okay(control.release(0)); }
    const events = engine.drain(), comparison = control.drain();
    assert.deepEqual(wire(events), wire(comparison), `The display must not alter MIDI at ${now} ms.`);
    assert.deepEqual(engine.voices(), control.voices(), `The display must not alter ownership at ${now} ms.`);
    withDisplay.push(...events.map(event => [now, ...wire([event])[0]]));
    withoutDisplay.push(...comparison.map(event => [now, ...wire([event])[0]]));
    for (const event of events) {
      if (isOff(event)) {
        assert.ok(gate && gate.note === event.data1, 'Every Arp release closes its sounding gate.');
        gate.off = now;
        assert.notEqual(gate.visible, null, `MIDI ${gate.note} at ${gate.on} ms never reached the panel during its gate.`);
        assert.ok(gate.visible < gate.off, `MIDI ${gate.note} only appeared after its release.`);
        rest = { off: now, visible: null, expected: engine.state.frame({ now, animations: false }) };
        rests.push(rest);
        gate = null;
      }
      if (isOn(event)) {
        assert.equal(gate, null, 'This single phrase owns one sounding note at a time.');
        if (rest) {
          assert.notEqual(rest.visible, null, 'The previous highlight must clear during its rest.');
          assert.ok(rest.visible < now, 'The previous highlight must clear before the next onset.');
          rest = null;
        }
        gate = { note: event.data1, on: now, off: null, visible: null,
          expected: engine.state.frame({ now, animations: false }) };
        gates.push(gate);
      }
    }
    if (now % 16 === 0) panel.setFrame(engine.state.frame({ now, animations: false }), now);
    else panel.advance(now);
    if (gate && gate.visible === null && Buffer.from(observedBand(panel.visible)).equals(Buffer.from(observedBand(gate.expected)))) {
      gate.visible = now;
    }
    if (rest && rest.visible === null && Buffer.from(observedBand(panel.visible)).equals(Buffer.from(observedBand(rest.expected)))) {
      rest.visible = now;
    }
  }
  assert.equal(gates.length, notes.length * 3, 'Exercise three complete chord cycles.');
  assert.deepEqual(gates.map(gate => gate.note), [...notes, ...notes, ...notes]);
  assert.ok(gates.every(gate => gate.off - gate.on === 63), '120 BPM, 1/16 and 50% gate provide 63 ms integer-time windows.');
  const latencies = gates.map(gate => gate.visible - gate.on);
  assert.ok(latencies.every(lag => lag <= 45), `Every full note band must match within 45 ms; observed ${latencies.join(', ')} ms.`);
  assert.equal(rests.length, notes.length * 3);
  assert.ok(rests.every(rest => rest.visible !== null && rest.visible - rest.off <= 45), 'Every released note returns to the dim input pool within 45 ms.');
  assert.deepEqual(withDisplay, withoutDisplay);
  assert.equal(engine.stats().activeVoices, 0);
  assert.equal(engine.arp.snapshot(0).inputs, 0);
  assert.equal(panel.stats().dirtyBytes, 0, 'Released input converges to the current panel target.');
  assert.deepEqual(panel.visible, engine.state.frame({ now: releaseAt + 160, animations: false }));
  t.diagnostic(`Modeled onset-to-confirmed-glyph latencies: ${latencies.join(', ')} ms (1.4 ms foreground gap; no hardware measurement).`);
});

}

test('C hierarchy slides keep their 180 ms deadline despite a 115 ms render-service gap', async () => {
  const engine = await createCore({ bytes });
  const before = engine.state.frame({ now: 0 });
  okay(engine.state.open());
  assert.deepEqual(engine.state.frame({ now: 1 }), before);
  assert.equal(engine.state.info().transitioning, 1);
  engine.state.frame({ now: 116 });
  assert.equal(engine.state.info().transitionElapsedMs, 115);
  assert.equal(engine.state.info().transitioning, 1);
  const completed = engine.state.frame({ now: 181 });
  assert.equal(engine.state.info().transitionElapsedMs, 180);
  assert.equal(engine.state.info().transitioning, 0);
  assert.deepEqual(completed, engine.state.frame({ now: 181, animations: false }));
  assert.equal(engine.stats().activeVoices, 0);
  assert.deepEqual(engine.drain(), []);
});

test('actual instrument uses stable C scene keys and final destination frames without navigation slides', async () => {
  const engine = await createCore({ bytes });
  okay(engine.state.navigationAnimations(false));
  const play = engine.state.sceneKey();
  engine.state.frame({ now: 0 });
  okay(engine.state.open());
  const groups = engine.state.sceneKey();
  assert.notEqual(groups, play);
  engine.state.frame({ now: 1 });
  assert.equal(engine.state.info().transitioning, 0);
  okay(engine.state.navigate('down'));
  assert.equal(engine.state.sceneKey(), groups, 'category selection stays on the same screen');
  okay(engine.state.navigate('right'));
  const performance = engine.state.sceneKey();
  assert.notEqual(performance, groups);
  engine.state.frame({ now: 2 });
  assert.equal(engine.state.info().transitioning, 0);
  okay(engine.state.navigate('down'));
  assert.equal(engine.state.sceneKey(), performance, 'row selection does not blank');
  const velocity = rowId('performance.velocity'); // Musical setup now lives in Chords.
  okay(engine.state.begin(velocity));
  assert.equal(engine.state.sceneKey(), performance, 'editing stays on its existing screen');
  okay(engine.state.adjust(1));
  okay(engine.state.commit());
  assert.equal(engine.state.sceneKey(), performance, 'a committed value does not blank');
  okay(engine.state.play());
  const beforeView = engine.state.sceneKey();
  const view = rowId('lights.view');
  const piano = MENU_ROWS[view].options.findIndex(option => option.value === 'piano');
  assert.notEqual(engine.state.get(view), piano);
  okay(engine.state.set(view, piano));
  assert.notEqual(engine.state.sceneKey(), beforeView, 'a changed Play view requests one coherent screen replacement');
  assert.equal(engine.stats().activeVoices, 0);
  assert.deepEqual(engine.drain(), []);
});
