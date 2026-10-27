/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_INSTRUMENT_TRANSPORT_H
#define THY_INSTRUMENT_TRANSPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "thy_core.h"
#define THY_INSTRUMENT_TRANSPORT_MAX_BUDGET 32u
#define THY_INSTRUMENT_TRANSPORT_RECONCILE_EVENTS 192u
#define THY_INSTRUMENT_TRANSPORT_DIN_RECONCILE_EVENTS 48u
#define THY_INSTRUMENT_TRANSPORT_QUEUE_CAPACITY THY_QUEUE_CAPACITY
#define THY_INSTRUMENT_TRANSPORT_USB_KEYS (2u * 16u * 128u)
typedef struct thy_instrument_transport_io {
    void *context;
    /* True accepts the whole message into a bounded sink-owned buffer.
     * False accepts nothing; exact bytes are retained. Not delivery proof. */
    bool (*write_usb)(void *context, const uint8_t packet[4]);
    bool (*write_din)(void *context, const uint8_t *bytes, size_t count);
} thy_instrument_transport_io;
typedef struct thy_instrument_event_queue {
    thy_midi_event events[THY_INSTRUMENT_TRANSPORT_QUEUE_CAPACITY];
    uint16_t head, count;
} thy_instrument_event_queue;
typedef struct thy_instrument_transport {
    thy_core *core;
    thy_instrument_transport_io io;
    thy_instrument_event_queue usb_queue, din_queue;
    thy_midi_event pending;
    uint8_t usb_blocked[THY_INSTRUMENT_TRANSPORT_USB_KEYS / 8u];
    uint32_t session_epoch, session_changes, usb_accepted, din_accepted;
    uint32_t backpressure, invalid_events, discarded_events;
    uint32_t usb_overruns, usb_suppressed;
    uint16_t reconcile_index, din_reconcile_index;
    uint8_t usb_restore_index, session_seen, mounted, pending_valid, invalid_pending;
} thy_instrument_transport;
void thy_instrument_transport_init(thy_instrument_transport *, thy_core *,
    const thy_instrument_transport_io *);
/* Call after tud_task(), before physical input, on the single main thread.
 * USB epoch changes on mount AND unmount. USB epochs discard only USB output,
 * preserving core owners, sustain, pending strums and DIN messages. Old held
 * USB pitches stay suppressed until release. USB cleanup is CC64/123/120=0,
 * Channel Pressure=0, Pitch Bend=center and CC74=64 on both cables/all channels,
 * then restores current core sustain levels. Non-neutral channel expression
 * cannot resurrect an old blocked channel with no fresh active notes.
 *
 * Independent queues prevent busy/disconnected USB from blocking DIN. A full
 * USB queue invalidates/reconciles USB only, with explicit counted USB loss.
 * DIN is never dropped: a full queue retains the next core event. Per call:
 * at most budget core events and budget writes PER destination, capped at32;
 * at most one failed sink attempt per destination; zero budget observes epochs.
 * Input uses ready(); NEVER panic/reset the instrument on a USB epoch. */
void thy_instrument_transport_poll(thy_instrument_transport *, bool mounted,
    uint32_t session_epoch, unsigned budget);
bool thy_instrument_transport_ready(const thy_instrument_transport *);
bool thy_instrument_transport_usb_ready(const thy_instrument_transport *);
bool thy_instrument_transport_din_ready(const thy_instrument_transport *);
#endif
