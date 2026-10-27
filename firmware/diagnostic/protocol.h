/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_DIAGNOSTIC_PROTOCOL_H
#define THY_DIAGNOSTIC_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

#define THY_DIAG_LINE_MAX 96u
#define THY_DIAG_REPLY_MAX 2048u
/* One command / one complete CRLF-terminated response. No persistent writes,
 * reset, update-mode transition, arbitrary memory access or MIDI transmission.
 * Requires at least THY_DIAG_REPLY_MAX output bytes for all commands. */
size_t thy_diag_command(const char *line, char *output, size_t capacity);
/* Tests ownership/release and pitch-bend encoding in RAM. No transport output. */
uint32_t thy_diag_core_selftest(void);
#endif
