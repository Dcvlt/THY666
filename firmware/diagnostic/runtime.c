/* SPDX-License-Identifier: GPL-3.0-or-later
 * Small freestanding byte routines, with no allocation or OS dependency. */
#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t count) {
    volatile unsigned char *out = destination;
    const volatile unsigned char *in = source;
    for (size_t i = 0; i < count; ++i) out[i] = in[i];
    return destination;
}

void *memset(void *destination, int value, size_t count) {
    volatile unsigned char *out = destination;
    for (size_t i = 0; i < count; ++i) out[i] = (unsigned char)value;
    return destination;
}

void *memmove(void *destination, const void *source, size_t count) {
    volatile unsigned char *out = destination;
    const volatile unsigned char *in = source;
    if ((size_t)out > (size_t)in && (size_t)out - (size_t)in < count) {
        while (count) { --count; out[count] = in[count]; }
    } else for (size_t i = 0; i < count; ++i) out[i] = in[i];
    return destination;
}

int memcmp(const void *left, const void *right, size_t count) {
    const unsigned char *a = left;
    const unsigned char *b = right;
    for (size_t i = 0; i < count; ++i) if (a[i] != b[i]) return (int)a[i] - b[i];
    return 0;
}

size_t strlen(const char *text) {
    size_t count = 0;
    while (text[count]) ++count;
    return count;
}

void __aeabi_memcpy(void *destination, const void *source, size_t count) { (void)memcpy(destination, source, count); }
void __aeabi_memcpy4(void *destination, const void *source, size_t count) { (void)memcpy(destination, source, count); }
void __aeabi_memcpy8(void *destination, const void *source, size_t count) { (void)memcpy(destination, source, count); }
void __aeabi_memmove(void *destination, const void *source, size_t count) { (void)memmove(destination, source, count); }
void __aeabi_memset(void *destination, size_t count, int value) { (void)memset(destination, value, count); }
void __aeabi_memclr(void *destination, size_t count) { (void)memset(destination, 0, count); }
void __aeabi_memclr4(void *destination, size_t count) { (void)memset(destination, 0, count); }
void __aeabi_memclr8(void *destination, size_t count) { (void)memset(destination, 0, count); }
