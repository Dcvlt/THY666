# Settings and storage

Applied instrument parameters, response calibration/overrides, routes,
expression, views, custom musical library and the current song persist in a
versioned journal. The development settings format is **V18**. Named profile/song
collections are not implemented.

## Saving

RIGHT applies an editor draft to runtime. Persistence follows after a 1.5-second
quiet debounce, with playback stopped, inputs released and musical ownership,
controllers, queues and editor workspaces quiet. Enabled but idle Arps do not
prevent saving; an active phrase does. Wait for **Saved** before disconnecting.

States distinguish Defaults, Pending, Saving, Saved, Error and Unsupported.
Error/Unsupported never become success merely because an edit was accepted.
The console's `settings` report exposes version, generation, loaded/migration
status and save/error counters. System → Storage shows saved-data usage and song
event capacity, separate from runtime RAM.

## Journal

Sectors 10–11 at `0x080c0000..0x08100000` provide a **256 KiB** two-sector
journal. Records use explicit length, schema version, generation, CRC32 and a
commit word programmed last. Programmed words and the complete committed record
are read back before reporting Saved. Rotation preserves an earlier valid record
until its successor commits.

Maximum V18 record size is **13,876 bytes**, reserved across four adjacent 4 KiB
slots. A valid unknown future format blocks automatic rewriting. Older supported
versions validate under their original rules before migration; corrupt or
inconsistent input is rejected before live mutation.

The STM32's single-bank flash stalls instruction fetch during program/erase.
Erase is a maintenance pause, not uninterrupted musical service. Idle gating
avoids starting it while notes are owned; software cannot cancel an erase after
it begins. The driver bounds addresses and validates the supported MCU/flash
geometry before writing.

## Byte format and lifetimes

Values are encoded explicitly little-endian, never copied as raw C structures.
V18 has a 5,032-byte parameter/library prefix followed by an independently
versioned THYS song. All V1–V17 offsets remain frozen for those source versions.
After the unchanged 740-byte library, V16 appends twelve bytes: Harmony
progression, hints, motion, view, lower-button bank, octave target, note-pad mode,
melody-analysis policy, bass policy, register low/high, and global cluster swap.
Imports validate the source version's original limits before migration; legacy
settings default to unswapped clusters and normal live Harmony additions.

The 740-byte custom library contains eight 16-byte scales, twenty-four 23-byte
chords and three 20-byte six-chord sets. Names have at most twelve printable
ASCII characters. Note validity is explicit, including MIDI zero. Active
references and all fields validate before restore. THYL library files wrap this
payload in a versioned 16-byte CRC header.

Left and Right have independent response, velocity, release and expression
defaults. Per-pad overrides and 96 learned release guards are independent.
System → Swap clusters changes musical roles only; these physical calibration
and per-pad override arrays are never permuted. It requires quiet ownership and
stopped transport. The preference is restored quietly at boot.
Migration preserves raw thresholds instead of retuning them to reproduce an old
percentage label. Restored settings do not send receiver setup or notes.

Transient state is excluded: held notes, baselines, raw learning samples,
calibration/editor drafts, selected Live Update harmony, owners, transport
position and running playback. Reboot restores configuration and content quietly.
Harmony captures, held melody contributions, suggestions and destination
previews and recent history are also transient. V17 added direction, pad map
and contrast. V18 appends Outside key, Stay in key, Lock map and Chord bank.
Legacy imports default to Balanced (old Adventurous becomes Travel), Adaptive,
35% outside-key preference and unlocked assignments. Existing hint contrast is
retained; the revised light curve keeps ordinary pads visible. Retired view and
reserved-pad preferences migrate to the unified Fifths/full-surface design.

## Export and restoration

The client's `.thyb` backup covers the whole current applied settings payload,
including unsaved applied changes, rather than only one flash slot. Restore
validates compatibility, applies atomically when quiet, waits for durable saving
and verifies a fresh payload readback. See [Client/backups](companion-and-backup.md).

Normal boot persistence does not prove preservation across every updater or
recovery after power loss during an update. Export before installation; see
[Installation](installation.md).
