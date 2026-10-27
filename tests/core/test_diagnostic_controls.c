/* Host tests of the actual RAM-only control observer; no peripheral access. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "board.h"
#include "controls.h"

static diag_report_t input;
const volatile diag_report_t *board_report(void) { return &input; }

static const diag_control_bank_t *bank(unsigned i)
{
    return &board_controls_report()->banks[i];
}

static void reset(void)
{
    input = (diag_report_t){0};
    board_controls_init();
}

static void sample(unsigned i, uint32_t raw, uint32_t ms)
{
    ++input.sensor[i].status_reads;
    input.sensor[i].last_status_ms = ms;
    input.touch_masks[i] = raw;
    input.touch_ok_mask |= UINT32_C(1) << i;
    board_controls_task(ms);
}

static void test_baseline_and_multi_bit(void)
{
    reset();
    board_controls_task(0u);
    assert(board_controls_report()->valid_mask == 0u);
    sample(0u, 0x100u, 10u);
    assert(bank(0u)->raw == 0x100u && bank(0u)->valid == 1u);
    assert(bank(0u)->samples == 1u && bank(0u)->seen_pressed == 0x100u);
    assert(bank(0u)->rose_latched == 0u && bank(0u)->fell_latched == 0u);
    assert(bank(0u)->changes == 0u && bank(0u)->last_change_ms == 0u);
    assert(bank(0u)->logical_pressed == 1u);
    /* A repeated task with no new AA sample must not count anything. */
    input.touch_masks[0] = 0u;
    board_controls_task(11u);
    assert(bank(0u)->raw == 0x100u && bank(0u)->samples == 1u);
    sample(0u, 0x1c0u, 20u);
    assert(bank(0u)->logical_pressed == 7u);
    assert(bank(0u)->rose_latched == 0xc0u && bank(0u)->changes == 2u);
    sample(0u, 0u, 30u);
    assert(bank(0u)->fell_latched == 0x1c0u && bank(0u)->changes == 5u);
    assert(bank(0u)->seen_pressed == 0x1c0u && bank(0u)->last_change_ms == 30u);
}

static void test_failure_and_recovery(void)
{
    reset();
    sample(2u, 8u, 10u);
    input.touch_ok_mask &= ~(UINT32_C(1) << 2);
    input.touch_masks[2] = 0u;
    board_controls_task(20u);
    assert(bank(2u)->valid == 0u && bank(2u)->raw == 8u);
    assert(bank(2u)->last_sample_ms == 10u && bank(2u)->fell_latched == 0u);
    /* Health cannot revalidate a stale publication. */
    input.touch_ok_mask |= UINT32_C(1) << 2;
    board_controls_task(21u);
    assert(bank(2u)->valid == 0u && bank(2u)->samples == 1u);
    sample(2u, 2u, 30u);
    assert(bank(2u)->valid == 1u && bank(2u)->raw == 2u);
    assert(bank(2u)->rose_latched == 0u && bank(2u)->fell_latched == 0u);
    assert(bank(2u)->seen_pressed == 10u && bank(2u)->changes == 0u);
    sample(2u, 0u, 40u);
    assert(bank(2u)->fell_latched == 2u && bank(2u)->changes == 1u);
}

static void test_clear_while_held_and_unknown(void)
{
    reset();
    sample(1u, 0u, 1u);
    sample(1u, 0x8040u, 2u);
    assert(bank(1u)->raw == 0x8040u && bank(1u)->rose_latched == 0x8040u);
    assert(bank(1u)->known_seen == 0x40u && bank(1u)->logical_pressed == 1u);
    assert(board_controls_report()->known_seen_count == 1u);
    board_controls_clear();
    assert(bank(1u)->raw == 0x8040u && bank(1u)->valid == 1u);
    assert(bank(1u)->seen_pressed == 0x8040u && bank(1u)->known_seen == 0x40u);
    assert(bank(1u)->rose_latched == 0u && bank(1u)->fell_latched == 0u);
    assert(bank(1u)->samples == 0u && bank(1u)->changes == 0u);
    assert(bank(1u)->last_sample_ms == 2u);
    board_controls_task(3u);
    assert(bank(1u)->samples == 0u);
    sample(1u, 0x8040u, 4u);
    assert(bank(1u)->samples == 1u && bank(1u)->changes == 0u);
    sample(1u, 0u, 5u);
    assert(bank(1u)->fell_latched == 0x8040u && bank(1u)->changes == 2u);
    assert(bank(1u)->logical_pressed == 0u);
    input.touch_ok_mask = 0u;
    board_controls_task(6u);
    board_controls_clear();
    assert(bank(1u)->valid == 0u && bank(1u)->seen_pressed == 0u);
    assert(board_controls_report()->known_seen_count == 0u);
}

static void test_mapping_and_coverage(void)
{
    static const uint16_t expected[4][14] = {
        {0x100,0x80,0x40,2,8},
        {0x40,0x20,1,2,0x80,0x10,8,0x100,0x200,0x400,0x800,0x1000,0x4000,4},
        {8,2,1,0x10,0x20,0x40,0x80,0x100,0x200,0x400,0x800,0x1000},
        {8,2,1,0x10,0x20,0x40,0x80,0x100,0x200,0x400,0x800,0x1000}
    };
    static const unsigned counts[] = {5,14,12,12};
    static const uint32_t masks[] = {0x01ca,0x5fff,0x1ffb,0x1ffb};
    reset();
    for (unsigned i = 0u; i < 4u; ++i) {
        assert(bank(i)->known_mask == masks[i]);
        for (unsigned j = 0u; j < counts[i]; ++j) {
            sample(i, expected[i][j], j + 1u);
            assert(bank(i)->logical_pressed == (UINT32_C(1) << j));
        }
        assert(bank(i)->known_seen == masks[i]);
        assert(bank(i)->logical_seen == (UINT32_C(1) << counts[i]) - 1u);
    }
    assert(board_controls_report()->known_seen_count == 43u);
    assert(board_controls_report()->total_known_controls == 43u);
    assert(board_controls_report()->valid_mask == 15u);
}

static void test_counter_and_time_wrap(void)
{
    reset();
    input.sensor[3].status_reads = UINT32_MAX - 1u;
    sample(3u, 0u, UINT32_MAX);
    assert(bank(3u)->samples == 1u);
    sample(3u, 8u, 0u);
    assert(input.sensor[3].status_reads == 0u);
    assert(bank(3u)->samples == 2u && bank(3u)->rose_latched == 8u);
    assert(bank(3u)->last_sample_ms == 0u && bank(3u)->last_change_ms == 0u);
    board_controls_task(1u);
    assert(bank(3u)->samples == 2u);
    /* A skipped publication cannot fabricate its intermediate press/release. */
    input.sensor[3].status_reads += 2u;
    input.sensor[3].last_status_ms = 2u;
    input.touch_masks[3] = 2u;
    board_controls_task(3u);
    assert(bank(3u)->samples == 3u && bank(3u)->changes == 3u);
    assert(bank(3u)->last_change_ms == 2u);
    assert(board_controls_report()->last_service_ms == 3u);
}

int main(void)
{
    test_baseline_and_multi_bit();
    test_failure_and_recovery();
    test_clear_while_held_and_unknown();
    test_mapping_and_coverage();
    test_counter_and_time_wrap();
    puts("diagnostic controls: baseline, edges, recovery, clear, 43-input mapping and wrap passed");
    return 0;
}
