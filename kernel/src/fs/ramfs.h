/*
 * ojjyOS v3 Kernel - RAMFS
 *
 * Simple in-memory read/write filesystem for user data.
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

typedef struct {
    const uint8_t *data_pool;
    uint8_t *data_pool_rw;
    uint64_t data_size;
} RamfsStore;

VfsOps *ramfs_init(void);
int ramfs_create_dir(const char *path);
int ramfs_create_file(const char *path);

#endif /* _OJJY_RAMFS_H */
