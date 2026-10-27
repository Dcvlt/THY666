/* Approved THY666 menu contract. Design rows are editable plans, never engine commands. */
const choices = values => values.map(value => typeof value === 'object' ? value : ({ value, label: String(value) }));
const row = (id, label, type, value, features, description, extra = {}) => ({
  id, label, type, default: value, feature_ids: features, description,
  status: 'design', binding: null, ...extra,
});
const toggle = (id, label, value, features, description, extra) => row(id, label, 'toggle', value, features, description, extra);
const select = (id, label, value, options, features, description, extra) => row(id, label, 'select', value, features, description, { options: choices(options), ...extra });
const number = (id, label, value, min, max, step, features, description, extra) => row(id, label, 'number', value, features, description, { min, max, step, ...extra });
const text = (id, label, value, features, description, extra) => row(id, label, 'text', value, features, description, { maxLength: 48, ...extra });
const action = (id, label, features, description, extra) => row(id, label, 'action', null, features, description, extra);
const info = (id, label, value, features, description, extra) => row(id, label, 'info', value, features, description, extra);
const live = binding => ({ status: 'live', binding });
const ON_OFF_ROUTES = [{ value: 'usb1-din', label: 'USB 1 + DIN' }, { value: 'din', label: 'DIN' }, { value: 'usb1', label: 'USB 1' }, { value: 'usb2', label: 'USB 2' }, { value: 'both-usb', label: 'USB 1 + USB 2' }, { value: 'usb2-din', label: 'USB 2 + DIN' }, { value: 'both-usb-din', label: 'USB 1 + USB 2 + DIN' }];
const DIVISIONS = ['1/1', '1/2', '1/2T', '1/4', '1/4T', '1/8', '1/8T', '1/16', '1/32'];
const CLOCK_FEATURES = ['midi.clock-source', 'midi.tempo_editor', 'midi.transport-position'];

