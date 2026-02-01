/*
 * ojjyOS v3 Kernel - OJFS (OjjyOS FileSystem)
 *
 * A simple read-only packed filesystem for MVP.
 * Designed to be embedded in the kernel or loaded from disk.
 *
 * Format:
 * ┌─────────────────────────────────────────┐
 * │ OjfsHeader (magic, version, counts)     │
 * ├─────────────────────────────────────────┤
 * │ OjfsEntry[] - file/directory entries    │
 * ├─────────────────────────────────────────┤
 * │ String table (null-terminated names)    │
 * ├─────────────────────────────────────────┤
 * │ File data (concatenated)                │
 * └─────────────────────────────────────────┘
 *
 * All offsets are from the start of the filesystem image.
 */

#ifndef _OJJY_OJFS_H
#define _OJJY_OJFS_H

#include "../types.h"
#include "vfs.h"

/*
 * OJFS Magic number: "OJFS" in little-endian
 */
#define OJFS_MAGIC      0x53464A4F  /* "OJFS" */
#define OJFS_VERSION    1

/*
 * Entry types
 */
#define OJFS_TYPE_FILE      1
#define OJFS_TYPE_DIR       2

/*
 * Filesystem header (32 bytes)
 */
typedef struct {
    uint32_t    magic;          /* OJFS_MAGIC */
    uint32_t    version;        /* OJFS_VERSION */
    uint32_t    entry_count;    /* Number of entries */
    uint32_t    string_offset;  /* Offset to string table */
    uint32_t    string_size;    /* Size of string table */
    uint32_t    data_offset;    /* Offset to file data */
    uint64_t    total_size;     /* Total image size */
} PACKED OjfsHeader;

/*
 * File/directory entry (32 bytes)
 */
typedef struct {
    uint32_t    name_offset;    /* Offset in string table */
    uint32_t    parent;         /* Parent directory index (0xFFFFFFFF = root) */
    uint32_t    type;           /* OJFS_TYPE_FILE or OJFS_TYPE_DIR */
    uint32_t    permissions;    /* VFS permission bits */
    uint64_t    data_offset;    /* Offset to data (files only) */
    uint64_t    size;           /* Size in bytes (files only) */
} PACKED OjfsEntry;

/*
 * Runtime file handle
 */
typedef struct {
    const OjfsEntry *entry;     /* Pointer to entry */
    const uint8_t   *data;      /* Pointer to file data */
    uint64_t        position;   /* Current read position */
} OjfsFile;

/*
 * Runtime directory handle
 */
typedef struct {
    uint32_t    parent_index;   /* Index of directory entry */
    uint32_t    current;        /* Current entry index for iteration */
    uint32_t    entry_count;    /* Total entries in filesystem */
} OjfsDirHandle;

/*
 * Mounted OJFS instance
 */
typedef struct {
    const uint8_t   *base;          /* Base pointer to filesystem image */
    const OjfsHeader *header;       /* Header pointer */
    const OjfsEntry  *entries;      /* Entry table pointer */
    const char       *strings;      /* String table pointer */
    const uint8_t    *data;         /* Data section pointer */
} OjfsInstance;

/*
 * Initialize OJFS from a memory buffer
 * Returns the VfsOps pointer to use with vfs_mount
 */
VfsOps *ojfs_init(const void *image, size_t size, OjfsInstance **instance);

/*
 * Get VFS operations for OJFS
 */
VfsOps *ojfs_get_ops(void);

/*
 * Validate an OJFS image
 */
bool ojfs_validate(const void *image, size_t size);

/*
 * Debug: Print filesystem contents
 */
void ojfs_print_tree(OjfsInstance *fs);

#endif /* _OJJY_OJFS_H */
