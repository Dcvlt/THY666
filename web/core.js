/* WebAssembly ABI only: ownership, sustain, queues and MIDI live in core/*.c. */
export async function createCore({ bytes, url = new URL('./generated/core.wasm', import.meta.url) } = {}) {
  if (!bytes && typeof document !== 'undefined') {
    const embedded = document.getElementById('thy-core-bytes');
    if (embedded) bytes = Uint8Array.from(atob(embedded.textContent.trim()), char => char.charCodeAt(0));
  }
  if (!bytes) {
    const response = await fetch(url);
    if (!response.ok) throw new Error(`Core build unavailable (${response.status}). Run python tools/build_core.py.`);
    bytes = await response.arrayBuffer();
  }
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const c = instance.exports;
  c.bridge_init();
  const integer = (value, min, max) => Number.isInteger(value) && value >= min && value <= max;
  const sequence = value => Array.isArray(value) || (ArrayBuffer.isView(value) && Number.isInteger(value.length));
  const result = (code) => ({ ok: code === 0, code, status: ({9:'in-use',10:'busy',20:'song-full',21:'capture-busy',22:'event-not-found',23:'invalid-song',24:'no-history',25:'take-failed'})[code] ?? ['ok', 'invalid', 'queue-full', 'owner-limit', 'handle-exhausted', 'stale-run', 'clock-not-ready', 'time-backward', 'time-overflow'][code] ?? 'invalid' });
  const invalid = () => result(1);
  const displayKeys = ['frames', 'chunks', 'commandBytes', 'dataBytes', 'submitted', 'coalesced', 'unchanged', 'generation', 'dirtyBytes', 'validBytes', 'initialComplete', 'active', 'lastGenerationAgeMs', 'maxGenerationAgeMs', 'failedChunks', 'sceneRequests', 'sceneCoalesced', 'sceneDeferred', 'sceneCompleted', 'panelVisible', 'sceneActive'];
  const display = {
    reset() { c.bridge_display_init(); },
    enableSceneControl(enabled) {
      if (typeof enabled !== 'boolean') throw new TypeError('Expected scene-control boolean');
      c.bridge_display_enable_scene_control(Number(enabled));
    },
    submit(frame, now) {
      if (!(frame instanceof Uint8Array) || frame.length !== 1024 || !integer(now, 0, 4294967295)) throw new TypeError('Expected native OLED frame and uint32 timestamp');
      new Uint8Array(c.memory.buffer, c.bridge_display_input(), 1024).set(frame);
      return Boolean(c.bridge_display_submit(now));
    },
    submitScene(frame, now) {
      if (!(frame instanceof Uint8Array) || frame.length !== 1024 || !integer(now, 0, 4294967295)) throw new TypeError('Expected native OLED frame and uint32 timestamp');
      new Uint8Array(c.memory.buffer, c.bridge_display_input(), 1024).set(frame);
      return Boolean(c.bridge_display_submit_scene(now));
    },
    prepare(now) {
      if (!integer(now, 0, 4294967295)) throw new TypeError('Expected uint32 timestamp');
      if (!c.bridge_display_prepare(now)) return null;
      const value = c.bridge_display_transfer_value;
      const count = value(1);
      const commandCount = value(24);
      return { offset: value(0), count, generation: value(2), submittedMs: value(3), kind: value(23), commandCount,
        command: Uint8Array.from({ length: commandCount }, (_, i) => value(4 + i)),
        data: Uint8Array.from({ length: count }, (_, i) => value(7 + i)) };
    },
    commit(success, now) {
      if (typeof success !== 'boolean' || !integer(now, 0, 4294967295)) throw new TypeError('Expected transfer result and uint32 timestamp');
      c.bridge_display_commit(Number(success), now);
    },
    stats() { return Object.fromEntries(displayKeys.map((key, i) => [key, c.bridge_display_report(i)])); },
  };
  display.reset();
  const route = (mask, channel) => integer(mask, 1, 7) && integer(channel, 0, 15);
  const notesValid = notes => sequence(notes) && notes.length <= 64 && Array.from(notes).every(note => integer(note, -32768, 32767));
  const velocityValid = value => value && typeof value === 'object' && !Array.isArray(value) &&
    integer(value.mode,0,1) && integer(value.curve,0,2) && integer(value.softRate,0,4094) &&
    integer(value.hardRate,1,4095) && value.softRate < value.hardRate &&
    integer(value.minimum,1,127) && integer(value.maximum,value.minimum,127);
  const shapeArgs = (shape = {}) => {
    if (!shape || typeof shape !== 'object' || Array.isArray(shape) || ArrayBuffer.isView(shape)) return null;
    const { spread = 0, inversion = 0, octave = 0, upper = 0, order = 0, delay = 0, excluded = 0 } = shape;
    if (!integer(spread, 0, 3) || !integer(inversion, 0, 64) || !integer(octave, -10, 10) ||
        !integer(upper, 0, 3) || !integer(order, 0, 3) || !integer(delay, 0, 1000) || !integer(excluded, 0, 127)) return null;
    return [spread, inversion, octave, upper, order, delay, excluded];
  };
  const keyInfo = (key) => ({ destination: Math.floor(key / 2048), channel: Math.floor(key / 128) % 16, note: key % 128 });
  const event = () => {
    const [destination, status, data1, data2] = new Uint8Array(c.memory.buffer, c.bridge_event(), 4);
    const usbSize = destination < 2 ? c.bridge_usb_size() : 0;
    const dinSize = destination === 2 ? c.bridge_din_size() : 0;
    return { destination, channel: status < 0xf0 ? status & 15 : null, status, data1, data2,
      usb: Array.from(new Uint8Array(c.memory.buffer, c.bridge_usb_bytes(), usbSize)),
      din: Array.from(new Uint8Array(c.memory.buffer, c.bridge_din_bytes(), dinSize)) };
  };
  const libraryCapacities = [8,24,3], librarySizes = [16,23,20];
  const librarySlot = (kind,slot) => integer(kind,0,2) && integer(slot,0,libraryCapacities[kind]-1);
  const libraryRecord = (kind,bytes) => {
    const end=bytes.subarray(0,13).indexOf(0);
    const name=String.fromCharCode(...bytes.subarray(0,end<0?13:end));
    if(kind===0)return {used:Boolean(bytes[15]),name,mask:bytes[13]|bytes[14]<<8};
    if(kind===1)return {used:Boolean(bytes[22]),name,root:bytes[13],notes:Array.from(bytes.subarray(14,21)),mask:bytes[21]};
    return {used:Boolean(bytes[19]),name,chords:Array.from(bytes.subarray(13,19))};
  };
  const libraryBytes = (kind,record) => {
    if(!integer(kind,0,2) || !record || typeof record!=='object' || Array.isArray(record) ||
      typeof record.used!=='boolean' || typeof record.name!=='string' || record.name.length>12 ||
      Array.from(record.name).some(char=>char.charCodeAt(0)>127))return null;
    const bytes=new Uint8Array(librarySizes[kind]);
    for(let n=0;n<record.name.length;n++)bytes[n]=record.name.charCodeAt(n);
    if(kind===0) {
      if(!integer(record.mask,0,65535))return null;
      bytes[13]=record.mask&255;bytes[14]=record.mask>>8;bytes[15]=Number(record.used);
    } else if(kind===1) {
      if(!integer(record.root,0,255) || !integer(record.mask,0,255) || !sequence(record.notes) ||
        record.notes.length!==7 || !Array.from(record.notes).every(v=>integer(v,0,255)))return null;
      bytes[13]=record.root;bytes.set(record.notes,14);bytes[21]=record.mask;bytes[22]=Number(record.used);
    } else {
      if(!sequence(record.chords) || record.chords.length!==6 ||
        !Array.from(record.chords).every(v=>integer(v,0,255)))return null;
      bytes.set(record.chords,13);bytes[19]=Number(record.used);
    }
    return bytes;
  };
  const library = {
    capacities: Object.freeze({scales:8,chords:24,sets:3}),
    record(kind,slot) {
      if(!librarySlot(kind,slot) || !c.bridge_library_record)return null;
      const values=Array.from({length:librarySizes[kind]},(_,byte)=>c.bridge_library_record(kind,slot,byte));
      return values.some(value=>!integer(value,0,255))?null:libraryRecord(kind,Uint8Array.from(values));
    },
    records(kind) {return integer(kind,0,2)?Array.from({length:libraryCapacities[kind]},(_,slot)=>this.record(kind,slot)):[];},
    set(kind,slot,record) {
      const bytes=librarySlot(kind,slot)?libraryBytes(kind,record):null;
      if(!bytes || !c.bridge_library_set)return invalid();
      new Uint8Array(c.memory.buffer,c.bridge_input(),bytes.length).set(bytes);
      return result(c.bridge_library_set(kind,slot,bytes.length));
    },
    delete(kind,slot) {return librarySlot(kind,slot)&&c.bridge_library_delete?result(c.bridge_library_delete(kind,slot)):invalid();},
    capture(kind,slot) {
      if(!librarySlot(kind,slot) || !c.bridge_library_capture)return {...invalid(),record:null};
      const length=c.bridge_library_capture(kind,slot);
      if(length!==librarySizes[kind])return {...result(length<0?-length:1),record:null};
      return {...result(0),record:libraryRecord(kind,new Uint8Array(c.memory.buffer,c.bridge_input(),length).slice())};
    },
    exportFile() {
      if(!c.bridge_library_export || !c.bridge_library_buffer)return null;
      const length=c.bridge_library_export();
      return length===756?new Uint8Array(c.memory.buffer,c.bridge_library_buffer(),length).slice():null;
    },
    importFile(bytes) {
      if(!(bytes instanceof Uint8Array) || bytes.length!==756 || !c.bridge_library_import || !c.bridge_library_buffer)return invalid();
      new Uint8Array(c.memory.buffer,c.bridge_library_buffer(),bytes.length).set(bytes);
      return result(c.bridge_library_import(bytes.length));
    },
    openEditor(kind,slot) {return librarySlot(kind,slot)&&c.bridge_library_open?result(c.bridge_library_open(kind,slot)):invalid();},
  };
  const firmware = {
    library,
    musicalName() { return textAt(c.bridge_state_musical_name()); },
    expressionProfile(scope=-1) {
      if(!integer(scope,-2,95))return null;
      const keys=['enabled','split','pressureCurve','aftertouchCurve','overridden'];
      const values=keys.map((_,field)=>c.bridge_expression_profile(scope,field));
      return values[0]<0?null:Object.fromEntries(keys.map((key,index)=>[key,index===0?Boolean(values[index]):values[index]]));
    },
    setExpressionProfile(scope,value) {
      return integer(scope,-2,95)&&value&&typeof value.enabled==='boolean'&&integer(value.split,10,90)&&
        integer(value.pressureCurve,0,2)&&integer(value.aftertouchCurve,0,2)?
        result(c.bridge_set_expression_profile(scope,Number(value.enabled),value.split,value.pressureCurve,value.aftertouchCurve)):invalid();
    },
    clearExpressionProfile(pad) {return integer(pad,0,95)?result(c.bridge_clear_expression_profile(pad)):invalid();},
    expressionAxes(scope,raw) {
      if(!integer(scope,-2,95)||!integer(raw,0,4095))return null;
      const packed=c.bridge_expression_profile_axes(scope,raw);
      return packed<0?null:{pressure:(packed>>8)&127,aftertouch:packed&127};
    },
    layout(pads, { anchor = 60, degrees = [0, 2, 4, 5, 7, 9, 11], root = 0 } = {}) {
      if (!Array.isArray(pads) || pads.length !== 96 || !integer(anchor, 0, 127) || !integer(root, 0, 11) ||
          !notesValid(degrees) || degrees.length < 1 || degrees.length > 12 || degrees[0] !== 0 ||
          degrees.some((v, i) => v < 0 || v > 11 || (i && v <= degrees[i - 1])) ||
          pads.some(p => !p || !sequence(p.notes) || p.notes.length !== 7 || !integer(p.mask, 0, 127) ||
            !integer(p.color, 0, 255) || p.notes.some((v, i) => !integer(v, 0, 127) && (p.mask & (1 << i))))) return invalid();
      for (let id = 0; id < 96; id++) {
        new Int16Array(c.memory.buffer, c.bridge_notes(), 7).set(pads[id].notes);
        const code = c.bridge_firmware_layout_pad(id, pads[id].mask, pads[id].color);
        if (code) return result(code);
      }
      new Int16Array(c.memory.buffer, c.bridge_notes(), degrees.length).set(degrees);
      return result(c.bridge_firmware_commit_layout(anchor, degrees.length, root));
    },
    performance({ inversion = 0, spread = 0, strum = false, delay = 24, order = 0, chordOctave = 0, melodyOctave = 0, upper = 0, melodyLayout = 0, selectedPad = 0 } = {}) {
      if (!integer(inversion, 0, 3) || !integer(spread, 0, 3) || typeof strum !== 'boolean' || !integer(delay, 1, 1000) ||
          !integer(order, 0, 3) || !integer(chordOctave, -3, 3) || !integer(melodyOctave, -3, 3) || !integer(upper, 0, 3) ||
          !integer(melodyLayout, 0, 8) || !integer(selectedPad, 0, 95)) return invalid();
      return result(c.bridge_firmware_performance(inversion, spread, Number(strum), delay, order, chordOctave, melodyOctave, upper, melodyLayout, selectedPad));
    },
    route(side, channel, mask, transpose = 0) { return integer(side, 0, 1) && route(mask, channel) && integer(transpose, -127, 127) ? result(c.bridge_firmware_route(side, channel, mask, transpose)) : invalid(); },
    padRoute(target) {
      return integer(target, 0, 8) ? Object.fromEntries(['channel', 'destinations', 'velocity', 'pressureEnabled'].map((key, field) => [key, c.bridge_firmware_route_value(target, field)])) : null;
    },
    routeForPad(pad) { return integer(pad, 0, 95) ? this.padRoute(c.bridge_firmware_route_target(pad)) : null; },
    activeRoutes() {
      const profile = c.bridge_firmware_state(32);
      return (profile === 0 ? [0] : profile === 1 ? [1, 2] : [3, 4, 5, 6, 7, 8]).map(target => this.padRoute(target));
    },
    editSideRoute(side, channel, mask) { return integer(side, 0, 1) && route(mask, channel) ? result(c.bridge_firmware_edit_side_route(side, channel, mask)) : invalid(); },
    transpose(value) { return integer(value, -127, 127) ? result(c.bridge_firmware_transpose(value)) : invalid(); },
    pressurePad(pad, pressure) { return integer(pad, 0, 95) && integer(pressure, 0, 127) ? result(c.bridge_firmware_pressure_pad(pad, pressure)) : invalid(); },
    slot(pad, mask) { return integer(pad, 0, 47) && integer(mask, 0, 127) ? result(c.bridge_firmware_slot(pad, mask)) : invalid(); },
    slotMask(pad) { return integer(pad, 0, 47) ? c.bridge_firmware_slot_value(pad) : -1; },
    chordCell(pad) {
      if(!integer(pad,0,47))return null;
      const mask=c.bridge_firmware_chord_cell(pad,0);
      return mask<0?null:{mask,root:c.bridge_firmware_chord_cell(pad,1),degree:c.bridge_firmware_chord_cell(pad,2),
        notes:Array.from({length:7},(_,n)=>c.bridge_firmware_chord_cell(pad,n+3))};
    },
    control(bank, index, pressed) { return integer(bank, 0, 3) && integer(index, 0, 31) && typeof pressed === 'boolean' ? result(c.bridge_firmware_control(bank, index, Number(pressed))) : invalid(); },
    scaleShortcut(key, assignment) {
      if (!integer(key, 0, 11)) return assignment === undefined ? -1 : invalid();
      return assignment === undefined ? c.bridge_firmware_scale_shortcut_value(key) :
        integer(assignment, 0, 78) ? result(c.bridge_firmware_scale_shortcut(key, assignment)) : invalid();
    },
    scaleShortcuts() { return Array.from({ length: 12 }, (_, key) => c.bridge_firmware_scale_shortcut_value(key)); },
    pressPad(pad) { return integer(pad, 0, 95) ? result(c.bridge_firmware_press_pad(pad)) : invalid(); },
    previewPad(pad) {
      if (!integer(pad, 0, 95)) return { ...invalid(), notes: [] };
      const response = result(c.bridge_firmware_preview_pad(pad));
      return { ...response, notes: response.ok ? Array.from(new Int16Array(c.memory.buffer, c.bridge_preview_notes(), c.bridge_preview_count())) : [] };
    },
    contextPad(pad) { return integer(pad, 0, 95) ? c.bridge_firmware_context(pad) : 0; },
    contextHeld(pad) { return integer(pad, 0, 95) && Boolean(c.bridge_firmware_context_held(pad)); },
    sample(pad, raw, now) { return integer(pad, 0, 95) && integer(raw, 0, 4095) && integer(now, 0, 4294967295) ? result(c.bridge_firmware_sample(pad, raw, now)) : invalid(); },
    sensor(pad) {
      if (!integer(pad, 0, 95)) return null;
      return Object.fromEntries(['raw', 'baseline', 'pressure', 'held', 'blocked', 'warmup', 'attack', 'release', 'profile', 'lastStrike', 'strikeRate', 'strikeReady','guard'].map((key, i) => [key, c.bridge_firmware_sensor(pad, i)]));
    },
    response(pad = -1) {
      if (!integer(pad, -2, 95)) return null;
      return Object.fromEntries(['attack', 'release', 'floor', 'ceiling', 'curve', 'overridden'].map((key, i) => [key, c.bridge_firmware_response(pad, i)]));
    },
    setResponse(pad, response) {
      if (!integer(pad, -2, 95) || !response || !['attack', 'release', 'floor', 'ceiling'].every(k => integer(response[k], 0, 4095)) || !integer(response.curve, 0, 2)) return invalid();
      return result(c.bridge_firmware_set_response(pad, response.attack, response.release, response.floor, response.ceiling, response.curve));
    },
    clearResponse(pad) { return integer(pad, 0, 95) ? result(c.bridge_firmware_clear_response(pad)) : invalid(); },
    releasePolicy(pad = -1) {
      return integer(pad,-2,95)?Object.fromEntries(['linked','gap','overridden'].map((key,index)=>[key,c.bridge_firmware_release_policy(pad,index)])):null;
    },
    setReleasePolicy(pad,policy) {
      return integer(pad,-2,95)&&policy&&integer(policy.linked,0,1)&&integer(policy.gap,1,4095)?
        result(c.bridge_firmware_set_release_policy(pad,policy.linked,policy.gap)):invalid();
    },
    setTrigger(pad,attack) {return integer(pad,-2,95)&&integer(attack,1,4095)?result(c.bridge_firmware_set_trigger(pad,attack)):invalid();},
    sensitivity(pad = -1) {return integer(pad,-2,95)?c.bridge_firmware_sensitivity_amount(pad):null;},
    sensitivityBounds(pad = -1) {
      if(!integer(pad,-2,95))return null;
      const min=c.bridge_firmware_sensitivity_bounds(pad,0),max=c.bridge_firmware_sensitivity_bounds(pad,1);
      return min<0||max<min?null:{min,max};
    },
    setSensitivityAmount(pad,amount) {
      return integer(pad,-2,95)&&integer(amount,0,100)?stateResult(c.bridge_firmware_set_sensitivity_amount(pad,amount)):invalid();
    },
    velocity(pad = -1) {
      if (!integer(pad,-2,95)) return null;
      return Object.fromEntries(['mode','curve','softRate','hardRate','minimum','maximum','overridden'].map((key,index)=>[key,c.bridge_firmware_velocity(pad,index)]));
    },
    velocitySummary() {
      const keys=['mode','curve','softRate','hardRate','minimum','maximum'];
      const reference=this.velocity(0);
      for(let pad=1;pad<96;pad++){
        const value=this.velocity(pad);
        if(keys.some(key=>value[key]!==reference[key]))return 'mixed';
      }
      return reference.mode===1?'strike':`fixed${this.state().velocity}`;
    },
    setVelocity(pad, config) {
      if (!integer(pad,-2,95) || !velocityValid(config)) return invalid();
      return result(c.bridge_firmware_set_velocity(pad,config.mode,config.curve,config.softRate,config.hardRate,config.minimum,config.maximum));
    },
    setSensitivity(pad, response, velocity, policy) {
      if (!integer(pad,-2,95) || !response || !['attack','release','floor','ceiling'].every(key=>integer(response[key],0,4095)) ||
          !integer(response.curve,0,2) || !velocityValid(velocity)) return invalid();
      const args=[pad,response.attack,response.release,response.floor,response.ceiling,response.curve,
        velocity.mode,velocity.curve,velocity.softRate,velocity.hardRate,velocity.minimum,velocity.maximum];
      if(policy!==undefined) {
        if(!policy||!integer(policy.linked,0,1)||!integer(policy.gap,1,4095))return invalid();
        return result(c.bridge_firmware_set_sensitivity_policy(...args,policy.linked,policy.gap));
      }
      return result(c.bridge_firmware_set_sensitivity(...args));
    },
    clearVelocity(pad) { return integer(pad,0,95) ? result(c.bridge_firmware_clear_velocity(pad)) : invalid(); },
    resetVelocity(pad = -1) { return integer(pad,-2,95) ? result(c.bridge_firmware_reset_velocity(pad)) : invalid(); },
    velocityValue(config, rate) {
      return velocityValid(config) && integer(rate,0,4095) ?
        c.bridge_firmware_velocity_value(config.mode,config.curve,config.softRate,config.hardRate,config.minimum,config.maximum,rate) : null;
    },
    expression(velocity, enabled) { return integer(velocity, 1, 127) && typeof enabled === 'boolean' ? result(c.bridge_firmware_expression(velocity, Number(enabled))) : invalid(); },
    lighting({ enabled, idle, held, fade }) { return typeof enabled === 'boolean' && integer(idle, 0, 8) && integer(held, 0, 8) && integer(fade, 0, 500) ? result(c.bridge_firmware_lighting(Number(enabled), idle, held, fade)) : invalid(); },
    factory() { return result(c.bridge_firmware_factory()); },
    navigate(action) {
      const index = ['left', 'up', 'select', 'down', 'right'].indexOf(action);
      return index < 0 ? invalid() : result(c.bridge_firmware_nav(index));
    },
    focus(row) { return integer(row, 0, 22) ? result(c.bridge_firmware_focus(row)) : invalid(); },
    state() { return Object.fromEntries(['menuOpen', 'menuEditing', 'menuRow', 'notice', 'inversion', 'spread', 'strum', 'delay', 'order', 'melodyLayout', 'chordOctave', 'melodyOctave', 'upper', 'root', 'shortcut', 'velocity', 'pressureEnabled', 'lightsEnabled', 'lightIdle', 'lightHeld', 'lightFade', 'selectedPad', 'guitarWindow', 'contextControls', 'centerPoll', 'displayView', 'chordColorMode', 'palette', 'keyContrast', 'heldStyle', 'heldFeedback', 'animationsEnabled', 'routingProfile', 'slotScope', 'globalExcludedSlots', 'selectedChord', 'keyDimPercent', 'centerBrightness', 'contextLocation', 'contextMask', 'bassOctave', 'lowerMode', 'lowerMask', 'lowerAvailable','voicingMode','dropMask','dropAvailable','chordLayout','liveUpdate','userScale'].map((key, i) => [key, c.bridge_firmware_state(i)])); },
    colors(now) {
      if (!integer(now, 0, 4294967295)) return new Uint32Array(96);
      const pointer = c.bridge_firmware_colors(now);
      return pointer ? new Uint32Array(c.memory.buffer, pointer, 96).slice() : new Uint32Array(96);
    },
    frame({ scaleName = 'MAJOR C', din = true, usb = true, now = 0, animations = true } = {}) {
      if (!integer(now, 0, 4294967295) || typeof scaleName !== 'string') return new Uint8Array(1024);
      const chars = Array.from(scaleName.slice(0, 31), x => x.charCodeAt(0));
      new Uint8Array(c.memory.buffer, c.bridge_input(), chars.length).set(chars);
      const pointer = c.bridge_firmware_frame(chars.length, Number(din), Number(usb), now, Number(animations));
      return pointer ? new Uint8Array(c.memory.buffer, pointer, 1024).slice() : new Uint8Array(1024);
    },
  };
  const textAt = pointer => {
    if (!pointer) return '';
    const bytes = new Uint8Array(c.memory.buffer, pointer, Math.min(256, c.memory.buffer.byteLength - pointer));
    const end = bytes.indexOf(0);
    return new TextDecoder().decode(bytes.subarray(0, end < 0 ? bytes.length : end));
  };
  const stateResult = code => ({ ok: code === 0, code, message: code === 0 ? '' : textAt(c.bridge_state_notice()) || 'Invalid shared-state operation.' });
  const rowValid = row => integer(row, 0, c.bridge_state_rows() - 1);
  const scalarValid = value => integer(value, -2147483648, 2147483647);
  const sendText = (value, callback) => {
    if (typeof value !== 'string' || value.includes('\0')) return stateResult(-1);
    const bytes = new TextEncoder().encode(value);
    if (bytes.length > 255) return stateResult(-1);
    new Uint8Array(c.memory.buffer, c.bridge_input(), bytes.length).set(bytes);
    return stateResult(callback(bytes.length));
  };
  const state = {
    rowCount: c.bridge_state_rows(),
    boot: {
      begin(now = 0) {
        if (!integer(now, 0, 4294967295)) return stateResult(-1);
        c.bridge_state_boot_begin(now); return stateResult(0);
      },
      progress(stage) { return integer(stage, 0, 4) ? stateResult(c.bridge_state_boot_progress(stage)) : stateResult(-1); },
      ready() { c.bridge_state_boot_ready(); },
      skip() { c.bridge_state_boot_skip(); },
      info: () => Object.fromEntries(['active', 'phase', 'stage', 'ready', 'elapsedMs', 'handoffMs'].map((key, field) => [key, c.bridge_state_boot_info(field)])),
    },
    schemaHash: () => textAt(c.bridge_state_schema()),
    designMode(enabled) { return typeof enabled === 'boolean' ? stateResult(c.bridge_state_design_mode(Number(enabled))) : stateResult(-1); },
    catalog(group, count = 70) { return integer(group, 0, 65535) && integer(count, 1, 65535) ? stateResult(c.bridge_state_catalog(group, count)) : stateResult(-1); },
    get(row) {
      if (!rowValid(row)) return undefined;
      const value = c.bridge_state_get(row);
      return value === -2147483648 ? textAt(c.bridge_state_text(row)) : value;
    },
    set(row, value) {
      if (!rowValid(row)) return stateResult(-1);
      if (typeof value === 'string') return sendText(value, length => c.bridge_state_set_text(row, length));
      return scalarValid(value) ? stateResult(c.bridge_state_set(row, value)) : stateResult(-1);
    },
    bounds(row,instance = -1) {
      if(!rowValid(row)||!integer(instance,-1,c.bridge_state_instances(row)-1))return null;
      const min=c.bridge_state_bounds(row,instance,0),max=c.bridge_state_bounds(row,instance,1);
      return min===-2147483648||max===-2147483648?null:{min,max};
    },
    begin(row) { return rowValid(row) ? stateResult(c.bridge_state_begin(row)) : stateResult(-1); },
    stage(value) {
      if (typeof value === 'string') return sendText(value, length => c.bridge_state_stage_text(length));
      return scalarValid(value) ? stateResult(c.bridge_state_stage(value)) : stateResult(-1);
    },
    adjust(delta) { return scalarValid(delta) ? stateResult(c.bridge_state_adjust(delta)) : stateResult(-1); },
    commit: () => stateResult(c.bridge_state_commit()),
    cancel: () => stateResult(c.bridge_state_cancel()),
    groupCount: c.bridge_state_groups(),
    visible(row) { return rowValid(row) && Boolean(c.bridge_state_visible(row)); },
    centerLevels() { return new Uint16Array(c.memory.buffer, c.bridge_state_center_levels(), 19).slice(); },
    focus(group, row = -1) { return integer(group, 0, c.bridge_state_groups() - 1) && (row === -1 || rowValid(row)) ? stateResult(c.bridge_state_focus(group, row)) : stateResult(-1); },
    open() { c.bridge_state_open(); return stateResult(0); },
    play() { c.bridge_state_play(); return stateResult(0); },
    navigate(action) {
      const key = ['left', 'up', 'select', 'down', 'right'].indexOf(action);
      if (key < 0) return stateResult(-1);
      c.bridge_state_boot_skip();
      return stateResult(c.bridge_state_nav(key));
    },
    cursor(group) { return integer(group, 0, c.bridge_state_groups() - 1) ? c.bridge_state_cursor(group) : -1; },
    pickingPad: () => Boolean(c.bridge_state_info(25)),
    info() {
      const keys = ['level', 'group', 'cursor', 'editing', 'editRow', 'editValue', 'revision', 'transitionRevision', 'direction', 'notice', 'changes', 'stateBytes', 'historyCount', 'transitioning', 'allocatedBytes', 'about', 'quickOpen', 'arpTarget', 'quickCursor', 'quickKeys', 'infoOpen', 'transitionElapsedMs', 'trackOptionsOpen', 'trackTarget', 'sensitivityTarget', 'sensitivityPick', 'sensitivityPicked'];
      const value = Object.fromEntries(keys.map((key, index) => [key, c.bridge_state_info(index)]));
      value.editText = textAt(c.bridge_state_edit_text());
      value.message = textAt(c.bridge_state_notice());
      return value;
    },
    allowed(row, value) { return rowValid(row) && scalarValid(value) && Boolean(c.bridge_state_allowed(row, value)); },
    format(row, useEdit = false) { return rowValid(row) ? textAt(c.bridge_state_format(row, Number(useEdit))) : ''; },
    compact(row, useEdit = false) { return rowValid(row) ? textAt(c.bridge_state_compact(row, Number(useEdit))) : ''; },
    instanceCount(row) { return rowValid(row) ? c.bridge_state_instances(row) : 0; },
    instance(row, index) {
      if (!rowValid(row) || !integer(index, 0, c.bridge_state_instances(row)-1)) return undefined;
      const value = c.bridge_state_instance_get(row, index);
      return value === -2147483648 ? textAt(c.bridge_state_instance_text(row, index)) : value;
    },
    events() {
      return Array.from({ length: c.bridge_state_info(12) }, (_, index) => ({
        sequence: c.bridge_state_event(index, 0) >>> 0, row: c.bridge_state_event(index, 1), kind: c.bridge_state_event(index, 2),
        value: c.bridge_state_event(index, 3), instance: c.bridge_state_event(index, 4), text: textAt(c.bridge_state_event_text(index)),
      }));
    },
    session: () => Object.fromEntries(['system', 'mode', 'transport', 'capture', 'clock', 'epoch'].map((key, index) => [key, c.bridge_state_session(index)])),
    frame({ scaleName = 'MAJOR C', din = true, usb = true, now = 0, animations = true } = {}) {
      if (typeof scaleName !== 'string' || !integer(now, 0, 4294967295)) return new Uint8Array(1024);
      const name = new TextEncoder().encode(scaleName.slice(0, 31));
      const size = Math.min(name.length, 31);
      new Uint8Array(c.memory.buffer, c.bridge_input(), size).set(name.subarray(0, size));
      const pointer = c.bridge_state_frame(size, Number(din), Number(usb), now, Number(animations));
      return pointer ? new Uint8Array(c.memory.buffer, pointer, 1024).slice() : new Uint8Array(1024);
    },
    navigationAnimations(enabled) {
      if (typeof enabled !== 'boolean') return stateResult(-1);
      c.bridge_state_navigation_animations(Number(enabled));
      return stateResult(0);
    },
    sceneKey: () => c.bridge_state_scene_key() >>> 0,
    companionSnapshot(now = 0) {
      if (!integer(now, 0, 4294967295) || !c.bridge_companion_snapshot) return null;
      const pointer = c.bridge_companion_snapshot(now);
      return pointer ? new Uint8Array(c.memory.buffer, pointer, 1680).slice() : null;
    },
    performanceSnapshot(now = 0) {
      if (!integer(now, 0, 4294967295) || !c.bridge_performance_snapshot) return null;
      const pointer = c.bridge_performance_snapshot(now);
      return pointer ? new Uint8Array(c.memory.buffer, pointer, 1024).slice() : null;
    },
    harmonySnapshot(now = 0) {
      if (!integer(now, 0, 4294967295) || !c.bridge_harmony_snapshot) return null;
      const pointer = c.bridge_harmony_snapshot(now);
      return pointer ? new Uint8Array(c.memory.buffer, pointer, 800).slice() : null;
    },
    harmonyAction(action,value=0) {
      if (!integer(action,0,10)||!integer(value,-2147483648,2147483647)||!c.bridge_harmony_action) return stateResult(-1);
      return stateResult(c.bridge_harmony_action(action,value));
    },
  };
  const settingsBytes = bytes => {
    if (!(bytes instanceof Uint8Array) || !bytes.length || bytes.length > c.bridge_settings_capacity()) return false;
    new Uint8Array(c.memory.buffer, c.bridge_settings_buffer(), bytes.length).set(bytes);
    return true;
  };
  const settings = {
    version: c.bridge_settings_version(),
    canApply: () => Boolean(c.bridge_settings_can_apply()),
    encode() { const length = c.bridge_settings_encode(); return length ? new Uint8Array(c.memory.buffer, c.bridge_settings_buffer(), length).slice() : null; },
    validate(bytes) { return settingsBytes(bytes) && Boolean(c.bridge_settings_validate(bytes.length)); },
    payloadVersion(bytes) { return settingsBytes(bytes) ? c.bridge_settings_payload_version(bytes.length) : 0; },
    crc(bytes) { return settingsBytes(bytes) ? c.bridge_settings_crc(bytes.length) >>> 0 : null; },
    catalog(bytes) { return this.validate(bytes) ? { group: c.bridge_settings_catalog(bytes.length, 0), root: c.bridge_settings_catalog(bytes.length, 1) } : null; },
    apply(bytes) { return settingsBytes(bytes) && Boolean(c.bridge_settings_apply(bytes.length)); },
  };
  const clockSources = ['internal', 'usb'];
  const clockStates = ['internal', 'waiting', 'locked', 'lost'];
  const quantizations = ['immediate', 'beat', 'bar'];
  const rhythmValue = field => c.bridge_rhythm_info(field) >>> 0;
  const unsigned64 = (low, high) => high <= 0x1fffff ? high * 4294967296 + low : null;
  const sourceID = (source, track = 0) => source === 'arp1' ? 1 : source === 'arp2' ? 2 :
    source === 'preview' ? 64 : source === 'track' && integer(track, 0, 20) ? 16 + track : 0;
  const rhythm = {
    snapshot() {
      const songTick = unsigned64(rhythmValue(6), rhythmValue(7));
      const beatTicks = rhythmValue(18), barTicks = rhythmValue(19);
      return {
        clockSource: clockSources[rhythmValue(0)], clockState: clockStates[rhythmValue(1)],
        bpm: rhythmValue(2) / 1000, ppqn: rhythmValue(3),
        tick: unsigned64(rhythmValue(4), rhythmValue(5)), songTick,
        transport: ['stopped', 'playing', 'paused'][rhythmValue(8)],
        meterNumerator: rhythmValue(9), meterDenominator: rhythmValue(10),
        quantization: quantizations[rhythmValue(11)], generatedCount: rhythmValue(12),
        generatedCapacity: rhythmValue(13), clockGeneration: rhythmValue(14),
        runtimeBytes: rhythmValue(15), nowMs: rhythmValue(16), suspended: Boolean(rhythmValue(17)),
        pulseIntervalMs: rhythmValue(20), timeoutMs: rhythmValue(21),
        bar: songTick === null ? null : Math.floor(songTick / barTicks) + 1,
        beat: songTick === null ? null : Math.floor(songTick / beatTicks) % rhythmValue(9) + 1,
        tracks: Array.from({ length: 21 }, (_, index) => {
          const field = n => c.bridge_rhythm_track(index, n) >>> 0;
          const pending = [null, 'start', 'stop'][field(2)];
          return { index, enabled: Boolean(field(0)), running: Boolean(field(1)), pending,
            pendingTick: pending ? unsigned64(field(3), field(4)) : null,
            playheadTick: field(5), generation: field(6), loopStart: field(7), loopEnd: field(7) + field(8) };
        }),
      };
    },
    configure(config) {
      if (!config || typeof config !== 'object' || Array.isArray(config)) return invalid();
      const current = this.snapshot();
      const values = { bpm: current.bpm, source: current.clockSource,
        meterNumerator: current.meterNumerator, meterDenominator: current.meterDenominator,
        quantization: current.quantization, ...config };
      if (typeof values.bpm !== 'number' || !Number.isFinite(values.bpm) || values.bpm < 20 || values.bpm > 300 ||
          !clockSources.includes(values.source) || !integer(values.meterNumerator, 1, 16) ||
          ![1, 2, 4, 8, 16, 32].includes(values.meterDenominator) || !quantizations.includes(values.quantization)) return invalid();
      return result(c.bridge_rhythm_configure(Math.round(values.bpm * 1000), clockSources.indexOf(values.source),
        values.meterNumerator, values.meterDenominator, quantizations.indexOf(values.quantization)));
    },
    advance(now) { return integer(now, 0, 4294967295) ? result(c.bridge_advance(now)) : invalid(); },
    usbReset(now) { return integer(now,0,4294967295)?result(c.bridge_expression_usb_reset(now)):invalid(); },
    start() { return result(c.bridge_rhythm_start()); },
    stop() { return result(c.bridge_rhythm_stop()); },
    toggleTrack(track) { return integer(track, 0, 20) ? result(c.bridge_rhythm_toggle(track)) : invalid(); },
    setTrackLoop(track, start, end) {
      return integer(track, 0, 20) && integer(start, 0, 4294967294) && integer(end, start + 1, 4294967295) ?
        result(c.bridge_rhythm_loop(track, start, end)) : invalid();
    },
    midi(status, data1 = 0, data2 = 0, now = rhythmValue(16)) {
      return [0xf2, 0xf8, 0xfa, 0xfb, 0xfc].includes(status) && integer(data1, 0, 127) && integer(data2, 0, 127) &&
        (status === 0xf2 || (data1 === 0 && data2 === 0)) && integer(now, 0, 4294967295) ?
        result(c.bridge_rhythm_midi(status, data1, data2, now)) : invalid();
    },
    generation(source, track = 0) { const id = sourceID(source, track); return id ? c.bridge_rhythm_generation(id) >>> 0 : 0; },
    acquire({ source, track = 0, note, channel, mask, velocity = 96, run } = {}) {
      const id = sourceID(source, track);
      if (!id || !integer(note, 0, 127) || !route(mask, channel) || !integer(velocity, 1, 127) ||
          (run !== undefined && !integer(run, 1, 4294967295))) return { ...invalid(), handle: null };
      const response = result(c.bridge_rhythm_acquire(id, run ?? (c.bridge_rhythm_generation(id) >>> 0), note, channel, mask, velocity));
      return { ...response, handle: response.ok ? c.bridge_rhythm_handle() >>> 0 : null };
    },
    release(handle) { return integer(handle, 1, 4294967295) ? result(c.bridge_rhythm_release(handle)) : invalid(); },
    clearSource(source, track = 0) { const id = sourceID(source, track); return id ? result(c.bridge_rhythm_clear(id)) : invalid(); },
  };
  const arpTypes = ['notes', 'chords'], arpGroupings = ['hybrid', 'combined', 'each'];
  const arpStarts = ['immediate', 'grid'], arpPatterns = ['up', 'down', 'up-down', 'down-up', 'as-played'];
  const arp = {
    select(index) { return integer(index, 0, 1) ? stateResult(c.bridge_arp_select(index)) : invalid(); },
    openQuick(index = c.bridge_state_info(17)) { return integer(index, 0, 1) ? stateResult(c.bridge_arp_quick_open(index)) : invalid(); },
    quickKeys(enabled) { return typeof enabled === 'boolean' ? stateResult(c.bridge_arp_quick_keys(Number(enabled))) : invalid(); },
    quickRows() { return Array.from({length:15}, (_, index) => c.bridge_arp_quick_row(index)); },
    quickState() { const info = state.info(); return {open:Boolean(info.quickOpen),selected:info.arpTarget,cursor:info.quickCursor,keys:Boolean(info.quickKeys)}; },
    snapshot(index = 0) {
      if (!integer(index, 0, 1)) return null;
      const value = field => c.bridge_arp_info(index, field);
      return {
        enabled: Boolean(value(0)), type: arpTypes[value(1)], grouping: arpGroupings[value(2)],
        sourceZones: value(3), layer: Boolean(value(4)), start: arpStarts[value(5)],
        restartOnChange: Boolean(value(6)), rateTicks: value(7), gatePercent: value(8), swingPercent: value(9),
        stepMask: value(10), pattern: arpPatterns[value(11)], channel: value(12), mask: value(13),
        inputs: value(14), phrases: value(15), owners: value(16),
        selectedChord: value(17) < 0 ? null : value(17), step: value(18) < 0 ? null : value(18),
        errors: value(19) >>> 0, skippedSteps: value(20) >>> 0, runtimeBytes: value(21),
        lastError: value(22), attacks: value(23) >>> 0, generation: value(24) >>> 0,
        offsets: Array.from({ length: 8 }, (_, n) => value(32 + n)),
      };
    },
    configure(index, changes = {}) {
      if (!integer(index, 0, 1) || !changes || typeof changes !== 'object' || Array.isArray(changes)) return invalid();
      const config = { ...this.snapshot(index), ...changes };
      if (typeof config.enabled !== 'boolean' || !arpTypes.includes(config.type) || !arpGroupings.includes(config.grouping) ||
          !integer(config.sourceZones, 0, 63) || typeof config.layer !== 'boolean' || !arpStarts.includes(config.start) ||
          typeof config.restartOnChange !== 'boolean' || !integer(config.rateTicks, 40, 3840) ||
          !integer(config.gatePercent, 1, 100) || !integer(config.swingPercent, 0, 50) || !integer(config.stepMask, 0, 255) ||
          !arpPatterns.includes(config.pattern) || !route(config.mask, config.channel) || !Array.isArray(config.offsets) ||
          config.offsets.length !== 8 || !config.offsets.every(v => integer(v, -24, 24))) return invalid();
      new Int16Array(c.memory.buffer, c.bridge_notes(), 8).set(config.offsets);
      return result(c.bridge_arp_configure(index, Number(config.enabled), arpTypes.indexOf(config.type),
        arpGroupings.indexOf(config.grouping), config.sourceZones, Number(config.layer), arpStarts.indexOf(config.start),
        Number(config.restartOnChange), config.rateTicks, config.gatePercent, config.swingPercent,
        config.stepMask, arpPatterns.indexOf(config.pattern), config.channel, config.mask));
    },
    clear(index) { return integer(index, 0, 1) ? result(c.bridge_arp_clear(index)) : invalid(); },
  };
  const sequenceSources = ['pads', 'arp1', 'arp2', 'live', 'strum', 'usb'];
  const sequenceTypes = ['note', 'cc', 'pressure', 'program', 'channel-pressure', 'bend'];
  const sequenceProcessors = ['direct', 'arp1', 'arp2'];
  const sequencePolicies = ['replace', 'overdub'];
  const sequenceCapture = ['off', 'armed', 'queued', 'recording', 'finishing', 'failed'];
  const sequenceLayouts = ['split', 'full-time', 'full-pitch'];
  const sequenceViews = ['tracks', 'steps'];
  const sequenceFields = ['layout', 'swapped', 'inputLayout', 'gridTicks', 'windowTick', 'basePitch', 'regionEnabled', 'regionStart', 'regionEnd', 'selectedStep', 'overdub', 'stepCount'];
  const sequenceCommands = { transport:0, record:1, launch:2, undo:3, redo:4, trackOptions:5, recordOptions:6, play:7,
    loopChanged:8, create:9, delete:10, playTrack:11, playAll:12, clearStep:13, overdub:14, inputToggle:15, help:16,
    inputContext:17, octaveDown:18, octaveUp:19 };
  const sequenceValue = field => c.bridge_seq_info(field) >>> 0;
  const sequencer = {
    snapshot() {
      const selected = sequenceValue(0), clock = rhythm.snapshot();
      return { selected, count: sequenceValue(1), revision: sequenceValue(2), voices: sequenceValue(3),
        attacks: sequenceValue(4), skipped: sequenceValue(5), failures: sequenceValue(6), lastError: sequenceValue(7),
        canUndo: Boolean(sequenceValue(8)), canRedo: Boolean(sequenceValue(9)),
        capture: { state: sequenceCapture[sequenceValue(10)], track: sequenceValue(11), source: sequenceSources[sequenceValue(12)],
          policy: sequencePolicies[sequenceValue(13)], count: sequenceValue(14), growing: Boolean(sequenceValue(15)),
          failed: Boolean(sequenceValue(16)), regionStart: sequenceValue(17), regionEnd: sequenceValue(18) },
        open: Boolean(sequenceValue(19)), view: sequenceViews[sequenceValue(20)], layout: sequenceLayouts[sequenceValue(21)],
        swapped: Boolean(sequenceValue(22)), inputLayout: ['notes','chords'][sequenceValue(23)],
        windowTick: sequenceValue(24), gridTicks: sequenceValue(25), basePitch: sequenceValue(26),
        regionEnabled: Boolean(sequenceValue(27)), regionStart: sequenceValue(28), regionEnd: sequenceValue(29),
        selectedPad: sequenceValue(30), uiError: sequenceValue(31), capacity: sequenceValue(32), takeCapacity: sequenceValue(33),
        fileCapacity: sequenceValue(34), runtimeBytes: sequenceValue(35), clock,
        remote:{record:Boolean(sequenceValue(36)),position:Boolean(sequenceValue(37)),channel:sequenceValue(38)},
        selectedStep:sequenceValue(39),overdub:Boolean(sequenceValue(40)),newTrackSelected:Boolean(sequenceValue(41)),
        deletePending:Boolean(sequenceValue(42)),solo:Boolean(sequenceValue(43)),helpOpen:Boolean(sequenceValue(44)),helpPage:sequenceValue(45),
        optionsOpen:Boolean(sequenceValue(46)),optionsTrack:sequenceValue(47),
        tracks: Array.from({length:21}, (_, track) => ({ ...this.track(track), ...clock.tracks[track] })),
        events: this.events(selected),
      };
    },
    track(track) {
      if (!integer(track, 0, 20)) return null;
      const value = field => c.bridge_seq_track(track, field) >>> 0;
      return { index: track, channel: value(0), mask: value(1), velocity: value(2), muted: Boolean(value(3)),
        processor: sequenceProcessors[value(4)], source: sequenceSources[value(5)], policy: sequencePolicies[value(6)],
        contentEnd: value(7), loopStart: value(8), loopEnd: value(9), revision: value(10), count: value(11), flags:value(12),
        gridTicks:value(13),exists:Boolean(value(14)),stepCount:Math.ceil((value(9)-value(8))/Math.max(1,value(13))),
        oneShot:Boolean(value(12)&1), quantize:(value(12)>>1)&7, recordControllers:Boolean(value(12)&16), sendPressure:Boolean(value(12)&32) };
    },
    events(track = sequenceValue(0)) {
      if (!integer(track, 0, 20)) return [];
      return Array.from({length:c.bridge_seq_track(track,11)}, (_, index) => {
        const value = field => c.bridge_seq_event(track,index,field) >>> 0;
        return { track, tick:value(0), duration:value(1), id:value(2), group:value(3), type:sequenceTypes[value(4)], pitch:value(5), value:value(6), chord:Boolean(value(7)) };
      });
    },
    select(track) { return integer(track,0,20) ? result(c.bridge_seq_select(track)) : invalid(); },
    open(view = 'tracks') { return sequenceViews.includes(view) ? result(c.bridge_seq_open(sequenceViews.indexOf(view))) : invalid(); },
    close() { return result(c.bridge_seq_close()); },
    action(command,track=sequenceValue(0)) { return Object.hasOwn(sequenceCommands,command)&&integer(track,0,20) ? result(c.bridge_seq_action(sequenceCommands[command],track)) : invalid(); },
    createTrack() { const created=this.action('create');return created.ok?this.open('steps'):created; },
    deleteTrack(track=sequenceValue(0)) { const deleted=this.action('delete',track);return deleted.ok?this.open('tracks'):deleted; },
    playTrack(track=sequenceValue(0)) { return this.action('playTrack',track); },
    playAll() { return this.action('playAll'); },
    clearStep() { return this.action('clearStep'); },
    configureTrack(track, changes = {}) {
      const current = this.track(track);
      if (!current || !changes || typeof changes !== 'object' || Array.isArray(changes)) return invalid();
      const config = {...current,...changes};
      let flags=config.flags;
      for(const [name,mask] of [['oneShot',1],['recordControllers',16],['sendPressure',32]]) {
        if(name in changes){if(typeof changes[name]!=='boolean')return invalid();flags=changes[name]?flags|mask:flags&~mask;}
      }
      if('quantize' in changes){if(!integer(changes.quantize,0,4))return invalid();flags=(flags&~14)|(changes.quantize<<1);}
      if (!route(config.mask,config.channel) || !integer(config.velocity,0,127) || typeof config.muted !== 'boolean' ||
          !sequenceProcessors.includes(config.processor) || !sequenceSources.includes(config.source) || !sequencePolicies.includes(config.policy) || !integer(flags,0,255)) return invalid();
      return result(c.bridge_seq_configure(track,config.channel,config.mask,config.velocity,Number(config.muted),sequenceProcessors.indexOf(config.processor),
        sequenceSources.indexOf(config.source),sequencePolicies.indexOf(config.policy),flags));
    },
    configureView(changes = {}) {
      if (!changes || typeof changes !== 'object' || Array.isArray(changes)) return invalid();
      const entries = Object.entries(changes).map(([field,value]) => {
        if (!sequenceFields.includes(field)) return null;
        if (field === 'layout') value = sequenceLayouts.indexOf(value);
        if (field === 'inputLayout') value = ['notes','chords'].indexOf(value);
        if (field === 'swapped' || field === 'regionEnabled' || field === 'overdub') { if (typeof value !== 'boolean') return null; value = Number(value); }
        return integer(value,0,0x3fffffff) ? [sequenceFields.indexOf(field),value] : null;
      });
      if (entries.some(entry => !entry)) return invalid();
      for (const [field,value] of entries) { const code = c.bridge_seq_view_configure(field,value); if (code) return result(code); }
      return result(0);
    },
    record() { return result(c.bridge_seq_record()); },
    finish(immediate = false) { return typeof immediate === 'boolean' ? result(c.bridge_seq_finish(Number(immediate))) : invalid(); },
    configureRemote(changes = {}) {
      const config={record:Boolean(sequenceValue(36)),position:Boolean(sequenceValue(37)),channel:sequenceValue(38),...changes};
      return typeof config.record==='boolean'&&typeof config.position==='boolean'&&integer(config.channel,0,16) ?
        result(c.bridge_seq_remote_configure(Number(config.record),Number(config.position),config.channel)) : invalid();
    },
    loop(track,start,end) { return integer(track,0,20) && integer(start,0,0x3ffffffe) && integer(end,start+1,0x3fffffff) ? result(c.bridge_seq_loop(track,start,end)) : invalid(); },
    add(track,events) {
      if (!integer(track,0,20) || !Array.isArray(events) || !events.length || events.length > 64 || events.some(event => !event ||
          !sequenceTypes.includes(event.type ?? 'note') || !integer(event.tick,0,0x3fffffff) || !integer(event.duration ?? 0,0,0x3fffffff) ||
          !integer(event.pitch,0,127) || !integer(event.value,0,127) || !integer(event.group ?? 0,0,65535) ||
          (event.chord !== undefined && typeof event.chord !== 'boolean'))) return invalid();
      const buffer = new DataView(c.memory.buffer,c.bridge_seq_input(),events.length*16);
      events.forEach((event,index) => { const offset=index*16; buffer.setUint32(offset,event.tick,true); buffer.setUint32(offset+4,event.duration ?? 0,true);
        buffer.setUint16(offset+8,0,true); buffer.setUint16(offset+10,event.group ?? 0,true); buffer.setUint8(offset+12,track);
        buffer.setUint8(offset+13,sequenceTypes.indexOf(event.type ?? 'note') | (event.chord ? 128 : 0)); buffer.setUint8(offset+14,event.pitch); buffer.setUint8(offset+15,event.value); });
      return result(c.bridge_seq_add(track,events.length));
    },
    remove(id) { return integer(id,1,65535) ? result(c.bridge_seq_remove(id)) : invalid(); },
    update(id,{tick,duration,pitch,value}) { return integer(id,1,65535) && integer(tick,0,0x3fffffff) && integer(duration,0,0x3fffffff) && integer(pitch,0,127) && integer(value,0,127) ? result(c.bridge_seq_update(id,tick,duration,pitch,value)) : invalid(); },
    clear(track) { return integer(track,0,20) ? result(c.bridge_seq_clear(track)) : invalid(); },
    duplicate(track,start,end,destination,extend = true) { return integer(track,0,20) && integer(start,0,0x3ffffffe) && integer(end,start+1,0x3fffffff) && integer(destination,0,0x3fffffff) && typeof extend === 'boolean' ? result(c.bridge_seq_duplicate(track,start,end,destination,Number(extend))) : invalid(); },
    trim(track,start,end) { return integer(track,0,20) && integer(start,0,0x3ffffffe) && integer(end,start+1,0x3fffffff) ? result(c.bridge_seq_trim(track,start,end)) : invalid(); },
    undo() { return result(c.bridge_seq_history(0)); }, redo() { return result(c.bridge_seq_history(1)); },
    exportBytes() { const length = c.bridge_seq_song_encode(); return length ? new Uint8Array(c.memory.buffer,c.bridge_seq_file(),length).slice() : null; },
    validateBytes(bytes) {
      if (!(bytes instanceof Uint8Array) || !bytes.length || bytes.length > sequenceValue(34)) return false;
      new Uint8Array(c.memory.buffer,c.bridge_seq_file(),bytes.length).set(bytes); return c.bridge_seq_song_validate(bytes.length) === 0;
    },
    importBytes(bytes) {
      if (!(bytes instanceof Uint8Array) || !bytes.length || bytes.length > sequenceValue(34)) return invalid();
      new Uint8Array(c.memory.buffer,c.bridge_seq_file(),bytes.length).set(bytes); return result(c.bridge_seq_song_decode(bytes.length));
    },
    pad(pad) {
      if (!integer(pad,0,95)) return null;
      const value = field => c.bridge_seq_pad(pad,field) >>> 0;
      return { role: ['none','editor','notes','chords','track','command','separator'][value(0)], held:Boolean(value(1)),
        eventId:value(2), tick:value(3), pitch:value(4), track:value(9) };
    },
    colors() { const pointer=c.bridge_seq_colors();return pointer?new Uint16Array(c.memory.buffer,pointer,96*4).slice():null; },
    previewPad(pad) {
      if (!integer(pad,0,95)) return {...invalid(),notes:[]};
      const response=result(c.bridge_seq_preview(pad));
      return {...response,notes:response.ok?Array.from(new Int16Array(c.memory.buffer,c.bridge_preview_notes(),c.bridge_preview_count())):[]};
    },
  };
  const calibrationActions=['learn','before','after','apply','retry','cancel'];
  const calibrationAliases={compare:'before',save:'apply',discard:'cancel'};
  const calibration={
    snapshot() {
      const keys=['open','state','target','selected','stage','hits','completed','reason','action','contact','revision','raw','rate','baseline','span','peak','rise','samples',
        'holdSoft','holdFirm','holdElapsed','holdSoftSamples','previewPercent','outlierCount','reviewReady','reviewCursor','reviewEditing','reviewDraft','pick','scopeFirst','scopeCount','leakOutlierCount'];
      const value=Object.fromEntries(keys.map((key,index)=>[key,c.bridge_calibration_info(index)]));
      value.open=Boolean(value.open);value.contact=Boolean(value.contact);value.revision>>>=0;
      value.message=textAt(c.bridge_calibration_reason());return value;
    },
    open() {return stateResult(c.bridge_calibration_open());},
    close() {c.bridge_calibration_close();return stateResult(c.bridge_calibration_info(0)?-2:0);},
    action(name) {const action=calibrationAliases[name]??name;return calibrationActions.includes(action)?stateResult(c.bridge_calibration_action(calibrationActions.indexOf(action))):stateResult(-1);},
    select(pad) {return integer(pad,0,95)?stateResult(c.bridge_calibration_select(pad)):stateResult(-1);},
    completed(pad) {return integer(pad,0,95)&&Boolean(c.bridge_calibration_completed(pad));},
    setReviewSensitivity(percent) {return integer(percent,0,100)?stateResult(c.bridge_calibration_set_sensitivity(percent)):stateResult(-1);},
    strength(pad) {
      if(!integer(pad,0,95))return null;
      const percent=c.bridge_calibration_strength(pad);
      return percent<0?null:{percent,flagged:Boolean(c.bridge_calibration_outlier(pad))};
    },
    candidate(pad) {
      if(!integer(pad,0,95)||c.bridge_calibration_candidate(pad,0)<0)return null;
      const value=Object.fromEntries(['trigger','release','floor','ceiling','softRate','hardRate'].map((key,index)=>[key,c.bridge_calibration_candidate(pad,index)]));
      value.anchors=Array.from({length:3},(_,index)=>c.bridge_calibration_candidate(pad,6+index));
      value.pressureCurve=c.bridge_calibration_candidate(pad,9);value.velocityCurve=c.bridge_calibration_candidate(pad,10);return value;
    },
    value(pad,raw,rate,{candidate=false,velocity=false}={}) {
      if(!integer(pad,0,95)||!integer(raw,0,4095)||!integer(rate,0,4095)||typeof candidate!=='boolean'||typeof velocity!=='boolean')return null;
      const value=c.bridge_calibration_value(pad,raw,rate,Number(candidate),Number(velocity));return value<0?null:value;
    },
  };
  const expression = {
    axes(pad,pressure,aftertouch) {return integer(pad,0,95)&&integer(pressure,0,127)&&integer(aftertouch,0,127)?result(c.bridge_expression_axes(pad,pressure,aftertouch)):invalid();},
    channelRange(destination,channel) {return integer(destination,0,2)&&integer(channel,0,15)?c.bridge_channel_range(destination,channel):2;},
    config() {
      const keys=['enabled','upperZone','members','bendRange','managerBendRange','glide','padWindow','semitoneWindow'];
      return Object.fromEntries(keys.map((key,i)=>[key,[0,1,5].includes(i)?Boolean(c.bridge_expression_value(i)):c.bridge_expression_value(i)]));
    },
    configure(changes={}) {
      if(!changes || typeof changes!=='object' || Array.isArray(changes))return invalid();
      const values={...this.config(),...changes};
      if(Object.keys(changes).some(key=>!Object.hasOwn(this.config(),key)) ||
        typeof values.enabled!=='boolean' || typeof values.upperZone!=='boolean' || typeof values.glide!=='boolean' ||
        !integer(values.members,1,15) || !integer(values.bendRange,1,96) || !integer(values.managerBendRange,1,96) ||
        !integer(values.padWindow,1,11) || !integer(values.semitoneWindow,1,24))return invalid();
      return result(c.bridge_expression_configure(Number(values.enabled),Number(values.upperZone),values.members,
        values.bendRange,values.managerBendRange,Number(values.glide),values.padWindow,values.semitoneWindow));
    },
    sendSetup() {return result(c.bridge_expression_setup());},
    pressure(pad,value) {return integer(pad,0,95)&&integer(value,0,127)?result(c.bridge_expression_pressure(pad,value)):invalid();},
    bend(pad,value) {return integer(pad,0,95)&&integer(value,0,16383)?result(c.bridge_expression_bend(pad,value)):invalid();},
    stats() {return {active:c.bridge_expression_value(8),consumed:c.bridge_expression_value(9)};},
    voices() {
      const keys=['pad','logical','pitch','channel','destinations','state','pressure','sentPressure','bend','sentBend','modifier','interval','modifierPressure','timbre','sentTimbre'];
      const voices=[];
      for(let slot=0;slot<15;slot++) {
        if(c.bridge_expression_voice(slot,5)<=0)continue;
        voices.push(Object.fromEntries(keys.map((key,field)=>[key,c.bridge_expression_voice(slot,field)])));
      }
      return voices;
    },
  };
  const guidanceMask = (mask, empty = false) => integer(mask, empty ? 0 : 1, 4095);
  const guidance = {
    families: Array.from({length:c.bridge_guidance_scale_count()},(_,id)=>({id,name:textAt(c.bridge_guidance_scale_name(id))})),
    scale(family,root) {
      if(!integer(family,0,c.bridge_guidance_scale_count()-1)||!integer(root,0,11))throw new RangeError('Unknown guidance scale');
      return c.bridge_guidance_scale_mask(family,root);
    },
    label(mask,root=255) {
      if(!guidanceMask(mask,true)||!(integer(root,0,11)||root===255))throw new RangeError('Invalid chord notes');
      return textAt(c.bridge_guidance_label(mask,root));
    },
    chords(key,root,current=0) {
      if(!guidanceMask(key)||!integer(root,0,11)||!guidanceMask(current,true))throw new RangeError('Invalid chord context');
      const count=c.bridge_guidance_chords(key,root,current);
      if(count<0)throw new RangeError('The tonic must be in the key');
      return Array.from({length:count},(_,row)=>Object.fromEntries(['mask','root','shared','score'].map((field,n)=>[field,c.bridge_guidance_chord_value(row,n)])));
    },
    discover(selected) {
      if(!guidanceMask(selected))throw new RangeError('Select at least one note');
      const count=c.bridge_guidance_scales(selected);
      return {total:c.bridge_guidance_scale_total(),matches:Array.from({length:count},(_,row)=>Object.fromEntries(['mask','family','root','extra'].map((field,n)=>[field,c.bridge_guidance_scale_value(row,n)])))};
    },
    modulation(source,target,current=0) {
      if(!guidanceMask(source)||!guidanceMask(target)||!guidanceMask(current,true))throw new RangeError('Invalid key comparison');
      if(c.bridge_guidance_modulation(source,target,current)!==0)throw new RangeError('Invalid key comparison');
      return Object.fromEntries(['common','added','removed','pivot','pivotRoot','currentFits'].map((field,n)=>[field,c.bridge_guidance_modulation_value(n)]));
    },
  };
  return {
    guidance,
    firmware, state, settings, rhythm, arp, display, sequencer, calibration, expression,
    press({ pad, notes, mask, channel, velocity = 96, transpose = 0, shape }) {
      if (!integer(pad, 0, 95) || !route(mask, channel) || !integer(velocity, 1, 127) || !integer(transpose, -32768, 32767) ||
          !notesValid(notes)) return invalid();
      const shaping = shape === undefined ? null : shapeArgs(shape);
      if (shape !== undefined && !shaping) return invalid();
      new Int16Array(c.memory.buffer, c.bridge_notes(), notes.length).set(notes);
      if (shaping) return result(c.bridge_press_shaped(pad, notes.length, transpose, channel, mask, velocity, ...shaping));
      return result(c.bridge_press(pad, notes.length, transpose, channel, mask, velocity));
    },
    preview(notes, { transpose = 0, shape = {} } = {}) {
      const shaping = shapeArgs(shape);
      if (!notesValid(notes) || !integer(transpose, -32768, 32767) || !shaping) return { ...invalid(), notes: [] };
      new Int16Array(c.memory.buffer, c.bridge_notes(), notes.length).set(notes);
      const response = result(c.bridge_preview(notes.length, transpose, ...shaping));
      return { ...response, notes: response.ok ? Array.from(new Int16Array(c.memory.buffer, c.bridge_preview_notes(), c.bridge_preview_count())) : [] };
    },
    advance(now) { return integer(now, 0, 4294967295) ? result(c.bridge_advance(now)) : invalid(); },
    melodyPitch({ layout, anchor, degrees, column, row }) {
      if (!integer(layout, 1, 3) || !integer(anchor, -32768, 32767) || !notesValid(degrees) || degrees.length > 12 || !integer(column, 0, 11) || !integer(row, 0, 3)) return { ...invalid(), note: null };
      new Int16Array(c.memory.buffer, c.bridge_notes(), degrees.length).set(degrees);
      const note = c.bridge_melody_pitch(layout, anchor, degrees.length, column, row);
      return { ...result(note < 0 ? 1 : 0), note: note < 0 ? null : note };
    },
    release(pad) { return integer(pad, 0, 95) ? result(c.bridge_release(pad)) : invalid(); },
    pressure(pad, value) { return integer(pad, 0, 95) && integer(value, 0, 127) ? result(c.bridge_pressure(pad, value)) : invalid(); },
    sustain(mask, channel, enabled) { return route(mask, channel) && typeof enabled === 'boolean' ? result(c.bridge_sustain(mask, channel, Number(enabled))) : invalid(); },
    control(mask, channel, cc, value) { return route(mask, channel) && integer(cc, 0, 127) && integer(value, 0, 127) ? result(c.bridge_control(mask, channel, cc, value)) : invalid(); },
    bend(mask, channel, value) { return route(mask, channel) && integer(value, -2147483648, 2147483647) ? result(c.bridge_bend(mask, channel, value)) : invalid(); },
    panic() { c.bridge_panic(); return result(0); },
    drain() { const events = []; while (c.bridge_poll()) events.push(event()); return events; },
    stats() { return { queued: c.bridge_queued(), reserved: c.bridge_reserved(), activeVoices: c.bridge_sounding(), pending: c.bridge_pending(), heldPads: c.bridge_held_count(), capacity: 1024, stateBytes: c.bridge_state_bytes() }; },
    voices() {
      const count = c.bridge_voice_snapshot();
      return Array.from(new Uint16Array(c.memory.buffer, c.bridge_voices(), count), key => ({ ...keyInfo(key), owners: c.bridge_refs(key), sustained: Boolean(c.bridge_deferred(key)) }));
    },
    isHeld(pad) { return integer(pad, 0, 95) && Boolean(c.bridge_held(pad)); },
    owner(pad) { return integer(pad, 0, 95) ? Array.from({ length: c.bridge_owner_count(pad) }, (_, i) => keyInfo(c.bridge_owner_key(pad, i))) : []; },
    receive(bytes, { now } = {}) {
      if (!sequence(bytes) || !Array.from(bytes).every(byte => integer(byte, 0, 255))) throw new TypeError('Expected MIDI bytes');
      if (now !== undefined && !integer(now, 0, 4294967295)) throw new TypeError('Expected uint32 timestamp');
      const source = Array.from(bytes);
      const events = [];
      for (let offset = 0; offset < bytes.length; offset += 256) {
        const chunk = source.slice(offset, offset + 256);
        new Uint8Array(c.memory.buffer, c.bridge_input(), chunk.length).set(chunk);
        const status = now === undefined ? c.bridge_feed(chunk.length) : c.bridge_feed_at(chunk.length, now);
        if (status !== 0) throw new RangeError(result(status).status);
        while (c.bridge_receive_poll()) events.push(event());
      }
      return events;
    },
    receiveStats() { return { received: c.bridge_rx_received(), invalid: c.bridge_rx_invalid(), unsupported: c.bridge_rx_unsupported(), fragmentBytes: c.bridge_rx_fragment(), overflow: c.bridge_rx_overflow() }; },
  };
}
