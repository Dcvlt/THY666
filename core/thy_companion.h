/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_COMPANION_H
#define THY_COMPANION_H
#include "thy_engine.h"

#define THY_COMPANION_VERSION 1u
#define THY_COMPANION_SNAPSHOT_BYTES 1680u
#define THY_COMPANION_CHUNK_BYTES 128u
#define THY_COMPANION_RESPONSE_BYTES 384u
#define THY_COMPANION_IDLE_MS 2000u
#define THY_COMPANION_MAX_AGE_MS 5000u
#define THY_COMPANION_MIN_CAPTURE_MS 50u
enum thy_companion_offset {
    THY_COMPANION_OLED=136, THY_COMPANION_RGB=1160, THY_COMPANION_CENTER=1448,
    THY_COMPANION_HELD=1467, THY_COMPANION_CONTROLS=1479,
    THY_COMPANION_NOTES=1485, THY_COMPANION_POOL=1501,
    THY_COMPANION_HARMONY=1517, THY_COMPANION_SCALE_NAME=1549,
    THY_COMPANION_GROUP=1581, THY_COMPANION_LEVEL=1582,
    THY_COMPANION_EDITING=1583, THY_COMPANION_NOTICE=1584,
    THY_COMPANION_ROW=1591, THY_COMPANION_EDIT_ROW=1593,
    THY_COMPANION_EDIT_INSTANCE=1595, THY_COMPANION_INSTANCE=1596,
    THY_COMPANION_EDIT_VALUE=1600, THY_COMPANION_FLAGS=1604,
    THY_COMPANION_INSPECT=1632, THY_COMPANION_LAYOUT_REVISION=1650,
    THY_COMPANION_SAVED_GENERATION=1654, THY_COMPANION_PANEL_VISIBLE=1658
};
typedef struct thy_companion_identity {
    const char *build, *uid; /* Immutable ASCII16/24; no pointers on the wire. */
    const uint32_t *saved_generation; /* Optional journal generation, read-only. */
} thy_companion_identity;
/* Foreground only. Copy the actual driver's sent OLED image (blank when off),
 * latest published logical RGB components, and nineteen logical center levels
 * directly into supplied snapshot staging. No rendering or I/O is requested.
 * Return1 on complete capture,0 if unavailable. RGB0..8, center0..255. */
typedef int (*thy_companion_visual_fn)(void *,uint8_t oled[1024],uint8_t rgb[288],
    uint8_t center[19],uint32_t *frame_ms,uint8_t *panel_visible);
typedef struct thy_companion {
    uint8_t *buffer;
    size_t capacity;
    thy_companion_identity identity;
    thy_companion_visual_fn visual;
    void *context;
    uint32_t sequence,crc,captured_ms,last_ms;
    uint8_t active,captured,selected_pad;
} thy_companion;
void thy_companion_init(thy_companion *,uint8_t *,size_t,
    const thy_companion_identity *,thy_companion_visual_fn,void *);
int thy_companion_expire(thy_companion *,uint32_t now);
/* Strict COMPANION/1 ASCII commands, one response per line. All fields are
 * little-endian in the frozen binary snapshot. CRC32 is standard reflected
 * 0xedb88320, init/final XOR allones. Commands:
 * companion caps | status | snapshot | get <seq> <offset> <count1..128>
 * companion release <seq> | pad <logical0..95>
 * companion read <schema64> <row> <instance>  (255 means current selection)
 * companion set <schema64> <row> <explicit-instance> <signed-int32>
 * companion nav <schema64> <action0..4>
 * companion control <schema64> <bank0..3> <index> (one released control tap)
 * companion action <schema64> <row> <explicit-current-instance>
 * Only caps/status/release work when shared staging is unavailable. SET uses
 * existing validation and rejects an active UI draft; NAV owns existing menu
 * transactions and ACTION is explicit, visible, live and instance-checked.
 * PAD changes inspection only: never musical selection, learning or Note On.
 * No raw memory, updater, firmware or remote pad-playing entry exists. */
size_t thy_companion_command(thy_companion *,thy_engine *,int available,
    uint32_t now,const char *line,char *response,size_t capacity);
#endif
