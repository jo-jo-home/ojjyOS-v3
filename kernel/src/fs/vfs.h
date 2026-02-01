/*
 * ojjyOS v3 Kernel - Virtual Filesystem Interface
 *
 * Provides a unified interface for all filesystem operations.
 * Inspired by POSIX but simplified for our needs.
 */

#ifndef _OJJY_VFS_H
#define _OJJY_VFS_H

#include "../types.h"

/*
 * File types
 */
typedef enum {
    VFS_TYPE_UNKNOWN = 0,
    VFS_TYPE_FILE,          /* Regular file */
    VFS_TYPE_DIR,           /* Directory */
    VFS_TYPE_BUNDLE,        /* App bundle (.app directory) */
    VFS_TYPE_SYMLINK,       /* Symbolic link (Phase 2) */
} VfsFileType;

/*
 * File open modes
 */
#define VFS_O_READ      (1 << 0)    /* Open for reading */
#define VFS_O_WRITE     (1 << 1)    /* Open for writing (Phase 2) */
#define VFS_O_CREATE    (1 << 2)    /* Create if doesn't exist (Phase 2) */
#define VFS_O_TRUNC     (1 << 3)    /* Truncate to zero (Phase 2) */
#define VFS_O_APPEND    (1 << 4)    /* Append mode (Phase 2) */

/*
 * Seek modes
 */
#define VFS_SEEK_SET    0   /* From beginning */
#define VFS_SEEK_CUR    1   /* From current position */
#define VFS_SEEK_END    2   /* From end */

/*
 * Permission bits (capability-based simplified model)
 */
#define VFS_PERM_READ       (1 << 0)
#define VFS_PERM_WRITE      (1 << 1)
#define VFS_PERM_EXECUTE    (1 << 2)
#define VFS_PERM_SYSTEM     (1 << 7)    /* System-owned, user cannot modify */

/*
 * File metadata (stat-like structure)
 */
typedef struct {
    VfsFileType type;           /* File type */
    uint64_t    size;           /* Size in bytes (0 for directories) */
    uint8_t     permissions;    /* Permission bits */
    uint32_t    uid;            /* Owner user ID (0 = system) */
    uint64_t    created;        /* Creation timestamp */
    uint64_t    modified;       /* Modification timestamp */
    uint64_t    inode;          /* Unique file identifier */
} VfsStat;

/*
 * Directory entry
 */
#define VFS_NAME_MAX    255

typedef struct {
    char        name[VFS_NAME_MAX + 1];
    VfsFileType type;
    uint64_t    size;
    uint64_t    inode;
} VfsDirEntry;

/*
 * File handle (opaque to users)
 */
typedef struct VfsFile VfsFile;

/*
 * Directory handle (opaque to users)
 */
typedef struct VfsDir VfsDir;

/*
 * Filesystem operations (implemented by each filesystem)
 */
typedef struct VfsOps {
    const char *name;   /* Filesystem name (e.g., "ojfs") */

    /* File operations */
    VfsFile *(*open)(const char *path, uint32_t mode);
    void (*close)(VfsFile *file);
    ssize_t (*read)(VfsFile *file, void *buf, size_t count);
    ssize_t (*write)(VfsFile *file, const void *buf, size_t count);
    int64_t (*seek)(VfsFile *file, int64_t offset, int whence);
    int64_t (*tell)(VfsFile *file);

    /* Metadata operations */
    int (*stat)(const char *path, VfsStat *stat);

    /* Directory operations */
    VfsDir *(*opendir)(const char *path);
    void (*closedir)(VfsDir *dir);
    int (*readdir)(VfsDir *dir, VfsDirEntry *entry);
    int (*rewinddir)(VfsDir *dir);

    /* Mutating operations (optional) */
    int (*mkdir)(const char *path);
    int (*unlink)(const char *path);
    int (*rename)(const char *from, const char *to);

    /* Path operations */
    int (*exists)(const char *path);
    int (*isdir)(const char *path);
    int (*isfile)(const char *path);

} VfsOps;

/*
 * Mount point
 */
typedef struct {
    char        path[256];      /* Mount point path (e.g., "/") */
    VfsOps      *ops;           /* Filesystem operations */
    void        *fs_data;       /* Filesystem-specific data */
    bool        readonly;       /* Read-only mount */
} VfsMount;

/*
 * Initialize VFS
 */
void vfs_init(void);

/*
 * Mount a filesystem at a path
 */
int vfs_mount(const char *path, VfsOps *ops, void *fs_data, bool readonly);

/*
 * Unmount a filesystem
 */
int vfs_unmount(const char *path);

/*
 * File operations (use mounted filesystems)
 */
VfsFile *vfs_open(const char *path, uint32_t mode);
void vfs_close(VfsFile *file);
ssize_t vfs_read(VfsFile *file, void *buf, size_t count);
ssize_t vfs_write(VfsFile *file, const void *buf, size_t count);
int64_t vfs_seek(VfsFile *file, int64_t offset, int whence);
int64_t vfs_tell(VfsFile *file);

/*
 * Read entire file into buffer (allocates memory)
 * Returns size read, or -1 on error
 * Caller must free the buffer
 */
ssize_t vfs_read_file(const char *path, void **buffer);

/*
 * Metadata operations
 */
int vfs_stat(const char *path, VfsStat *stat);
int vfs_exists(const char *path);
int vfs_isdir(const char *path);
int vfs_isfile(const char *path);

/*
 * Directory operations
 */
VfsDir *vfs_opendir(const char *path);
void vfs_closedir(VfsDir *dir);
int vfs_readdir(VfsDir *dir, VfsDirEntry *entry);
int vfs_rewinddir(VfsDir *dir);

/*
 * Directory creation
 */
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);
int vfs_rename(const char *from, const char *to);

/*
 * Path utilities
 */
const char *vfs_basename(const char *path);
int vfs_dirname(const char *path, char *dir, size_t size);
int vfs_join_path(char *dest, size_t size, const char *base, const char *name);
int vfs_normalize_path(const char *path, char *normalized, size_t size);

/*
 * Check if path is an app bundle (.app extension)
 */
bool vfs_is_bundle(const char *path);

#endif /* _OJJY_VFS_H */
