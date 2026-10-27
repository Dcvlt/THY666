/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_FREESTANDING_STRING_H
#define THY_FREESTANDING_STRING_H
#include <stddef.h>
void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
size_t strlen(const char *text);
#endif
