/*
 * ojjyOS v3 Kernel - Virtual Filesystem Implementation
 *
 * Routes filesystem calls to the appropriate mounted filesystem.
 */

#include "vfs.h"
#include "../serial.h"
#include "../string.h"

/*
 * Maximum number of mount points
 */
#define MAX_MOUNTS  8

/*
 * Mount table
 */
static VfsMount mounts[MAX_MOUNTS];
static int mount_count = 0;

/*
 * File handle structure
 */
struct VfsFile {
    VfsMount    *mount;         /* Which mount this file belongs to */
    void        *fs_file;       /* Filesystem-specific file data */
    uint32_t    mode;           /* Open mode */
    int64_t     position;       /* Current position */
};

/*
 * Directory handle structure
 */
struct VfsDir {
    VfsMount    *mount;         /* Which mount this dir belongs to */
    void        *fs_dir;        /* Filesystem-specific dir data */
};

/*
 * Simple file/dir handle pools (no malloc yet)
 */
#define MAX_OPEN_FILES  32
#define MAX_OPEN_DIRS   16

static VfsFile file_pool[MAX_OPEN_FILES];
static bool file_used[MAX_OPEN_FILES];

static VfsDir dir_pool[MAX_OPEN_DIRS];
static bool dir_used[MAX_OPEN_DIRS];

/*
 * Allocate a file handle
 */
static VfsFile *alloc_file(void)
{
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!file_used[i]) {
            file_used[i] = true;
            memset(&file_pool[i], 0, sizeof(VfsFile));
            return &file_pool[i];
        }
    }
    return NULL;
}

/*
 * Free a file handle
 */
static void free_file(VfsFile *file)
{
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (&file_pool[i] == file) {
            file_used[i] = false;
            return;
        }
    }
}

/*
 * Allocate a directory handle
 */
static VfsDir *alloc_dir(void)
{
    for (int i = 0; i < MAX_OPEN_DIRS; i++) {
        if (!dir_used[i]) {
            dir_used[i] = true;
            memset(&dir_pool[i], 0, sizeof(VfsDir));
            return &dir_pool[i];
        }
    }
    return NULL;
}

/*
 * Free a directory handle
 */
static void free_dir(VfsDir *dir)
{
    for (int i = 0; i < MAX_OPEN_DIRS; i++) {
        if (&dir_pool[i] == dir) {
            dir_used[i] = false;
            return;
        }
    }
}

/*
 * Find mount point for a path
 * Returns the mount with the longest matching prefix
 */
static VfsMount *find_mount(const char *path)
{
    VfsMount *best = NULL;
    size_t best_len = 0;

    for (int i = 0; i < mount_count; i++) {
        size_t mlen = strlen(mounts[i].path);

        /* Check if path starts with mount path */
        if (strncmp(path, mounts[i].path, mlen) == 0) {
            /* Must match at directory boundary */
            if (path[mlen] == '\0' || path[mlen] == '/' || mlen == 1) {
                if (mlen > best_len) {
                    best = &mounts[i];
                    best_len = mlen;
                }
            }
        }
    }

    return best;
}

/*
 * Get path relative to mount point
 */
static const char *get_relative_path(VfsMount *mount, const char *path)
{
    size_t mlen = strlen(mount->path);

    /* Root mount */
    if (mlen == 1 && mount->path[0] == '/') {
        return path;
    }

    /* Skip mount prefix */
    const char *rel = path + mlen;
    if (*rel == '\0') {
        return "/";
    }
    return rel;
}

/*
 * Initialize VFS
 */
void vfs_init(void)
{
    serial_printf("[VFS] Initializing virtual filesystem...\n");

    mount_count = 0;
    memset(mounts, 0, sizeof(mounts));
    memset(file_used, 0, sizeof(file_used));
    memset(dir_used, 0, sizeof(dir_used));

    serial_printf("[VFS] VFS initialized (max %d mounts, %d files, %d dirs)\n",
        MAX_MOUNTS, MAX_OPEN_FILES, MAX_OPEN_DIRS);
}

/*
 * Mount a filesystem
 */
int vfs_mount(const char *path, VfsOps *ops, void *fs_data, bool readonly)
{
    if (mount_count >= MAX_MOUNTS) {
        serial_printf("[VFS] ERROR: Too many mounts\n");
        return -1;
    }

    if (!path || !ops) {
        serial_printf("[VFS] ERROR: Invalid mount parameters\n");
        return -1;
    }

    /* Check for duplicate mount point */
    for (int i = 0; i < mount_count; i++) {
        if (strcmp(mounts[i].path, path) == 0) {
            serial_printf("[VFS] ERROR: Already mounted at %s\n", path);
            return -1;
        }
    }

    VfsMount *m = &mounts[mount_count++];
    strncpy(m->path, path, sizeof(m->path) - 1);
    m->ops = ops;
    m->fs_data = fs_data;
    m->readonly = readonly;

    serial_printf("[VFS] Mounted %s at %s (%s)\n",
        ops->name, path, readonly ? "ro" : "rw");

    return 0;
}

