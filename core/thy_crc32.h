/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_CRC32_H
#define THY_CRC32_H
#include <stddef.h>
#include <stdint.h>

/* Reflected IEEE CRC32. Update takes an unfinalized state; start with
 * UINT32_MAX and complement once after the final chunk. The byte range must
 * be readable when count is nonzero. Zero-length updates accept NULL. */
uint32_t thy_crc32_update(uint32_t state,const uint8_t *bytes,size_t count);
uint32_t thy_crc32(const uint8_t *bytes,size_t count);

#endif
