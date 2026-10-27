/* SPDX-License-Identifier: GPL-3.0-or-later
 * Integer format macros used by TinyUSB's disabled diagnostic logging.
 * This compatibility surface is for the 32-bit ARM target only. */
#ifndef THY_FREESTANDING_INTTYPES_H
#define THY_FREESTANDING_INTTYPES_H
#include <stdint.h>
#if __SIZEOF_POINTER__ != 4
#error This header is for the 32-bit freestanding diagnostic target
#endif
#define PRId8 "d"
#define PRIu8 "u"
#define PRIx8 "x"
#define PRId16 "d"
#define PRIu16 "u"
#define PRIx16 "x"
#define PRId32 "d"
#define PRIi32 "i"
#define PRIu32 "u"
#define PRIx32 "x"
#define PRIX32 "X"
#define PRId64 "lld"
#define PRIu64 "llu"
#define PRIx64 "llx"
#define PRIxPTR "x"
#define PRIuPTR "u"
#endif
