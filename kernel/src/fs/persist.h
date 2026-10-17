/*
 * ojjyOS v3 Kernel - Disk Persistence Layer
 *
 * Snapshots the writable RAMFS instances (/Users and /Library) to the
 * "ojjyOS Data" disk partition and restores them on the next boot, so
 * documents, notes, calendar events and settings survive a reboot.
 *
 * Two 8 MiB slots each contain a checksummed commit header and payload.
 * Payload is flushed before its header. The previous valid slot survives
 * interrupted writes. Legacy v1 checkpoints are validated and migrated.
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
#define PERSIST_SLOT_SECTORS     16384ULL

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

/* Incremental autosave: immutable snapshot, at most eight payload sectors
 * per step. Returns 1 while working, 0 when complete, -1 on failure. */
int persist_begin_save(void **instances, int count);
int persist_step(void);
bool persist_save_active(void);
void persist_cancel_save(void);

/*
 * True when any of the instances has unsaved changes.
 */
bool persist_any_dirty(void **instances, int count);

#endif /* _OJJY_PERSIST_H */
