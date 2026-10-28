/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_COMPANION_H
#define THY_COMPANION_H
#include "thy_engine.h"

#define THY_COMPANION_VERSION 1u
#define THY_COMPANION_SNAPSHOT_BYTES 1680u
#define THY_COMPANION_HARMONY_VERSION 4u
#define THY_COMPANION_HARMONY_BYTES 800u
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
    THY_COMPANION_SAVED_GENERATION=1654, THY_COMPANION_PANEL_VISIBLE=1658,
    THY_COMPANION_CLUSTER_SWAP=1663, THY_COMPANION_HARMONY_REVISION=1668,
    THY_COMPANION_RESET_OPEN=1672, THY_COMPANION_RESET_CHOICE=1673,
    THY_COMPANION_RESET_GROUP=1674, THY_COMPANION_RESET_TRACK=1675,
    THY_COMPANION_RESET_EPOCH=1676
};
/* Optional THYHRM1 snapshot, version2. Four32-byte C-rendered labels occupy
 * 192..319;24 eight-byte rows at320 contain mask:u16,root,shared/family,
 * degree/extra,reason,pad,flags. A valid row has flags bit0. Chord rows name
 * normal LEFT playing pads; scale rows use255 for inapplicable reason/pad.
 * Masks describe absolute pitch classes; these are not a persistence format. */
enum thy_companion_harmony_offset {
    THY_COMPANION_H_REVISION=136, THY_COMPANION_H_KEY=140,
    THY_COMPANION_H_INPUT=142, THY_COMPANION_H_TARGET=144,
    THY_COMPANION_H_COMMON=146, THY_COMPANION_H_ADDED=148,
    THY_COMPANION_H_REMOVED=150, THY_COMPANION_H_PIVOT=152,
    THY_COMPANION_H_TOTAL=154, THY_COMPANION_H_ACTIVE=156,
    THY_COMPANION_H_PAGE=157, THY_COMPANION_H_SOURCE_ROOT=158,
    THY_COMPANION_H_SOURCE_FAMILY=159, THY_COMPANION_H_TARGET_ROOT=160,
    THY_COMPANION_H_TARGET_FAMILY=161, THY_COMPANION_H_CURSOR=162,
    THY_COMPANION_H_COUNT=163, THY_COMPANION_H_FLAGS=164,
    THY_COMPANION_H_PIVOT_ROOT=165, THY_COMPANION_H_FITS=166,
    THY_COMPANION_H_CAN_APPLY=167, THY_COMPANION_H_CURRENT_PAD=168,
    THY_COMPANION_H_SELECTED=169, THY_COMPANION_H_CATALOG=170,
    THY_COMPANION_H_CURRENT_ROOT=171, THY_COMPANION_H_CURRENT_PADS=172,
    THY_COMPANION_H_SELECTED_PADS=178, THY_COMPANION_H_SUGGESTED_PADS=184,
    THY_COMPANION_H_PATTERN=190, THY_COMPANION_H_PRESENTATION=191,
    THY_COMPANION_H_INPUT_LABEL=192, THY_COMPANION_H_KEY_LABEL=224,
    THY_COMPANION_H_TARGET_LABEL=256, THY_COMPANION_H_SELECTED_LABEL=288,
    THY_COMPANION_H_ROWS=320, THY_COMPANION_H_BASE=512,
    THY_COMPANION_H_RIGHT=514, THY_COMPANION_H_VIEW=516,
    THY_COMPANION_H_CONTROL_BANK=517, THY_COMPANION_H_OCTAVE_TARGET=518,
    THY_COMPANION_H_RIGHT_MODE=519, THY_COMPANION_H_ANALYSIS=520,
    THY_COMPANION_H_BASS_ROOT=521, THY_COMPANION_H_LOW_NOTE=522,
    THY_COMPANION_H_HIGH_NOTE=523, THY_COMPANION_H_SWAP=524,
    THY_COMPANION_H_CHORD_LAYERS=525, THY_COMPANION_H_NOTE_LAYERS=526,
    THY_COMPANION_H_PHYSICAL_CURRENT=528, THY_COMPANION_H_PHYSICAL_SELECTED=540,
    THY_COMPANION_H_PHYSICAL_SUGGESTED=552, THY_COMPANION_H_SCALE_BUTTONS=564,
    THY_COMPANION_H_PREVIEW_SCALE=571, THY_COMPANION_H_VOICING=572,
    THY_COMPANION_H_SUGGESTION_COUNT=573, THY_COMPANION_H_CHORD_OCTAVE=574,
    THY_COMPANION_H_NOTE_OCTAVE=575, THY_COMPANION_H_BASE_LABEL=576,
    THY_COMPANION_H_MELODY_LABEL=608
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
    uint32_t sequence,crc,captured_ms,last_ms,last_visual_ms;
    uint16_t captured_bytes;
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
 * companion harmony snapshot (optional512-byte cached Harmony projection)
 * companion harmony <schema64> <action0..9> <signed-int32>
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
