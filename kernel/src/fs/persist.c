/*
 * ojjyOS v3 - Recoverable RAMFS checkpoints. Created by Jonas Lee.
 *
 * Two fixed-size slots alternate. Payload is written and flushed before
 * the checksummed commit header, so an interrupted save leaves the other
 * slot intact. Restore validates into scratch memory before touching RAMFS.
 */
#include "persist.h"
#include "ramfs.h"
#include "../drivers/blockdev.h"
#include "../serial.h"
#include "../string.h"

#define PERSIST_MAGIC 0x4A534F50U
#define PERSIST_VERSION 2U
#define BATCH_SECTORS 8U

typedef struct {
    uint32_t magic, version, instance_size, instance_count;
    uint32_t payload_sectors, reserved;
    uint64_t generation;
    uint32_t payload_crc, header_crc;
} PACKED PersistHeader;

/* Legacy v1 used the first 24 bytes of this header, without checksums. */
static uint8_t snapshot[PERSIST_MAX_INSTANCES * RAMFS_SNAPSHOT_MAX_BYTES]
    __attribute__((aligned(16)));
static uint8_t stage[BATCH_SECTORS * 512] __attribute__((aligned(16)));
static int active_slot = -1;
static uint64_t generation;
static bool scanned;
static struct {
    bool active;
    int slot, count;
    uint32_t sector;
    void *instances[PERSIST_MAX_INSTANCES];
    PersistHeader header;
} saving;

static uint64_t slot_lba(int slot)
{
    return OJJY_DATA_START_SECTOR + (uint64_t)slot * PERSIST_SLOT_SECTORS;
}

static uint32_t crc32(const void *data, size_t length)
{
    static uint32_t table[256];
    static bool ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t crc = i;
            for (unsigned bit = 0; bit < 8; bit++) {
                crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1)));
            }
            table[i] = crc;
        }
        ready = true;
    }
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; i++) {
        crc = table[(crc ^ bytes[i]) & 255] ^ (crc >> 8);
    }
    return ~crc;
}

static bool valid_arguments(void **instances, int count)
{
    if (!instances || count <= 0 || count > PERSIST_MAX_INSTANCES ||
        !block_available() || block_sector_count() < slot_lba(2)) return false;
    for (int i = 0; i < count; i++) if (!instances[i]) return false;
    return ramfs_instance_size() <= RAMFS_SNAPSHOT_MAX_BYTES;
}

static bool read_header(int slot, int count, PersistHeader *header)
{
    if (block_read_sectors(slot_lba(slot), stage, 1) != 0) return false;
    memcpy(header, stage, sizeof(*header));
    uint64_t bytes = (uint64_t)count * ramfs_instance_size();
    if (header->magic != PERSIST_MAGIC || header->instance_count != (uint32_t)count ||
        header->instance_size != ramfs_instance_size() ||
        header->payload_sectors != (bytes + 511) / 512 ||
        header->payload_sectors >= PERSIST_SLOT_SECTORS) return false;
    if (header->version == 1 && slot == 0) {
        header->generation = 0;
        return true;
    }
    if (header->version != PERSIST_VERSION || !header->generation) return false;
    uint32_t expected = header->header_crc;
    header->header_crc = 0;
    uint32_t actual = crc32(header, sizeof(*header));
    header->header_crc = expected;
    return actual == expected;
}

static bool read_snapshot(int slot, const PersistHeader *header)
{
    size_t bytes = (size_t)header->instance_count * header->instance_size;
    size_t done = 0;
    for (uint32_t sector = 0; sector < header->payload_sectors; ) {
        uint32_t batch = MIN(BATCH_SECTORS, header->payload_sectors - sector);
        if (block_read_sectors(slot_lba(slot) + 1 + sector, stage, batch) != 0) return false;
        size_t chunk = MIN(bytes - done, (size_t)batch * 512);
        memcpy(snapshot + done, stage, chunk);
        done += chunk;
        sector += batch;
    }
    if (header->version == PERSIST_VERSION && crc32(snapshot, bytes) != header->payload_crc) {
        serial_printf("[PERSIST] Slot %d payload checksum failed\n", slot);
        return false;
    }
    for (uint32_t i = 0; i < header->instance_count; i++) {
        if (!ramfs_validate_snapshot(snapshot + (size_t)i * header->instance_size,
                                     header->instance_size)) return false;
    }
    return true;
}

/* Leaves the newest valid snapshot in scratch, without mutating live RAMFS. */
static bool scan_slots(int count)
{
    PersistHeader headers[2];
    bool valid[2] = {read_header(0, count, &headers[0]), false};
    valid[1] = read_header(1, count, &headers[1]);
    int first = valid[1] && (!valid[0] || headers[1].generation > headers[0].generation) ? 1 : 0;
    active_slot = -1;
    generation = 0;
    scanned = true;
    for (int attempt = 0; attempt < 2; attempt++) {
        int slot = first ^ attempt;
        if (valid[slot] && read_snapshot(slot, &headers[slot])) {
            active_slot = slot;
            generation = headers[slot].generation;
            if (headers[slot].version == 1) {
                serial_printf("[PERSIST] Legacy checkpoint validated; next save migrates to v2\n");
            }
            return true;
        }
    }
    return false;
}

