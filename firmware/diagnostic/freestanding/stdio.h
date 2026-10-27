/* SPDX-License-Identifier: GPL-3.0-or-later
 * TinyUSB includes stdio even at CFG_TUSB_DEBUG=0. No stdio implementation is
 * linked; enabling logging must supply a real transport and implementation. */
#ifndef THY_FREESTANDING_STDIO_H
#define THY_FREESTANDING_STDIO_H
#include <stddef.h>
int printf(const char *format, ...);
int snprintf(char *output, size_t count, const char *format, ...);
#endif
