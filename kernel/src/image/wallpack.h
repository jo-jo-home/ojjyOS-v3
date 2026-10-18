/*
 * ojjyOS v3 - Desktop picture pack
 *
 * A read-only pack of baseline JPEG wallpapers (+ thumbnails) written by
 * tools/import_wallpapers.py into the tail of the data partition. The
 * region belongs to the build: tools/finalize_image.py refreshes it from
 * the new image and preserves only the user data before it. An empty or
 * damaged region simply means "no pictures"; the built-in wallpapers stay.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_IMAGE_WALLPACK_H
#define _OJJY_IMAGE_WALLPACK_H

#include "types.h"

/* Contract with scripts/mkimg.sh and tools/finalize_image.py (PACK_FIRST). */
#define WALLPACK_LBA            102400ULL
#define WALLPACK_HEADER_BYTES   (16 * 1024)
#define WALLPACK_MAX            ((WALLPACK_HEADER_BYTES - 16) / 128)

typedef enum { WALLPACK_ANY = 0, WALLPACK_LIGHT = 1, WALLPACK_DARK = 2 } WallpackKind;

typedef struct {
    char name[64];
    uint32_t kind;
    uint32_t width, height;
    uint32_t image_sector, image_bytes, image_crc;   /* sectors relative to the pack */
    uint32_t thumb_sector, thumb_bytes, thumb_crc;
} WallpackEntry;

/* Parse and validate a pack header. `region_sectors` bounds every entry.
 * Returns the number of entries written to `out` (0 when invalid/empty). */
int wallpack_parse(const uint8_t *header, size_t len, uint64_t region_sectors,
                   WallpackEntry *out, int max);

/* Kernel side: read the header from disk once (cached), then entries. */
int wallpack_load(void);
int wallpack_count(void);
const WallpackEntry *wallpack_entry(int index);
int wallpack_find(const char *name);
/* Read an entry's JPEG (thumb or full image) into buf; checks the CRC.
 * Returns the byte count or -1. */
int wallpack_read(int index, bool thumb, uint8_t *buf, size_t cap);

uint32_t wallpack_crc32(const void *data, size_t len);

#endif /* _OJJY_IMAGE_WALLPACK_H */
