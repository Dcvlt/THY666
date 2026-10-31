/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual transport and ownership engine, independent atomic sink mocks. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/instrument/transport.h"
#include "thy_profile.h"
typedef struct fake_sink {
    uint8_t usb[8192][4], din[8192][3];
    size_t din_lengths[8192];
    unsigned usb_count, din_count, usb_attempts, din_attempts;
    int usb_quota, din_quota;
} fake_sink;
static thy_core core;
static thy_instrument_transport transport;
static fake_sink sink;
static thy_profile profile;
static thy_profile *trace_profile;
static unsigned trace_start_profile;
static uint32_t profile_now_ms;
static unsigned profile_clock_calls;
static uint32_t profile_clock(void *context)
{
    assert(context == &profile_now_ms);
    ++profile_clock_calls;
    return profile_now_ms;
}
static void block_hint_is_conservative(void)
{
    /* The local archived-source differential also uses this harness; the old
     * implementation has no internal hint, so compare only its public state. */
#ifndef THY_TRANSPORT_REFERENCE
    if (transport.usb_blocks_pending) return;
    for (unsigned i = 0u; i < sizeof transport.usb_blocked; ++i)
        assert(transport.usb_blocked[i] == 0u);
#endif
}
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
{
    thy_instrument_transport_poll(&transport, mounted, epoch, 32u);
    block_hint_is_conservative();
}
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
static void test_empty_map_preserves_expression_on_every_usb_channel(void)
{
    ready();
    for(unsigned neutral=0u;neutral<2u;++neutral)
        for(unsigned channel=0u;channel<16u;++channel){
            clear_capture();expression(THY_ROUTE_ALL,(uint8_t)channel,(int)neutral);
            poll(true,1u);
            assert(sink.usb_count==6u && sink.din_count==3u && !transport.usb_suppressed);
            for(unsigned cable=0u;cable<2u;++cable){
                usb_is(cable,(uint8_t)cable,(uint8_t)(0xd0u|channel),neutral?0u:110u,0u);
                usb_is(2u+cable,(uint8_t)cable,(uint8_t)(0xe0u|channel),neutral?0u:127u,neutral?64u:127u);
                usb_is(4u+cable,(uint8_t)cable,(uint8_t)(0xb0u|channel),74u,neutral?64u:100u);
            }
            assert(sink.din_lengths[0]==2u && sink.din[0][0]==(uint8_t)(0xd0u|channel));
            assert(sink.din[0][1]==(neutral?0u:110u));
            din_is(1u,(uint8_t)(0xe0u|channel),neutral?0u:127u,neutral?64u:127u);
            din_is(2u,(uint8_t)(0xb0u|channel),74u,neutral?64u:100u);
        }
}
static void test_final_old_off_then_expression_keeps_queue_order(void)
{
    reconnect_old_note();finish_reconnect();clear_capture();
    unsigned suppressed=transport.usb_suppressed;
    sink.usb_quota=0;
    assert(thy_core_release(&core,0u)==THY_OK);
    expression(THY_ROUTE_USB0|THY_ROUTE_DIN,3u,0);
    poll(true,2u);
    /* The old Off clears its bit during staging, before the hint is refreshed.
     * Following controls must pass even with that conservatively stale hint. */
    assert(transport.usb_suppressed==suppressed+1u);
    assert(sink.usb_count==0u && transport.usb_queue.count==3u && sink.din_count==4u);
    din_is(0u,0x83u,60u,0u);
    assert(sink.din_lengths[1]==2u && sink.din[1][0]==0xd3u && sink.din[1][1]==110u);
    din_is(2u,0xe3u,127u,127u);din_is(3u,0xb3u,74u,100u);
#ifndef THY_TRANSPORT_REFERENCE
    assert(!transport.usb_blocks_pending);
#endif
    sink.usb_quota=-1;poll(true,2u);
    assert(sink.usb_count==3u && !transport.usb_queue.count && !thy_core_sounding(&core));
    usb_is(0u,0u,0xd3u,110u,0u);usb_is(1u,0u,0xe3u,127u,127u);usb_is(2u,0u,0xb3u,74u,100u);
}
static void test_empty_block_map_rearms_after_idle_and_zero_budget_epoch(void)
{
    ready();
    for (unsigned i = 0u; i < 100u; ++i) poll(true, 1u);
    assert(!sink.usb_attempts && !sink.din_attempts);
#ifndef THY_TRANSPORT_REFERENCE
    assert(!transport.usb_blocks_pending);
#endif
    int16_t note = 127;
    assert(thy_core_press(&core, 0u, &note, 1u, 0, 15u, THY_ROUTE_ALL, 96u) == THY_OK);
    poll(true, 1u);
    thy_instrument_transport_poll(&transport, true, 2u, 0u);
    block_hint_is_conservative();
#ifndef THY_TRANSPORT_REFERENCE
    assert(transport.usb_blocks_pending);
#endif
    for (unsigned i = 0u; i < 8u; ++i) poll(true, 2u);
    clear_capture();
    assert(thy_core_release(&core, 0u) == THY_OK); poll(true, 2u);
    assert(!sink.usb_count && sink.din_count == 1u);
    din_is(0u, 0x8fu, 127u, 0u);
#ifndef THY_TRANSPORT_REFERENCE
    assert(!transport.usb_blocks_pending);
#endif
    /* A later disconnected attack must re-arm the hint after a completely
     * empty interval, even though its USB attack is suppressed at staging. */
    poll(false, 3u); clear_capture();
    note = 0;
    assert(thy_core_press(&core, 0u, &note, 1u, 0, 0u, THY_ROUTE_ALL, 97u) == THY_OK);
    poll(false, 3u);
#ifndef THY_TRANSPORT_REFERENCE
    assert(transport.usb_blocks_pending);
#endif
    assert(thy_core_release(&core, 0u) == THY_OK); poll(false, 3u);
    assert(!sink.usb_count && sink.din_count == 2u);
    for (unsigned i = 0u; i < 8u; ++i) poll(true, 4u);
#ifndef THY_TRANSPORT_REFERENCE
    assert(!transport.usb_blocks_pending);
#endif
}
static void test_sustained_overlap_keeps_block_until_final_pedal_release(void)
{
    ready(); int16_t note = 60;
    assert(thy_core_sustain(&core, THY_ROUTE_ALL, 0u, 1) == THY_OK);
    for (unsigned pad = 0u; pad < 2u; ++pad)
        assert(thy_core_press(&core, (uint8_t)pad, &note, 1u, 0, 0u,
            THY_ROUTE_ALL, 96u) == THY_OK);
    poll(true, 1u); poll(false, 2u);
    for (unsigned pad = 0u; pad < 2u; ++pad)
        assert(thy_core_release(&core, (uint8_t)pad) == THY_OK);
    for (unsigned i = 0u; i < 8u; ++i) poll(true, 3u);
    assert(core.refs[60u] == 0u && thy_core_deferred(&core, 60u));
#ifndef THY_TRANSPORT_REFERENCE
    assert(transport.usb_blocks_pending);
#endif
    clear_capture(); sink.usb_quota = 0;
    assert(thy_core_sustain(&core, THY_ROUTE_ALL, 0u, 0) == THY_OK);
    /* The staged pedal message waits under backpressure while old note-offs
     * clear only their own blocks; DIN release order remains independent. */
    for (unsigned i = 0u; i < 8u; ++i) {
        thy_instrument_transport_poll(&transport, true, 3u, 1u);
        block_hint_is_conservative();
    }
    assert(!thy_core_sounding(&core) && sink.din_count == 2u);
    din_is(0u, 0xb0u, 64u, 0u); din_is(1u, 0x80u, 60u, 0u);
#ifndef THY_TRANSPORT_REFERENCE
    assert(!transport.usb_blocks_pending);
#endif
    sink.usb_quota = -1; poll(true, 3u);
    assert(sink.usb_count == 2u);
    usb_is(0u, 0u, 0xb0u, 64u, 0u); usb_is(1u, 1u, 0xb0u, 64u, 0u);
}
static uint32_t trace_byte(uint32_t hash, uint8_t value)
{ return (hash ^ value) * UINT32_C(16777619); }
static uint32_t trace_word(uint32_t hash, uint32_t value)
{
    for (unsigned i = 0u; i < 4u; ++i) hash = trace_byte(hash, (uint8_t)(value >> (i * 8u)));
    return hash;
}
static uint32_t trace_bytes(uint32_t hash, const uint8_t *bytes, unsigned count)
{
    for (unsigned i = 0u; i < count; ++i) hash = trace_byte(hash, bytes[i]);
    return hash;
}
static uint32_t trace_state(uint32_t hash)
{
    hash = trace_word(hash, sink.usb_count); hash = trace_word(hash, sink.din_count);
    hash = trace_word(hash, sink.usb_attempts); hash = trace_word(hash, sink.din_attempts);
    hash = trace_bytes(hash, &sink.usb[0][0], sink.usb_count * 4u);
    for (unsigned i = 0u; i < sink.din_count; ++i) {
        hash = trace_word(hash, (uint32_t)sink.din_lengths[i]);
        hash = trace_bytes(hash, sink.din[i], (unsigned)sink.din_lengths[i]);
    }
    hash = trace_bytes(hash, transport.usb_blocked, sizeof transport.usb_blocked);
    const uint32_t counts[] = {transport.session_epoch, transport.session_changes,
        transport.usb_accepted, transport.din_accepted, transport.backpressure,
        transport.invalid_events, transport.discarded_events, transport.usb_overruns,
        transport.usb_suppressed, transport.reconcile_index, transport.din_reconcile_index,
        transport.usb_restore_index, transport.mounted, transport.pending_valid,
        transport.invalid_pending, core.queue_count, core.sounding_count,
        core.pending_keys, core.sustain_count, core.now_ms};
    for (unsigned i = 0u; i < sizeof counts / sizeof counts[0]; ++i)
        hash = trace_word(hash, counts[i]);
    hash = trace_bytes(hash, core.refs, sizeof core.refs);
    hash = trace_bytes(hash, core.deferred_bits, sizeof core.deferred_bits);
    hash = trace_bytes(hash, core.sustain, sizeof core.sustain);
    for (unsigned pad = 0u; pad < THY_PAD_COUNT; ++pad) {
        const thy_pad_owner *owner = &core.owners[pad];
        hash = trace_word(hash, owner->count); hash = trace_word(hash, owner->held);
        hash = trace_word(hash, owner->activated); hash = trace_word(hash, owner->next_due);
        for (unsigned i = 0u; i < owner->count; ++i)
            hash = trace_word(hash, thy_pad_owner_key(owner, (uint8_t)i));
    }
    return hash;
}
static void test_seeded_session_ownership_trace(void)
{
    ready(); uint32_t random = UINT32_C(0x37652a91), hash = UINT32_C(2166136261);
    if(trace_profile){
        thy_instrument_transport_profile_bind(&transport,trace_profile,profile_clock,&profile_now_ms);
        assert(transport.profile==trace_profile);
        if(trace_start_profile)assert(thy_profile_start(trace_profile,60u,0u,168000000u));
    }
    uint32_t epoch = 1u, now = 0u; bool mounted = true;
    for (unsigned step = 0u; step < 2048u; ++step) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        unsigned action = (random >> 16u) % 12u;
        uint8_t pad = (uint8_t)((random >> 8u) % 12u), channel = (uint8_t)(pad % 4u);
        uint8_t routes = (uint8_t)(1u + ((random >> 24u) % THY_ROUTE_ALL));
        int result = THY_OK;
        clear_capture();
        sink.usb_quota = step % 11u < 4u ? 0 : (step % 7u == 0u ? 1 : -1);
        sink.din_quota = step % 17u < 2u ? 0 : -1;
        switch (action) {
        case 0u: mounted = !mounted; ++epoch; break;
        case 1u: ++epoch; break;
        case 2u: case 3u: {
            int16_t notes[] = {(int16_t)(48u + pad % 4u), (int16_t)(55u + pad % 4u)};
            thy_shape shape = {.strum_ms = (uint16_t)(action == 2u ? 0u : 10u)};
            result = thy_core_press_shaped(&core, pad, notes, 2u, 0, channel,
                routes, (uint8_t)(80u + pad), &shape); break;
        }
        case 4u: result = thy_core_release(&core, pad); break;
        case 5u: result = thy_core_sustain(&core, routes, channel, (random >> 7u) & 1u); break;
        case 6u: result = thy_core_pressure(&core, pad, (uint8_t)(random & 127u)); break;
        case 7u: result = thy_core_cc(&core, routes, channel, 74u, (uint8_t)(random & 127u)); break;
        case 8u: result = thy_core_channel_pressure(&core, routes, channel, (uint8_t)(random & 127u)); break;
        case 9u: result = thy_core_bend(&core, routes, channel, (uint16_t)(random & 16383u)); break;
        default: break;
        }
        now += random % 23u;
        profile_now_ms=now;
        hash = trace_word(hash, (uint32_t)result);
        hash = trace_word(hash, (uint32_t)thy_core_advance(&core, now));
        thy_instrument_transport_poll(&transport, mounted, epoch, step % 33u);
        block_hint_is_conservative(); hash = trace_state(hash);
    }
    thy_core_panic(&core); sink.usb_quota = sink.din_quota = -1;
    for (unsigned i = 0u; i < 80u; ++i) {
        clear_capture(); poll(true, epoch + 1u); hash = trace_state(hash);
    }
    assert(!thy_core_sounding(&core) && !thy_core_queued(&core));
    assert(!transport.usb_queue.count && !transport.din_queue.count && !transport.pending_valid);