int persist_load(void **instances, int count)
{
    if (saving.active || !valid_arguments(instances, count)) return -1;
    if (!scan_slots(count)) {
        serial_printf("[PERSIST] No valid checkpoint; live filesystems unchanged\n");
        return -1;
    }
    size_t size = ramfs_instance_size();
    for (int i = 0; i < count; i++) {
        memcpy(instances[i], snapshot + (size_t)i * size, size);
        ramfs_clear_dirty(instances[i]);
    }
    serial_printf("[PERSIST] Restored %d instance(s), slot %d, generation %llu\n",
                  count, active_slot, generation);
    return 0;
}

int persist_begin_save(void **instances, int count)
{
    if (saving.active || !valid_arguments(instances, count)) return -1;
    if (!scanned) scan_slots(count);
    if (generation == ~0ULL) return -1;
    size_t size = ramfs_instance_size();
    for (int i = 0; i < count; i++) {
        if (!ramfs_validate_snapshot(instances[i], size)) return -1;
    }
    memset(&saving, 0, sizeof(saving));
    saving.slot = active_slot == 0 ? 1 : 0;
    saving.count = count;
    for (int i = 0; i < count; i++) {
        saving.instances[i] = instances[i];
        memcpy(snapshot + (size_t)i * size, instances[i], size);
        /* Edits after this snapshot set dirty again and must not be cleared
         * on completion. A failed/cancelled write restores dirty below. */
        ramfs_clear_dirty(instances[i]);
    }
    PersistHeader *header = &saving.header;
    header->magic = PERSIST_MAGIC;
    header->version = PERSIST_VERSION;
    header->instance_size = (uint32_t)size;
    header->instance_count = (uint32_t)count;
    header->payload_sectors = (uint32_t)(((size_t)count * size + 511) / 512);
    header->generation = generation + 1;
    header->payload_crc = crc32(snapshot, (size_t)count * size);
    header->header_crc = crc32(header, sizeof(*header));
    saving.active = true;
    return 0;
}

void persist_cancel_save(void)
{
    if (!saving.active) return;
    for (int i = 0; i < saving.count; i++) ramfs_mark_dirty(saving.instances[i]);
    saving.active = false;
}

bool persist_save_active(void)
{
    return saving.active;
}

/* One bounded batch per main-loop turn. Flush/commit each get their own
 * turn too. The snapshot is immutable while user edits continue in RAMFS. */
int persist_step(void)
{
    if (!saving.active) return 0;
    PersistHeader *header = &saving.header;
    uint32_t sectors = header->payload_sectors;
    if (saving.sector < sectors) {
        uint32_t batch = MIN(BATCH_SECTORS, sectors - saving.sector);
        size_t offset = (size_t)saving.sector * 512;
        size_t chunk = MIN((size_t)batch * 512,
                           (size_t)saving.count * header->instance_size - offset);
        memset(stage, 0, batch * 512);
        memcpy(stage, snapshot + offset, chunk);
        if (block_write_sectors(slot_lba(saving.slot) + 1 + saving.sector, stage, batch) != 0) goto fail;
        saving.sector += batch;
        return 1;
    }
    if (saving.sector == sectors) {
        if (block_flush() != 0) goto fail;
    } else if (saving.sector == sectors + 1) {
        memset(stage, 0, 512);
        memcpy(stage, header, sizeof(*header));
        if (block_write_sectors(slot_lba(saving.slot), stage, 1) != 0) goto fail;
    } else {
        if (block_flush() != 0) goto fail;
        active_slot = saving.slot;
        generation = header->generation;
        saving.active = false;
        serial_printf("[PERSIST] Saved %d instance(s), slot %d, generation %llu\n",
                      saving.count, active_slot, generation);
        return 0;
    }
    saving.sector++;
    return 1;
fail:
    serial_printf("[PERSIST] Save failed; previous checkpoint retained\n");
    persist_cancel_save();
    return -1;
}

int persist_save(void **instances, int count)
{
    /* Finish an older autosave, then include edits made since it started. */
    while (saving.active) if (persist_step() < 0) return -1;
    if (persist_begin_save(instances, count) != 0) return -1;
    while (saving.active) if (persist_step() < 0) return -1;
    return 0;
}

bool persist_any_dirty(void **instances, int count)
{
    if (!instances || count <= 0 || count > PERSIST_MAX_INSTANCES) return false;
    for (int i = 0; i < count; i++) if (ramfs_is_dirty(instances[i])) return true;
    return false;
}
