/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_DIAGNOSTIC_USB_H
#define THY_DIAGNOSTIC_USB_H
#include <stdbool.h>
#include <stdint.h>

typedef struct thy_diag_usb_stats {
    uint32_t midi_received;
    uint32_t midi_invalid;
    uint32_t midi_unsupported;
    uint32_t cdc_commands;
    bool initialized;
    bool mounted;
} thy_diag_usb_stats;

/* Requires board_init(), running SysTick and a verified 48 MHz USB clock.
 * USB RX changes only volatile parser counters. No MIDI echo, memory writes,
 * update personality, mass storage, boot-mode command or device reset. */
bool thy_diag_usb_init(void);
void thy_diag_usb_task(void);
const thy_diag_usb_stats *thy_diag_usb_status(void);
/* Unique chip ID text for string descriptor; no read of application storage. */
void thy_diag_usb_serial(char out[25]);
#endif