/*
 * Unmount a filesystem
 */
int vfs_unmount(const char *path)
{
    for (int i = 0; i < mount_count; i++) {
        if (strcmp(mounts[i].path, path) == 0) {
            /* Shift remaining mounts */
            for (int j = i; j < mount_count - 1; j++) {
                mounts[j] = mounts[j + 1];
            }
            mount_count--;
            serial_printf("[VFS] Unmounted %s\n", path);
            return 0;
        }
    }

    serial_printf("[VFS] ERROR: Not mounted: %s\n", path);
    return -1;
}

/*
 * Open a file
 */
VfsFile *vfs_open(const char *path, uint32_t mode)
{
    VfsMount *mount = find_mount(path);
    if (!mount) {
        serial_printf("[VFS] ERROR: No mount for path: %s\n", path);
        return NULL;
    }

    /* Check write permission on readonly mount */
    if (mount->readonly && (mode & (VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC))) {
        serial_printf("[VFS] ERROR: Read-only filesystem\n");
        return NULL;
    }

    if (!mount->ops->open) {
        serial_printf("[VFS] ERROR: Filesystem doesn't support open\n");
        return NULL;
    }

    const char *rel_path = get_relative_path(mount, path);
    VfsFile *file = mount->ops->open(rel_path, mode);
    if (!file) {
        return NULL;
    }

    /* Wrap in our handle */
    VfsFile *vfile = alloc_file();
    if (!vfile) {
        if (mount->ops->close) {
            mount->ops->close(file);
        }
        serial_printf("[VFS] ERROR: Too many open files\n");
        return NULL;
    }

    vfile->mount = mount;
    vfile->fs_file = file;
    vfile->mode = mode;
    vfile->position = 0;

    return vfile;
}

/*
 * Close a file
 */
void vfs_close(VfsFile *file)
{
    if (!file) return;

    if (file->mount && file->mount->ops->close && file->fs_file) {
        file->mount->ops->close(file->fs_file);
    }

    free_file(file);
}

/*
 * Read from a file
 */
ssize_t vfs_read(VfsFile *file, void *buf, size_t count)
{
    if (!file || !file->mount || !file->mount->ops->read) {
        return -1;
    }

    return file->mount->ops->read(file->fs_file, buf, count);
}

/*
 * Write to a file
 */
ssize_t vfs_write(VfsFile *file, const void *buf, size_t count)
{
    if (!file || !file->mount || !file->mount->ops->write) {
        return -1;
    }

    if (file->mount->readonly) {
        return -1;
    }

    return file->mount->ops->write(file->fs_file, buf, count);
}

/*
 * Seek in a file
 */
int64_t vfs_seek(VfsFile *file, int64_t offset, int whence)
{
    if (!file || !file->mount || !file->mount->ops->seek) {
        return -1;
    }

    return file->mount->ops->seek(file->fs_file, offset, whence);
}

/*
 * Get current position
 */
int64_t vfs_tell(VfsFile *file)
{
    if (!file || !file->mount || !file->mount->ops->tell) {
        return -1;
    }

    return file->mount->ops->tell(file->fs_file);
}

/*
 * Read entire file into buffer
 */
ssize_t vfs_read_file(const char *path, void **buffer)
{
    /* Not implemented yet - needs malloc */
    (void)path;
    (void)buffer;
    return -1;
}

/*
 * Get file metadata
 */
int vfs_stat(const char *path, VfsStat *stat)
{
    VfsMount *mount = find_mount(path);
    if (!mount || !mount->ops->stat) {
        return -1;
    }

    const char *rel_path = get_relative_path(mount, path);
    return mount->ops->stat(rel_path, stat);
}

/*
 * Check if path exists
 */
int vfs_exists(const char *path)
{
    VfsMount *mount = find_mount(path);
    if (!mount) return 0;

    if (mount->ops->exists) {
        const char *rel_path = get_relative_path(mount, path);
        return mount->ops->exists(rel_path);
    }

    /* Fallback: try stat */
    VfsStat st;
    return vfs_stat(path, &st) == 0;
}

/*
 * Check if path is a directory
 */
int vfs_isdir(const char *path)
{
    VfsMount *mount = find_mount(path);
    if (!mount) return 0;

    if (mount->ops->isdir) {
        const char *rel_path = get_relative_path(mount, path);
        return mount->ops->isdir(rel_path);
    }

    VfsStat st;
    if (vfs_stat(path, &st) != 0) return 0;
    return st.type == VFS_TYPE_DIR || st.type == VFS_TYPE_BUNDLE;
}

/*
 * Check if path is a file
 */
int vfs_isfile(const char *path)
{
    VfsMount *mount = find_mount(path);
    if (!mount) return 0;

    if (mount->ops->isfile) {
        const char *rel_path = get_relative_path(mount, path);
        return mount->ops->isfile(rel_path);
    }

    VfsStat st;
    if (vfs_stat(path, &st) != 0) return 0;
    return st.type == VFS_TYPE_FILE;
}

