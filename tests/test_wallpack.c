/* Desktop picture pack tests: the kernel parser reads what
 * tools/import_wallpapers.py writes, and refuses damaged or out-of-range
 * tables (fuzzed under ASan/UBSan). Created by Jonas Lee. */
#include "image/wallpack.h"
#include "test_support.h"

extern int open(const char *path, int flags, ...);
extern long read(int fd, void *buf, unsigned long count);

static uint8_t pack[64 * 1024];
static uint8_t work[WALLPACK_HEADER_BYTES];
static WallpackEntry out[WALLPACK_MAX];

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void reseal(uint8_t *h, uint32_t count)
{
    wr32(h + 12, wallpack_crc32(h + 16, (size_t)count * 128));
}

static uint32_t rng = 0x4F4A5750u;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

int main(int argc, char **argv)
{
    if (argc < 2) { emit("usage: test_wallpack sample.pack"); return 1; }
    int fd = open(argv[1], 0);
    long len = fd >= 0 ? read(fd, pack, sizeof(pack)) : -1;
    CHECK(len > WALLPACK_HEADER_BYTES, "reads the sample pack");
    if (len <= WALLPACK_HEADER_BYTES) return finish();
    uint64_t region = (uint64_t)(len + 511) / 512;

    int n = wallpack_parse(pack, (size_t)len, region, out, WALLPACK_MAX);
    CHECK(n == 3, "parses all three entries");
    CHECK(strcmp(out[0].name, "Tahoe Light") == 0 && out[0].kind == WALLPACK_LIGHT, "first entry name and kind");
    CHECK(strcmp(out[1].name, "Tahoe Dark") == 0 && out[1].kind == WALLPACK_DARK, "second entry name and kind");
    CHECK(out[2].kind == WALLPACK_ANY && out[2].width == 2880 && out[2].height == 1800, "size recorded");
    bool blobs = true;
    for (int i = 0; i < n; i++) {
        const WallpackEntry *e = &out[i];
        blobs &= (uint64_t)e->image_sector * 512 + e->image_bytes <= (uint64_t)len &&
                 wallpack_crc32(pack + (size_t)e->image_sector * 512, e->image_bytes) == e->image_crc;
        blobs &= (uint64_t)e->thumb_sector * 512 + e->thumb_bytes <= (uint64_t)len &&
                 wallpack_crc32(pack + (size_t)e->thumb_sector * 512, e->thumb_bytes) == e->thumb_crc;
        blobs &= e->image_sector * 512u >= WALLPACK_HEADER_BYTES;
    }
    CHECK(blobs, "blob offsets and checksums agree with the writer");
    CHECK(wallpack_parse(pack, (size_t)len, region, out, 2) == 2, "honours the output limit");
    CHECK(wallpack_parse(pack, (size_t)len, region - 1, out, WALLPACK_MAX) == 2,
          "drops an entry that runs past the region");
    CHECK(wallpack_parse(pack, WALLPACK_HEADER_BYTES - 1, region, out, WALLPACK_MAX) == 0, "short header");

    memcpy(work, pack, sizeof(work));
    work[0] = 'X';
    CHECK(wallpack_parse(work, sizeof(work), region, out, WALLPACK_MAX) == 0, "bad magic");
    memcpy(work, pack, sizeof(work));
    work[16 + 70] ^= 1;
    CHECK(wallpack_parse(work, sizeof(work), region, out, WALLPACK_MAX) == 0, "table checksum");
    memcpy(work, pack, sizeof(work));
    wr32(work + 8, WALLPACK_MAX + 1);
    CHECK(wallpack_parse(work, sizeof(work), region, out, WALLPACK_MAX) == 0, "too many entries");
    memset(work, 0, sizeof(work));
    CHECK(wallpack_parse(work, sizeof(work), region, out, WALLPACK_MAX) == 0, "empty region");

    /* Sealed but hostile tables: whatever survives must be in range. */
    bool safe = true;
    for (int round = 0; round < 4000; round++) {
        memcpy(work, pack, sizeof(work));
        int flips = 1 + (int)(next() % 8);
        for (int f = 0; f < flips; f++) {
            uint32_t at = 16 + next() % (3 * 128);
            work[at] = (next() & 3) ? (uint8_t)next() : 0xFF;
        }
        reseal(work, 3);
        int m = wallpack_parse(work, sizeof(work), region, out, WALLPACK_MAX);
        for (int i = 0; i < m; i++) {
            const WallpackEntry *e = &out[i];
            safe &= e->name[63] == '\0' && e->name[0] && e->kind <= WALLPACK_DARK;
            safe &= (uint64_t)e->image_sector + (e->image_bytes + 511) / 512 <= region;
            safe &= (uint64_t)e->thumb_sector + (e->thumb_bytes + 511) / 512 <= region;
            safe &= (uint64_t)e->image_sector * 512 >= WALLPACK_HEADER_BYTES && e->image_bytes;
        }
    }
    CHECK(safe, "fuzzed tables only yield in-range entries");
    return finish();
}
