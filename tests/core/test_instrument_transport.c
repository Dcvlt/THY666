/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual transport and ownership engine, independent atomic sink mocks. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/instrument/transport.h"
typedef struct fake_sink {
    uint8_t usb[8192][4], din[8192][3];
    size_t din_lengths[8192];
    unsigned usb_count, din_count, usb_attempts, din_attempts;
    int usb_quota, din_quota;
} fake_sink;
static thy_core core;
static thy_instrument_transport transport;
static fake_sink sink;
static bool accept_usb(void *context, const uint8_t packet[4])
{
    fake_sink *s = context;
    ++s->usb_attempts;
    if (s->usb_quota == 0) return false;
    if (s->usb_quota > 0) --s->usb_quota;
    assert(s->usb_count < 8192u);
    memcpy(s->usb[s->usb_count++], packet, 4u);
    return true;
}
static bool accept_din(void *context, const uint8_t *bytes, size_t count)
{
    fake_sink *s = context;
    ++s->din_attempts;
    if (s->din_quota == 0) return false;
    if (s->din_quota > 0) --s->din_quota;
    assert(s->din_count < 8192u && count >= 1u && count <= 3u);
    memcpy(s->din[s->din_count], bytes, count);
    s->din_lengths[s->din_count++] = count;
    return true;
}
static void initialize(bool din)
{
    memset(&sink, 0, sizeof sink);
    sink.usb_quota = sink.din_quota = -1;
    thy_core_init(&core);
    thy_instrument_transport_io io = {&sink, accept_usb, din ? accept_din : NULL};
    thy_instrument_transport_init(&transport, &core, &io);
}
static void poll(bool mounted, uint32_t epoch)
{ thy_instrument_transport_poll(&transport, mounted, epoch, 32u); }
static void clear_capture(void)
{ sink.usb_count = sink.din_count = sink.usb_attempts = sink.din_attempts = 0u; }
static void ready(void)
{
    initialize(true);
    for (unsigned i = 0u; i < 7u; ++i) poll(true, 1u);
    assert(thy_instrument_transport_ready(&transport));
    assert(thy_instrument_transport_usb_ready(&transport));
    assert(thy_instrument_transport_din_ready(&transport));
    assert(sink.usb_count == 192u && sink.din_count == 48u);
    clear_capture();
}
static void usb_is(unsigned index, uint8_t destination, uint8_t status,
    uint8_t first, uint8_t second)
{
    assert(index < sink.usb_count);
    assert(sink.usb[index][0] == (uint8_t)((destination << 4u) | (status >> 4u)));
    assert(sink.usb[index][1] == status);
    assert(sink.usb[index][2] == first && sink.usb[index][3] == second);
}
static void din_is(unsigned index, uint8_t status, uint8_t first, uint8_t second)
{
    assert(index < sink.din_count && sink.din_lengths[index] == 3u);
    assert(sink.din[index][0] == status && sink.din[index][1] == first &&
        sink.din[index][2] == second);
}
static void test_standalone_din_cleanup_and_note_release(void)
{
    initialize(true);
    poll(false, 0u);
    assert(!thy_instrument_transport_ready(&transport));
    poll(false, 0u);
    assert(thy_instrument_transport_ready(&transport));
    assert(!thy_instrument_transport_usb_ready(&transport));
    assert(sink.usb_count == 0u && sink.din_count == 48u);
    for (unsigned route = 0u; route < 16u; ++route) {
        din_is(route * 3u, (uint8_t)(0xb0u | route), 64u, 0u);
        din_is(route * 3u + 1u, (uint8_t)(0xb0u | route), 123u, 0u);
        din_is(route * 3u + 2u, (uint8_t)(0xb0u | route), 120u, 0u);
    }
    clear_capture();
    int16_t note = 60;
    assert(thy_core_press(&core, 0u, &note, 1u, 0, 0u,
        THY_ROUTE_USB0 | THY_ROUTE_DIN, 96u) == THY_OK);
    poll(false, 0u);
    assert(thy_core_release(&core, 0u) == THY_OK);
    poll(false, 0u);
    assert(sink.din_count == 2u && !sink.usb_count);
    din_is(0u, 0x90u, 60u, 96u); din_is(1u, 0x80u, 60u, 0u);
}
static void test_usb_cleanup_order_budget_and_usb_only_compatibility(void)
{
    initialize(false);
    thy_instrument_transport_poll(&transport, true, 1u, 10000u);
    assert(sink.usb_count == 32u && !thy_instrument_transport_ready(&transport));
    for (unsigned i = 0u; i < 6u; ++i) poll(true, 1u);
    assert(thy_instrument_transport_ready(&transport));
    for (unsigned route = 0u; route < 32u; ++route) {
        usb_is(route * 6u, route / 16u, (uint8_t)(0xb0u | (route % 16u)), 64u, 0u);
        usb_is(route * 6u + 1u, route / 16u, (uint8_t)(0xb0u | (route % 16u)), 123u, 0u);
        usb_is(route * 6u + 2u, route / 16u, (uint8_t)(0xb0u | (route % 16u)), 120u, 0u);
        usb_is(route * 6u + 3u, route / 16u, (uint8_t)(0xd0u | (route % 16u)), 0u, 0u);
        usb_is(route * 6u + 4u, route / 16u, (uint8_t)(0xe0u | (route % 16u)), 0u, 64u);
        usb_is(route * 6u + 5u, route / 16u, (uint8_t)(0xb0u | (route % 16u)), 74u, 64u);
    }
    assert(transport.usb_accepted == 192u);
}
static void test_busy_usb_cannot_starve_din_or_note_off(void)
{
    ready(); sink.usb_quota = 0;
    int16_t note = 0;
    assert(thy_core_press(&core, 0u, &note, 1u, 0, 7u,
        THY_ROUTE_ALL, 103u) == THY_OK);
    poll(true, 1u);
    assert(sink.usb_attempts == 1u && !sink.usb_count && sink.din_count == 1u);
    assert(thy_core_release(&core, 0u) == THY_OK);
    poll(true, 1u);
    assert(sink.usb_attempts == 2u && sink.din_count == 2u);
    assert(transport.usb_queue.count == 4u && !thy_core_queued(&core));
    din_is(0u, 0x97u, 0u, 103u); din_is(1u, 0x87u, 0u, 0u);
    sink.usb_quota = -1; poll(true, 1u);
    assert(sink.usb_count == 4u && !transport.usb_queue.count);
    usb_is(0u, 0u, 0x97u, 0u, 103u); usb_is(1u, 1u, 0x97u, 0u, 103u);
    usb_is(2u, 0u, 0x87u, 0u, 0u); usb_is(3u, 1u, 0x87u, 0u, 0u);
}
static void test_din_busy_exact_note_off_is_retained(void)
{
    ready(); int16_t note = 61;
    assert(thy_core_press(&core, 0u, &note, 1u, 0, 2u, THY_ROUTE_DIN, 99u) == THY_OK);
    poll(true, 1u);
    assert(thy_core_release(&core, 0u) == THY_OK);
    sink.din_quota = 0; poll(true, 1u); poll(true, 1u);
    assert(transport.din_queue.count == 1u && sink.din_count == 1u);
    assert(sink.din_attempts == 3u && !thy_core_sounding(&core));
    sink.din_quota = -1; poll(true, 1u);
    din_is(1u, 0x82u, 61u, 0u);
}
static void test_usb_disconnect_keeps_din_sustain_strum_and_owners(void)
{
    ready(); const int16_t notes[] = {60, 64, 67};
    thy_shape shape = {.strum_ms = 10u};
    assert(thy_core_press_shaped(&core, 0u, notes, 3u, 0, 4u,
        THY_ROUTE_USB0 | THY_ROUTE_DIN, 100u, &shape) == THY_OK);
    assert(thy_core_sustain(&core, THY_ROUTE_USB0 | THY_ROUTE_DIN, 4u, 1) == THY_OK);
    poll(true, 1u);
    assert(sink.din_count == 2u);
    poll(false, 2u);
    assert(thy_instrument_transport_ready(&transport));
    assert(thy_core_pad_held(&core, 0u) && thy_core_pending(&core) == 4u);
    assert(core.sustain_count == 2u);
    assert(thy_core_advance(&core, 20u) == THY_OK); poll(false, 2u);
    assert(sink.din_count == 4u);
    din_is(2u, 0x94u, 64u, 100u); din_is(3u, 0x94u, 67u, 100u);
    assert(thy_core_release(&core, 0u) == THY_OK);
    assert(thy_core_sustain(&core, THY_ROUTE_USB0 | THY_ROUTE_DIN, 4u, 0) == THY_OK);
    poll(false, 2u);
    assert(sink.din_count == 8u && !thy_core_sounding(&core));
    din_is(4u, 0xb4u, 64u, 0u);
    din_is(5u, 0x84u, 60u, 0u); din_is(6u, 0x84u, 64u, 0u); din_is(7u, 0x84u, 67u, 0u);
}
static void test_same_mount_boolean_new_epoch_never_replays_held_attack(void)
{
    ready(); int16_t note = 55;
    assert(thy_core_press(&core, 1u, &note, 1u, 0, 15u,
        THY_ROUTE_USB1 | THY_ROUTE_DIN, 127u) == THY_OK);
    poll(true, 1u); clear_capture();
    thy_instrument_transport_poll(&transport, true, 3u, 0u);
    assert(transport.session_changes == 2u && thy_core_pad_held(&core, 1u));
    for (unsigned i = 0u; i < 7u; ++i) poll(true, 3u);
    assert(sink.usb_count == 192u && !sink.din_count);
    assert(thy_core_release(&core, 1u) == THY_OK); poll(true, 3u);
    assert(sink.usb_count == 192u && sink.din_count == 1u);
    din_is(0u, 0x8fu, 55u, 0u);
    assert(thy_core_press(&core, 1u, &note, 1u, 0, 15u,
        THY_ROUTE_USB1 | THY_ROUTE_DIN, 127u) == THY_OK);
    poll(true, 3u); usb_is(192u, 1u, 0x9fu, 55u, 127u);
}
static void test_cancelled_old_pending_strum_does_not_block_later_press(void)
{
    ready(); const int16_t notes[] = {60, 64, 67}; thy_shape shape = {.strum_ms = 10u};
    assert(thy_core_press_shaped(&core, 0u, notes, 3u, 0, 0u,
        THY_ROUTE_USB0 | THY_ROUTE_DIN, 96u, &shape) == THY_OK);
    poll(true, 1u); poll(false, 2u);
    assert(thy_core_release(&core, 0u) == THY_OK); poll(false, 2u);
    for (unsigned i = 0u; i < 7u; ++i) poll(true, 3u);
    clear_capture(); int16_t note = 64;
    assert(thy_core_press(&core, 0u, &note, 1u, 0, 0u,
        THY_ROUTE_USB0 | THY_ROUTE_DIN, 96u) == THY_OK);
    poll(true, 3u);
    assert(sink.usb_count == 1u && sink.din_count == 1u);
    usb_is(0u, 0u, 0x90u, 64u, 96u);
}
static void test_overlap_still_releases_only_last_din_owner_after_reconnect(void)
{
    ready(); int16_t note = 60;
    for (unsigned pad = 0u; pad < 2u; ++pad)
        assert(thy_core_press(&core, pad, &note, 1u, 0, 0u,
            THY_ROUTE_USB0 | THY_ROUTE_DIN, 96u) == THY_OK);
    poll(true, 1u); poll(false, 2u);
    assert(thy_core_release(&core, 0u) == THY_OK); poll(false, 2u);
    assert(sink.din_count == 1u);
    for (unsigned i = 0u; i < 7u; ++i) poll(true, 3u);
    assert(thy_core_release(&core, 1u) == THY_OK); poll(true, 3u);
    assert(sink.din_count == 2u); din_is(1u, 0x80u, 60u, 0u);
}
static void test_usb_overrun_reconciles_usb_without_losing_din(void)
{
    ready(); sink.usb_quota = 0;
    for (unsigned i = 0u; i < 1100u; ++i) {
        assert(thy_core_cc(&core, THY_ROUTE_USB0 | THY_ROUTE_DIN, 0u, 1u,
            (uint8_t)(i % 128u)) == THY_OK);
        poll(true, 1u);
    }
    assert(transport.usb_overruns == 1u && transport.discarded_events >= 1025u);
    assert(sink.din_count == 1100u && !sink.usb_count);
    assert(thy_instrument_transport_din_ready(&transport));
    sink.usb_quota = -1;
    for (unsigned i = 0u; i < 8u; ++i) poll(true, 1u);
    assert(thy_instrument_transport_usb_ready(&transport));
}
static void test_full_din_queue_keeps_guaranteed_note_off(void)
{
    ready(); int16_t note = 60;
    assert(thy_core_press(&core, 0u, &note, 1u, 0, 0u, THY_ROUTE_DIN, 96u) == THY_OK);
    poll(true, 1u); sink.din_quota = 0;
    for (unsigned i = 0u; i < 1024u; ++i) {
        assert(thy_core_cc(&core, THY_ROUTE_DIN, 0u, 1u, (uint8_t)(i % 128u)) == THY_OK);
        poll(true, 1u);
    }
    assert(transport.din_queue.count == 1024u);
    assert(thy_core_release(&core, 0u) == THY_OK); poll(true, 1u);
    assert(transport.pending_valid && transport.pending.status == 0x80u);
    sink.din_quota = -1;
    for (unsigned i = 0u; i < 40u; ++i) poll(true, 1u);
    assert(!transport.din_queue.count && !transport.pending_valid);
    assert(sink.din_count == 1026u); din_is(1025u, 0x80u, 60u, 0u);
}
static void test_corrupt_event_retained_once_and_din_fault_not_hidden_by_usb_epoch(void)
{
    ready(); core.queue_head = 0u; core.queue_count = 1u;
    core.queue[0] = (thy_midi_event){255u, 0x90u, 60u, 127u};
    poll(true, 1u); poll(true, 1u);
    assert(transport.pending_valid && transport.invalid_pending && transport.invalid_events == 1u);
    assert(!thy_instrument_transport_ready(&transport));
    poll(false, 2u);
    assert(transport.pending_valid && transport.invalid_pending && transport.invalid_events == 1u);
}
static void test_one_two_three_byte_din_messages_in_order(void)
{
    ready(); core.queue_head = 0u; core.queue_count = 3u;
    core.queue[0] = (thy_midi_event){THY_DIN, 0xf8u, 0u, 0u};
    core.queue[1] = (thy_midi_event){THY_DIN, 0xc3u, 12u, 0u};
    core.queue[2] = (thy_midi_event){THY_DIN, 0xa3u, 60u, 101u};
    poll(true, 1u);
    assert(sink.din_count == 3u && sink.din_lengths[0] == 1u && sink.din[0][0] == 0xf8u);
    assert(sink.din_lengths[1] == 2u && sink.din[1][0] == 0xc3u && sink.din[1][1] == 12u);
    din_is(2u, 0xa3u, 60u, 101u);
}
static void test_wrapped_core_compaction_keeps_din_order(void)
{
    ready(); core.queue_head = 1000u;
    for (unsigned i = 0u; i < 64u; ++i)
        assert(thy_core_cc(&core, THY_ROUTE_USB0 | THY_ROUTE_DIN, 0u, 1u, i) == THY_OK);
    thy_instrument_transport_poll(&transport, false, 2u, 0u);
    assert(thy_core_queued(&core) == 64u);
    for (unsigned i = 0u; i < 3u; ++i) poll(false, 2u);
    assert(sink.din_count == 64u);
    for (unsigned i = 0u; i < 64u; ++i) din_is(i, 0xb0u, 1u, i);
}
static void test_sustain_transition_during_usb_restore_is_not_lost(void)
{
    ready(); poll(false, 2u);
    for (unsigned i = 0u; i < 6u; ++i) poll(true, 3u);
    /* Restore channel0 while off, then change it before other routes finish. */
    thy_instrument_transport_poll(&transport, true, 3u, 1u);
    assert(thy_core_sustain(&core, THY_ROUTE_USB0 | THY_ROUTE_DIN, 0u, 1) == THY_OK);
    poll(true, 3u); poll(true, 3u);
    assert(thy_instrument_transport_usb_ready(&transport));
    usb_is(sink.usb_count - 1u, 0u, 0xb0u, 64u, 127u);
}
static void test_released_old_strum_backlog_cannot_replay_into_new_usb_session(void)
{
    ready(); int16_t notes[64];
    for (unsigned i = 0u; i < 64u; ++i) notes[i] = i;
    thy_shape shape = {.strum_ms = 10u};
    assert(thy_core_press_shaped(&core, 0u, notes, 64u, 0, 0u,
        THY_ROUTE_USB0 | THY_ROUTE_DIN, 96u, &shape) == THY_OK);
    poll(true, 1u); poll(false, 2u);
    for (unsigned i = 0u; i < 7u; ++i) poll(true, 3u);
    clear_capture();
    assert(thy_core_advance(&core, 630u) == THY_OK);
    assert(thy_core_release(&core, 0u) == THY_OK);
    /* More than a per-call budget of queued old attacks remain after release.
     * Zero refs must not cause their blocks to disappear before they drain. */
    for (unsigned i = 0u; i < 260u; ++i)
        thy_instrument_transport_poll(&transport, true, 3u, 1u);
    assert(sink.usb_count == 0u && sink.din_count == 127u);
    for (unsigned i = 0u; i < sizeof transport.usb_blocked; ++i)
        assert(transport.usb_blocked[i] == 0u);
    int16_t note = 42;
    assert(thy_core_press(&core, 1u, &note, 1u, 0, 0u,
        THY_ROUTE_USB0 | THY_ROUTE_DIN, 96u) == THY_OK);
    poll(true, 3u); usb_is(0u, 0u, 0x90u, 42u, 96u);
}
static void test_null_arguments(void)
{
    thy_instrument_transport_init(NULL, NULL, NULL);
    thy_instrument_transport_poll(NULL, false, 0u, 32u);
    assert(!thy_instrument_transport_ready(NULL));
    thy_instrument_transport_init(&transport, NULL, NULL); poll(false, 0u);
    assert(!thy_instrument_transport_ready(&transport));
}
static void expression(uint8_t destinations,uint8_t channel,int neutral)
{
    assert(thy_core_channel_pressure(&core,destinations,channel,neutral?0u:110u)==THY_OK);
    assert(thy_core_bend(&core,destinations,channel,neutral?8192:16383)==THY_OK);
    assert(thy_core_cc(&core,destinations,channel,74u,neutral?64u:100u)==THY_OK);
}
static void reconnect_old_note(void)
{
    ready();int16_t note=60;
    assert(thy_core_press(&core,0,&note,1,0,3,THY_ROUTE_USB0|THY_ROUTE_DIN,96)==THY_OK);
    poll(true,1);clear_capture();
    thy_instrument_transport_poll(&transport,true,2,0);
}
static void finish_reconnect(void)
{for(unsigned n=0;n<8u;++n)poll(true,2);assert(thy_instrument_transport_usb_ready(&transport));}
static void test_expression_cleanup_retries_exact_phase(void)
{
    initialize(false);sink.usb_quota=3;
    poll(true,1);assert(sink.usb_count==3&&transport.reconcile_index==3);
    unsigned attempts=sink.usb_attempts;poll(true,1);
    assert(sink.usb_count==3&&transport.reconcile_index==3&&sink.usb_attempts==attempts+1);
    sink.usb_quota=1;poll(true,1);
    assert(sink.usb_count==4&&transport.reconcile_index==4);usb_is(3,0,0xd0,0,0);
    sink.usb_quota=1;poll(true,1);
    assert(sink.usb_count==5&&transport.reconcile_index==5);usb_is(4,0,0xe0,0,64);
    sink.usb_quota=1;poll(true,1);
    assert(sink.usb_count==6&&transport.reconcile_index==6);usb_is(5,0,0xb0,74,64);
    sink.usb_quota=-1;for(unsigned n=0;n<8u;++n)poll(true,1);
    assert(sink.usb_count==192&&thy_instrument_transport_usb_ready(&transport)&&!sink.din_count);
}
static void test_old_channel_expression_is_suppressed_during_and_after_cleanup(void)
{
    reconnect_old_note();unsigned suppressed=transport.usb_suppressed;
    expression(THY_ROUTE_USB0|THY_ROUTE_DIN,3,0);
    poll(true,2);
    assert(transport.usb_suppressed==suppressed+3&&sink.usb_count==32&&sink.din_count==3);
    assert(sink.din_lengths[0]==2&&sink.din[0][0]==0xd3&&sink.din[0][1]==110);
    din_is(1,0xe3,127,127);din_is(2,0xb3,74,100);
    finish_reconnect();assert(sink.usb_count==192&&thy_core_pad_held(&core,0));
    clear_capture();expression(THY_ROUTE_USB0|THY_ROUTE_DIN,3,0);poll(true,2);
    assert(!sink.usb_count&&sink.din_count==3&&transport.usb_suppressed==suppressed+6);
    expression(THY_ROUTE_USB0|THY_ROUTE_DIN,3,1);poll(true,2);
    assert(sink.usb_count==3&&sink.din_count==6);
    usb_is(0,0,0xd3,0,0);usb_is(1,0,0xe3,0,64);usb_is(2,0,0xb3,74,64);
}
static void test_fresh_note_on_old_channel_allows_expression_until_release(void)
{
    reconnect_old_note();finish_reconnect();clear_capture();int16_t note=62;
    assert(thy_core_press(&core,1,&note,1,0,3,1,96)==THY_OK);
    expression(1,3,0);poll(true,2);assert(sink.usb_count==4);
    usb_is(0,0,0x93,62,96);usb_is(1,0,0xd3,110,0);
    usb_is(2,0,0xe3,127,127);usb_is(3,0,0xb3,74,100);
    clear_capture();assert(thy_core_release(&core,1)==THY_OK);
    expression(1,3,0);poll(true,2);assert(sink.usb_count==1);
    usb_is(0,0,0x83,62,0);
    assert(thy_core_pad_held(&core,0)&&thy_core_sounding(&core)==2);
}
static void test_fresh_short_tap_retains_ordered_expression_before_its_off(void)
{
    reconnect_old_note();finish_reconnect();clear_capture();int16_t note=62;
    assert(thy_core_press(&core,1,&note,1,0,3,1,96)==THY_OK);
    assert(thy_core_channel_pressure(&core,1,3,110)==THY_OK);
    assert(thy_core_release(&core,1)==THY_OK);
    poll(true,2);assert(sink.usb_count==3);
    usb_is(0,0,0x93,62,96);usb_is(1,0,0xd3,110,0);usb_is(2,0,0x83,62,0);
    clear_capture();
    assert(thy_core_press(&core,1,&note,1,0,3,1,96)==THY_OK);
    assert(thy_core_release(&core,1)==THY_OK);
    assert(thy_core_channel_pressure(&core,1,3,110)==THY_OK);
    poll(true,2);assert(sink.usb_count==2);
    usb_is(0,0,0x93,62,96);usb_is(1,0,0x83,62,0);
}
int main(void)
{
    test_standalone_din_cleanup_and_note_release();
    test_usb_cleanup_order_budget_and_usb_only_compatibility();
    test_busy_usb_cannot_starve_din_or_note_off();
    test_din_busy_exact_note_off_is_retained();
    test_usb_disconnect_keeps_din_sustain_strum_and_owners();
    test_same_mount_boolean_new_epoch_never_replays_held_attack();
    test_cancelled_old_pending_strum_does_not_block_later_press();
    test_overlap_still_releases_only_last_din_owner_after_reconnect();
    test_usb_overrun_reconciles_usb_without_losing_din();
    test_full_din_queue_keeps_guaranteed_note_off();
    test_corrupt_event_retained_once_and_din_fault_not_hidden_by_usb_epoch();
    test_one_two_three_byte_din_messages_in_order();
    test_wrapped_core_compaction_keeps_din_order();
    test_sustain_transition_during_usb_restore_is_not_lost();
    test_released_old_strum_backlog_cannot_replay_into_new_usb_session();
    test_null_arguments();
    test_expression_cleanup_retries_exact_phase();
    test_old_channel_expression_is_suppressed_during_and_after_cleanup();
    test_fresh_note_on_old_channel_allows_expression_until_release();
    test_fresh_short_tap_retains_ordered_expression_before_its_off();
    puts("instrument transport tests passed: 20 scenarios");
    return 0;
}
