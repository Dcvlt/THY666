/* SPDX-License-Identifier: GPL-3.0-or-later
 * Instrument acquisition profile using the tested ADC/mux/cleanup driver.
 * The diagnostic keeps its 2ms settling and ten-second bounds unchanged.
 * The instrument uses a finite ten-second lease, renewed by its main loop,
 * and a conservative initial 150us DWT-based mux settle. The reference implementation immediately
 * restarts acquisition after mux selection; 150us is our initial policy and
 * must be measured for noise and velocity before being called calibrated. */
#define board_pads_task thy_slow_pad_task_unused
#include "../diagnostic/pads.c"
#undef board_pads_task

static uint32_t settle_cycles;

void board_pads_task(uint32_t now)
{
    if (!running) return;
    if (pad_report.state == DIAG_PADS_SETTLE && !stop_requested &&
        (int32_t)(now - pad_report.expires_ms) < 0) {
        uint32_t cycles = THY_PADS_MMIO32(UINT32_C(0xe0001004));
        if ((uint32_t)(cycles - settle_cycles) < 150u * 168u) {
            /* SysTick is independent of DWT. Renewing the acquisition lease
             * must not hide a stalled microsecond timebase and held notes. */
            if ((uint32_t)(now - selected_ms) >= 2u)
                fail_and_restore(now, DIAG_TIMEOUT);
            return;
        }
        /* The tested branch owns all validation and start/error handling. */
        selected_ms = now - 2u;
    }
    uint32_t previous = pad_report.state;
    thy_slow_pad_task_unused(now);
    if (pad_report.state == DIAG_PADS_SETTLE && previous != DIAG_PADS_SETTLE)
        settle_cycles = THY_PADS_MMIO32(UINT32_C(0xe0001004));
}

void thy_instrument_pads_renew(uint32_t now)
{
    if (running && pad_report.state != DIAG_PADS_FAULT && !stop_requested)
        pad_report.expires_ms = now + DIAG_PAD_TEST_MS;
}
