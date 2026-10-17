/*
 * ojjyOS v3 Kernel - Disk Persistence Layer
 *
 * Snapshots the writable RAMFS instances (/Users and /Library) to the
 * "ojjyOS Data" disk partition and restores them on the next boot, so
 * documents, notes, calendar events and settings survive a reboot.
 *
 * The on-disk layout is simply:
 *
 *   [PersistHeader][RamfsInstance #0][RamfsInstance #1]...
 *
 * RAMFS instances are flat structures (no pointers), so the kernel can
 * snapshot them byte-for-byte and memcpy them back at boot.
 *
 * Layout constants must match scripts/mkimg.sh.
 *
 * ojjyOS - Created by Jonas Lee
 */

#ifndef _OJJY_PERSIST_H
#define _OJJY_PERSIST_H

#include "../types.h"

/* Must match the data partition start sector in scripts/mkimg.sh
 * (2048 + 32MB ESP) */
#define OJJY_DATA_START_SECTOR   67584ULL

/* Maximum instances in one checkpoint (two RAMFS mounts today) */
#define PERSIST_MAX_INSTANCES    4

/*
 * Try to restore the writable filesystems from the disk.
 * `instances` is an array of instance pointers (from ramfs_create),
 * `count` entries. Returns 0 if a valid checkpoint was restored,
 * -1 otherwise (fresh boot: the caller should seed the filesystems).
 */
int persist_load(void **instances, int count);

/*
 * Save the given instances to the disk (checkpoint).
 * Returns 0 on success, -1 if there is no disk or the write failed.
 */
int persist_save(void **instances, int count);

/*
 * True when any of the instances has unsaved changes.
 */
bool persist_any_dirty(void **instances, int count);

#endif /* _OJJY_PERSIST_H */
