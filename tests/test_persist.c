/* Real RAMFS + fault-injected block device. Created by Jonas Lee. */
#include "fs/persist.h"
#include "fs/ramfs.h"
#include "drivers/blockdev.h"
#include "test_support.h"

#define DISK_BYTES (2 * PERSIST_SLOT_SECTORS * 512)
static uint8_t disk[DISK_BYTES], saved_disk[DISK_BYTES];
static uint8_t saved_fs[2 * RAMFS_SNAPSHOT_MAX_BYTES] __attribute__((aligned(16)));
static int writes, flushes, fail_write = -1, fail_flush = -1;
static bool available = true;
static uint64_t capacity = 128 * 2048;
bool block_available(void) { return available; }
uint64_t block_sector_count(void) { return capacity; }
int block_read_sectors(uint64_t lba, void *buf, uint32_t count)
{
    if (!available || lba < OJJY_DATA_START_SECTOR ||
        lba - OJJY_DATA_START_SECTOR + count > DISK_BYTES / 512) return -1;
    memcpy(buf, disk + (lba - OJJY_DATA_START_SECTOR) * 512, (size_t)count * 512);
    return 0;
}
int block_write_sectors(uint64_t lba, const void *buf, uint32_t count)
{
    if (!available || lba < OJJY_DATA_START_SECTOR ||
        lba - OJJY_DATA_START_SECTOR + count > DISK_BYTES / 512) return -1;
    size_t offset = (size_t)(lba - OJJY_DATA_START_SECTOR) * 512;
    if (writes++ == fail_write) {
        /* Model a torn write, not merely a cleanly rejected request. */
        memcpy(disk + offset, buf, count == 1 ? 16 : (size_t)count * 256);
        return -1;
    }
    memcpy(disk + offset, buf, (size_t)count * 512);
    return 0;
}
int block_flush(void) { return flushes++ == fail_flush ? -1 : 0; }