/*
 * Open a directory
 */
VfsDir *vfs_opendir(const char *path)
{
    VfsMount *mount = find_mount(path);
    if (!mount || !mount->ops->opendir) {
        return NULL;
    }

    const char *rel_path = get_relative_path(mount, path);
    void *fs_dir = mount->ops->opendir(rel_path);
    if (!fs_dir) {
        return NULL;
    }

    VfsDir *dir = alloc_dir();
    if (!dir) {
        if (mount->ops->closedir) {
            mount->ops->closedir(fs_dir);
        }
        return NULL;
    }

    dir->mount = mount;
    dir->fs_dir = fs_dir;
    return dir;
}

/*
 * Close a directory
 */
void vfs_closedir(VfsDir *dir)
{
    if (!dir) return;

    if (dir->mount && dir->mount->ops->closedir && dir->fs_dir) {
        dir->mount->ops->closedir(dir->fs_dir);
    }

    free_dir(dir);
}

/*
 * Read next directory entry
 */
int vfs_readdir(VfsDir *dir, VfsDirEntry *entry)
{
    if (!dir || !dir->mount || !dir->mount->ops->readdir) {
        return -1;
    }

    return dir->mount->ops->readdir(dir->fs_dir, entry);
}

/*
 * Rewind directory to beginning
 */
int vfs_rewinddir(VfsDir *dir)
{
    if (!dir || !dir->mount || !dir->mount->ops->rewinddir) {
        return -1;
    }

    return dir->mount->ops->rewinddir(dir->fs_dir);
}

/*
 * Create directory
 */
int vfs_mkdir(const char *path)
{
    VfsMount *mount = find_mount(path);
    if (!mount || !mount->ops->mkdir) {
        return -1;
    }

    if (mount->readonly) {
        return -1;
    }

    const char *rel_path = get_relative_path(mount, path);
    return mount->ops->mkdir(rel_path);
}

int vfs_unlink(const char *path)
{
    VfsMount *mount = find_mount(path);
    if (!mount || !mount->ops->unlink) {
        return -1;
    }

    if (mount->readonly) {
        return -1;
    }

    const char *rel_path = get_relative_path(mount, path);
    return mount->ops->unlink(rel_path);
}

int vfs_rename(const char *from, const char *to)
{
    VfsMount *mount = find_mount(from);
    if (!mount || !mount->ops->rename) {
        return -1;
    }

    if (mount->readonly) {
        return -1;
    }

    VfsMount *mount_to = find_mount(to);
    if (mount_to != mount) {
        return -1;
    }

    const char *rel_from = get_relative_path(mount, from);
    const char *rel_to = get_relative_path(mount, to);
    return mount->ops->rename(rel_from, rel_to);
}

/*
 * Get basename of path
 */
const char *vfs_basename(const char *path)
{
    const char *last_slash = NULL;

    for (const char *p = path; *p; p++) {
        if (*p == '/') {
            last_slash = p;
        }
    }

    return last_slash ? last_slash + 1 : path;
}

/*
 * Get directory part of path
 */
int vfs_dirname(const char *path, char *dir, size_t size)
{
    const char *last_slash = NULL;

    for (const char *p = path; *p; p++) {
        if (*p == '/') {
            last_slash = p;
        }
    }

    if (!last_slash) {
        /* No directory component */
        if (size > 0) dir[0] = '\0';
        return 0;
    }

    size_t len = last_slash - path;
    if (len == 0) len = 1;  /* Root directory */

    if (len >= size) {
        return -1;
    }

    memcpy(dir, path, len);
    dir[len] = '\0';
    return 0;
}

/*
 * Join path components
 */
int vfs_join_path(char *dest, size_t size, const char *base, const char *name)
{
    size_t base_len = strlen(base);
    size_t name_len = strlen(name);

    /* Check for trailing slash on base */
    bool need_slash = (base_len > 0 && base[base_len - 1] != '/');

    size_t total = base_len + (need_slash ? 1 : 0) + name_len + 1;
    if (total > size) {
        return -1;
    }

    strcpy(dest, base);
    if (need_slash) {
        dest[base_len] = '/';
        strcpy(dest + base_len + 1, name);
    } else {
        strcpy(dest + base_len, name);
    }

    return 0;
}

/*
 * Normalize a path (remove . and .., handle multiple slashes)
 */
int vfs_normalize_path(const char *path, char *normalized, size_t size)
{
    if (!path || !normalized || size == 0) {
        return -1;
    }

    /* Simple implementation - just copy for now */
    /* TODO: Handle . and .. properly */
    size_t len = strlen(path);
    if (len >= size) {
        return -1;
    }

    strcpy(normalized, path);
    return 0;
}

/*
 * Check if path is an app bundle
 */
bool vfs_is_bundle(const char *path)
{
    size_t len = strlen(path);
    if (len < 4) return false;

    return strcmp(path + len - 4, ".app") == 0;
}
