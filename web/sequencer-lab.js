/* SPDX-License-Identifier: GPL-3.0-or-later
 * A host control surface for the actual portable C song, editor and transport.
 */
import { parseMidiFile, midiToSongBytes, sequencerToMidi } from './sequencer-midi.js';
import { createDeviceSongClient } from './device-song.js';

export function createSequencerLabModel({ engine, onEvents = () => {}, onChange = () => {}, now = () => Math.floor(performance.now()) >>> 0 }) {
  let notice = '';
  const invoke = (operation, message = '') => {
    const advanced = engine.advance(now());
    if (!advanced.ok) { notice = advanced.status; return advanced; }
    const result = operation();
    notice = result.ok ? message : `Not applied: ${result.status || result.message || result.code}.`;
    onEvents(); onChange(); return result;
  };
  const model = {
    snapshot: () => ({...engine.sequencer.snapshot(),notice}),
    select: track => invoke(()=>engine.sequencer.select(track)),
    open: view => invoke(()=>engine.sequencer.open(view),view==='steps'?'Tap a left step, then play on the right.':'Created tracks on the OLED; LEFT pads stay dark.'),
    create: () => invoke(()=>engine.sequencer.createTrack(),'Track created. Tap a left step, then play on the right.'),
    delete: () => invoke(()=>engine.sequencer.deleteTrack(),'Track deleted. Undo can restore it.'),
    close: () => invoke(()=>engine.sequencer.close(),'Returned to Play; track playback continues.'),
    options: () => invoke(()=>engine.sequencer.action('trackOptions'),'Track settings open. LEFT returns directly to this track; pads stay active.'),
    configure: changes => invoke(()=>engine.sequencer.configureView(changes)),
    configureTrack: changes => invoke(()=>engine.sequencer.configureTrack(engine.sequencer.snapshot().selected,changes)),
    configureRemote: changes => invoke(()=>engine.sequencer.configureRemote(changes)),
    midi(status,data1=0,data2=0) {
      if(!Number.isInteger(status)||status<0x80||status>0xff||![data1,data2].every(value=>Number.isInteger(value)&&value>=0&&value<=127)) {
        notice='Invalid MIDI channel data.';return{ok:false,status:'invalid'};
      }
      const cin=status>=0xf8?15:status===0xf2?3:status>>4;
      return invoke(()=>{engine.receive(Uint8Array.of(cin,status,data1,data2),{now:now()});return{ok:true,code:0};});
    },
    play: () => invoke(()=>engine.sequencer.playAll()),
    playTrack: () => invoke(()=>engine.sequencer.playTrack()),
    clearStep: () => invoke(()=>engine.sequencer.clearStep(),'Selected step cleared.'),
    record: () => invoke(()=>engine.sequencer.action('record')),
    finish: immediate => invoke(()=>engine.sequencer.finish(Boolean(immediate))),
    launch: track => invoke(()=>engine.rhythm.toggleTrack(track)),
    undo: () => invoke(()=>engine.sequencer.undo()), redo: () => invoke(()=>engine.sequencer.redo()),
    add: events => invoke(()=>engine.sequencer.add(engine.sequencer.snapshot().selected,events)),
    update: (id,event) => invoke(()=>engine.sequencer.update(id,event)),
    remove: id => invoke(()=>engine.sequencer.remove(id)),
    clear: () => invoke(()=>engine.sequencer.clear(engine.sequencer.snapshot().selected)),
    loop: (start,end) => invoke(()=>engine.sequencer.loop(engine.sequencer.snapshot().selected,start,end)),
    duplicate: (start,end,destination) => invoke(()=>engine.sequencer.duplicate(engine.sequencer.snapshot().selected,start,end,destination,true)),
    trim: (start,end) => invoke(()=>engine.sequencer.trim(engine.sequencer.snapshot().selected,start,end)),
    exportSong: () => engine.sequencer.exportBytes(),
    exportMidi: () => sequencerToMidi(engine.sequencer,engine.rhythm.snapshot()),
    importSong(bytes) {
      if (!engine.sequencer.validateBytes(bytes)) { notice = 'Invalid song: nothing was changed.'; return {ok:false,status:'invalid-song'}; }
      return invoke(()=>engine.sequencer.importBytes(bytes),'Song imported; transport stopped. Live pads retain their owners.');
    },
    importMidi(bytes) {
      let midi, candidate;
      try {
        midi = parseMidiFile(bytes,{ppqn:960,maxTracks:21,maxNotes:512});
        if (midi.bpm < 20 || midi.bpm > 300 || midi.meter.numerator < 1 || midi.meter.numerator > 16 || ![1,2,4,8,16,32].includes(midi.meter.denominator))
          throw new RangeError('The initial tempo or meter exceeds the device clock range.');
        candidate = midiToSongBytes(midi);
        if (!engine.sequencer.validateBytes(candidate)) throw new RangeError('The device rejected the converted song.');
      } catch (error) { notice = error.message; return {ok:false,status:'invalid-midi',message:error.message}; }
      return invoke(()=>{
        const result=engine.sequencer.importBytes(candidate);
        if (!result.ok) return result;
        const clock=engine.rhythm.configure({bpm:midi.bpm,meterNumerator:midi.meter.numerator,meterDenominator:midi.meter.denominator});
        if (!clock.ok) return clock;
        return result;
      },`${midi.tracks.length} channel tracks imported. ${midi.warnings.join(' ')}`);
    },
    saveLocal(storage) {
      const bytes=model.exportSong();
      try { storage.setItem('thy666.song.v1',JSON.stringify(Array.from(bytes)));notice='Song saved in this browser.';return {ok:true}; }
      catch { notice='Browser song storage is unavailable; use Export song.';return {ok:false}; }
    },
    loadLocal(storage) {
      try { const raw=storage.getItem('thy666.song.v1');if(!raw)throw new Error();const values=JSON.parse(raw);
        if(!Array.isArray(values)||values.some(value=>!Number.isInteger(value)||value<0||value>255))throw new Error();return model.importSong(Uint8Array.from(values)); }
      catch { notice='No valid saved browser song was found.';return {ok:false}; }
    },
  };
  return model;
}