static VfsOps *ops;
static void put(void *fs, const char *text)
{
    VfsFile *f = ops->open(fs, "/note", VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
    CHECK(f != NULL, "open checkpoint test file");
    if (!f) return;
    CHECK(ops->write(f, text, strlen(text)) == (ssize_t)strlen(text), "write checkpoint test file");
    ops->close(f);
}
static bool has(void *fs, const char *text)
{
    char buffer[32] = {0};
    VfsFile *f = ops->open(fs, "/note", VFS_O_READ);
    if (!f) return false;
    ssize_t length = ops->read(f, buffer, sizeof(buffer) - 1);
    ops->close(f);
    return length == (ssize_t)strlen(text) && strcmp(buffer, text) == 0;
}
static void reset_faults(void) { writes = flushes = 0; fail_write = fail_flush = -1; }

/* VFS access-control hooks; users.c owns the real ones. Unauthenticated
 * stubs make every path allowed, matching pre-gate behaviour here. */
bool users_is_authenticated(void) { return false; }
bool users_current_is_admin(void) { return false; }
const char *users_current_username(void) { return "guest"; }

int main(void)
{
    ops = ramfs_get_ops();
    void *instances[2] = {ramfs_create(), ramfs_create()};
    CHECK(instances[0] && instances[1], "two filesystem instances");
    CHECK(persist_load(instances, 2) == -1, "fresh disk has no checkpoint");
    put(instances[0], "old"); put(instances[1], "accounts");
    CHECK(ramfs_validate_snapshot(instances[0], ramfs_instance_size()), "live RAMFS validates");
    CHECK(persist_save(instances, 2) == 0, "first checkpoint commits");
    CHECK(flushes == 2, "payload and commit header have separate durability barriers");
    CHECK(!persist_any_dirty(instances, 2), "committed snapshot clears old dirty state");
    memcpy(saved_disk, disk, sizeof(disk));

    put(instances[0], "new");
    reset_faults();
    CHECK(persist_begin_save(instances, 2) == 0 && persist_save_active(), "incremental save starts");
    CHECK(persist_step() == 1 && writes == 1, "one step writes only one bounded batch");
    put(instances[0], "edited-during-save");
    while (persist_save_active()) CHECK(persist_step() >= 0, "incremental save progresses");
    CHECK(persist_any_dirty(instances, 2), "edits during save remain dirty");
    CHECK(persist_load(instances, 2) == 0 && has(instances[0], "new") && has(instances[1], "accounts"),
          "immutable snapshot restores both instances consistently");

    disk[PERSIST_SLOT_SECTORS * 512 + 512 + 33] ^= 1;
    CHECK(persist_load(instances, 2) == 0 && has(instances[0], "old"), "corrupt newest payload falls back to previous slot");
    memcpy(disk, saved_disk, sizeof(disk));
    put(instances[0], "new");
    reset_faults(); CHECK(persist_save(instances, 2) == 0, "second checkpoint commits");
    disk[PERSIST_SLOT_SECTORS * 512 + 24] ^= 1;
    CHECK(persist_load(instances, 2) == 0 && has(instances[0], "old"), "corrupt generation/header CRC falls back");

    const int points[] = {0, 1, 2, 100, 500};
    for (unsigned i = 0; i < ARRAY_SIZE(points); i++) {
        memcpy(disk, saved_disk, sizeof(disk)); reset_faults();
        CHECK(persist_load(instances, 2) == 0, "reset to last committed save");
        put(instances[0], "new"); fail_write = points[i];
        CHECK(persist_save(instances, 2) == -1, "torn payload write is reported");
        CHECK(persist_any_dirty(instances, 2), "failed save remains dirty");
        reset_faults();
        CHECK(persist_load(instances, 2) == 0 && has(instances[0], "old"), "old save survives torn write");
    }
    for (int fault = 0; fault < 2; fault++) {
        memcpy(disk, saved_disk, sizeof(disk)); reset_faults(); persist_load(instances, 2);
        put(instances[0], "new"); fail_flush = fault;
        CHECK(persist_save(instances, 2) == -1, "flush failure reported");
        CHECK(persist_any_dirty(instances, 2), "flush failure retains dirty state");
        reset_faults();
        CHECK(persist_load(instances, 2) == 0, "flush failure retains a recoverable complete checkpoint");
    }
    memcpy(disk, saved_disk, sizeof(disk)); reset_faults(); persist_load(instances, 2);
    put(instances[0], "new");
    CHECK(persist_begin_save(instances, 2) == 0 && persist_step() == 1, "start save before simulated power loss");
    persist_cancel_save();
    CHECK(persist_load(instances, 2) == 0 && has(instances[0], "old"), "power loss before commit retains old checkpoint");

    /* Tear the final commit header after all payload batches. */
    memcpy(disk, saved_disk, sizeof(disk)); reset_faults(); persist_load(instances, 2);
    put(instances[0], "new");
    fail_write = (int)(((2 * ramfs_instance_size() + 511) / 512 + 7) / 8);
    CHECK(persist_save(instances, 2) == -1, "torn commit header reported");
    reset_faults();
    CHECK(persist_load(instances, 2) == 0 && has(instances[0], "old"), "torn commit header cannot replace old checkpoint");

    put(instances[0], "live-unsaved");
    size_t size = ramfs_instance_size();
    memcpy(saved_fs, instances[0], size); memcpy(saved_fs + size, instances[1], size);
    memset(disk, 0, sizeof(disk));
    CHECK(persist_load(instances, 2) == -1 && memcmp(instances[0], saved_fs, size) == 0 &&
          memcmp(instances[1], saved_fs + size, size) == 0, "failed restore does not partially overwrite either filesystem");

    /* Build a real v1 disk, as produced by previous releases. */
    uint32_t legacy[6] = {0x4A534F50, 1, (uint32_t)size, 2, (uint32_t)((2 * size + 511) / 512), 0};
    memcpy(disk, legacy, sizeof(legacy));
    memcpy(disk + 512, saved_fs, 2 * size);
    CHECK(persist_load(instances, 2) == 0 && has(instances[0], "live-unsaved"), "legacy checkpoint remains readable");
    reset_faults();
    CHECK(persist_save(instances, 2) == 0 && ((uint32_t *)(disk + PERSIST_SLOT_SECTORS * 512))[1] == 2,
          "first migration writes v2 to alternate slot");
    CHECK(((uint32_t *)disk)[1] == 1, "legacy checkpoint preserved during first migration");
    memset(disk + PERSIST_SLOT_SECTORS * 512, 0, 512);
    ((RamfsNode *)(disk + 512))[0].parent = 0;
    CHECK(persist_load(instances, 2) == -1, "malformed legacy filesystem rejected");

    CHECK(persist_save(NULL, 2) == -1 && persist_save(instances, 0) == -1 &&
          persist_save(instances, 5) == -1, "invalid save arguments rejected");
    available = false;
    CHECK(persist_save(instances, 2) == -1, "missing disk rejected");
    available = true; capacity = OJJY_DATA_START_SECTOR + 1;
    CHECK(persist_save(instances, 2) == -1, "undersized disk rejected before writing");
    return finish();
}
