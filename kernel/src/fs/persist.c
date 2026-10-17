/*
 * ojjyOS v3 Kernel - Disk Persistence Implementation
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "persist.h"
#include "ramfs.h"
#include "../serial.h"
#include "../string.h"
#include "../drivers/blockdev.h"

#define PERSIST_MAGIC       0x4A534F50UL      /* "POSJ" */
#define PERSIST_VERSION     1

typedef struct {
    uint32_t magic;              /* PERSIST_MAGIC */
    uint32_t version;            /* PERSIST_VERSION */
    uint32_t instance_size;      /* sizeof(RamfsInstance) at save time */
    uint32_t instance_count;     /* Number of instances stored */
    uint32_t payload_sectors;    /* Sectors used by the instance payloads */
    uint32_t reserved;
} PACKED PersistHeader;

/* Staging buffer: the ATA driver moves whole 512-byte sectors */
static uint8_t stage[512];

/*
 * Write `count` instances (each ramfs_instance_size() bytes) starting
 * at the given LBA. Uses the staging buffer, so instances may be any size.
 */
static int write_instances(uint64_t lba, void **instances, int count)
{
    if (!block_available()) return -1;

    size_t inst_size = ramfs_instance_size();
    uint64_t total_bytes = (uint64_t)count * inst_size;
    uint64_t total_sectors = (total_bytes + 511) / 512;

    /* Stream the instance bytes through the staging buffer */
    uint64_t sector = lba;
    size_t done = 0;
    memset(stage, 0, sizeof(stage));

    while (done < total_bytes) {
        /* Fill the staging sector from all instances in order */
        size_t in_stage = 0;
        while (in_stage < sizeof(stage) && done < total_bytes) {
            size_t global_off = done;                    /* Byte in payload */
            int which = (int)(global_off / inst_size);
            size_t off_in_inst = global_off % inst_size;

            size_t chunk = inst_size - off_in_inst;
            if (chunk > sizeof(stage) - in_stage) chunk = sizeof(stage) - in_stage;
            if (chunk > total_bytes - done) chunk = total_bytes - done;

            const uint8_t *src = (const uint8_t *)instances[which] + off_in_inst;
            memcpy(stage + in_stage, src, chunk);
            in_stage += chunk;
            done += chunk;
        }

        /* Zero the tail of the final sector */
        if (in_stage < sizeof(stage)) {
            memset(stage + in_stage, 0, sizeof(stage) - in_stage);
        }

        if (block_write_sectors(sector, stage, 1) != 0) {
            serial_printf("[PERSIST] Write failed at sector %d\n", (int)sector);
            return -1;
        }
        sector++;
    }

    (void)total_sectors;
    return 0;
}

/*
 * Read `count` instances back from the given LBA.
 */
static int read_instances(uint64_t lba, void **instances, int count)
{
    if (!block_available()) return -1;

    size_t inst_size = ramfs_instance_size();
    uint64_t total_bytes = (uint64_t)count * inst_size;

    uint64_t sector = lba;
    size_t done = 0;

    while (done < total_bytes) {
        if (block_read_sectors(sector, stage, 1) != 0) {
            return -1;
        }

        size_t in_stage = 0;
        while (in_stage < sizeof(stage) && done < total_bytes) {
            size_t global_off = done;
            int which = (int)(global_off / inst_size);
            size_t off_in_inst = global_off % inst_size;

            size_t chunk = inst_size - off_in_inst;
            if (chunk > sizeof(stage) - in_stage) chunk = sizeof(stage) - in_stage;
            if (chunk > total_bytes - done) chunk = total_bytes - done;

            memcpy((uint8_t *)instances[which] + off_in_inst, stage + in_stage, chunk);
            in_stage += chunk;
            done += chunk;
        }
        sector++;
    }

    return 0;
}

int persist_save(void **instances, int count)
{
    if (!instances || count <= 0 || count > PERSIST_MAX_INSTANCES) {
        return -1;
    }

    if (!block_available()) {
        /* No disk attached: persistence is silently unavailable */
        return -1;
    }

    size_t inst_size = ramfs_instance_size();
    uint64_t total_bytes = (uint64_t)count * inst_size;
    uint64_t payload_sectors = (total_bytes + 511) / 512;

    PersistHeader hdr;
    hdr.magic = PERSIST_MAGIC;
    hdr.version = PERSIST_VERSION;
    hdr.instance_size = (uint32_t)inst_size;
    hdr.instance_count = (uint32_t)count;
    hdr.payload_sectors = (uint32_t)payload_sectors;
    hdr.reserved = 0;

    /* Header occupies the first sector */
    memset(stage, 0, sizeof(stage));
    memcpy(stage, &hdr, sizeof(hdr));
    if (block_write_sectors(OJJY_DATA_START_SECTOR, stage, 1) != 0) {
        serial_printf("[PERSIST] Header write failed\n");
        return -1;
    }

    if (write_instances(OJJY_DATA_START_SECTOR + 1, instances, count) != 0) {
        return -1;
    }

    /* All instances are now consistent with what is on disk */
    for (int i = 0; i < count; i++) {
        ramfs_clear_dirty(instances[i]);
    }

    serial_printf("[PERSIST] Saved %d instance(s), %d sectors\n",
        count, (int)(payload_sectors + 1));
    return 0;
}

int persist_load(void **instances, int count)
{
    if (!instances || count <= 0 || count > PERSIST_MAX_INSTANCES) {
        return -1;
    }

    if (!block_available()) {
        return -1;
    }

    if (block_read_sectors(OJJY_DATA_START_SECTOR, stage, 1) != 0) {
        return -1;
    }

    PersistHeader hdr;
    memcpy(&hdr, stage, sizeof(hdr));

    if (hdr.magic != PERSIST_MAGIC || hdr.version != PERSIST_VERSION) {
        serial_printf("[PERSIST] No valid checkpoint (fresh boot)\n");
        return -1;
    }

    if (hdr.instance_count != (uint32_t)count ||
        hdr.instance_size != (uint32_t)ramfs_instance_size()) {
        serial_printf("[PERSIST] Checkpoint layout mismatch - ignoring\n");
        return -1;
    }

    if (read_instances(OJJY_DATA_START_SECTOR + 1, instances, count) != 0) {
        serial_printf("[PERSIST] Checkpoint read failed\n");
        return -1;
    }

    /* Restored state is exactly what was saved */
    for (int i = 0; i < count; i++) {
        ramfs_clear_dirty(instances[i]);
    }

    serial_printf("[PERSIST] Restored %d instance(s) from disk\n", count);
    return 0;
}

bool persist_any_dirty(void **instances, int count)
{
    for (int i = 0; i < count; i++) {
        if (ramfs_is_dirty(instances[i])) {
            return true;
        }
    }
    return false;
}