const seqNode=(document,tag,text,className='')=>{const node=document.createElement(tag);if(text!==undefined)node.textContent=text;node.className=className;return node;};
const sequenceNote=pitch=>`${['C','C♯','D','E♭','E','F','F♯','G','A♭','A','B♭','B'][pitch%12]}${Math.floor(pitch/12)-1}`;

export function initializeSequencerLab({engine,onEvents,onChange,now,document=globalThis.document}) {
  const model=createSequencerLabModel({engine,onEvents,onChange,now});
  const panel=seqNode(document,'section',undefined,'panel sequencer-lab');panel.id='sequencer-lab';
  panel.setAttribute('aria-label','Shared C sequencer');
  panel.innerHTML=[
    '<div class="engine-heading"><div><p class="eyebrow">THY666 / SHARED C SONG ENGINE</p><h2>Sequencer</h2><p>Create a track. Tap a step on the left. Play notes or chords on the right.</p></div><span class="engine-scope-badge">C / WebAssembly</span></div>',
    '<div class="seq-toolbar"><button class="button" data-action="tracks">Open sequencer</button><button class="button" data-action="create">New track</button><button class="button button-small" data-action="close">Return to Play</button><span data-readout="surface"></span></div>',
    '<div class="seq-toolbar"><button class="button" data-action="play">Play all</button><button class="button" data-action="play-track">Play track</button><button class="button" data-action="record">Record</button><button class="button button-small" data-action="undo">Undo</button><button class="button button-small" data-action="redo">Redo</button><span data-readout="transport"></span></div>',
    '<p class="seq-help">Tracks live on the OLED. The large pads stay dark until you enter a track. Select a created track below, then choose Edit track: LEFT becomes the sequence, RIGHT the playing surface.</p>',
    '<div class="seq-tracks" role="group" aria-label="Created tracks"></div><div class="seq-empty" data-readout="empty">No tracks yet. Choose New track to begin.</div>',
    '<section class="seq-editor" data-editor><div class="seq-grid-heading"><span data-readout="track"></span><span data-readout="window"></span></div><div class="seq-fields">',
    '<label>Input<select data-field="inputLayout"><option value="notes">Notes</option><option value="chords">Chords</option></select></label>',
    '<label>Length / steps<input type="number" min="1" max="48" step="1" data-field="stepCount"></label>',
    '<label>Step size<select data-field="gridTicks"><option value="120">1/32</option><option value="240">1/16</option><option value="480">1/8</option><option value="960">1/4</option></select></label>',
    '<label>Overdub<select data-field="overdub"><option value="false">OFF / replace step</option><option value="true">ON / add notes</option></select></label></div>',
    '<div class="seq-toolbar"><button class="button button-small" data-action="steps">Edit track</button><button class="button button-small" data-action="track-options">Track settings</button><button class="button button-small" data-action="clear-step">Clear step</button><button class="button button-small seq-danger" data-action="delete-track">Delete track</button><span data-readout="step"></span></div>',
    '<div class="seq-grid" role="group" aria-label="48 chronological sequence steps"></div>',
    '<p class="seq-help">Tap a step, release it, then play on the RIGHT pads above. The step stays selected. With Overdub OFF, a fresh playing gesture replaces that step; overlapping notes build one chord. With Overdub ON, new notes are added. The four rows run left to right, top to bottom.</p></section>',
    '<details><summary>Controls & short guide</summary><p class="seq-help"><strong>Tracks:</strong> UP/DOWN select a track or New track; RIGHT opens it. M2 requests deletion; RIGHT confirms, LEFT cancels. LEFT from the editor returns to Tracks. CENTER returns to Play.</p>',
    '<p class="seq-help"><strong>Playback:</strong> 5 plays/stops the song. M1 plays/stops only the selected track. 12 starts recording directly, or queues the next bar while playback is running; press again to finish. Fixed loops record one pass and continue playing.</p>',
    '<p class="seq-help"><strong>Editing:</strong> M2 clears the selected step. M3 toggles Overdub. M4 switches Notes/Chords. M5/M6 Undo/Redo. M7 opens help. U1/U2 shift octave; U3 cycles the scale in Chords mode or the note layout in Notes mode. Upper LEFT keys choose root.</p></details>',
    '<p class="seq-help"><strong>Track settings:</strong> enter the track, then press RIGHT, or use Track settings above. Its dedicated OLED page also appears under Engine & menus below. LEFT returns directly to the editor; the LEFT steps and RIGHT input remain active. Global Sequencer settings contain song-wide controls only.</p>',
    '<details><summary>USB remote input / simulator</summary><div class="seq-fields"><label>Recording permission<select data-remote="record"><option value="false">OFF / default</option><option value="true">ON</option></select></label><label>Song position permission<select data-remote="position"><option value="false">OFF / default</option><option value="true">ON</option></select></label><label>Input channel / 0 = all<input data-remote="channel" type="number" min="0" max="16" step="1"></label><label>Simulated channel<input data-usb="channel" type="number" min="1" max="16" value="1"></label><label>Note<input data-usb="note" type="number" min="0" max="127" value="60"></label><button class="button button-small" data-action="usb-on">USB note on</button><button class="button button-small" data-action="usb-off">USB note off</button><label>Song position / sixteenths<input data-usb="position" type="number" min="0" max="16383" value="0"></label><button class="button button-small" data-action="usb-position">Send position</button></div><p class="seq-help">USB cable 1 only. Incoming notes record only into a USB-source take with permission enabled. They are not automatically echoed.</p></details>',
    '<details><summary>Song files</summary><div class="seq-toolbar"><button class="button button-small" data-action="export">Export song</button><label class="button button-small seq-file">Import song<input type="file" accept=".thys,.bin" data-file="song"></label><button class="button button-small" data-action="midi-export">Export MIDI</button><label class="button button-small seq-file">Import MIDI<input type="file" accept=".mid,.midi" data-file="midi"></label><button class="button button-small" data-action="save-local">Save browser song</button><button class="button button-small" data-action="load-local">Load browser song</button></div><p class="seq-help">THYS v2 preserves created tracks, per-track grids, routes, grouped chords and sparse messages. Legacy v1 songs import without discarding notes. MIDI import preserves timing and supported messages. Import replaces the song and stops the sequencer; live notes retain their owners.</p></details>',
    '<details><summary>Device song transfer</summary><div class="seq-toolbar"><button class="button button-small" data-device="connect">Connect device</button><button class="button button-small" data-device="upload" disabled>Upload preview song</button><button class="button button-small" data-device="download" disabled>Download device song</button><button class="button button-small" data-device="disconnect" disabled>Disconnect</button><span data-readout="device">Not connected</span></div><p class="seq-help">Only these explicit buttons access a device. Upload replaces its song and queues saving. Download verifies the native song before loading it here. Firmware updates use the official updater separately.</p></details>',
    '<dl class="seq-runtime"><div><dt>Events</dt><dd data-readout="count"></dd></div><div><dt>Generated notes</dt><dd data-readout="voices"></dd></div><div><dt>Skipped / errors</dt><dd data-readout="health"></dd></div></dl><p data-readout="notice" class="seq-notice" role="status" aria-live="polite"></p>'
  ].join('');
  (document.querySelector('#arp-lab')||document.querySelector('#rhythm-lab')||document.querySelector('.instrument')).insertAdjacentElement('afterend',panel);
  const find=selector=>panel.querySelector(selector),field=name=>find('[data-field="'+name+'"]');
  const device=createDeviceSongClient({onStatus:text=>{find('[data-readout="device"]').textContent=text;}});
  let lastTracks='',lastGrid='',confirmDelete=false,deleteIdentity='';
  const value=(selector,name)=>Number(find('['+selector+'="'+name+'"]').value);
  const sync=(node,value,force)=>{if(force||document.activeElement!==node)node.value=String(value);};
  const changed=()=>{refresh(true);onChange?.();};
  const download=(bytes,name,type)=>{if(!bytes)return;const url=URL.createObjectURL(new Blob([bytes],{type}));const link=document.createElement('a');link.href=url;link.download=name;link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);};
  const queueText=(track,clock)=>{if(!track.pending)return track.running?'Playing':'Stopped';const left=Math.max(0,track.pendingTick-clock.songTick),beat=clock.ppqn*4/clock.meterDenominator;return 'Queued · '+Math.ceil(left/beat)+' beats';};
  function refresh(force=false) {
    const snapshot=model.snapshot(),track=snapshot.tracks[snapshot.selected],exists=track.exists;
    if(confirmDelete&&deleteIdentity!==snapshot.selected+':'+track.revision)confirmDelete=false;
    find('[data-readout="surface"]').textContent=snapshot.open?(snapshot.optionsOpen?'T'+(snapshot.optionsTrack+1)+' settings · LEFT steps / RIGHT input':snapshot.view==='tracks'?'OLED track list':'LEFT steps / RIGHT input'):'Live Play surface';
    find('[data-readout="transport"]').textContent=snapshot.clock.transport.toUpperCase()+' · '+(snapshot.capture.state==='off'?'Ready':snapshot.capture.state+' / T'+(snapshot.capture.track+1))+' · bar '+snapshot.clock.bar;
    find('[data-action="play"]').textContent=snapshot.clock.transport==='playing'&&!snapshot.solo?'Stop all':'Play all';
    find('[data-action="play-track"]').textContent=track.running&&snapshot.solo?'Stop track':'Play track';
    find('[data-action="record"]').textContent=['recording','finishing'].includes(snapshot.capture.state)?'Finish recording':snapshot.capture.state==='off'?'Record':'Cancel queued recording';
    for(const name of ['undo','redo'])find('[data-action="'+name+'"]').disabled=!snapshot[name==='undo'?'canUndo':'canRedo'];
    for(const name of ['play-track','record','steps','track-options','clear-step','delete-track'])find('[data-action="'+name+'"]').disabled=!exists;
    find('[data-action="clear-step"]').disabled=!exists||!snapshot.open||snapshot.view!=='steps';
    find('[data-action="create"]').disabled=snapshot.tracks.every(track=>track.exists);
    find('[data-readout="empty"]').hidden=snapshot.tracks.some(track=>track.exists);
    find('[data-editor]').hidden=!exists;
    const gridInput=field('gridTicks');
    gridInput.querySelectorAll('[data-imported-grid]').forEach(option=>{if(Number(option.value)!==snapshot.gridTicks)option.remove();});
    if(!Array.from(gridInput.options).some(option=>Number(option.value)===snapshot.gridTicks)){
      const option=seqNode(document,'option','Imported / '+snapshot.gridTicks+' ticks');option.value=snapshot.gridTicks;option.dataset.importedGrid='true';gridInput.append(option);
    }
    for(const name of ['inputLayout','gridTicks','overdub'])sync(field(name),snapshot[name],force);sync(field('stepCount'),track.stepCount,force);
    panel.querySelectorAll('[data-remote]').forEach(node=>sync(node,snapshot.remote[node.dataset.remote],force));
    const trackKey=JSON.stringify([snapshot.selected,snapshot.optionsOpen,snapshot.capture.state,snapshot.capture.track,snapshot.tracks.map(track=>[track.exists,track.count,track.running,track.pending,track.pendingTick]),Math.floor(snapshot.clock.songTick/240)]);
    if(trackKey!==lastTracks||force){
      lastTracks=trackKey;const list=find('.seq-tracks');list.replaceChildren();
      snapshot.tracks.filter(track=>track.exists).forEach(current=>{
        const button=seqNode(document,'button',undefined,'seq-track-card');button.type='button';button.dataset.track=current.index;button.disabled=snapshot.optionsOpen;button.setAttribute('aria-pressed',String(current.index===snapshot.selected));button.dataset.state=current.running?'playing':current.pending?'queued':'ready';
        const armed=snapshot.capture.state!=='off'&&snapshot.capture.track===current.index;
        button.append(seqNode(document,'strong','T'+(current.index+1)+' → '+current.count+' notes'),seqNode(document,'span',(armed?'REC · ':'')+queueText(current,snapshot.clock)));
        const progress=seqNode(document,'progress');progress.max=Math.max(1,current.loopEnd-current.loopStart);progress.value=current.running?Math.max(0,current.playheadTick-current.loopStart):0;progress.setAttribute('aria-label','Track '+(current.index+1)+' position');button.append(progress);
        button.addEventListener('click',()=>{confirmDelete=false;model.select(current.index);changed();});list.append(button);
      });
    }
    find('[data-readout="track"]').textContent='Track '+(snapshot.selected+1)+' · Ch '+(track.channel+1)+' · '+(snapshot.inputLayout==='chords'?'Chords':'Notes')+(snapshot.overdub?' · Overdub':'');
    find('[data-readout="window"]').textContent=track.stepCount+' steps · '+track.gridTicks+' ticks each';
    find('[data-readout="step"]').textContent='Step '+(snapshot.selectedStep+1)+' selected';
    find('[data-action="delete-track"]').textContent=confirmDelete?'Confirm delete track':'Delete track';
    const gridKey=JSON.stringify([snapshot.selected,snapshot.revision,snapshot.gridTicks,snapshot.selectedStep,track.stepCount]);
    if(gridKey!==lastGrid||force){
      lastGrid=gridKey;const grid=find('.seq-grid'),colors=engine.sequencer.colors();grid.replaceChildren();
      for(let step=0;step<48;step++){
        const tick=track.loopStart+step*track.gridTicks,events=snapshot.events.filter(event=>event.type==='note'&&event.tick>=tick&&event.tick<tick+track.gridTicks);
        const button=seqNode(document,'button');button.type='button';button.dataset.step=step;button.dataset.tick=tick;button.dataset.onset=Boolean(events.length);button.disabled=!exists||step>=track.stepCount;button.setAttribute('aria-pressed',String(step===snapshot.selectedStep));
        button.setAttribute('aria-label','Step '+(step+1)+': '+(events.length?events.map(event=>sequenceNote(event.pitch)).join(', '):'empty'));
        button.append(seqNode(document,'span',String(step+1),'seq-step-number'),seqNode(document,'span',events.length?sequenceNote(events[0].pitch)+(events.length>1?' +'+(events.length-1):''):'—','seq-step-notes'));
        const logical=step%12*4+Math.floor(step/12),offset=logical*4;
        if(events.length&&colors?.[offset])button.style.setProperty('--step-color',Array.from(colors.subarray(offset+1,offset+4),value=>Math.round(value*255/4095)).join(' '));
        button.addEventListener('click',()=>{confirmDelete=false;if(!model.snapshot().open||model.snapshot().view!=='steps')model.open('steps');model.configure({selectedStep:step});changed();});grid.append(button);
      }
    }
    panel.querySelectorAll('.seq-grid [data-tick]').forEach(button=>{const tick=Number(button.dataset.tick);button.dataset.playhead=track.running&&track.playheadTick>=tick&&track.playheadTick<tick+track.gridTicks;});
    find('[data-readout="count"]').textContent=snapshot.count+' / '+snapshot.capacity+' · take '+snapshot.capture.count+'/'+snapshot.takeCapacity;
    find('[data-readout="voices"]').textContent=snapshot.voices;find('[data-readout="health"]').textContent=snapshot.skipped+' / '+snapshot.failures;find('[data-readout="notice"]').textContent=snapshot.notice;
    return snapshot;
  }
  const actions={
    tracks:()=>model.open('tracks'),steps:()=>model.open('steps'),'track-options':()=>model.options(),create:()=>{confirmDelete=false;return model.create();},close:()=>model.close(),
    play:()=>model.play(),'play-track':()=>model.playTrack(),record:()=>model.record(),undo:()=>model.undo(),redo:()=>model.redo(),'clear-step':()=>model.clearStep(),
    'delete-track':()=>{if(confirmDelete){confirmDelete=false;return model.delete();}const snapshot=model.snapshot();deleteIdentity=snapshot.selected+':'+snapshot.tracks[snapshot.selected].revision;confirmDelete=true;},
    export:()=>download(model.exportSong(),'thy666-song.thys','application/octet-stream'),'midi-export':()=>download(model.exportMidi(),'thy666-song.mid','audio/midi'),
    'save-local':()=>{try{model.saveLocal(globalThis.localStorage);}catch{model.saveLocal({setItem(){throw new Error();}});}},'load-local':()=>{try{model.loadLocal(globalThis.localStorage);}catch{model.loadLocal({getItem(){throw new Error();}});}},
    'usb-on':()=>model.midi(0x90|(value('data-usb','channel')-1),value('data-usb','note'),96),'usb-off':()=>model.midi(0x80|(value('data-usb','channel')-1),value('data-usb','note'),0),
    'usb-position':()=>{const position=value('data-usb','position');model.midi(0xf2,position&127,(position>>7)&127);}
  };
  panel.querySelectorAll('[data-action]').forEach(node=>node.addEventListener('click',()=>{if(node.dataset.action!=='delete-track')confirmDelete=false;actions[node.dataset.action]?.();changed();}));
  for(const name of ['inputLayout','gridTicks','overdub','stepCount'])field(name).addEventListener('change',()=>{let value=field(name).value;if(name==='overdub')value=value==='true';else if(name!=='inputLayout')value=Number(value);model.configure({[name]:value});changed();});
  panel.querySelectorAll('[data-remote]').forEach(node=>node.addEventListener('change',()=>{const name=node.dataset.remote,value=name==='channel'?Number(node.value):node.value==='true';model.configureRemote({[name]:value});changed();}));
  panel.querySelectorAll('[data-file]').forEach(node=>node.addEventListener('change',async()=>{const file=node.files?.[0];if(!file)return;try{const bytes=new Uint8Array(await file.arrayBuffer());if(node.dataset.file==='midi')model.importMidi(bytes);else model.importSong(bytes);}catch(error){find('[data-readout="notice"]').textContent=error.message;}node.value='';changed();}));
  panel.querySelectorAll('[data-device]').forEach(button=>button.addEventListener('click',async()=>{
    panel.querySelectorAll('[data-device]').forEach(node=>node.disabled=true);
    try{
      if(button.dataset.device==='connect')await device.connect();else if(button.dataset.device==='disconnect')await device.disconnect();
      else if(button.dataset.device==='upload'){const bytes=model.exportSong();if(!engine.sequencer.validateBytes(bytes))throw new Error('Preview song failed validation.');await device.upload(bytes);}
      else{const bytes=await device.download();if(!engine.sequencer.validateBytes(bytes))throw new Error('Downloaded song failed shared C validation.');download(bytes,'thy666-device-song.thys','application/octet-stream');model.importSong(bytes);changed();}
    }catch(error){find('[data-readout="device"]').textContent=error.message;}
    finally{panel.querySelectorAll('[data-device]').forEach(node=>node.disabled=node.dataset.device==='connect'?device.connected:!device.connected);}
  }));
  refresh(true);return{model,refresh,element:panel,device};
}
