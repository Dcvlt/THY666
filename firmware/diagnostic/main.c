/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "board.h"
#include <stdbool.h>
#include "led.h"
#include "controls.h"
#include "pads.h"
#include "center.h"
#if THY_DIAG_WITH_USB
#include "usb.h"
#include "protocol.h"
#endif

/* Transport can replace these hooks. No hardware action occurs on the host. */
#if !THY_DIAG_WITH_USB
__attribute__((weak)) bool thy_diag_usb_init(void) { return false; }
__attribute__((weak)) void thy_diag_usb_task(void) {}
#endif

int main(void)
{
    board_init();
    board_led_init();
    board_controls_init();
    board_pads_init();
    board_center_init();
    if (g_diag_mailbox.errors & DIAG_ERROR_ENTRY_STATE) {
        /* No synthetic exception return or privilege escalation. A rejected
         * loader handoff is displayed before enabling IRQs or USB. */
        board_boot_stage(UINT32_C(0xbad));
        for (;;) __asm volatile("nop");
    }
    /* Dim center legends are a boot default for this diagnostic. The
     * independent CPU driver is serviced by board_poll, never by pad DMA. */
    (void)board_center_request(DIAG_CENTER_ALL, 0u, DIAG_CENTER_DEFAULT_BRIGHTNESS);
    __asm volatile("cpsie i" ::: "memory");
#if THY_DIAG_WITH_USB
    g_diag_mailbox.core_selftest = thy_diag_core_selftest();
#endif
    board_boot_stage(6u); /* Reached USB init; remains visible if stack hangs. */
    if (!thy_diag_usb_init()) g_diag_mailbox.errors |= DIAG_ERROR_USB_INIT;
    board_boot_stage(5u);
    for (;;) {
        thy_diag_usb_task();
        board_poll();
        board_controls_task(board_millis());
    }
}
