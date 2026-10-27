/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "transport.h"

static void queue_push(thy_instrument_event_queue *queue, thy_midi_event event)
{
    unsigned tail = (queue->head + queue->count) % THY_INSTRUMENT_TRANSPORT_QUEUE_CAPACITY;
    queue->events[tail] = event;
    ++queue->count;
}
static void queue_pop(thy_instrument_event_queue *queue)
{
    queue->head = (uint16_t)((queue->head + 1u) % THY_INSTRUMENT_TRANSPORT_QUEUE_CAPACITY);
    --queue->count;
}
void thy_instrument_transport_init(thy_instrument_transport *transport,
    thy_core *core, const thy_instrument_transport_io *io)
{
    if (transport == NULL) return;
    *transport = (thy_instrument_transport){0};
    transport->core = core;
    if (io != NULL) transport->io = *io;
}
bool thy_instrument_transport_usb_ready(const thy_instrument_transport *transport)
{
    return transport != NULL && transport->core != NULL &&
        transport->session_seen && transport->mounted && !transport->invalid_pending &&
        transport->io.write_usb != NULL &&
        transport->reconcile_index == THY_INSTRUMENT_TRANSPORT_RECONCILE_EVENTS &&
        transport->usb_restore_index == 32u;
}
bool thy_instrument_transport_din_ready(const thy_instrument_transport *transport)
{
    return transport != NULL && transport->core != NULL &&
        transport->io.write_din != NULL && !transport->invalid_pending &&
        transport->din_reconcile_index == THY_INSTRUMENT_TRANSPORT_DIN_RECONCILE_EVENTS;
}
bool thy_instrument_transport_ready(const thy_instrument_transport *transport)
{
    return thy_instrument_transport_din_ready(transport) ||
        thy_instrument_transport_usb_ready(transport);
}
static void block_key(thy_instrument_transport *transport, uint16_t key)
{
    if (key < THY_INSTRUMENT_TRANSPORT_USB_KEYS)
        transport->usb_blocked[key / 8u] |= (uint8_t)(1u << (key & 7u));
}
static bool key_blocked(const thy_instrument_transport *transport, uint16_t key)
{
    return key < THY_INSTRUMENT_TRANSPORT_USB_KEYS &&
        (transport->usb_blocked[key / 8u] & (uint8_t)(1u << (key & 7u))) != 0u;
}
static void mark_old_usb_owners(thy_instrument_transport *transport)
{
    for (unsigned key = 0u; key < THY_INSTRUMENT_TRANSPORT_USB_KEYS; ++key)
        if (transport->core->refs[key] || thy_core_deferred(transport->core, (uint16_t)key))
            block_key(transport, (uint16_t)key);
    for (unsigned pad = 0u; pad < THY_PAD_COUNT; ++pad) {
        const thy_pad_owner *owner = &transport->core->owners[pad];
        for (unsigned i = owner->activated; i < owner->count; ++i)
            block_key(transport, thy_pad_owner_key(owner, (uint8_t)(i)));
    }
}
static void invalidate_usb(thy_instrument_transport *transport)
{
    mark_old_usb_owners(transport);
    transport->discarded_events += transport->usb_queue.count;
    transport->usb_queue.head = transport->usb_queue.count = 0u;
    transport->reconcile_index = 0u;
    transport->usb_restore_index = 0u;
    if (transport->pending_valid && transport->pending.destination <= THY_USB1) {
        ++transport->discarded_events;
        transport->pending_valid = transport->invalid_pending = 0u;
    }
    /* Keep DIN order while compacting the core ring in place. Write never
     * overtakes read. No ownership/refcount/strum/sustain fields are altered. */
    uint16_t keep = 0u, original = transport->core->queue_count;
    for (uint16_t i = 0u; i < original; ++i) {
        unsigned from = (transport->core->queue_head + i) % THY_QUEUE_CAPACITY;
        thy_midi_event event = transport->core->queue[from];
        if (event.destination <= THY_USB1) ++transport->discarded_events;
        else {
            unsigned to = (transport->core->queue_head + keep) % THY_QUEUE_CAPACITY;
            transport->core->queue[to] = event;
            ++keep;
        }
    }
    transport->core->queue_count = keep;
}
static void observe_session(thy_instrument_transport *transport, bool mounted,
    uint32_t session_epoch)
{
    if (transport->session_seen && transport->mounted == (uint8_t)mounted &&
        transport->session_epoch == session_epoch) return;
    transport->session_seen = 1u;
    transport->mounted = (uint8_t)mounted;
    transport->session_epoch = session_epoch;
    ++transport->session_changes;
    invalidate_usb(transport);
}
static void reap_cancelled_usb_keys(thy_instrument_transport *transport)
{
    uint8_t pending[THY_INSTRUMENT_TRANSPORT_USB_KEYS / 8u] = {0};
    for (unsigned pad = 0u; pad < THY_PAD_COUNT; ++pad) {
        const thy_pad_owner *owner = &transport->core->owners[pad];
        for (unsigned i = owner->activated; i < owner->count; ++i) {
            unsigned key = thy_pad_owner_key(owner, (uint8_t)(i));
            if (key < THY_INSTRUMENT_TRANSPORT_USB_KEYS)
                pending[key / 8u] |= (uint8_t)(1u << (key & 7u));
        }
    }
    /* A late strum attack may already be queued while its pad has since
     * released. Preserve its block until BOTH queued attack and off pass. */
    for (unsigned i = 0u; i < transport->core->queue_count; ++i) {
        unsigned index = (transport->core->queue_head + i) % THY_QUEUE_CAPACITY;
        thy_midi_event event = transport->core->queue[index];
        uint8_t kind = event.status & 0xf0u;
        if (event.destination <= THY_USB1 &&
            (kind == 0x80u || kind == 0x90u || kind == 0xa0u)) {
            uint16_t key = thy_core_key(event.destination, event.status & 15u, event.data1);
            if (key < THY_INSTRUMENT_TRANSPORT_USB_KEYS)
                pending[key / 8u] |= (uint8_t)(1u << (key & 7u));
        }
    }
    /* Canceled pending strum keys do not generate an Off. Reap their blocks
     * from current ownership after processing older core events. */
    for (unsigned byte = 0u; byte < sizeof transport->usb_blocked; ++byte) {
        uint8_t blocked = transport->usb_blocked[byte];
        if (!blocked) continue;
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            unsigned key = byte * 8u + bit;
            uint8_t mask = (uint8_t)(1u << bit);
            if ((blocked & mask) && !transport->core->refs[key] &&
                !thy_core_deferred(transport->core, (uint16_t)key) && !(pending[byte] & mask))
                transport->usb_blocked[byte] &= (uint8_t)~mask;
        }
    }
}
static bool blocked_expression_channel(const thy_instrument_transport *transport,
    thy_midi_event event)
{
    uint8_t kind=event.status&0xf0u;
    if(!((kind==0xd0u&&event.data1!=0u) ||
        (kind==0xe0u&&(event.data1!=0u||event.data2!=64u)) ||
        (kind==0xb0u&&event.data1==74u&&event.data2!=64u)))return false;
    unsigned route=event.destination*16u+(event.status&15u),first=route*128u;
    bool blocked=false;
    for(unsigned pitch=0;pitch<128u;++pitch) {
        unsigned key=first+pitch;
        if(key_blocked(transport,(uint16_t)key))blocked=true;
        else if(transport->core->refs[key]||thy_core_deferred(transport->core, (uint16_t)key))return false;
    }
    if(!blocked)return false;
    /* A complete fresh tap can already have released in the core while its
     * ordered attack/expression/off still waits in USB's queue. Such a fresh
     * queued lifetime remains eligible for its channel controls. */
    uint8_t queued_fresh[16]={0u};
    for(unsigned n=0;n<transport->usb_queue.count;++n) {
        const thy_midi_event *queued=&transport->usb_queue.events[
            (transport->usb_queue.head+n)%THY_INSTRUMENT_TRANSPORT_QUEUE_CAPACITY];
        unsigned status=queued->status&0xf0u;
        if(queued->destination!=event.destination || (queued->status&15u)!=(event.status&15u) ||
            (status!=0x80u&&status!=0x90u) ||
            key_blocked(transport,thy_core_key(queued->destination,
                queued->status&15u,queued->data1)))continue;
        unsigned pitch=queued->data1;uint8_t bit=(uint8_t)(1u<<(pitch&7u));
        if(status==0x90u&&queued->data2)queued_fresh[pitch/8u]|=bit;
        else queued_fresh[pitch/8u]&=(uint8_t)~bit;
    }
    for(unsigned n=0;n<sizeof queued_fresh;++n)if(queued_fresh[n])return false;
    return true;
}
static bool suppress_usb(thy_instrument_transport *transport, thy_midi_event event)
{
    if(blocked_expression_channel(transport,event)) {
        ++transport->usb_suppressed;
        return true;
    }
    if (!thy_instrument_transport_usb_ready(transport)) {
        uint8_t kind = event.status & 0xf0u;
        /* New controls are retained behind cleanup while mounted. In
         * particular a sustain transition after its restore slot was scanned
         * must still reach USB; otherwise later releases could be deferred
         * in the core while the receiver has a different pedal state. */
        if (transport->mounted && kind >= 0xb0u && kind <= 0xe0u) return false;
        if (kind == 0x90u)
            block_key(transport, thy_core_key(event.destination,
                event.status & 15u, event.data1));
        ++transport->usb_suppressed;
        return true;
    }
    uint8_t kind = event.status & 0xf0u;
    if (kind == 0x80u || kind == 0x90u || kind == 0xa0u) {
        uint16_t key = thy_core_key(event.destination, event.status & 15u, event.data1);
        if (key_blocked(transport, key)) {
            if (kind == 0x80u || (kind == 0x90u && event.data2 == 0u))
                transport->usb_blocked[key / 8u] &= (uint8_t)~(1u << (key & 7u));
            ++transport->usb_suppressed;
            return true;
        }
    }
    return false;
}
static void stage_core(thy_instrument_transport *transport, unsigned budget)
{
    for (unsigned i = 0u; i < budget; ++i) {
        if (!transport->pending_valid) {
            if (!thy_core_poll(transport->core, &transport->pending)) return;
            transport->pending_valid = 1u;
        }
        thy_midi_event event = transport->pending;
        uint8_t bytes[4];
        if (!thy_midi_encode_din(&event, bytes)) {
            if (!transport->invalid_pending) ++transport->invalid_events;
            transport->invalid_pending = 1u;
            return;
        }
        if (event.destination <= THY_USB1) {
            if (suppress_usb(transport, event)) {
                transport->pending_valid = 0u;
                continue;
            }
            if (transport->usb_queue.count == THY_INSTRUMENT_TRANSPORT_QUEUE_CAPACITY) {
                ++transport->usb_overruns;
                invalidate_usb(transport);
                continue;
            }
            queue_push(&transport->usb_queue, event);
        } else {
            if (transport->din_queue.count == THY_INSTRUMENT_TRANSPORT_QUEUE_CAPACITY) {
                ++transport->backpressure;
                return;
            }
            queue_push(&transport->din_queue, event);
        }
        transport->pending_valid = 0u;
    }
}
static bool write_message(thy_instrument_transport *transport, bool din,
    unsigned route,uint8_t kind,uint8_t data1,uint8_t data2)
{
    thy_midi_event event = {(uint8_t)(din ? THY_DIN : route / 16u),
        (uint8_t)(kind | (route % 16u)), data1, data2};
    uint8_t bytes[4];
    if (din) {
        size_t count = thy_midi_encode_din(&event, bytes);
        if (transport->io.write_din == NULL ||
            !transport->io.write_din(transport->io.context, bytes, count)) return false;
        ++transport->din_accepted;
    } else {
        (void)thy_midi_encode_usb(&event, bytes);
        if (transport->io.write_usb == NULL ||
            !transport->io.write_usb(transport->io.context, bytes)) return false;
        ++transport->usb_accepted;
    }
    return true;
}
static bool write_cleanup(thy_instrument_transport *transport,bool din,
    unsigned route,uint8_t controller,uint8_t value)
{return write_message(transport,din,route,0xb0u,controller,value);}
static void flush_destination(thy_instrument_transport *transport, bool din,
    unsigned budget)
{
    static const uint8_t controllers[] = {64u, 123u, 120u};
    if ((!din && !transport->mounted) ||
        (din ? transport->io.write_din == NULL : transport->io.write_usb == NULL)) return;
    for (unsigned i = 0u; i < budget; ++i) {
        uint16_t index = din ? transport->din_reconcile_index : transport->reconcile_index;
        unsigned limit = din ? THY_INSTRUMENT_TRANSPORT_DIN_RECONCILE_EVENTS :
            THY_INSTRUMENT_TRANSPORT_RECONCILE_EVENTS;
        if (index < limit) {
            unsigned width=din?3u:6u,phase=index%width,route=index/width;
            bool accepted=phase<3u?write_cleanup(transport,din,route,controllers[phase],0u):
                phase==3u?write_message(transport,false,route,0xd0u,0u,0u):
                phase==4u?write_message(transport,false,route,0xe0u,0u,64u):
                    write_cleanup(transport,false,route,74u,64u);
            if (!accepted) goto busy;
            if (din) ++transport->din_reconcile_index;
            else ++transport->reconcile_index;
            continue;
        }
        if (!din && transport->usb_restore_index < 32u) {
            unsigned route = transport->usb_restore_index;
            if (transport->core->sustain[route] &&
                !write_cleanup(transport, false, route, 64u, 127u)) goto busy;
            ++transport->usb_restore_index;
            continue;
        }
        thy_instrument_event_queue *queue = din ? &transport->din_queue : &transport->usb_queue;
        if (!queue->count) return;
        thy_midi_event event = queue->events[queue->head];
        uint8_t bytes[4];
        if (din) {
            size_t count = thy_midi_encode_din(&event, bytes);
            if (!transport->io.write_din(transport->io.context, bytes, count)) goto busy;
            ++transport->din_accepted;
        } else {
            (void)thy_midi_encode_usb(&event, bytes);
            if (!transport->io.write_usb(transport->io.context, bytes)) goto busy;
            ++transport->usb_accepted;
        }
        queue_pop(queue);
    }
    return;
busy:
    ++transport->backpressure;
}
void thy_instrument_transport_poll(thy_instrument_transport *transport,
    bool mounted, uint32_t session_epoch, unsigned budget)
{
    if (transport == NULL || transport->core == NULL) return;
    observe_session(transport, mounted, session_epoch);
    if (budget > THY_INSTRUMENT_TRANSPORT_MAX_BUDGET)
        budget = THY_INSTRUMENT_TRANSPORT_MAX_BUDGET;
    if (!budget) return;
    stage_core(transport, budget);
    flush_destination(transport, true, budget);
    flush_destination(transport, false, budget);
    reap_cancelled_usb_keys(transport);
}