export const STUDIO_MENU = {
  format: 'thy666.studio-menu', version: 1,
  scope: 'Approved feature set; live host-core settings and editable future firmware design',
  navigation: { select: 'Play: open Settings; Settings: return to Play and discard an uncommitted edit', up: 'Previous item / increase while editing', down: 'Next item / decrease while editing', left: 'Back / cancel edit', right: 'Enter category, edit/apply value or run action', visibleRows: 5, wrap: true },
  groups: [
    { id: 'performance', label: 'Performance', shortLabel: 'PLAY', description: 'Key, playing context and expressive pad behavior.', rows: [
      select('performance.scale', 'Scale', 0, [], ['playing.scales_performance', 'chords.classic_catalog', 'chords.extended_catalog'], 'Use the recovered lossless library; preserve original note slots and octaves.', { dynamic: 'scales', ...live('core.scale') }),
      select('performance.root', 'Root', 0, ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'].map((label, value) => ({ value, label })), ['chords.root_keys', 'controls.root-buttons'], 'Chromatic root; already held notes retain their original release ownership.', live('core.root')),
      select('performance.mode', 'Playing mode', 'play', [{value:'play',label:'Play'}, {value:'drums',label:'Drums'}, {value:'lightboard',label:'Lightboard'}, {value:'user',label:'User'}], ['playing.mode_selection', 'playing.scales_performance', 'playing.drums', 'playing.lightboard', 'playing.user_mode'], 'Design reference for future playing contexts. Use Engines to select the active workspace; chromatic and scale degrees are layouts within Play.'),
      select('performance.melodyLayout', 'Right layout', 'chromatic', [{value:'stored',label:'Library'}, {value:'condensed',label:'Scale degrees'}, {value:'in-key-lit',label:'In-key notes'}, {value:'chromatic',label:'Chromatic'}, {value:'all-lit',label:'All notes'}, {value:'guitar',label:'Guitar'}, {value:'thirds',label:'Thirds'}, {value:'fourths',label:'Fourths'}, {value:'piano',label:'Piano'}], ['chords.melody_layouts', 'playing.guitar_preset', 'playing.piano_placeholder'], 'Choose the right-side note map within Play: Chromatic uses all semitones, Scale degrees follows the selected scale, and Guitar uses standard tuning. Layout changes do not change the engine or retune held notes.', live('core.melodyLayout')),
      select('performance.guitarWindow', 'Guitar strings', '6-3', [{value:'6-3',label:'E A D G (strings 6–3)'}, {value:'5-2',label:'A D G B (strings 5–2)'}, {value:'4-1',label:'D G B E (strings 4–1)'}], ['playing.guitar_preset'], 'Standard tuning, low string first from the top; columns advance frets. The chord side is independent.', live('core.guitarWindow')),
      action('performance.guitarShortcut', 'Guitar shortcut', ['playing.guitar_preset', 'controls.top-shortcuts'], 'Select Guitar immediately; configure the four-string window above.', live('core.guitarShortcut')),
      number('performance.transpose', 'Transpose', 0, -48, 48, 1, ['playing.octave_shift'], 'Semitone offset for new presses; current owners retain their captured notes and routes.', live('core.transpose')),
      number('performance.chordOctave', 'Chord octave', 0, -3, 3, 1, ['playing.octave_shift'], 'Shift the left side in octaves for subsequent presses.', live('core.chordOctave')),
      number('performance.melodyOctave', 'Melody octave', 0, -3, 3, 1, ['playing.octave_shift'], 'Shift the right side in octaves for subsequent presses.', live('core.melodyOctave')),
      toggle('performance.keepTranspose', 'Keep transpose', true, ['settings.keep_transpose'], 'Keep explicit transpose across root/scale changes. OFF resets transpose only on a different root or scale; layout changes and chord selection preserve it.', live('core.keepTranspose')),
      toggle('performance.liveUpdate', 'Live Update', false, ['playing.live_update'], 'Future right-hand notes follow the last successfully selected chord. Scale layouts use ordered tones; chromatic/Guitar layouts quantize upward. Existing held notes keep their pitches and routes.', live('core.liveUpdate')),
      toggle('performance.liveUpdateLockScale', 'Keep key notes', true, ['playing.live_update'], 'ON rotates the configured key notes from the selected chord root. OFF uses only the selected chord tones. Release does not erase the selected harmony.', live('core.liveLockScale')),
      toggle('performance.liveUpdateMuteChord', 'Mute direct chord', false, ['playing.live_update'], 'Select harmony without a direct chord attack. Arp inputs and Pads recording still receive the chord; existing notes release normally.', live('core.liveMuteChord')),
      info('performance.liveUpdateNoteColor', 'Live Update · Note Color', 'Independent behavior unresolved', ['playing.live_update'], 'The legacy row duplicates a Lock Scale assignment; an independent musical/lighting effect is not established and is not claimed by the preview.'),
      number('performance.velocity', 'Default velocity', 96, 1, 127, 1, ['playing.expression_velocity'], 'Fallback fixed velocity for routes without their own value. MIDI routes have separate fixed velocities; Velocity mode selects Fixed or Strike for the chosen half. This fallback does not change strike calibration or aftertouch.', live('core.velocity')),
      select('performance.velocitySource', 'Velocity source', 'fixed', ['fixed', 'strike'], ['playing.expression_velocity', 'controls.velocity-presets'], 'Strike-derived velocity is approved future work; selecting it here only changes the design draft.'),
      toggle('performance.aftertouch', 'Legacy pressure default', false, ['midi.poly-aftertouch'], 'Compatibility default for inherited routes. Configure Aftertouch and Pressure bend in MIDI; each application uses pad pressure without a separate master switch.', {retired:true,...live('core.aftertouch')}),
      select('performance.pressureMode', 'Aftertouch', 'off', [{value:'off',label:'Off'},{value:'poly',label:'Poly AT'},{value:'channel',label:'Channel AT'}], ['midi.poly-aftertouch'], 'Ordinary MIDI aftertouch for Response for: Off, per-note Poly AT, or Channel AT shared by notes on that channel. Same policy as MIDI; held notes retain their captured response. MPE uses its own member pressure and optional upper stage instead.', live('midi.pressure')),
      number('performance.bendRange', 'Bend range', 2, 1, 24, 1, ['midi.pressure-pitchbend'], 'Ordinary pressure-bend range in semitones for Response for. Same value as MIDI; match the receiver range. This edit sends no setup messages and does not change the separate MPE glide range.', live('midi.bendRange')),
      toggle('performance.padOverlays', 'Context pad controls', false, ['playing.control_overlays'], 'Enable the optional context pads at the selected upper-right or lower-left position. Chord/melody octave pairs, inversion and bass octave are independently selectable. Change only with all pads released.', live('core.contextPads')),
      select('performance.userSlot', 'User shortcut', 'U1', ['U1', 'U2', 'U3'], ['chords.user_scale_slots', 'chords.modal_shortcuts', 'controls.top-shortcuts'], 'Choose a user shortcut target; store and recall are owned by Profiles & Library.'),
      number('performance.drumLayer', 'Drum layer', 1, 1, 16, 1, ['playing.drum_layers', 'playing.drums'], 'Select one of sixteen layer assignments for the drum context.'),
      info('performance.drumAssignments', 'Drum assignments', '16 pad notes per layer', ['playing.drum_layers', 'playing.drums'], 'Target editor maps drum-layer pad notes and routes; this is a design summary, not a saved instrument map.'),
    ] },
    { id: 'chords', label: 'Chords & Strum', shortLabel: 'CHORD', description: 'Voicing, enabled notes and two independently configured strummers.', rows: [
      select('chords.layout', 'Chord layout', 'classic', ['classic', 'chromatic', 'diatonic', 'six-default', 'six-user','user-chords'], ['chords.chord_layouts'], 'Classic preserves the library. Generated rows add tones. Six chords places six qualities over eight scale roots. User chords maps 24 saved records across the upper two rows, with lower-octave copies below.',live('core.chordLayout')),
      select('chords.quality','Chord family','major',['major','minor','dominant','diminished','augmented','sus2','sus4'],['chords.chord_layouts'],'Quality family for generated Chromatic chords. Classic and Diatonic retain their own tones.',live('core.chordQuality')),
      select('chords.userScale','Custom scale',0,[{value:0,label:'Built-in'},...Array.from({length:8},(_,n)=>({value:n+1,label:`Scale ${n+1}`}))],['chords.user_scale_slots'],'Use a saved custom scale for future generated chords and melody layouts. Selecting a built-in scale returns to that library. Held notes retain their original assignment.',live('library.userScale')),
      select('chords.sixBank','Six-chord set',0,[{value:0,label:'Set 1'},{value:1,label:'Set 2'},{value:2,label:'Set 3'}],['chords.chord_layouts'],'Named user set for Six User. Each of its six columns references one saved chord template.',live('library.sixBank')),
      action('chords.editScales','Edit scales',['storage.user-scale-records'],'Browse eight named scale slots, edit degrees and names, then explicitly Save or Cancel.',live('library.editScales')),
      action('chords.editChords','Edit chords',['chords.chord_layouts'],'Browse 24 named seven-slot chords. Copy the selected base chord or edit note values, root and name. MIDI zero is valid.',live('library.editChords')),
      action('chords.editSix','Edit six sets',['chords.chord_layouts'],'Assign six saved chord templates to each named set. Empty columns remain silent.',live('library.editSix')),
      select('chords.voicingMode','Voicing','inversions',['inversions','drop-voices','auto-leading','octave-fold','guitar-register'],['chords.voicing_swap_1','chords.voicing_swap_2','chords.voicing_swap_3'],'Inversions preserves the current I/II/III behavior. Drop voices makes them independent lower-octave toggles for the second, third and fourth ascending voices. Auto leading chooses nearby octave placements from the last successful chord before Spread, bass octave and octave copies. Octave fold compacts the source into one octave. Guitar register uses standard EADGBE string pitches; seven distinct pitch classes remain unchanged. I/II/III are inactive in these three automatic modes. All changes affect future chords.',live('core.voicingMode')),
      toggle('chords.drop2','Lower voice 2',false,['chords.voicing_swap_1'],'Lower the second distinct enabled tone one octave in Drop voices mode. MIDI zero and original slot ordering remain valid.',live('core.drop2')),
      toggle('chords.drop3','Lower voice 3',false,['chords.voicing_swap_2'],'Lower the third distinct enabled tone one octave in Drop voices mode.',live('core.drop3')),
      toggle('chords.drop4','Lower voice 4',false,['chords.voicing_swap_3'],'Lower the fourth distinct enabled tone one octave in Drop voices mode. Chords with fewer voices are unaffected.',live('core.drop4')),
      number('chords.inversion', 'Inversion', 0, 0, 3, 1, ['controls.chord-functions'], 'Raise the original prefix by one octave in Inversions mode. The separate Drop voices mode uses independent substitutions.', live('core.inversion')),
      number('chords.spread', 'Spread', 0, 0, 3, 1, ['chords.octave_stack_12', 'chords.octave_stack_24', 'chords.octave_stack_36', 'controls.chord-functions'], 'Current core spreading operation; keyboard shortcuts retain their physical position.', live('core.spread')),
      number('chords.octaveStacks', 'Upper stacks', 0, 0, 3, 1, ['chords.octave_stack_12', 'chords.octave_stack_24', 'chords.octave_stack_36'], 'Extra upward copies in Stored notes mode. Octave layers mode uses the seven lower buttons instead, so the two systems never multiply.', live('core.octaveStacks')),
      toggle('chords.strum', 'Strum', false, ['chords.strummer'], 'Enable the existing staggered chord-attack engine.', live('core.strum')),
      number('chords.strumDelay', 'Strum delay · ms', 24, 1, 1000, 1, ['chords.strum_delay'], 'Delay between chord-tone attacks, in milliseconds.', live('core.strumDelay')),
      select('chords.strumDirection', 'Strum order', 0, [{value:0,label:'Stored'}, {value:1,label:'Low to high'}, {value:2,label:'High to low'}, {value:3,label:'Alternating'}], ['chords.strum_direction'], 'Ordering uses the current core, with queued attacks canceled correctly on release.', live('core.strumDirection')),
      select('chords.strummerTarget', 'Edit strummer', 1, [1, 2], ['chords.strummer', 'chords.strum_route'], 'Select one target configuration. Arp-to-Strum links identify their destination explicitly.'),
      toggle('chords.continuous', 'Continuous strum', false, ['chords.strum_continuous'], 'Target repeat engine sustains reattacks while the source remains active.'),
      toggle('chords.strumLatch', 'Strum latch', false, ['chords.strum_latched'], 'Target toggle latch retains rhythmic ownership until explicitly toggled off or stopped.'),
      select('chords.cutoff', 'Strum cutoff', '1/8', DIVISIONS, ['chords.strum_cutoff'], 'Target note gate is a named clock division, independent of attack delay.'),
      select('chords.repeatDelay', 'Repeat division', '1/4', ['1/32','1/16','1/8','1/4','1/2','1/1','2/1','4/1'], ['chords.strum_repeat_delay'], 'Proposed repeat interval follows the shared musical clock.'),
      info('chords.pattern', 'Strum pattern', 'Behavior contract pending', ['chords.strum_pattern'], 'Approved pattern intention needs an explicit pattern contract; no legacy flag is presented as a working engine.'),
      select('chords.destinations', 'Strummer output', 'usb1-din', ON_OFF_ROUTES, ['chords.strum_route'], 'Strummer route is independent of pad routing and captured when each note is scheduled.'),
      number('chords.channel', 'Strummer channel', 1, 1, 16, 1, ['chords.strum_route'], 'Human-facing channels are 1–16; the transport converts to zero-based wire channels.'),
      action('chords.guidance', 'Harmony guidance', ['chords.harmony'], 'Read-only chord suggestions, scale discovery and modulation on the board. Capture held notes explicitly; navigation never plays or applies a key.', live('chords.guidance')),
      info('chords.ownership', 'Held-note contract', 'Press-time snapshot', ['chords.held_snapshots'], 'Changing the key, voicing, routing or menu never loses the original release destination.'),
    ] },
    { id: 'arps', label: 'Arpeggiators', shortLabel: 'ARPS', description: 'Two instances use the same editor and independent configuration and owners.', rows: [
      action('arps.open', 'Open Arps', ['arpeggiators.two_instances'], 'Open the working two-Arp quick page. Its temporary M-key controls operate only while the page is open.', live('arps.open')),
      select('arps.target', 'Edit Arp', 1, [1, 2], ['arpeggiators.two_instances'], 'Select the independently configured Arp to edit.', live('arps.target')),
      toggle('arps.quickKeys', 'Quick M controls', true, ['arpeggiators.two_instances', 'controls.top-shortcuts'], 'Only on the Arps quick page: M1/M2 enable, M3/M4 explain Hold-only, M5/M6 clear, M7 selects the editor target. Ordinary M-key behavior returns on leaving the page.', live('arps.quickKeys')),
      toggle('arps.enabled', 'Enabled', false, ['arpeggiators.two_instances'], 'Disable releases only this Arp’s generated owners.', live('arps.enabled')),
      info('arps.trigger', 'Trigger', 'Hold only; Latch unavailable', ['arpeggiators.trigger_latch'], 'The current engine follows held input. Latch gestures are future work.', live('arps.trigger')),
      select('arps.type', 'Type', 'notes', [{value:'notes',label:'Notes'},{value:'chord',label:'Chord'}], ['arpeggiators.two_instances'], 'Notes orders pitches; Chord applies scale-degree offsets to the captured chord.', live('arps.type')),
      select('arps.grouping', 'Grouping', 'hybrid', [{value:'hybrid',label:'Hybrid'},{value:'combined',label:'Combined'},{value:'each',label:'Each'}], ['arpeggiators.two_instances'], 'Hybrid keeps chord phrases distinct while single notes combine. Combined merges all inputs; Each keeps pad contributions separate.', live('arps.grouping')),
      number('arps.sourceZones', 'Source zones mask', 63, 0, 63, 1, ['arpeggiators.two_instances'], 'Six bits, left three zones then right three zones. Zero admits no new pads; existing admitted contacts still release.', live('arps.sourceZones')),
      ...Array.from({length:6},(_, i)=>toggle(`arps.sourceZone${i+1}`,`Source ${i<3?'left':'right'} ${i%3+1}`,true,['arpeggiators.two_instances'],'Admission for this four-column zone applies to new presses only.',live(`arps.sourceZone${i+1}`))),
      toggle('arps.layer', 'Layer dry notes', false, ['arpeggiators.two_instances'], 'Also play the original pad attack. The choice is captured when that pad is pressed.', live('arps.layer')),
      select('arps.noteValue', 'Note division', '1/16', [...DIVISIONS, {value:'custom',label:'Custom ticks'}], ['arpeggiators.note_value'], 'Shared musical ticks: one quarter note is 960 ticks. Custom values are edited with Rate ticks.', live('arps.noteValue')),
      number('arps.rateTicks', 'Rate ticks', 240, 40, 3840, 1, ['arpeggiators.note_value'], 'Exact rate, including custom divisions, at 960 ticks per quarter.', live('arps.rateTicks')),
      select('arps.sync', 'Start alignment', 'immediate', [{value:'immediate',label:'Immediate'},{value:'grid',label:'Grid'}], ['arpeggiators.sync'], 'Launch immediately or on the rate grid.', live('arps.sync')),
      select('arps.restart', 'Input changes', 'continue', [{value:'continue',label:'Continue'},{value:'restart',label:'Restart'}], ['arpeggiators.sync'], 'Preserve surviving phase, or restart after admitted input changes.', live('arps.restart')),
      select('arps.pattern', 'Notes pattern', 'up', [{value:'up',label:'Up'},{value:'down',label:'Down'},{value:'up-down',label:'Up / Down'},{value:'down-up',label:'Down / Up'},{value:'as-played',label:'As Played'}], ['arpeggiators.patterns'], 'Five implemented pitch-order patterns; no unverified legacy pattern table is implied.', live('arps.pattern')),
      ...Array.from({length:8}, (_, i) => toggle(`arps.step${i + 1}`, `Step ${i + 1}`, true, ['arpeggiators.step_enables'], 'Enable this step; disabled steps consume their timing position.',live(`arps.step${i+1}`))),
      ...Array.from({length:8}, (_, i) => number(`arps.offset${i + 1}`, `Chord offset ${i + 1}`, 0,-24,24,1,['arpeggiators.patterns'],'Signed scale-degree offset for this Chord step.',live(`arps.offset${i+1}`))),
      number('arps.gate', 'Gate %', 50, 1,100,1, ['arpeggiators.sustain'], 'Generated note gate as a percentage of the step interval.',live('arps.gate')),
      number('arps.swing', 'Swing %', 0, 0, 50, 1, ['arpeggiators.swing'], 'Paired-step timing offset preserves the pair duration.', live('arps.swing')),
      number('arps.releaseTail', 'Release tail · periods', 0, 0, 8, 1, ['arpeggiators.length'], 'Define Length as post-release service time, not an assumed pattern step count.'),
      toggle('arps.keepKey', 'Keep Key', false, ['arpeggiators.keep_key'], 'Cache the launch root while inactive; active launches take an explicit root snapshot.'),
      select('arps.toStrum', 'Arp to Strum', 'off', ['off','strummer-1','strummer-2'], ['arpeggiators.to_strum'], 'Explicit one-way processor link; reject feedback loops in the engine graph.'),
      select('arps.destinations', 'Arp output', 'usb1-din', ON_OFF_ROUTES, ['arpeggiators.output_route'], 'Generated owners retain their captured output route.', live('arps.destinations')),
      number('arps.channel', 'Arp channel', 3, 1, 16, 1, ['arpeggiators.output_route'], 'Generated MIDI channel, independent from physical pad routes.', live('arps.channel')),
      action('arps.clear', 'Clear selected Arp', ['arpeggiators.two_instances'], 'Release and forget only the selected Arp’s admitted inputs; preserve its settings.', live('arps.clear')),
    ] },
    { id: 'sequencer', label: 'Sequencer', shortLabel: 'SEQ', description: 'Twenty-one tracks, a song view and deliberate recording and transport ownership.', rows: [
      number('sequencer.track', 'Selected track', 1, 1, 21, 1, ['sequencers.tracks', 'sequencers.performance_controls'], 'Three pages of seven tracks map coherently to M1–M7; retain each track cursor and edit state.'),
      toggle('sequencer.active', 'Track active', true, ['sequencers.active_route'], 'Muting a track releases its owned notes; other tracks and playing pads keep their owners.'),
      select('sequencer.interaction', 'Pad interaction', 'live-play', ['live-play','program','record','clean-dub'], ['sequencers.track_interaction_mode', 'sequencers.record_capture'], 'Program means track programming, separate from MIDI Program Change assignments.'),
      select('sequencer.playMode', 'Playback mode', 'loop', ['master','loop','unlatched','snapback','slice'], ['sequencers.play_modes'], 'Approved mode set; launch/release/wrap transitions require the documented state-machine contract.'),
      info('sequencer.layout', 'Track pad layout', 'Performance grid', ['sequencers.layouts'], 'Proposed named layout editor must expose meaningful mappings rather than legacy numeric indices.'),
      select('sequencer.quantize', 'Record quantize', '1/16', ['off','1/4','1/8','1/16'], ['sequencers.quantization', 'sequencers.record_capture'], 'Proposed grid options; preserve paired note durations when quantizing capture.'),
      toggle('sequencer.followScale', 'Follow root / scale', false, ['sequencers.root_scale'], 'Opt in to key changes at a defined scheduling boundary; already emitted notes retain their original releases.'),
      number('sequencer.root', 'Track root', 0, 0, 11, 1, ['sequencers.root_scale'], 'Independent track key used when Follow root / scale is disabled.'),
      select('sequencer.scale', 'Track scale', 0, [], ['sequencers.root_scale'], 'Independent selected-track scale used when Follow root / scale is disabled; changing this design draft does not run sequencer playback.', { dynamic: 'scales' }),
      text('sequencer.name', 'Track name', 'Track 1', ['sequencers.name_presets'], 'Target text/name-preset editor; names are stored with the song rather than firmware global state.'),
      number('sequencer.position', 'Position cue', 0, 0, 31, 1, ['sequencers.position_swapcut'], 'Target cue index; an engine contract must define the cue-to-tick map before playback implementation.'),
      info('sequencer.swapCut', 'Swap / Cut policy', 'Transition contract pending', ['sequencers.position_swapcut'], 'Retained intent receives a new explicit boundary/release policy; legacy incomplete rows are not reused.'),
      number('sequencer.latency', 'Capture offset · ms', 0, -100, 100, 1, ['sequencers.latency'], 'Proposed recording compensation in milliseconds.'),
      select('sequencer.destinations', 'Track output', 'usb1-din', ON_OFF_ROUTES, ['sequencers.active_route'], 'Each track has its own captured channel/destination policy.'),
      number('sequencer.channel', 'Track channel', 1, 1, 16, 1, ['sequencers.active_route'], 'Selected-track MIDI output channel, 1–16.'),
      toggle('sequencer.songMix', 'Song Mix view', false, ['sequencers.song_mix', 'display.track-song-overview'], 'Target overview shows track activity, selected page, record arm and transport.'),
      toggle('sequencer.followTempo', 'Follow song tempo', true, ['sequencers.follow_tempo'], 'Song timing follows the chosen shared clock policy; external clock remains independently configured.'),
      toggle('sequencer.recordControllers', 'Record CC / Program Change', true, ['sequencers.controller_messages'], 'Target controller lane records supported events with explicit routes and timing.'),
      toggle('sequencer.sendPressure', 'Send recorded pressure', false, ['sequencers.note_cut_pressure'], 'Opt in to captured pressure playback using the track expression policy.'),
      select('sequencer.noteCut', 'Song note cutoff', '1/8', DIVISIONS, ['sequencers.note_cut_pressure'], 'Named gate division for Song Mix note cutoff.'),
      action('sequencer.play', 'Play / Continue', ['sequencers.transport'], 'Design-only transport action until the scheduler is implemented; no MIDI emitted from this preview.'),
      action('sequencer.stop', 'Stop', ['sequencers.transport'], 'Future transport stop releases sequencer owners and pending events; local playing owners remain independent.'),
      action('sequencer.clearTrack', 'Clear selected track', ['sequencers.clear'], 'Requires confirmation and a target track; does not clear the entire song.', { confirm: true }),
      action('sequencer.clearSong', 'Clear all song tracks', ['sequencers.clear', 'sequencers.song_mix'], 'Requires a separate explicit confirmation; current preview records no destructive action.', { confirm: true }),
      action('sequencer.save', 'Save song', ['sequencers.save_recall'], 'Target persistent save uses a versioned song format; draft action does not write storage.'),
      action('sequencer.recall', 'Recall song', ['sequencers.save_recall'], 'Recall validates a song before changing active state and releases the replaced song owners.'),
      action('sequencer.importMidi', 'Import MIDI file', ['sequencers.midi_files'], 'Target host import needs format, duration, event and track limits; no file is imported by this action.'),
    ] },
    { id: 'midi', label: 'MIDI & Routing', shortLabel: 'MIDI', description: 'Routes, shared clock, USB input filters and configurable controller assignments.', rows: [
      select('midi.routingProfile', 'Pad routing profile', 'split-sides', [{value:'whole-board',label:'Whole Board'}, {value:'split-sides',label:'Split Sides'}, {value:'six-zones',label:'Six Zones · advanced'}], ['midi.bank-route-modes'], 'Live routing for new attacks: default Split Sides, one Whole Board route, or six four-column by four-row zones.', live('midi.routingProfile')),
      select('midi.routeTarget', 'Edit pad route', 'left', ['whole-board','left','right','left-1','left-2','left-3','right-1','right-2','right-3'], ['midi.bank-route-modes'], 'Select Left or Right in Split Sides; Whole Board has one target, Six Zones has six four-column targets.', live('midi.routeTarget')),
      number('midi.channel', 'Route channel', 1, 1, 16, 1, ['midi.bank-route-modes', 'midi.output-destinations'], 'Live channel 1–16 for the selected side or zone. Held notes and queued strums keep their original route until release.', live('midi.channel')),
      select('midi.destinations', 'Route destinations', 'usb1-din', ON_OFF_ROUTES, ['midi.output-destinations', 'midi.din-output', 'midi.usb-personality'], 'Live DIN and USB cable selection for the selected side or zone. Existing owners release through their captured destinations.', live('midi.destinations')),
      number('midi.velocity', 'Route velocity', 96, 1, 127, 1, ['midi.bank-route-modes', 'controls.velocity-presets'], 'Fixed attack velocity for the selected side or zone. New attacks only; this does not enable an unfinished strike-velocity curve.', live('midi.velocity')),
      select('midi.pressure', 'Aftertouch', 'off', [{value:'off',label:'Off'},{value:'poly',label:'Poly'},{value:'channel',label:'Channel'}], ['midi.bank-route-modes', 'midi.poly-aftertouch'], 'Pressure output for this route. Poly merges shared pitches; Channel uses the strongest held-pad pressure on the captured channel. New attacks retain their chosen mode until release.', live('midi.pressure')),
      select('midi.pressureBend', 'Pressure bend', 'off', [{value:'off',label:'Off'},{value:'up',label:'Up'},{value:'down',label:'Down'}], ['midi.pressure-pitchbend'], 'Map pressure to channel pitch bend for this route. The newest participating held pad controls a shared channel; releasing it returns control to the previous held pad. Captured channels and outputs remain stable.', live('midi.pressureBend')),
      number('midi.bendRange', 'Bend range', 2, 1, 24, 1, ['midi.pressure-pitchbend'], 'Pressure bend range in semitones. Match the receiver pitch-bend sensitivity; Send bend setup requests that range explicitly without changing the stored MIDI route.', live('midi.bendRange')),
      action('midi.bendSetup', 'Send bend setup', ['midi.pressure-pitchbend'], 'Explicitly send Pitch Bend Sensitivity (RPN 0) on the selected route. This changes compatible receiver configuration. Release pads and sustain first; disabled while MPE is enabled. No setup is sent at boot or on preference edits.', live('midi.bendSetup')),
      select('midi.clock', 'Clock source', 'internal', ['internal','usb'], CLOCK_FEATURES, 'Internal or USB external clock. USB loss waits without silently falling back.', live('clock.source')),
      number('midi.tempo', 'Internal BPM', 120, 20, 300, 1, ['midi.tempo_editor'], 'Internal tempo remains independently editable while following USB.', live('clock.tempo')),
      number('midi.meterNumerator','Meter beats',4,1,16,1,CLOCK_FEATURES,'Numerator for beat and bar launch boundaries.',live('clock.numerator')),
      select('midi.meterDenominator','Meter unit',4,[1,2,4,8,16,32],CLOCK_FEATURES,'Meter denominator; editing does not reset musical phase.',live('clock.denominator')),
      toggle('midi.receiveTransport', 'USB transport / position', false, ['midi.transport-position'], 'Separate opt-in consumer for Start, Continue, Stop and Song Position Pointer.'),
      toggle('midi.sendClock', 'Transmit clock / transport', false, ['midi.clock-source', 'midi.transport-position'], 'Avoid routed MIDI loops; configure clock emission independently of musical note routes.'),
      toggle('midi.remoteRecord', 'USB recording', false, ['midi.usb-remote-sequence'], 'OFF by default. Incoming notes record only into the explicitly armed USB-input take, after the input channel filter.',live('sequencer.remoteRecord')),
      toggle('midi.remotePosition', 'USB position', false, ['midi.usb-remote-sequence'], 'OFF by default. Allow incoming MIDI Song Position to seek the sequencer; this does not turn ordinary incoming notes into chords.',live('sequencer.remotePosition')),
      select('midi.remoteSource', 'Remote source', 'usb', ['usb'], ['midi.usb-remote-sequence'], 'Only the existing USB MIDI receive path is selected; no DIN receive feature is implied.'),
      number('midi.remoteCable', 'Remote input cable', 1, 1, 1, 1, ['midi.usb-remote-sequence', 'midi.usb-personality'], 'The current USB descriptor offers one incoming cable, displayed as cable 1.'),
      number('midi.remoteChannel', 'USB input channel', 0, 0, 16, 1, ['midi.usb-remote-sequence'], '0 admits any channel; 1–16 select one channel before recording. Recording and position have separate permissions.',live('sequencer.remoteChannel')),
      number('midi.remoteLow', 'Remote lowest note', 36, 0, 127, 1, ['midi.usb-remote-sequence'], 'Inclusive lower filter; must be less than or equal to the upper bound.'),
      number('midi.remoteHigh', 'Remote highest note', 67, 0, 127, 1, ['midi.usb-remote-sequence'], 'Inclusive upper filter. Position mapping must reject cues outside its explicit map.'),
      info('midi.remoteEcho', 'Remote echo policy', 'No automatic echo / THRU', ['midi.usb-remote-sequence'], 'An accepted input command is consumed once. MIDI clock and remote-note input are separate subscriptions.'),
      select('midi.assignmentKind', 'Custom message', 'cc', ['cc','program-change'], ['midi.packet-capabilities-boundary'], 'Add named assignments for external gear; no custom message is emitted by these draft controls.'),
      text('midi.assignmentControl', 'Assignment control', 'Unassigned', ['midi.packet-capabilities-boundary', 'controls.chord-functions', 'controls.top-shortcuts'], 'Target binding editor selects an eligible control and requires an explicit override of its default function.'),
      select('midi.assignmentTrigger', 'Assignment trigger', 'press', ['press','press-release','toggle'], ['midi.packet-capabilities-boundary'], 'Proposed trigger policies; Program Change uses press only, CC may use release/toggle values.'),
      number('midi.assignmentNumber', 'CC number / Program', 0, 0, 127, 1, ['midi.packet-capabilities-boundary'], 'Raw MIDI values 0–127. CC64 participates in shared sustain ownership.'),
      number('midi.assignmentOn', 'CC active value', 127, 0, 127, 1, ['midi.packet-capabilities-boundary'], 'Active CC value for the selected trigger policy; unused by Program Change.'),
      number('midi.assignmentOff', 'CC release value', 0, 0, 127, 1, ['midi.packet-capabilities-boundary'], 'Release/toggle-off CC value; unrelated channel-wide cleanup messages are never substituted.'),
      number('midi.assignmentChannel', 'Assignment channel', 1, 1, 16, 1, ['midi.packet-capabilities-boundary'], 'Each custom assignment has an explicit channel and destination snapshot.'),
      select('midi.assignmentOutput', 'Assignment output', 'din', ON_OFF_ROUTES, ['midi.packet-capabilities-boundary'], 'Route external-gear control through the shared transport layer.'),
      toggle('midi.pedalEnabled', 'Pedal enabled', false, ['midi.pedal-polarity'], 'Pedal bridge is target work; current host preview does not read a physical pedal.'),
      select('midi.pedalPolarity', 'Pedal polarity', 'normal', ['normal','inverted'], ['midi.pedal-polarity'], 'Apply a changed polarity only after qualifying the current switch state.'),
      select('midi.pedalAction', 'Pedal action', 'sustain', ['sustain','cc','start','stop','continue'], ['midi.pedal-cc', 'midi.pedal-transport'], 'No dedicated pedal-note trigger. Sustain uses the same CC64 ownership as other sustain sources.'),
      number('midi.pedalCc', 'Pedal CC', 64, 0, 127, 1, ['midi.pedal-cc'], 'Controller number for the CC action; CC64 is interpreted as sustain.'),
      select('midi.pedalOutput', 'Pedal output', 'din', ON_OFF_ROUTES, ['midi.pedal-cc', 'midi.pedal-transport'], 'Snapshot the pedal route on activation so release cleans up the original route.'),
      number('midi.pedalChannel', 'Pedal channel', 1, 1, 16, 1, ['midi.pedal-cc'], 'Pedal channel uses the same 1–16 editor as pad and processor routes.'),
    ] },
    { id: 'sensitivity', label: 'Sensitivity', shortLabel: 'SENSE', description: 'One coherent response model, with inherited global defaults and per-pad overrides.', rows: [
      toggle('sensitivity.profileEnabled','Aftertouch stage',false,['midi.poly-aftertouch','midi.pressure-pitchbend'],'Optional upper aftertouch stage. At AT start 70%, the lower 70% maps to full 0–127 pressure; the upper 30% maps to full aftertouch while pressure stays at 127. MPE uses Channel Pressure then CC74. OFF keeps full-range pressure. Edit only while released.',live('expression.profile.enabled')),
      number('sensitivity.profileSplit','AT start %',70,10,90,1,['midi.poly-aftertouch','midi.pressure-pitchbend'],'Percentage of the normalized sensor range where pressure reaches maximum and aftertouch begins. This is a response split, not measured force. Leave headroom for both stages.',live('expression.profile.split')),
      select('sensitivity.pressureCurve','Pressure curve',0,[{value:0,label:'Linear'},{value:1,label:'Soft'},{value:2,label:'Firm'}],['midi.poly-aftertouch'],'Curve for the pressure stage before AT start. Pressure and aftertouch share one sensor measurement and have separate response curves.',live('expression.profile.pressureCurve')),
      select('sensitivity.aftertouchCurve','AT curve',0,[{value:0,label:'Linear'},{value:1,label:'Soft'},{value:2,label:'Firm'}],['midi.poly-aftertouch'],'Curve for the aftertouch stage beyond AT start. The MPE expression adapter maps this additional stage separately from pressure.',live('expression.profile.aftertouchCurve')),
      number('sensitivity.centerResponse', 'Center response ms', 8, 8, 80, 2, ['controls.navigation', 'controls.chord-functions'], 'Center touch status polling only. Hardware thresholds are unchanged; top controls use 20 ms.', { shortLabel: 'CENTER POLL', ...live('controls.centerPoll') }),
      info('sensitivity.touchConfig', 'Touch configuration', 'Read-only USB capture', ['controls.navigation', 'controls.chord-functions'], 'USB console touch capture 15 preserves all 128 bytes and controller identities without writing sensor configuration.'),
      info('sensitivity.scope', 'Menu edit scope', 'Global defaults', ['controls.noise-floor', 'controls.pad-acquisition'], 'These live menu controls edit global defaults atomically; the Sensor Lab below handles per-pad overrides for the shimmed pads.'),
      number('sensitivity.attack', 'Trigger · ADC', 1377, 1, 4095, 1, ['controls.noise-floor', 'controls.pad-acquisition'], 'Two fresh samples qualify attack. Keep floor ≤ release < trigger ≤ ceiling.', live('response.attack')),
      number('sensitivity.release', 'Release · ADC', 1354, 0, 4094, 1, ['controls.noise-floor'], 'Hysteresis release is independent of the trigger; response edits require released pads.', live('response.release')),
      number('sensitivity.floor', 'Pressure floor', 1230, 0, 4094, 1, ['controls.noise-floor'], 'Continuous pressure zero point, separate from strike velocity estimation.', live('response.floor')),
      number('sensitivity.ceiling', 'Pressure ceiling', 4095, 1, 4095, 1, ['controls.noise-floor'], 'ADC level mapped to maximum pressure. A pressure curve is not a velocity calibration.', live('response.ceiling')),
      select('sensitivity.curve', 'Legacy AT curve', 0, [{value:0,label:'Linear'},{value:1,label:'Soft'},{value:2,label:'Firm'}], ['controls.noise-floor', 'controls.velocity-presets', 'display.sensitivity-curve-view'], 'Current pressure-normalization curve; preview the graph in the Sensor Lab.', live('response.curve')),
      select('sensitivity.velocityPreset', 'Strike curve preset', 'linear', ['linear','soft','firm','custom'], ['controls.velocity-presets', 'controls.custom-velocity-curve'], 'Target strike-velocity response presets; current attack remains fixed velocity.'),
      info('sensitivity.customCurve', 'Custom strike curve', 'Four ordered regions', ['controls.custom-velocity-curve'], 'Target editor exposes bounded type, power, base, offset and slope per region with continuity validation.'),
      action('sensitivity.reset', 'Reset selected response', ['controls.curve-reset'], 'Target reset makes scope explicit and confirms overrides; the working Sensor Lab offers a separate current reset.'),
      info('sensitivity.drift', 'Baseline policy', 'Bounded quiet-only adaptation', ['controls.noise-floor', 'controls.pad-acquisition'], 'Design constraint: do not absorb sustained touches into the baseline. Automatic adaptation still needs a specified bounded algorithm.'),
    ] },
    { id: 'lights', label: 'Lights & Display', shortLabel: 'LIGHT', description: 'Note-colored pads, held feedback and the real monochrome OLED viewport.', rows: [
      toggle('lights.enabled', 'Note lights', true, ['lights.note-palette'], 'Use the fixed note palette; local lighting has no legacy palette SysEx dependency.', live('lighting.enabled')),
      number('lights.idle', 'Resting brightness', 1, 0, 8, 1, ['lights.brightness-banks'], 'Current shared-runtime resting note intensity.', live('lighting.idle')),
      number('lights.held', 'Held brightness', 8, 0, 8, 1, ['lights.brightness-banks', 'lights.held-feedback'], 'Current shared-runtime held intensity returns to resting color after release.', live('lighting.held')),
      select('lights.chordColor', 'Chord color source', 'root', [{value:'root',label:'Harmonic root'},{value:'bass',label:'Bass note'},{value:'degree',label:'Scale degree I-VII'}], ['lights.note-palette'], 'Root uses the recovered archive degree reference without altering voicings; Bass and fixed degree colors are optional policies.', live('lighting.chordMode')),
      select('lights.noteColors', 'Note colors', 'classic', [{value:'classic',label:'Classic'},{value:'distinct',label:'Distinct 12'}], ['lights.note-palette'], 'Classic preserves reference hues; Distinct 12 offers twelve colors at idle brightness 3.', live('lighting.palette')),
      toggle('lights.keyContrast', 'Black-key contrast', true, ['lights.note-palette'], 'Dim actual sharp/flat pitch classes independently of grid column, preserving distinction between similar Classic hues.', live('lighting.keyContrast')),
      select('lights.heldStyle', 'Held highlight', 'note-color', [{value:'note-color',label:'Note color'},{value:'white',label:'White'}], ['lights.held-feedback'], 'Held pads brighten in their captured note color or white; release restores the resting color.', live('lighting.heldStyle')),
      number('lights.fade', 'Release fade · ms', 100, 0, 500, 10, ['lights.live-fade', 'lights.held-feedback'], 'Visual fade never postpones MIDI note release.', live('lighting.fade')),
      toggle('lights.heldFeedback', 'Held feedback', true, ['lights.held-feedback'], 'Enable held brightening independently of resting note colors.', live('lighting.heldFeedback')),
      number('lights.center', 'Center brightness', 8, 0, 8, 1, ['lights.center-backlights', 'lights.control-state-indicators'], 'Center state indicators remain separate from acquisition and the 96-pad RGB stream.'),
      toggle('lights.exploder', 'Led Exploder', false, ['lights.led-exploder'], 'Target pressure-dependent diamond overlay with an independent enable switch.'),
      info('lights.exploderPolicy', 'Exploder color policy', 'Note-based', ['lights.led-exploder'], 'Target named effect policies need a fixed tested palette; no custom color editor is included.'),
      number('lights.effectFade', 'Effect decay · ms', 200, 0, 2000, 10, ['lights.live-fade', 'lights.led-exploder'], 'Target overlay decay returns toward underlying note colors independently of held-note fade.'),
      select('lights.view', 'Play overview', 'notes', ['notes','piano','layouts','routes','tracks'], ['display.note-chord-view', 'display.mode-layout-overview', 'display.route-overview', 'display.track-song-overview', 'playing.piano_placeholder'], 'Notes, Piano, Layouts and Routes are working C views. Tracks remains unavailable until its engine exists.', live('display.view')),
      number('lights.contrast', 'OLED contrast', 127, 0, 255, 1, ['display.contrast'], 'Device contrast register setting; browser canvas previews pixels rather than panel electrical contrast.'),
      toggle('lights.animations', 'UI animations', true, ['display.menu-navigation', 'settings.menu-navigation'], '280 ms eased hierarchy transitions advance with completed frames; cursor and edit changes stay immediate.', live('display.animations')),
      info('lights.oled', 'Physical viewport', '128 × 64 monochrome', ['display.oled-framebuffer', 'display.pickers-editors'], 'Keep the hardware-resolution preview; the separate concept display remains an optional design view.'),
    ] },
    { id: 'storage', label: 'Profiles & Library', shortLabel: 'FILES', description: 'THY666-only versioned data; browser drafts are distinct from device persistence.', rows: [
      text('storage.profileName', 'Profile name', 'My TheoryBoard', ['storage.format-migration', 'storage.snapshot-slots'], 'Target profile groups validated configuration plus per-pad overrides using a versioned THY666 schema.'),
      action('storage.saveProfile', 'Save profile', ['storage.save-all', 'storage.snapshot-slots', 'storage.format-migration'], 'Future device save must be atomic and checksummed; this design action does not write external flash.'),
      action('storage.loadProfile', 'Load profile', ['storage.reload', 'storage.snapshot-slots'], 'Stage and validate before committing; release replaced owners and preserve profile-independent library assets.'),
      action('storage.exportProfile', 'Export profile', ['storage.format-migration'], 'THY666 exports include schema version and content identity.'),
      action('storage.importProfile', 'Import profile', ['storage.format-migration'], 'Validate bounds and supported schema migrations before activation. This action opens no file in the preview.'),
      action('storage.deleteProfile', 'Delete profile', ['storage.snapshot-slots'], 'Explicit target and confirmation, with active profile ownership handled independently.', { confirm: true }),
      action('storage.resetSettings', 'Reset settings', ['storage.reset-settings'], 'Separate runtime defaults from persistent reset and library deletion; require explicit scope.', { confirm: true }),
      select('storage.userSlot', 'User scale slot', 'U1', ['U1','U2','U3'], ['storage.user-scale-records', 'chords.user_scale_slots'], 'Assign named scale records to the retained physical U shortcuts.'),
      info('storage.userScale', 'Custom library', 'Chords: Edit scales / chords / sets', ['storage.user-scale-records'], 'Eight named scales, 24 seven-slot chords and three six-chord sets are saved with settings. Edit them in Chords or the local Custom library panel; THYL files export/import the local library.'),
      action('storage.library', 'Manage chord library', ['storage.chord-flash-library', 'chords.classic_catalog', 'chords.extended_catalog'], 'Host-library workflow preserves exact notes, octaves, pad positions and source color IDs.'),
      action('storage.usbDisk', 'USB disk mode', ['storage.usb-disk', 'midi.usb-personality'], 'Future mode switch quiesces MIDI/storage owners; exclusive host disk ownership and validated bounds are required.', { confirm: true }),
      info('storage.format', 'Settings format', 'THY666 versioned only', ['storage.format-migration'], 'Supported THY666 upgrades use explicit conversions, never raw structure reinterpretation.'),
    ] },
    { id: 'system', label: 'System', shortLabel: 'SYS', description: 'UI identity, lifecycle and the current installation path with a separately researched updater roadmap.', rows: [
      select('system.startupView', 'Startup view', 'play', ['play','last-context'], ['system.startup'], 'Start from validated defaults or the supported saved context; do not auto-start music implicitly.'),
      toggle('system.startupAnimation', 'Startup animation', true, ['sequencers.startup_animation', 'display.boot-splash'], 'Selected visual startup animation; this is not a song autoplay option.'),
      info('system.branding', 'Startup identity', 'THY666', ['display.boot-splash'], 'Independent branding and requested logo need visual validation; no edition or activation screens.'),
      action('system.sleep', 'Sleep', ['system.sleep'], 'Target transition releases owned notes, cancels timers and blanks outputs while retaining a defined wake path.'),
      action('system.shutdown', 'Shutdown', ['system.shutdown'], 'Target controlled shutdown commits only requested data and then enters the supported device power state.', { confirm: true }),
      info('system.installation', 'Current installation', '.333 / official updater', ['system.updater-boundary'], 'Retain held-pad update entry and the existing bootloader/application layout; no device action is available here.'),
      info('system.futureUpdater', 'THY666 updater roadmap', 'Verification · backup · restore', ['system.updater-boundary', 'storage.format-migration'], 'Future own client may reuse the proven protocol. Full raw readback and automatic rollback remain unverified capabilities.'),
      info('system.rollback', 'Rollback boundary', 'Device support must be established', ['system.updater-boundary'], 'Known-good package restore differs from automatic interrupted-update fallback; host software alone cannot guarantee the latter.'),
      info('system.about', 'About / credits', 'THY666 · Dcvlt · assistant collaboration', ['system.about'], 'Show build identity, community credits and format versions without serial entitlement gates.'),
      info('system.menuContract', 'Navigation contract', 'Five controls · cursor history', ['settings.configuration-surface', 'settings.menu-navigation', 'settings.value-pickers', 'controls.navigation'], 'Center/right enters or commits; left cancels/back; up/down moves or adjusts. Editors retain parent category and row.'),
    ] },
  ],
};

// Live UI organization. Preserve stable setting IDs while moving presentation
// controls into their own clear category.
const lightGroup = STUDIO_MENU.groups.find(group => group.id === 'lights');
const takeLightRow = id => lightGroup.rows.splice(lightGroup.rows.findIndex(item => item.id === id), 1)[0];
const viewChoice = takeLightRow('lights.view');
viewChoice.label = 'View';
viewChoice.default = 'simple';
viewChoice.options = choices([{value:'notes',label:'Detailed'}, {value:'piano',label:'Piano'}, {value:'removed-layouts',label:'Retired'}, {value:'removed-routes',label:'Retired'}, {value:'simple',label:'Simple'}, {value:'chladni',label:'Chladni'}, {value:'orbit',label:'Orbit'}, {value:'waves',label:'Waves'}, {value:'fifths',label:'Circle of fifths'}]);
viewChoice.description = 'Choose Detailed, Piano, Simple, Chladni, Orbit, Waves or Circle of fifths. Each view keeps its own information slots and presentation preferences; retired IDs stay unavailable.';
const uiMotion = takeLightRow('lights.animations');
const navOptions = choices([{value:'hide',label:'Hidden'}, {value:'views',label:'Views'}, {value:'performance',label:'Performance'}, {value:'midi',label:'MIDI'}, {value:'lights',label:'Lights'}, {value:'sensitivity',label:'Sensitivity'}, {value:'chords',label:'Chords'}, {value:'arps',label:'Arps'}, {value:'engines',label:'Engines'}]);
const INFO_SLOTS = choices([{value:'off',label:'Off'},{value:'velocity',label:'Velocity'},{value:'counts',label:'Pad / note counts'},{value:'notes',label:'Playing notes'},{value:'scale',label:'Scale / root'},{value:'channels',label:'MIDI channels'},{value:'outputs',label:'Outputs'},{value:'modifiers',label:'Chord modifiers'},{value:'pressure',label:'Pressure'},{value:'octaves',label:'Octaves'},{value:'clock',label:'Clock / tempo'},{value:'arps',label:'Arp status'},{value:'musical-names',label:'Musical names'}]);
STUDIO_MENU.groups.push(
  {id:'views',label:'Views',shortLabel:'VIEWS',description:'Select the Play screen and edit its options.',rows:[
    viewChoice,
    select('views.slot1','Info slot 1','musical-names',INFO_SLOTS,['display.note-chord-view'],'First information line in the selected Piano, Simple or graphical view.',live('display.slot1')),
    select('views.slot2','Info slot 2','counts',INFO_SLOTS,['display.note-chord-view'],'Second information line. Choices are independent for every view.',live('display.slot2')),
    toggle('views.header','Scale header',true,['display.note-chord-view'],'Show the scale and root header in this view.',live('display.header')),
    toggle('views.motion','Visual motion',true,['display.note-chord-view'],'Animate this graphical view. UI animations OFF and reduced motion also freeze the visual.',live('display.motion')),
    ...['musical-names','notes','modifiers','outputs','velocity','counts'].map((value,index)=>select(`views.detail${index+1}`,`Detail row ${index+1}`,value,INFO_SLOTS,['display.note-chord-view'],'Choose the information shown on this Detailed screen row, or Off to leave it blank.',live(`display.detail${index+1}`))),
    uiMotion,
  ]},
  {id:'controls',label:'Controls',shortLabel:'CONTROLS',description:'Play-screen navigation shortcuts and whole-side note buttons.',rows:[
    select('controls.left','Left button','engines',navOptions,['controls.navigation'],'On Play, open Engines by default. Saved custom shortcuts remain selectable; hiding it disables the backlight and action.',live('controls.left')),
    select('controls.up','Up button','views',navOptions,['controls.navigation'],'Play-screen shortcut. Menu navigation remains unchanged.',live('controls.up')),
    select('controls.down','Down button','performance',navOptions,['controls.navigation'],'Play-screen shortcut. Menu navigation remains unchanged.',live('controls.down')),
    select('controls.right','Right button','chords',navOptions,['controls.navigation'],'On Play, open Chords for root, scales, layouts and voicing. CENTER toggles Settings/Play; inside Settings RIGHT enters, edits and applies.',live('controls.right')),
    select('controls.lowerMode','Lower buttons','octave-layers',[{value:'stored-notes',label:'Stored notes'},{value:'octave-layers',label:'Octave layers'}],['controls.chord-functions','chords.note_slots'],'Octave layers: the seven lower controls add full-chord copies at −3 / −2 / −1 / original / +1 / +2 / +3 octaves. Original is on by default; turning the last layer off restores it. Stored notes instead omits existing notes 1–7; unavailable slots stay dim. Neither mode invents harmonic extensions. Changes apply on the next chord press.',live('core.lowerMode')),
    select('controls.slotScope','Apply to','whole-left',[{value:'whole-left',label:'Whole left side'},{value:'selected-pad',label:'Selected chord'}],['controls.chord-functions'],'Apply the lower buttons to every left chord, or keep a separate selection for the last played left chord. Melody presses do not retarget the selection. Already held notes retain their original pitches and routes.',live('controls.slotScope')),
  ]}
);
STUDIO_MENU.groups.unshift({id:'engines',label:'Engines',shortLabel:'ENGINES',description:'Choose Play and configure its musical layout in Chords. Browse all 70 scales and assign the twelve scale shortcuts below.',rows:[
  action('engines.play','Play',['playing.scales_performance'],'Play chords and melody. Chromatic, scale degrees and Guitar are layouts within this engine; changing engine does not replace the selected layout or retune held notes.',live('core.engine.play')),
  action('engines.arp','Arp',['arpeggiators.two_instances'],'Enable the selected Arp and open its quick page. Selecting it again keeps that Arp enabled; the other Arp and playing workspace are unchanged.',live('arps.activate')),
  action('engines.sequencer','Sequencer',['sequencers.tracks'],'Open the track list. Create or open a track with RIGHT, then tap a left step and play notes or chords on the right. CENTER returns to Play while playback continues.',live('sequencer.open')),
  info('engines.drums','Drums','In development. Drum assignments and the pad map are not implemented.',['playing.drums'],'Unavailable workspaces are named honestly rather than selectable empty engines.'),
  info('engines.lightboard','Lightboard','In development. The independent playing map is not implemented.',['playing.lightboard'],'No silent placeholder is activated.'),
  info('engines.user','User','Separate User engine in development.',['playing.user_mode'],'Custom scales, chords and six-chord sets already work in Play through the Chords menu and Custom library editor.'),
  select('engines.scale','Scale library',21,[],['playing.scales_performance'],'All 70 recovered scale families, applied at the selected root. This edits the same scale as Chords.',{dynamic:'scales',...live('core.scale')}),
  select('engines.root','Root',0,['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'].map((label,value)=>({value,label})),['chords.root_keys'],'Change the root for new pad attacks.',live('core.root')),
  select('engines.shortcut','Edit shortcut','5',['5','12','M1','M2','M3','M4','M5','M6','M7','U1','U2','U3'],['controls.top-shortcuts'],'Choose one of the twelve scale controls above the right grid.',live('core.shortcutTarget')),
  select('engines.assignment','Assigned scale',56,[],['controls.top-shortcuts'],'Assign any library scale, or Cycle layout. U1–U3 have useful defaults and every assignment is saved.',{dynamic:'scale-shortcuts',...live('core.shortcutAssignment')}),
]});
const performanceRows = STUDIO_MENU.groups.find(group => group.id === 'performance').rows;
performanceRows.push(
  select('performance.contextLocation','Pad position','upper-right',[{value:'upper-right',label:'Upper right'},{value:'lower-left',label:'Lower left'}],['playing.control_overlays'],'Position of the optional context controls on each half; relocation waits for all pads to be released. Stored upper/lower preferences keep their ordinal and move to the corrected corner.',live('core.contextLocation')),
  toggle('performance.contextChordOctaves','Chord octave pads',true,['playing.control_overlays'],'Show both octave down/up pads on the left when context controls are enabled.',live('core.contextChordOctaves')),
  toggle('performance.contextMelodyOctaves','Melody octave pads',true,['playing.control_overlays'],'Show both octave down/up pads on the right when context controls are enabled.',live('core.contextMelodyOctaves')),
  toggle('performance.contextBass','Bass octave pad',true,['playing.control_overlays'],'Show the independent bass-only octave control in the left context overlay.',live('core.contextBass')),
  toggle('performance.contextInversion','Inversion pad',true,['playing.control_overlays'],'Show the inversion control in the left context overlay.',live('core.contextInversion')),
  number('performance.bassOctave','Bass octave',0,-3,3,1,['playing.octave_shift'],'Shift the lowest enabled chord tone after inversion/spread and before octave stacks. Held notes retain their original pitches.',live('core.bassOctave')),
);
lightGroup.rows.splice(lightGroup.rows.findIndex(item=>item.id==='lights.keyContrast')+1,0,
  number('lights.keyDim','Black-key level %',45,0,100,1,['lights.note-palette'],'Remaining brightness for sharp/flat pitch colors when key contrast is ON. Lower values create stronger contrast.',live('lighting.keyDim')));
const centerLight = lightGroup.rows.find(item=>item.id==='lights.center'); Object.assign(centerLight,live('lighting.center'));
const systemRows=STUDIO_MENU.groups.find(group=>group.id==='system').rows;
systemRows[systemRows.findIndex(item=>item.id==='system.about')]=action('system.about','Credits',['system.about'],'THY666 logo, build identity and community credits. LEFT returns without changing settings.',live('system.about'));
systemRows.push(info('system.storage','Storage','Current-state record and song event usage',['storage.format-migration'],'Read-only usage of the current encoded settings/song record and the song event pool. The 256 KiB rotating journal is not a named collection; external NOR is not allocated library space.',live('system.storage')));
const sensitivityRows=STUDIO_MENU.groups.find(group=>group.id==='sensitivity').rows;
const sense=id=>sensitivityRows.find(item=>item.id===`sensitivity.${id}`);
sensitivityRows.unshift(
  number('sensitivity.target','Edit pads',0,0,97,1,['controls.pad-acquisition'],'Left and Right have independent defaults. Individual pads keep their own overrides. UP returns to the containing cluster; UP on a cluster switches sides.',live('response.target')),
  action('sensitivity.pick','Pick pad',['controls.pad-acquisition'],'Tap a large pad to select it without notes or sequencer entry. All contacts in the selection gesture remain muted through release, even after leaving this page.',live('response.pick')));
sensitivityRows.push(
  number('sensitivity.amount','Pad sensitivity',66,0,100,1,['controls.noise-floor'],'0–100% spans this cluster or pad’s safe usable range. Higher needs a lighter press. Cluster changes preserve relative per-pad calibration; the other side is unchanged. Noise and release guards remain active.',live('response.amount')),
  toggle('sensitivity.linkedRelease','Auto release',true,['controls.noise-floor'],'Keep a release gap below Trigger. Editing the raw Release value selects Manual. Aftertouch range is independent.',live('response.linkedRelease')),
  number('sensitivity.releaseGap','Release gap',23,1,4095,1,['controls.noise-floor'],'Advanced hysteresis gap in ADC counts. With Auto release enabled, Trigger changes update Release atomically.',live('response.releaseGap')),
  action('sensitivity.calibrate','Calibrate cluster',['controls.pad-acquisition'],'Calibrate the selected Left or Right cluster: Soft/Mid/Firm strikes and a separate pressure hold, then playable whole-cluster comparison, sensitivity tuning and Apply or Discard.',live('calibration.open')));
sense('target').description='RIGHT edits the target; UP returns to the containing Left or Right cluster, DOWN selects the next pad. RIGHT applies and LEFT cancels. Every response draft keeps its captured scope.';
sense('attack').description='Advanced press threshold. Auto release keeps its gap when this changes. Detector limits are independent from aftertouch range.';
sense('floor').label='Aftertouch floor';sense('floor').shortLabel='AT floor';
sense('ceiling').label='Aftertouch ceiling';sense('ceiling').shortLabel='AT ceiling';
sense('curve').label='Legacy AT curve';sense('curve').shortLabel='Legacy curve';
Object.assign(sense('scope'),info('sensitivity.scope','Overrides','Inherited global defaults',['controls.pad-acquisition'],'Shows detector and strike-velocity override ownership for the selected pad. The two inheritance choices are independent.',live('response.scope')));
Object.assign(sense('customCurve'),info('sensitivity.customCurve','Response curves','Live pressure and strike curves',['display.sensitivity-curve-view'],'Read-only graphs of the effective selected-pad settings. RIGHT opens the page; selecting its row does not change views.',live('response.graph')));
Object.assign(sense('reset'),action('sensitivity.reset','Defaults',['controls.curve-reset'],'Restore the selected cluster’s factory response, Fixed velocity and single-stage expression defaults, preserving explicit per-pad overrides. All affected inherited pads must be quiet.',live('response.defaults')));
sense('velocityPreset').retired=true;
for(const id of ['attack','release','floor','ceiling','curve'])
  sense(id).description='Edit the selected cluster’s inherited response, or create an override for one pad. The other cluster and strike velocity stay unchanged; affected pads must be released.';
sense('curve').description='Single-stage aftertouch curve used when Dual-stage pressure is OFF. The optional pressure-stage and AT-stage curves are separate settings.';
sensitivityRows.push(
  select('sensitivity.velocityMode','Velocity mode',0,[{value:0,label:'Fixed'},{value:1,label:'Strike'},{value:2,label:'Mixed'}],['controls.velocity-presets'],'Fixed uses each route’s velocity. Strike uses its calibrated response. Applies only the mode to Both, Left or Right, preserving pad curves and ranges. Mixed is a read-only summary; select Fixed or Strike to unify the chosen scope. Release affected pads before applying.',live('velocity.mode')),
  select('sensitivity.velocityCurve','Strike curve',0,[{value:0,label:'Linear'},{value:1,label:'Soft'},{value:2,label:'Firm'}],['controls.velocity-presets'],'Shape attack velocity independently from continuous pressure.',live('velocity.curve')),
  number('sensitivity.softSpeed','Soft speed',2,0,4094,1,['controls.velocity-presets'],'ADC counts per millisecond mapped to minimum strike velocity. Must be below Hard speed.',live('velocity.soft')),
  number('sensitivity.hardSpeed','Hard speed',128,1,4095,1,['controls.velocity-presets'],'ADC counts per millisecond mapped to maximum strike velocity. Must exceed Soft speed.',live('velocity.hard')),
  number('sensitivity.minVelocity','Minimum velocity',1,1,127,1,['controls.velocity-presets'],'Minimum MIDI strike velocity. Must not exceed Maximum velocity.',live('velocity.minimum')),
  number('sensitivity.maxVelocity','Maximum velocity',127,1,127,1,['controls.velocity-presets'],'Maximum MIDI strike velocity. Must not be below Minimum velocity.',live('velocity.maximum')),
  action('sensitivity.inherit','Use cluster',['controls.curve-reset'],'Clear the selected pad’s detector and strike-velocity overrides together and use its cluster defaults. Other pads remain unchanged.',live('response.inherit')));
// Retired identifiers remain in the schema for saved-setting compatibility.
// They are not navigation choices in the device or ordinary web menus.
const seqGroup=STUDIO_MENU.groups.find(group=>group.id==='sequencer');
const seqExisting=id=>seqGroup.rows.find(item=>item.id===`sequencer.${id}`);
for(const id of ['track','active','quantize','destinations','channel','recordControllers','sendPressure','play','stop','clearTrack','clearSong','save','recall'])
  Object.assign(seqExisting(id),live(`sequencer.${id}`));
Object.assign(seqExisting('interaction'),select('sequencer.interaction','Record policy','replace',[{value:'replace',label:'Replace'},{value:'overdub',label:'Overdub'}],['sequencers.record_capture'],'Replace affects the reached recording interval once; Overdub adds. The active take keeps its original policy.',live('sequencer.interaction')));
Object.assign(seqExisting('playMode'),select('sequencer.playMode','Playback','loop',[{value:'loop',label:'Loop'},{value:'once',label:'One shot'}],['sequencers.play_modes'],'Loop repeats the editable bounds; One shot completes one pass and releases only this track.',live('sequencer.playMode')));
Object.assign(seqExisting('layout'),select('sequencer.layout','Pad layout','split',[{value:'split',label:'Split'},{value:'full-time',label:'Full time'},{value:'full-pitch',label:'Full pitch'}],['sequencers.layouts'],'Split provides twelve time columns plus a full live input half. Full time uses 24 columns; Full pitch shows eight pitches. Full-grid layouts have no live input bank.',live('sequencer.layout')));
seqExisting('quantize').options=choices([{value:'off',label:'Off'},{value:'1/4',label:'1/4'},{value:'1/8',label:'1/8'},{value:'1/16',label:'1/16'},{value:'1/32',label:'1/32'}]);
seqExisting('play').label='Play'; seqExisting('stop').label='Stop / reset';
seqExisting('recordControllers').label='Record CC / PC';
seqExisting('sendPressure').label='Recorded pressure';
seqExisting('clearTrack').description='Clear only the selected track, with Undo available. Other tracks and live notes keep playing.';
seqExisting('clearSong').description='Clear all song tracks as one undoable transaction; live notes are independent.';
seqExisting('save').description='Save the versioned THY666 song through the device journal or export it in the web preview.';
seqExisting('recall').description='Recall the validated saved THY666 song without restoring held notes or starting transport.';
Object.assign(seqExisting('importMidi'),info('sequencer.importMidi','Import MIDI','Use the web sequencer importer',['sequencers.midi_files'],'Import and review Standard MIDI files in the web preview. The device has no file picker.'));
seqGroup.rows.unshift(
  action('sequencer.open','Open sequencer',['sequencers.tracks'],'Open the OLED track list. RIGHT creates or opens a track; the large pads remain quiet until its editor opens.',live('sequencer.open')),
  action('sequencer.steps','Open Steps',['sequencers.layouts'],'Retired direct entry; select or create a track from Open sequencer.',live('sequencer.steps')),
  action('sequencer.help','Quick help',['sequencers.performance_controls'],'Five short pages explain tracks, step entry, playback, recording and the physical shortcuts.',live('sequencer.help')),
  number('sequencer.tempo','Tempo',120,20,300,1,CLOCK_FEATURES,'Shared tempo in beats per minute. The selected clock source remains configured in MIDI.',live('sequencer.tempo')),
  number('sequencer.length','Length',16,1,48,1,['sequencers.layouts'],'Selected track length in steps. Notes outside a shorter loop remain stored; increasing the length makes them reachable again.',live('sequencer.length')));
seqGroup.rows.push(
  select('sequencer.source','Record source','pads',[{value:'pads',label:'Pads'},{value:'arp1',label:'Arp 1'},{value:'arp2',label:'Arp 2'},{value:'live',label:'Live mix'},{value:'strum',label:'Strummer'},{value:'usb',label:'USB input'}],['sequencers.record_capture'],'Choose the next take source before arming. USB input additionally requires its explicit permission and channel filter. Track playback is excluded from live capture.',live('sequencer.source')),
  select('sequencer.processor','Playback through','direct',[{value:'direct',label:'Direct'},{value:'arp1',label:'Arp 1'},{value:'arp2',label:'Arp 2'}],['sequencers.active_route'],'Track-to-Arp uses a separate phrase context and the selected Arp preset; it never merges into live inputs.',live('sequencer.processor')),
  toggle('sequencer.mute','Mute',false,['sequencers.active_route'],'Silence this track while keeping its phase. Enabled chooses eligibility for main Play separately.',live('sequencer.mute')),
  toggle('sequencer.swap','Swap halves',false,['sequencers.layouts'],'In Split, put the editor on the right and live input on the left. Held roles stay frozen until release.',live('sequencer.swap')),
  select('sequencer.input','Input','notes',[{value:'notes',label:'Notes'},{value:'chords',label:'Chords'}],['sequencers.layouts'],'M4 switches the right playing surface between single notes and chords. The left half always selects steps.',live('sequencer.input')),
  select('sequencer.grid','Step size','1/16',['1/4','1/8','1/16','1/32'],['sequencers.layouts'],'Musical duration represented by each of the 48 left pads. Each track stores its own step size.',live('sequencer.grid')),
  select('sequencer.launch','Start timing','bar',[{value:'immediate',label:'Immediate'},{value:'beat',label:'Next beat'},{value:'bar',label:'Next bar'}],['sequencers.transport'],'Queued recording and track launches use this boundary. M1 track audition and Stop remain immediate.',live('sequencer.launch')),
  number('sequencer.loopStart','Loop start',0,0,1073718783,240,['sequencers.layouts'],'Absolute musical ticks, 960 per quarter. Changing bounds keeps out-of-loop material.',live('sequencer.loopStart')),
  number('sequencer.loopEnd','Loop end',3840,1,1073741823,240,['sequencers.layouts'],'Exclusive loop end in ticks. End must be after Start; tracks may have different lengths.',live('sequencer.loopEnd')),
  toggle('sequencer.region','Record region',false,['sequencers.record_capture'],'Use the explicit Start/End region for the next take; the current take is never retargeted.',live('sequencer.region')),
  number('sequencer.regionStart','Region start',0,0,1073718783,240,['sequencers.record_capture'],'Next capture begins at this track tick when Record region is enabled.',live('sequencer.regionStart')),
  number('sequencer.regionEnd','Region end',3840,1,1073741823,240,['sequencers.record_capture'],'Exclusive bounded capture end. A bounded take records one pass then continues playback.',live('sequencer.regionEnd')),
  number('sequencer.stepLength','Note length',240,1,1073741823,120,['sequencers.layouts'],'Precise length of the selected onset. Hold an onset and tap its endpoint for direct editing.',live('sequencer.stepLength')),
  number('sequencer.stepVelocity','Note velocity',96,1,127,1,['sequencers.layouts'],'Velocity of the selected note; selecting a tail or ambiguous cell never deletes hidden notes.',live('sequencer.stepVelocity')),
  action('sequencer.record','Arm / Record / Finish',['sequencers.record_capture'],'Stopped: arm, then use Play to start. Running: queue next boundary. Recording: Finish at the next boundary.',live('sequencer.record')),
  action('sequencer.undo','Undo',['sequencers.layouts'],'Undo the last complete step gesture, edit or take.',live('sequencer.undo')),
  action('sequencer.redo','Redo',['sequencers.layouts'],'Redo the most recently undone transaction.',live('sequencer.redo')),
  action('sequencer.duplicate','Duplicate loop',['sequencers.layouts'],'Copy the current loop to its end and extend the loop; one undoable transaction.',live('sequencer.duplicate')),
  action('sequencer.trim','Trim to loop',['sequencers.layouts'],'Remove material outside the current loop as an explicit undoable edit.',{confirm:true,...live('sequencer.trim')}));
const retiredSequencerRows = new Set(['steps','track','active','interaction','layout','swap','play','record','stop','loopStart','loopEnd','region','regionStart','regionEnd']);
for (const item of seqGroup.rows) if (retiredSequencerRows.has(item.id.slice('sequencer.'.length))) item.retired = true;
seqGroup.description = 'Create a track, tap a left step, then play notes or chords on the right. Physical controls provide playback, recording and step editing.';
const shortGroups={chords:'Chords',arps:'Arps',midi:'MIDI',lights:'Lights',storage:'Profiles'};
const shortTitles={ 'performance.velocity':'Default velocity','performance.padOverlays':'Context pads','sensitivity.centerResponse':'Button response','sensitivity.attack':'Trigger','sensitivity.release':'Release','sensitivity.floor':'Pressure floor','sensitivity.ceiling':'Pressure ceiling','lights.chordColor':'Chord colors','lights.idle':'Idle brightness','lights.keyContrast':'Key contrast','midi.routingProfile':'Routing','midi.routeTarget':'Edit route','midi.channel':'Channel','midi.destinations':'Outputs','midi.pressure':'Aftertouch' };
for(const group of STUDIO_MENU.groups){
  if(shortGroups[group.id]) group.label=shortGroups[group.id];
  for(const item of group.rows){if(shortTitles[item.id]) item.label=shortTitles[item.id];}
}

const OLED_LABELS = {
  'sensitivity.touchConfig': 'Touch config',
  'sensitivity.ceiling': 'AT ceiling',
  'sensitivity.minVelocity': 'Min velocity',
  'sensitivity.maxVelocity': 'Max velocity',
  'performance.contextChordOctaves': 'Chord oct pad',
  'performance.contextMelodyOctaves': 'Melody oct pad',
  'performance.contextBass': 'Bass pad',
  'performance.liveUpdateLockScale': 'Lock scale',
  'performance.liveUpdateMuteChord': 'Mute chord',
  'performance.liveUpdateNoteColor': 'Follow color',
  'midi.routingProfile': 'Routing',
  'midi.routeTarget': 'Edit route',
  'midi.channel': 'Channel',
  'midi.destinations': 'Outputs',
  'midi.velocity': 'Velocity',
  'midi.pressure': 'Aftertouch',
  'midi.remoteCable': 'Input cable',
  'midi.remoteChannel': 'Input chan',
  'midi.assignmentControl': 'Bind control',
  'midi.assignmentChannel': 'Assign chan',
};
for (const group of STUDIO_MENU.groups) for (const item of group.rows) {
  if (OLED_LABELS[item.id]) item.shortLabel = OLED_LABELS[item.id];
}

// MPE is an explicit whole-board live-expression mode. The receiver setup
// action is separate: changing a preference or restoring a profile sends no
// unsolicited RPN messages to connected instruments.
STUDIO_MENU.groups.splice(STUDIO_MENU.groups.findIndex(group=>group.id==='midi'),0,{
  id:'expression',label:'Expression',shortLabel:'EXPR',
  description:'Optional MPE pressure and neighboring-pad glide for live playing.',rows:[
    toggle('expression.enabled','MPE',false,['midi.poly-aftertouch','midi.pressure-pitchbend'],'One member channel per sounding voice. Applies to live pads on their DIN/USB destinations; ordinary route channels resume when OFF. Arps and sequencer playback are separate modes in this first implementation. Change only while released.',live('expression.enabled')),
    select('expression.zone','Zone','lower',[{value:'lower',label:'Lower'},{value:'upper',label:'Upper'}],['midi.pressure-pitchbend'],'Lower uses channel 1 as manager and members above it; Upper uses channel 16 and members below it. Configure the receiver to match.',live('expression.zone')),
    number('expression.members','Voices',15,1,15,1,['midi.pressure-pitchbend'],'Member-channel count. A chord needs one member per resolved note; an attack that does not fit is rejected as a whole, without stealing held or sustained voices.',live('expression.members')),
    number('expression.bendRange','Bend range',48,1,96,1,['midi.pressure-pitchbend'],'Member pitch-bend range in semitones. The MPE receiver must use the same range; Send setup can request it explicitly.',live('expression.bendRange')),
    toggle('expression.glide','Pressure glide',false,['midi.pressure-pitchbend'],'A qualifying neighboring press controls the original note or whole chord, without another Note On. Releasing that neighbor restores the original pitch; releasing the source stops it. Whole-chord voicing is preserved.',live('expression.glide')),
    number('expression.padWindow','Neighbor pads',1,1,11,1,['midi.pressure-pitchbend'],'Maximum column distance in the same physical row and half. Musical interval is checked separately, so adjacent scale pads need not be semitones.',live('expression.padWindow')),
    number('expression.interval','Glide interval',2,1,24,1,['midi.pressure-pitchbend'],'Maximum resolved source-to-target interval in semitones. A pad outside this window plays normally instead of becoming a glide modifier.',live('expression.interval')),
    number('expression.managerRange','Manager range',2,1,96,1,['midi.pressure-pitchbend'],'Manager-channel pitch range in semitones for the explicit receiver setup message. Member range is independent.',live('expression.managerRange')),
    action('expression.setup','Send setup',['midi.pressure-pitchbend'],'Explicitly send MPE zone configuration (RPN 6) and pitch ranges (RPN 0) to configured DIN/USB destinations. This reconfigures a compatible receiver; no setup is sent on boot or preference edits. Release pads first.',live('expression.setup')),
    info('expression.help','Quick help','Match receiver zone/range. Hold a pad; press a neighbor to glide. Neighbor release returns; source release stops. Arp/Seq not combined yet.',['midi.pressure-pitchbend'],'Match the receiver zone and bend range. Send setup only when wanted. Hold a pad, then press its neighbor to glide. Release the neighbor to return. Source release ends the phrase. Arps, sequencer playback and per-note expression recording are not combined with MPE yet.',live('expression.help')),
  ],
});

// Broad playing choices stay immediately reachable. Detailed configuration is
// still present in the same list after a visual divider; the divider owns no
// row identity, cursor position, setting storage or action.
// Keep logical IDs/bindings stable while grouping all musical setup together.
// Numeric internal row IDs regenerate with the shared C/web schema; the
// explicit persisted codec does not serialize this presentation ordering.
const musicalSetup = ['root','scale','melodyLayout','guitarWindow','guitarShortcut',
  'transpose','chordOctave','melodyOctave','bassOctave','keepTranspose',
  'liveUpdate','liveUpdateLockScale','liveUpdateMuteChord','liveUpdateNoteColor'];
const chordGroup = STUDIO_MENU.groups.find(group => group.id === 'chords');
const movedSetup = musicalSetup.map(id => {
  const index = performanceRows.findIndex(item => item.id === `performance.${id}`);
  if (index < 0) throw new Error(`Missing musical setup row: ${id}`);
  return performanceRows.splice(index, 1)[0];
});
chordGroup.rows.unshift(...movedSetup);
chordGroup.description = 'Root, scales, left/right layouts, chord voicing and strum.';
STUDIO_MENU.groups.find(group => group.id === 'performance').description = 'Live pad response, expression and optional context controls.';
// Keep the velocity identity and persisted sensor values stable; its new
// Performance scope is independent from the calibration/pad-picker target.
const velocityModeIndex=sensitivityRows.findIndex(item=>item.id==='sensitivity.velocityMode');
const velocityMode=sensitivityRows.splice(velocityModeIndex,1)[0];
performanceRows.unshift(
  select('performance.velocityTarget','Velocity for',0,[{value:0,label:'Both'},{value:1,label:'Left'},{value:2,label:'Right'}],['controls.velocity-presets'],'Scope for Velocity mode only; Default velocity is a separate legacy fallback and each MIDI route has its own fixed velocity. This editor selection does not change MIDI routes, calibration targets or held notes.',live('velocity.target')),
  velocityMode,
  toggle('performance.mpe','MPE',false,['midi.poly-aftertouch','midi.pressure-pitchbend'],'Same MPE switch as Expression. Live pads only; change while released with Arps and sequencer stopped. Receiver setup remains a separate explicit action.',live('expression.enabled')));
performanceRows.find(item=>item.id==='performance.velocitySource').retired=true;
// Ordinary expression is edited here and in MIDI through the same route fields.
// Keep a visible target: Velocity for is deliberately independent from routing.
const responseIndex=performanceRows.findIndex(item=>item.id==='performance.pressureMode');
const responseMode=performanceRows.splice(responseIndex,1)[0];
const midiRouteTarget=STUDIO_MENU.groups.find(group=>group.id==='midi').rows.find(item=>item.id==='midi.routeTarget');
performanceRows.splice(performanceRows.findIndex(item=>item.id==='performance.velocity')+1,0,
  select('performance.routeTarget','Response for',midiRouteTarget.default,midiRouteTarget.options,['midi.bank-route-modes'],'Side or zone for ordinary Aftertouch, Bend and Bend range. Shares the MIDI route selector: Whole Board, Left/Right, or six zones according to the routing profile. Does not change Velocity for or MPE.',live('midi.routeTarget')),
  responseMode,
  select('performance.pressureBend','Bend','off',[{value:'off',label:'Off'},{value:'up',label:'Up'},{value:'down',label:'Down'}],['midi.pressure-pitchbend'],'Ordinary MIDI pressure bend for Response for. Same Off/Up/Down policy as MIDI; held notes keep their captured response. With MPE ON, the separate MPE pressure-glide settings apply instead.',live('midi.pressureBend')));

const BASIC_ROWS = {
  engines: ['play', 'arp', 'sequencer', 'drums', 'lightboard', 'user'],
  performance: ['velocityTarget', 'mpe', 'velocity', 'routeTarget', 'pressureMode', 'pressureBend', 'padOverlays'],
  chords: ['guidance','layout','quality','userScale','sixBank','voicingMode','editScales','editChords','editSix', 'inversion', 'spread', 'strum', 'strumDirection', 'strummerTarget', 'continuous', 'strumLatch'],
  arps: ['open', 'target', 'enabled', 'type', 'grouping', 'noteValue', 'pattern', 'gate', 'swing', 'clear'],
  sequencer: ['open', 'help', 'tempo', 'length', 'grid', 'input', 'launch'],
  midi: ['routingProfile', 'routeTarget', 'channel', 'destinations', 'velocity', 'pressure', 'clock', 'tempo', 'receiveTransport', 'sendClock', 'remoteRecord', 'remotePosition', 'assignmentKind', 'pedalEnabled', 'pedalAction'],
  expression: ['enabled', 'zone', 'members', 'bendRange', 'glide'],
  sensitivity: ['target', 'pick', 'amount', 'profileEnabled', 'profileSplit', 'calibrate', 'centerResponse'],
  lights: ['enabled', 'idle', 'held', 'chordColor', 'noteColors', 'keyContrast', 'heldStyle', 'heldFeedback', 'center', 'exploder'],
  storage: ['profileName', 'saveProfile', 'loadProfile', 'exportProfile', 'importProfile'],
  system: ['startupView', 'startupAnimation', 'sleep', 'shutdown', 'storage', 'about'],
  views: ['slot1', 'slot2', 'header', 'motion'],
  controls: ['left', 'up', 'down', 'right', 'lowerMode'],
};
for (const group of STUDIO_MENU.groups) {
  const basic = new Set(BASIC_ROWS[group.id].map(id => `${group.id}.${id}`));
  // The View picker retains its original lights.view identity after relocation.
  if (group.id === 'views') basic.add('lights.view');
  if (group.id === 'performance') basic.add('sensitivity.velocityMode');
  if (group.id === 'chords') for (const id of ['root','scale','melodyLayout','guitarShortcut',
    'transpose','chordOctave','melodyOctave','liveUpdate']) basic.add(`performance.${id}`);
  for (const item of group.rows) item.advanced = !basic.has(item.id);
  group.rows = [...group.rows.filter(item => !item.advanced), ...group.rows.filter(item => item.advanced)];
}

export const MENU_ROWS = STUDIO_MENU.groups.flatMap(group => group.rows.map(item => ({ ...item, group: group.id })));
export const MENU_BY_ID = new Map(MENU_ROWS.map(item => [item.id, item]));
export function findMenuRow(id) { return MENU_BY_ID.get(id) ?? null; }
