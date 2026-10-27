/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_SONG_TRANSFER_H
#define THY_SONG_TRANSFER_H
#include "thy_engine.h"
enum thy_song_transfer_state { THY_SONG_IDLE, THY_SONG_UPLOAD, THY_SONG_DOWNLOAD };
typedef struct thy_song_transfer {
    uint8_t *buffer;
    size_t capacity;
    uint32_t bytes, received, crc, last_ms;
    uint8_t state;
} thy_song_transfer;
void thy_song_transfer_init(thy_song_transfer *,uint8_t *,size_t);
int thy_song_transfer_expire(thy_song_transfer *,uint32_t now);
/* Bounded SONG/1 protocol, one complete response per line. available is false
 * while the shared staging buffer is owned by an in-progress journal write.
 * Only a fully received, CRC/semantic-valid commit replaces musical content;
 * import stops the sequencer and leaves independent live owners untouched. */
size_t thy_song_transfer_command(thy_song_transfer *,thy_engine *,int available,
    uint32_t now,const char *line,char *response,size_t capacity);
#endif
