/*
 * ojjyOS v3 Kernel - Block Cache Implementation
 *
 * Simple LRU cache with write-through policy.
 */

#include "block_cache.h"
#include "ata.h"
#include "../serial.h"
#include "../string.h"
#include "../console.h"
#include "../timer.h"

/* Number of cache entries */
#define CACHE_SIZE 64

/* Cache entry */
typedef struct {
    uint64_t block_num;     /* Block number (0 = invalid) */
    uint64_t last_access;   /* Tick count of last access */
    bool     valid;         /* Entry is valid */
    bool     dirty;         /* Needs to be written back */
    uint8_t  data[BLOCK_SIZE];
} CacheEntry;

/* Cache */
static CacheEntry cache[CACHE_SIZE];

/* Statistics */
static uint64_t cache_hits = 0;
static uint64_t cache_misses = 0;
static uint64_t cache_writes = 0;
static uint64_t cache_flushes = 0;

/*
 * Find cache entry by block number
 */
static CacheEntry *cache_find(uint64_t block_num)
{
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].block_num == block_num) {
            return &cache[i];
        }
    }
    return NULL;
}

/*
 * Find LRU entry for eviction
 */
static CacheEntry *cache_find_lru(void)
{
    CacheEntry *lru = NULL;
    uint64_t oldest = ~0ULL;

    /* First try to find an invalid entry */
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!cache[i].valid) {
            return &cache[i];
        }
    }

    /* Find least recently used */
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].last_access < oldest) {
            oldest = cache[i].last_access;
            lru = &cache[i];
        }
    }

    return lru;
}

/*
 * Write back a dirty cache entry
 */
static int cache_writeback(CacheEntry *entry)
{
    if (!entry || !entry->valid || !entry->dirty) {
        return 0;
    }

    AtaDevice *dev = ata_get_device(0);
    if (!dev) {
        return -1;
    }

    int ret = ata_write_sectors(dev, entry->block_num, 1, entry->data);
    if (ret == 0) {
        entry->dirty = false;
        cache_flushes++;
    }

    return ret;
}

/*
 * Initialize block cache
 */
void block_cache_init(void)
{
    serial_printf("[CACHE] Initializing block cache (%d entries)...\n", CACHE_SIZE);

    memset(cache, 0, sizeof(cache));
    cache_hits = 0;
    cache_misses = 0;
    cache_writes = 0;
    cache_flushes = 0;

    serial_printf("[CACHE] Block cache ready\n");
}

/*
 * Read a block
 */
int block_cache_read(uint64_t block_num, void *buffer)
{
    if (!buffer) return -1;

    /* Check cache first */
    CacheEntry *entry = cache_find(block_num);
    if (entry) {
        /* Cache hit */
        entry->last_access = timer_get_ticks();
        memcpy(buffer, entry->data, BLOCK_SIZE);
        cache_hits++;
        return 0;
    }

    /* Cache miss - read from disk */
    cache_misses++;

    AtaDevice *dev = ata_get_device(0);
    if (!dev) {
        serial_printf("[CACHE] No disk device available\n");
        return -1;
    }

    /* Find entry to use */
    entry = cache_find_lru();
    if (!entry) {
        /* Should never happen */
        serial_printf("[CACHE] ERROR: No cache entry available\n");
        return -1;
    }

    /* Write back if dirty */
    if (entry->valid && entry->dirty) {
        cache_writeback(entry);
    }

    /* Read from disk */
    int ret = ata_read_sectors(dev, block_num, 1, entry->data);
    if (ret != 0) {
        return ret;
    }

    /* Update cache entry */
    entry->block_num = block_num;
    entry->last_access = timer_get_ticks();
    entry->valid = true;
    entry->dirty = false;

    /* Copy to output buffer */
    memcpy(buffer, entry->data, BLOCK_SIZE);

    return 0;
}

/*
 * Write a block (write-through)
 */
int block_cache_write(uint64_t block_num, const void *buffer)
{
    if (!buffer) return -1;

    cache_writes++;

    AtaDevice *dev = ata_get_device(0);
    if (!dev) {
        serial_printf("[CACHE] No disk device available\n");
        return -1;
    }

    /* Write to disk immediately (write-through) */
    int ret = ata_write_sectors(dev, block_num, 1, buffer);
    if (ret != 0) {
        return ret;
    }

    /* Update cache if block is cached */
    CacheEntry *entry = cache_find(block_num);
    if (entry) {
        memcpy(entry->data, buffer, BLOCK_SIZE);
        entry->last_access = timer_get_ticks();
        entry->dirty = false;
    } else {
        /* Add to cache */
        entry = cache_find_lru();
        if (entry) {
            if (entry->valid && entry->dirty) {
                cache_writeback(entry);
            }

            entry->block_num = block_num;
            entry->last_access = timer_get_ticks();
            entry->valid = true;
            entry->dirty = false;
            memcpy(entry->data, buffer, BLOCK_SIZE);
        }
    }

    return 0;
}

/*
 * Invalidate a cached block
 */
void block_cache_invalidate(uint64_t block_num)
{
    CacheEntry *entry = cache_find(block_num);
    if (entry) {
        /* Don't write back dirty data when invalidating */
        entry->valid = false;
        entry->dirty = false;
    }
}

/*
 * Flush all dirty blocks
 */
void block_cache_flush(void)
{
    serial_printf("[CACHE] Flushing dirty blocks...\n");

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].dirty) {
            cache_writeback(&cache[i]);
        }
    }
}

/*
 * Print cache statistics
 */
void block_cache_print_stats(void)
{
    console_printf("\n=== Block Cache Stats ===\n");
    console_printf("  Entries: %d\n", CACHE_SIZE);
    console_printf("  Hits:    %d\n", (int)cache_hits);
    console_printf("  Misses:  %d\n", (int)cache_misses);
    console_printf("  Writes:  %d\n", (int)cache_writes);
    console_printf("  Flushes: %d\n", (int)cache_flushes);

    if (cache_hits + cache_misses > 0) {
        int hit_rate = (cache_hits * 100) / (cache_hits + cache_misses);
        console_printf("  Hit rate: %d%%\n", hit_rate);
    }

    /* Count valid/dirty entries */
    int valid = 0, dirty = 0;
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].valid) valid++;
        if (cache[i].dirty) dirty++;
    }
    console_printf("  Valid: %d, Dirty: %d\n", valid, dirty);
    console_printf("\n");
}
