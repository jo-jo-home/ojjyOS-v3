/*
 * ojjyOS v3 - Desktop picture pack (see wallpack.h)
 *
 * ojjyOS - Created by Jonas Lee
 */
#include "image/wallpack.h"
#include "string.h"

#ifndef WALLPACK_HOST_TEST
#include "drivers/blockdev.h"
#include "serial.h"
#endif

#define SECTOR  512U

uint32_t wallpack_crc32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* A blob must start after the header and end inside the region. */
static bool blob_ok(uint32_t sector, uint32_t bytes, uint64_t region_sectors)
{
    if (bytes == 0 || bytes > 64u * 1024 * 1024) return false;
    if ((uint64_t)sector * SECTOR < WALLPACK_HEADER_BYTES) return false;
    return (uint64_t)sector + (bytes + SECTOR - 1) / SECTOR <= region_sectors;
}

int wallpack_parse(const uint8_t *header, size_t len, uint64_t region_sectors,
                   WallpackEntry *out, int max)
{
    if (!header || len < WALLPACK_HEADER_BYTES || !out || max <= 0) return 0;
    if (memcmp(header, "OJWP", 4) != 0 || rd32(header + 4) != 1) return 0;
    uint32_t count = rd32(header + 8);
    if (count == 0 || count > WALLPACK_MAX) return 0;
    if (wallpack_crc32(header + 16, (size_t)count * 128) != rd32(header + 12)) return 0;

    int n = 0;
    for (uint32_t i = 0; i < count && n < max; i++) {
        const uint8_t *e = header + 16 + i * 128;
        WallpackEntry *w = &out[n];
        memset(w, 0, sizeof(*w));
        memcpy(w->name, e, 63);
        w->name[63] = '\0';
        w->kind = rd32(e + 64);
        w->width = rd32(e + 68);
        w->height = rd32(e + 72);
        w->image_sector = rd32(e + 76);
        w->image_bytes = rd32(e + 80);
        w->image_crc = rd32(e + 84);
        w->thumb_sector = rd32(e + 88);
        w->thumb_bytes = rd32(e + 92);
        w->thumb_crc = rd32(e + 96);
        if (!w->name[0] || w->kind > WALLPACK_DARK) continue;
        if (w->width == 0 || w->height == 0 || w->width > 8192 || w->height > 8192) continue;
        if (!blob_ok(w->image_sector, w->image_bytes, region_sectors) ||
            !blob_ok(w->thumb_sector, w->thumb_bytes, region_sectors)) continue;
        n++;
    }
    return n;
}

#ifndef WALLPACK_HOST_TEST

static WallpackEntry entries[WALLPACK_MAX];
static int entry_count = -1;            /* -1 = not loaded yet */
static uint64_t region_sectors;

/* Bounded chunks: the ATA PIO path takes at most 256 sectors per command. */
static int read_sectors(uint64_t lba, uint8_t *buf, uint32_t count)
{
    while (count) {
        uint32_t n = count > 64 ? 64 : count;
        if (block_read_sectors(lba, buf, n) != 0) return -1;
        lba += n;
        buf += (size_t)n * SECTOR;
        count -= n;
    }
    return 0;
}

int wallpack_load(void)
{
    if (entry_count >= 0) return entry_count;
    entry_count = 0;
    uint64_t total = block_available() ? block_sector_count() : 0;
    /* The backup GPT occupies the last 33 sectors. */
    if (total < WALLPACK_LBA + WALLPACK_HEADER_BYTES / SECTOR + 33) return 0;
    region_sectors = total - 33 - WALLPACK_LBA;
    static uint8_t header[WALLPACK_HEADER_BYTES];
    if (read_sectors(WALLPACK_LBA, header, WALLPACK_HEADER_BYTES / SECTOR) != 0) return 0;
    entry_count = wallpack_parse(header, sizeof(header), region_sectors, entries, WALLPACK_MAX);
    serial_printf("[WALLPACK] %d desktop pictures\n", entry_count);
    return entry_count;
}

int wallpack_count(void)
{
    return entry_count < 0 ? wallpack_load() : entry_count;
}

const WallpackEntry *wallpack_entry(int index)
{
    if (index < 0 || index >= wallpack_count()) return NULL;
    return &entries[index];
}

int wallpack_find(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < wallpack_count(); i++)
        if (strcmp(entries[i].name, name) == 0) return i;
    return -1;
}

int wallpack_read(int index, bool thumb, uint8_t *buf, size_t cap)
{
    const WallpackEntry *e = wallpack_entry(index);
    if (!e || !buf) return -1;
    uint32_t sector = thumb ? e->thumb_sector : e->image_sector;
    uint32_t bytes = thumb ? e->thumb_bytes : e->image_bytes;
    uint32_t crc = thumb ? e->thumb_crc : e->image_crc;
    uint32_t sectors = (bytes + SECTOR - 1) / SECTOR;
    if ((size_t)sectors * SECTOR > cap) return -1;
    if (read_sectors(WALLPACK_LBA + sector, buf, sectors) != 0) return -1;
    if (wallpack_crc32(buf, bytes) != crc) {
        serial_printf("[WALLPACK] %s: checksum mismatch\n", e->name);
        return -1;
    }
    return (int)bytes;
}

#endif /* WALLPACK_HOST_TEST */
