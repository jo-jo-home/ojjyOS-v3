/*
 * ojjyOS v3 Kernel - RAMFS
 *
 * Simple in-memory read/write filesystem for user data.
 * Each mount point gets its own independent instance.
 */

#ifndef _OJJY_RAMFS_H
#define _OJJY_RAMFS_H

#include "vfs.h"

typedef struct {
    char name[VFS_NAME_MAX + 1];
    uint32_t parent;
    VfsFileType type;
    uint32_t permissions;
    uint64_t size;
    uint64_t capacity;
    uint64_t data_offset;
} RamfsNode;

#define RAMFS_MAX_NODES 256
#define RAMFS_DATA_SIZE (1024 * 1024)
/* Includes the node count, dirty byte, and structure alignment padding. */
#define RAMFS_SNAPSHOT_MAX_BYTES (RAMFS_MAX_NODES * sizeof(RamfsNode) + RAMFS_DATA_SIZE + 16)

/*
 * Create a new, independent RAMFS instance.
 * Returns the instance pointer to pass to vfs_mount() as fs_data,
 * or NULL if the maximum instance count was reached.
 */
void *ramfs_create(void);

/*
 * Get the shared VFS ops for RAMFS instances.
 */
VfsOps *ramfs_get_ops(void);

/*
 * Helpers (operate on the instance returned by ramfs_create)
 */
int ramfs_create_dir(void *fs, const char *path);
int ramfs_create_file(void *fs, const char *path);

/*
 * Persistence support: instances are flat (no pointers), so the whole
 * state can be snapshotted to disk and restored on the next boot.
 */
size_t ramfs_instance_size(void);
bool ramfs_is_dirty(void *fs);
void ramfs_clear_dirty(void *fs);
void ramfs_mark_dirty(void *fs);
bool ramfs_validate_snapshot(const void *snapshot, size_t size);

#endif /* _OJJY_RAMFS_H */