#ifndef THY_TRANSPORT_REFERENCE
    assert(!transport.usb_blocks_pending);
#endif
    /* Frozen pre-optimization transport: emitted bytes, counter/order state,
     * blocked pitches and immutable owner keys after every synthetic action. */
    assert(hash == UINT32_C(0x381b76a6));
}
#ifndef THY_TRANSPORT_REFERENCE
static void start_profile(uint32_t now, uint32_t seconds)
{
    thy_profile_init(&profile);
    profile_clock_calls=0u;profile_now_ms=now;
    thy_instrument_transport_profile_bind(&transport,&profile,profile_clock,&profile_now_ms);
    assert(transport.profile==&profile);
    assert(thy_profile_start(&profile,seconds,now,168000000u));
}
static void test_inactive_profile_and_binding_are_inert(void)
{
    ready();thy_profile_init(&profile);profile_clock_calls=0u;profile_now_ms=100u;
    thy_instrument_transport_profile_bind(&transport,&profile,profile_clock,&profile_now_ms);
    thy_profile before=profile;
    assert(thy_core_cc(&core,THY_ROUTE_USB0|THY_ROUTE_DIN,0u,1u,2u)==THY_OK);
    poll(true,1u);
    assert(!profile_clock_calls && !memcmp(&profile,&before,sizeof profile));
    assert(sink.usb_count==1u && sink.din_count==1u);
    sink.usb_quota=0;
    assert(thy_core_cc(&core,THY_ROUTE_USB0,0u,1u,4u)==THY_OK);poll(true,1u);
    poll(false,2u);
    assert(!profile_clock_calls && !memcmp(&profile,&before,sizeof profile));
    sink.usb_quota=-1;
    assert(thy_profile_start(&profile,1u,100u,168000000u));
    thy_instrument_transport_profile_bind(&transport,NULL,NULL,NULL);
    assert(transport.profile==&profile); /* Active detach is rejected. */
    poll(true,1u);assert(profile_clock_calls);
    thy_profile_stop(&profile,100u);before=profile;
    unsigned clocks=profile_clock_calls;
    assert(thy_core_cc(&core,THY_ROUTE_USB1|THY_ROUTE_DIN,15u,1u,3u)==THY_OK);
    poll(true,1u);
    assert(profile_clock_calls==clocks && !memcmp(&profile,&before,sizeof profile));
    thy_instrument_transport_profile_bind(&transport,NULL,NULL,NULL);
    assert(!transport.profile && !transport.profile_now_ms && !transport.profile_context);
    assert(thy_profile_start(&profile,1u,100u,168000000u));
    thy_instrument_transport_profile_bind(&transport,&profile,profile_clock,&profile_now_ms);
    assert(!transport.profile); /* A running capture cannot inherit old rings. */
    thy_profile_stop(&profile,100u);
    thy_instrument_transport_profile_bind(NULL,&profile,profile_clock,&profile_now_ms);
    thy_instrument_transport_profile_bind(&transport,&profile,NULL,&profile_now_ms);
    assert(!transport.profile);
}
static void test_profile_queue_wait_ring_wrap_and_retry(void)
{
    ready();transport.usb_queue.head=transport.din_queue.head=1023u;
    start_profile(100u,60u);profile_now_ms=105u;
    sink.usb_quota=sink.din_quota=0;
    assert(thy_core_cc(&core,THY_ROUTE_USB0|THY_ROUTE_DIN,0u,1u,1u)==THY_OK);
    poll(true,1u);
    assert(profile.queue_stamps[0][1023]==6u && profile.queue_stamps[1][1023]==6u);
    assert(profile.peaks[THY_PROFILE_PEAK_CORE]==2u);
    profile_now_ms=113u;
    assert(thy_core_cc(&core,THY_ROUTE_USB0|THY_ROUTE_DIN,0u,1u,2u)==THY_OK);
    poll(true,1u);
    assert(profile.queue_stamps[0][0]==14u && profile.queue_stamps[1][0]==14u);
    assert(profile.stats[THY_PROFILE_USB_WAIT_MS].count==0u);
    assert(profile.stats[THY_PROFILE_DIN_WAIT_MS].count==0u);
    assert(profile.peaks[THY_PROFILE_PEAK_USB]==2u && profile.peaks[THY_PROFILE_PEAK_DIN]==2u);
    profile_now_ms=125u;sink.usb_quota=sink.din_quota=1;poll(true,1u);
    assert(transport.usb_queue.head==0u && transport.din_queue.head==0u);
    assert(!profile.queue_stamps[0][1023] && !profile.queue_stamps[1][1023]);
    assert(profile.queue_stamps[0][0]==14u && profile.queue_stamps[1][0]==14u);
    profile_now_ms=130u;sink.usb_quota=sink.din_quota=-1;poll(true,1u);
    for(unsigned q=0u;q<2u;++q){
        const thy_profile_stat *stat=&profile.stats[THY_PROFILE_USB_WAIT_MS+q];
        assert(stat->count==2u && stat->sum==37u && stat->min==17u && stat->max==20u);
        assert(!profile.queue_stamps[q][0] && !profile.excluded[q] && !profile.discarded[q]);
    }
    usb_is(0u,0u,0xb0u,1u,1u);usb_is(1u,0u,0xb0u,1u,2u);
    din_is(0u,0xb0u,1u,1u);din_is(1u,0xb0u,1u,2u);
    thy_profile_stop(&profile,profile_now_ms);
}
static void test_profile_preexisting_cleanup_and_restart(void)
{
    initialize(true);start_profile(0u,60u);
    for(unsigned n=0u;n<8u;++n)poll(true,1u);
    assert(sink.usb_count==192u && sink.din_count==48u);
    for(unsigned q=0u;q<2u;++q)
        assert(!profile.stats[THY_PROFILE_USB_WAIT_MS+q].count && !profile.excluded[q]);
    thy_profile_stop(&profile,0u);
    ready();sink.usb_quota=sink.din_quota=0;
    assert(thy_core_cc(&core,THY_ROUTE_USB0|THY_ROUTE_DIN,0u,1u,1u)==THY_OK);
    poll(true,1u);start_profile(100u,60u);profile_now_ms=105u;
    sink.usb_quota=sink.din_quota=-1;poll(true,1u);
    for(unsigned q=0u;q<2u;++q){
        assert(profile.excluded[q]==1u && !profile.stats[THY_PROFILE_USB_WAIT_MS+q].count);
        assert(profile.peaks[THY_PROFILE_PEAK_USB+q]==1u);
    }
    profile_now_ms=106u;sink.usb_quota=sink.din_quota=0;
    assert(thy_core_cc(&core,THY_ROUTE_USB0|THY_ROUTE_DIN,0u,1u,2u)==THY_OK);
    poll(true,1u);assert(profile.queue_stamps[0][1]==7u && profile.queue_stamps[1][1]==7u);
    thy_profile_stop(&profile,110u);
    assert(thy_profile_start(&profile,60u,110u,168000000u));
    assert(!profile.queue_stamps[0][1] && !profile.queue_stamps[1][1]);
    profile_now_ms=120u;sink.usb_quota=sink.din_quota=-1;poll(true,1u);
    for(unsigned q=0u;q<2u;++q)
        assert(profile.excluded[q]==1u && !profile.stats[THY_PROFILE_USB_WAIT_MS+q].count);
    thy_profile_stop(&profile,profile_now_ms);
}
static void test_profile_disconnect_discards_without_acceptance(void)
{
    ready();start_profile(100u,60u);profile_now_ms=110u;sink.usb_quota=0;
    assert(thy_core_cc(&core,THY_ROUTE_USB0|THY_ROUTE_DIN,0u,1u,1u)==THY_OK);
    poll(true,1u);assert(transport.usb_queue.count==1u);
    assert(thy_core_cc(&core,THY_ROUTE_USB1|THY_ROUTE_DIN,0u,1u,2u)==THY_OK);
    assert(thy_core_cc(&core,THY_ROUTE_USB0,0u,1u,3u)==THY_OK);
    profile_now_ms=120u;thy_instrument_transport_poll(&transport,false,2u,0u);
    assert(profile.active && profile.discarded[0]==3u && !profile.stats[THY_PROFILE_USB_WAIT_MS].count);
    assert(profile.peaks[THY_PROFILE_PEAK_CORE]==3u && core.queue_count==1u);
    for(unsigned slot=0u;slot<THY_QUEUE_CAPACITY;++slot)assert(!profile.queue_stamps[0][slot]);
    assert(profile.stats[THY_PROFILE_DIN_WAIT_MS].count==1u && !profile.discarded[1]);
    profile_now_ms=130u;poll(false,2u);
    assert(profile.stats[THY_PROFILE_DIN_WAIT_MS].count==2u && sink.din_count==2u);
    din_is(0u,0xb0u,1u,1u);din_is(1u,0xb0u,1u,2u);
    int16_t note=60;unsigned suppressed=transport.usb_suppressed;
    assert(thy_core_press(&core,0u,&note,1u,0,0u,THY_ROUTE_USB1|THY_ROUTE_DIN,96u)==THY_OK);
    poll(false,2u);assert(thy_core_release(&core,0u)==THY_OK);poll(false,2u);
    assert(transport.usb_suppressed==suppressed+2u && profile.discarded[0]==3u);
    assert(profile.stats[THY_PROFILE_DIN_WAIT_MS].count==4u);
    assert(!profile.stats[THY_PROFILE_USB_WAIT_MS].count && !profile.excluded[0]);
    din_is(2u,0x90u,60u,96u);din_is(3u,0x80u,60u,0u);
    sink.usb_quota=-1;for(unsigned n=0u;n<8u;++n)poll(true,3u);
    assert(profile.active && !profile.stats[THY_PROFILE_USB_WAIT_MS].count && !profile.excluded[0]);
    thy_profile_stop(&profile,profile_now_ms);
}
static void test_profile_usb_overrun_has_no_false_acceptance(void)
{
    ready();start_profile(0u,60u);sink.usb_quota=0;
    for(unsigned n=0u;n<1100u;++n){
        profile_now_ms=n;
        assert(thy_core_cc(&core,THY_ROUTE_USB0|THY_ROUTE_DIN,0u,1u,(uint8_t)(n%128u))==THY_OK);
        poll(true,1u);
    }
    assert(transport.usb_overruns==1u && profile.discarded[0]==1025u);
    assert(profile.peaks[THY_PROFILE_PEAK_USB]==THY_QUEUE_CAPACITY);
    assert(!profile.stats[THY_PROFILE_USB_WAIT_MS].count && !profile.excluded[0]);
    assert(profile.stats[THY_PROFILE_DIN_WAIT_MS].count==1100u);
    sink.usb_quota=-1;
    for(unsigned n=0u;n<12u;++n){profile_now_ms=1100u+n;poll(true,1u);}
    assert(!transport.usb_queue.count && profile.stats[THY_PROFILE_USB_WAIT_MS].count==75u);
    assert(profile.discarded[0]==1025u && !profile.excluded[0]);
    thy_profile_stop(&profile,profile_now_ms);
}
static void test_profile_timeout_wrap_and_frozen_stop(void)
{
    const uint32_t start=UINT32_MAX-499u;
    ready();start_profile(start,1u);profile_now_ms=start+999u;sink.usb_quota=0;
    assert(thy_core_cc(&core,THY_ROUTE_USB0,0u,1u,1u)==THY_OK);poll(true,1u);
    assert(profile.queue_stamps[0][0]==1000u && profile.active);
    unsigned clocks=profile_clock_calls;profile_now_ms=start+1000u;
    thy_instrument_transport_poll(&transport,true,1u,0u);
    assert(profile_clock_calls==clocks+1u && !profile.active && profile.status==THY_PROFILE_TIMED_OUT);
    assert(profile.elapsed_ms==1000u && transport.usb_queue.count==1u);
    thy_profile before=profile;clocks=profile_clock_calls;
    profile_now_ms=start+1001u;sink.usb_quota=-1;poll(true,1u);
    assert(profile_clock_calls==clocks && !memcmp(&profile,&before,sizeof profile));
    assert(sink.usb_count==1u && !transport.usb_queue.count);
    usb_is(0u,0u,0xb0u,1u,1u);
}
static void test_profile_active_and_disabled_keep_reference_stream(void)
{
    thy_profile_init(&profile);trace_profile=&profile;trace_start_profile=0u;profile_clock_calls=0u;
    thy_profile before=profile;test_seeded_session_ownership_trace();
    assert(!profile_clock_calls && !memcmp(&profile,&before,sizeof profile));
    trace_start_profile=1u;profile_clock_calls=0u;test_seeded_session_ownership_trace();
    assert(profile_clock_calls && profile.active && !profile.invalid_samples);
    assert(profile.stats[THY_PROFILE_DIN_WAIT_MS].count && profile.peaks[THY_PROFILE_PEAK_CORE]);
    trace_profile=NULL;trace_start_profile=0u;
    thy_profile_stop(&profile,profile_now_ms);
}
#endif
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
    test_empty_map_preserves_expression_on_every_usb_channel();
    test_final_old_off_then_expression_keeps_queue_order();
    test_empty_block_map_rearms_after_idle_and_zero_budget_epoch();
    test_sustained_overlap_keeps_block_until_final_pedal_release();
    test_seeded_session_ownership_trace();
#ifndef THY_TRANSPORT_REFERENCE
    test_inactive_profile_and_binding_are_inert();
    test_profile_queue_wait_ring_wrap_and_retry();
    test_profile_preexisting_cleanup_and_restart();
    test_profile_disconnect_discards_without_acceptance();
    test_profile_usb_overrun_has_no_false_acceptance();
    test_profile_timeout_wrap_and_frozen_stop();
    test_profile_active_and_disabled_keep_reference_stream();
#endif
    puts("instrument transport tests passed: 32 scenarios");
    return 0;
}
