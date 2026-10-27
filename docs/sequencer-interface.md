# Sequencer: quick guide

The shared C engine and the web preview use the same workflow. Open **Engines → Sequencer**, or **Settings → Sequencer → Open sequencer**. Opening it never starts playback.

## 1. Create or open a track

The OLED lists only tracks you have created, with note counts, progress, recording status and queued-start countdowns. The large pads stay quiet on this screen.

- **UP / DOWN:** choose a track or **+ New track**.
- **RIGHT:** create the new track, or open the selected track.
- **LEFT:** ask to delete the selected track. RIGHT confirms; LEFT cancels.
- **CENTER:** return directly to Play, without stopping backing tracks.

There are 21 available tracks. An empty created track is saved too.

## 2. Choose a step, then play

In a track, the **LEFT 48 pads are 48 chronological steps**: top row left-to-right, then the next row. The **RIGHT 48 pads play notes or chords**.

1. Tap and release a LEFT step. It stays selected.
2. Play on the RIGHT. You do not have to hold the LEFT pad.
3. Tap another LEFT step to change the entry position.

A new gesture replaces that step's notes. Keep one right pad held while adding others to build a chord as one gesture. **Overdub** keeps existing notes and adds the new gesture. **Clear** empties the selected step. One Undo reverses the entire gesture.

New tracks start with 16 steps at 1/16. Entering a later step extends the loop, up to 48 steps. Length and Step size are in track options. Changing Step size retimes the track while retaining step positions. Shortening Length retains later content for subsequent extension.

Occupied pads use a representative note's color from your chosen palette. The selected step is brighter; the playback position is white. The right surface keeps its normal note/chord and held feedback.

## 3. Physical shortcuts

These apply only while the sequencer is open. The upper RIGHT controls become:

| Control | Action |
| --- | --- |
| **5** | Play all tracks / Stop all |
| **12** | Record / Finish |
| **M1** | Play this track alone / Stop |
| **M2** | Clear selected step |
| **M3** | Overdub on/off |
| **M4** | RIGHT input: Notes / Chords |
| **M5 / M6** | Undo / Redo |
| **M7** | Quick help |
| **U1 / U2** | Input octave down / up |
| **U3** | Next scale in Chords; next note layout in Notes |

The upper LEFT twelve keys choose the root. Center inversion, spread, strum and chord-tone controls retain their musical functions for chord input. In the editor, **RIGHT** opens this track's settings, **LEFT** returns to Tracks, and **CENTER** returns to Play.

Switching between isolated and all-track playback restarts sequencer playback at the loop starts. Live pads and independent live Arps keep their owners. It does not store temporary solo state.

## 4. Real-time recording

On an opened track, press **12** to start recording immediately from stopped; pressing it on Tracks opens that track's input surface automatically. If a song is already playing, recording joins at the selected Start timing, normally the next bar. The OLED shows the countdown.

The selected loop records one pass, then plays back. **12** finishes early at the next boundary; **5** stops immediately and closes the take. Notes already held when capture begins are included. Overdub applies to recording as well as step entry. During a take, right-hand input feeds the recorder rather than writing repeatedly to the selected step.

Record source defaults to Pads, before generators. Track options can instead select Arp 1, Arp 2, Live mix, Strummer or permitted USB input. Track playback never records itself into an ordinary live take. A track can also play through an independent Arp context.

## 5. Track settings

From the step editor, press **RIGHT** to open **T1 Settings** (or the current track's number). This dedicated page contains that track's **Length, Step size and Input**, followed by recording, processing and MIDI options below the separator. It is reached from the track, not from the global Settings menu.

- **UP / DOWN:** select an option; while editing, change its value.
- **RIGHT:** edit the selected option, then apply it.
- **LEFT:** cancel an unfinished edit; otherwise return directly to the same track and selected step.
- **CENTER:** leave the sequencer interface and return to Play, discarding an unfinished edit.

The LEFT step grid and RIGHT playing surface remain active while track settings are open, including their lighting and existing held-note ownership. Backing playback continues.

Each track has its own **MIDI channel** and **Outputs**. A new T1 initially uses channel 1, T2 channel 2, and so on; these routes are independent of the live halves' routes. If live pads sound but track playback does not, open the track's settings and check its channel and outputs against the receiving instrument's filters.

## 6. Global settings and saving

**Settings → Sequencer** contains **Open sequencer, Quick help, Tempo and Start timing**, followed by song save/history tools. It does not contain the selected track's options. Per-track controls are kept in the track editor rather than the global menu.

Preferences and song content save through the existing journal when playback is stopped and controls/pads are released. Wait for Saved before disconnecting power. Reboot restores content and preferences without starting playback or restoring held notes. Web file export/import and explicit device song transfer remain available.

**Quick help** in the device settings contains five short pages with these steps and shortcuts.

## Engineering limits

The song pool holds 512 events across 21 tracks; a step can contain multiple notes or a chord. The 48-pad limit counts steps, not individual MIDI notes. Undo/Redo currently retains one complete transaction. The device has 128 generated-note owners shared with Arps and other processors.

Native THYS v2 saves created tracks and each track's grid. V1 songs import without changing their notes, durations, groups or loop timing; nonempty old tracks become created tracks. Long imported loops use a coarser display grid to fit 48 steps. Unsupported/corrupt files are rejected before replacing the song. Global tempo and Arp presets belong to settings rather than the native song file.
