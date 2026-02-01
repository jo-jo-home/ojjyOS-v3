/*
 * ojjyOS v3 Kernel - OJFS Implementation
 *
 * Read-only packed filesystem for MVP.
 */

#include "ojfs.h"
#include "../serial.h"
#include "../string.h"
#include "../console.h"

/*
 * Maximum OJFS instances (for multiple mounts)
 */
#define MAX_OJFS_INSTANCES  4

static OjfsInstance instances[MAX_OJFS_INSTANCES];
static int instance_count = 0;

/* Current instance for VFS operations (set during open) */
static OjfsInstance *current_instance = NULL;

/*
 * File handle pool
 */
#define MAX_OJFS_FILES  16
static OjfsFile file_pool[MAX_OJFS_FILES];
static bool file_used[MAX_OJFS_FILES];

/*
 * Directory handle pool
 */
#define MAX_OJFS_DIRS   8
static OjfsDirHandle dir_pool[MAX_OJFS_DIRS];
static bool dir_used[MAX_OJFS_DIRS];

/*
 * Get entry name from string table
 */
static const char *get_entry_name(OjfsInstance *fs, const OjfsEntry *entry)
{
    return fs->strings + entry->name_offset;
}

/*
 * Find entry by path
 * Returns entry index or -1 if not found
 */
static int find_entry(OjfsInstance *fs, const char *path)
{
    /* Handle root directory */
    if (strcmp(path, "/") == 0 || path[0] == '\0') {
        /* Find root directory entry (parent = 0xFFFFFFFF) */
        for (uint32_t i = 0; i < fs->header->entry_count; i++) {
            if (fs->entries[i].parent == 0xFFFFFFFF &&
                fs->entries[i].type == OJFS_TYPE_DIR) {
                return i;
            }
        }
        return -1;
    }

    /* Skip leading slash */
    if (path[0] == '/') path++;

    /* Parse path components */
    uint32_t parent = 0xFFFFFFFF;  /* Start at root */
    char component[VFS_NAME_MAX + 1];

    while (*path) {
        /* Extract next component */
        size_t len = 0;
        while (path[len] && path[len] != '/') {
            if (len < VFS_NAME_MAX) {
                component[len] = path[len];
            }
            len++;
        }
        component[len] = '\0';

        /* Find entry with this name and parent */
        bool found = false;
        for (uint32_t i = 0; i < fs->header->entry_count; i++) {
            if (fs->entries[i].parent == parent) {
                const char *name = get_entry_name(fs, &fs->entries[i]);
                if (strcmp(name, component) == 0) {
                    parent = i;
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            return -1;
        }

        /* Move to next component */
        path += len;
        if (*path == '/') path++;
    }

    return (int)parent;
}

/*
 * Allocate file handle
 */
static OjfsFile *alloc_ojfs_file(void)
{
    for (int i = 0; i < MAX_OJFS_FILES; i++) {
        if (!file_used[i]) {
            file_used[i] = true;
            memset(&file_pool[i], 0, sizeof(OjfsFile));
            return &file_pool[i];
        }
    }
    return NULL;
}

/*
 * Free file handle
 */
static void free_ojfs_file(OjfsFile *file)
{
    for (int i = 0; i < MAX_OJFS_FILES; i++) {
        if (&file_pool[i] == file) {
            file_used[i] = false;
            return;
        }
    }
}

/*
 * Allocate directory handle
 */
static OjfsDirHandle *alloc_ojfs_dir(void)
{
    for (int i = 0; i < MAX_OJFS_DIRS; i++) {
        if (!dir_used[i]) {
            dir_used[i] = true;
            memset(&dir_pool[i], 0, sizeof(OjfsDirHandle));
            return &dir_pool[i];
        }
    }
    return NULL;
}

/*
 * Free directory handle
 */
static void free_ojfs_dir(OjfsDirHandle *dir)
{
    for (int i = 0; i < MAX_OJFS_DIRS; i++) {
        if (&dir_pool[i] == dir) {
            dir_used[i] = false;
            return;
        }
    }
}

/*
 * VFS open implementation
 */
static VfsFile *ojfs_open(const char *path, uint32_t mode)
{
    if (!current_instance) {
        serial_printf("[OJFS] ERROR: No instance set\n");
        return NULL;
    }

    /* OJFS is read-only */
    if (mode & (VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC)) {
        serial_printf("[OJFS] ERROR: Read-only filesystem\n");
        return NULL;
    }

    int idx = find_entry(current_instance, path);
    if (idx < 0) {
        serial_printf("[OJFS] ERROR: File not found: %s\n", path);
        return NULL;
    }

    const OjfsEntry *entry = &current_instance->entries[idx];
    if (entry->type != OJFS_TYPE_FILE) {
        serial_printf("[OJFS] ERROR: Not a file: %s\n", path);
        return NULL;
    }

    OjfsFile *file = alloc_ojfs_file();
    if (!file) {
        serial_printf("[OJFS] ERROR: Too many open files\n");
        return NULL;
    }

    file->entry = entry;
    file->data = current_instance->data + (entry->data_offset - current_instance->header->data_offset);
    file->position = 0;

    return (VfsFile *)file;
}

/*
 * VFS close implementation
 */
static void ojfs_close(VfsFile *file)
{
    free_ojfs_file((OjfsFile *)file);
}

/*
 * VFS read implementation
 */
static ssize_t ojfs_read(VfsFile *vfile, void *buf, size_t count)
{
    OjfsFile *file = (OjfsFile *)vfile;
    if (!file || !file->entry || !file->data) {
        return -1;
    }

    /* Calculate how much we can read */
    uint64_t remaining = file->entry->size - file->position;
    if (count > remaining) {
        count = remaining;
    }

    if (count == 0) {
        return 0;
    }

    memcpy(buf, file->data + file->position, count);
    file->position += count;

    return count;
}

/*
 * VFS write implementation (not supported)
 */
static ssize_t ojfs_write(VfsFile *file, const void *buf, size_t count)
{
    (void)file;
    (void)buf;
    (void)count;
    return -1;  /* Read-only */
}

/*
 * VFS seek implementation
 */
static int64_t ojfs_seek(VfsFile *vfile, int64_t offset, int whence)
{
    OjfsFile *file = (OjfsFile *)vfile;
    if (!file || !file->entry) {
        return -1;
    }

    int64_t new_pos;
    switch (whence) {
        case VFS_SEEK_SET:
            new_pos = offset;
            break;
        case VFS_SEEK_CUR:
            new_pos = file->position + offset;
            break;
        case VFS_SEEK_END:
            new_pos = file->entry->size + offset;
            break;
        default:
            return -1;
    }

    if (new_pos < 0 || new_pos > (int64_t)file->entry->size) {
        return -1;
    }

    file->position = new_pos;
    return new_pos;
}

/*
 * VFS tell implementation
 */
static int64_t ojfs_tell(VfsFile *vfile)
{
    OjfsFile *file = (OjfsFile *)vfile;
    if (!file) return -1;
    return file->position;
}

/*
 * VFS stat implementation
 */
static int ojfs_stat(const char *path, VfsStat *stat)
{
    if (!current_instance || !stat) return -1;

    int idx = find_entry(current_instance, path);
    if (idx < 0) return -1;

    const OjfsEntry *entry = &current_instance->entries[idx];

    stat->type = (entry->type == OJFS_TYPE_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;

    /* Check if it's a bundle */
    const char *name = get_entry_name(current_instance, entry);
    if (entry->type == OJFS_TYPE_DIR && vfs_is_bundle(name)) {
        stat->type = VFS_TYPE_BUNDLE;
    }

    stat->size = entry->size;
    stat->permissions = entry->permissions;
    stat->uid = 0;
    stat->created = 0;
    stat->modified = 0;
    stat->inode = idx;

    return 0;
}

/*
 * VFS opendir implementation
 */
static VfsDir *ojfs_opendir(const char *path)
{
    if (!current_instance) return NULL;

    int idx = find_entry(current_instance, path);
    if (idx < 0) {
        /* Special case: root with no explicit entry */
        if (strcmp(path, "/") == 0 || path[0] == '\0') {
            OjfsDirHandle *dir = alloc_ojfs_dir();
            if (!dir) return NULL;

            dir->parent_index = 0xFFFFFFFF;
            dir->current = 0;
            dir->entry_count = current_instance->header->entry_count;
            return (VfsDir *)dir;
        }
        return NULL;
    }

    const OjfsEntry *entry = &current_instance->entries[idx];
    if (entry->type != OJFS_TYPE_DIR) {
        return NULL;
    }

    OjfsDirHandle *dir = alloc_ojfs_dir();
    if (!dir) return NULL;

    dir->parent_index = idx;
    dir->current = 0;
    dir->entry_count = current_instance->header->entry_count;

    return (VfsDir *)dir;
}

/*
 * VFS closedir implementation
 */
static void ojfs_closedir(VfsDir *vdir)
{
    free_ojfs_dir((OjfsDirHandle *)vdir);
}

/*
 * VFS readdir implementation
 */
static int ojfs_readdir(VfsDir *vdir, VfsDirEntry *entry)
{
    OjfsDirHandle *dir = (OjfsDirHandle *)vdir;
    if (!dir || !entry || !current_instance) return -1;

    /* Find next entry with matching parent */
    while (dir->current < dir->entry_count) {
        const OjfsEntry *e = &current_instance->entries[dir->current];
        dir->current++;

        if (e->parent == dir->parent_index) {
            const char *name = get_entry_name(current_instance, e);
            strncpy(entry->name, name, VFS_NAME_MAX);
            entry->name[VFS_NAME_MAX] = '\0';

            entry->type = (e->type == OJFS_TYPE_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
            if (e->type == OJFS_TYPE_DIR && vfs_is_bundle(name)) {
                entry->type = VFS_TYPE_BUNDLE;
            }

            entry->size = e->size;
            entry->inode = dir->current - 1;

            return 0;
        }
    }

    return -1;  /* No more entries */
}

/*
 * VFS rewinddir implementation
 */
static int ojfs_rewinddir(VfsDir *vdir)
{
    OjfsDirHandle *dir = (OjfsDirHandle *)vdir;
    if (!dir) return -1;
    dir->current = 0;
    return 0;
}

/*
 * VFS exists implementation
 */
static int ojfs_exists(const char *path)
{
    if (!current_instance) return 0;
    return find_entry(current_instance, path) >= 0;
}

/*
 * VFS isdir implementation
 */
static int ojfs_isdir(const char *path)
{
    if (!current_instance) return 0;

    int idx = find_entry(current_instance, path);
    if (idx < 0) return 0;

    return current_instance->entries[idx].type == OJFS_TYPE_DIR;
}

/*
 * VFS isfile implementation
 */
static int ojfs_isfile(const char *path)
{
    if (!current_instance) return 0;

    int idx = find_entry(current_instance, path);
    if (idx < 0) return 0;

    return current_instance->entries[idx].type == OJFS_TYPE_FILE;
}

/*
 * OJFS VFS operations
 */
static VfsOps ojfs_ops = {
    .name = "ojfs",
    .open = ojfs_open,
    .close = ojfs_close,
    .read = ojfs_read,
    .write = ojfs_write,
    .seek = ojfs_seek,
    .tell = ojfs_tell,
    .stat = ojfs_stat,
    .opendir = ojfs_opendir,
    .closedir = ojfs_closedir,
    .readdir = ojfs_readdir,
    .rewinddir = ojfs_rewinddir,
    .exists = ojfs_exists,
    .isdir = ojfs_isdir,
    .isfile = ojfs_isfile,
    .mkdir = NULL,
    .unlink = NULL,
    .rename = NULL,
};

/*
 * Validate OJFS image
 */
bool ojfs_validate(const void *image, size_t size)
{
    if (!image || size < sizeof(OjfsHeader)) {
        return false;
    }

    const OjfsHeader *header = (const OjfsHeader *)image;

    if (header->magic != OJFS_MAGIC) {
        serial_printf("[OJFS] Invalid magic: 0x%x (expected 0x%x)\n",
            header->magic, OJFS_MAGIC);
        return false;
    }

    if (header->version != OJFS_VERSION) {
        serial_printf("[OJFS] Unsupported version: %d\n", header->version);
        return false;
    }

    if (header->total_size > size) {
        serial_printf("[OJFS] Image truncated\n");
        return false;
    }

    return true;
}

/*
 * Initialize OJFS from memory buffer
 */
VfsOps *ojfs_init(const void *image, size_t size, OjfsInstance **out_instance)
{
    if (!ojfs_validate(image, size)) {
        return NULL;
    }

    if (instance_count >= MAX_OJFS_INSTANCES) {
        serial_printf("[OJFS] Too many instances\n");
        return NULL;
    }

    OjfsInstance *fs = &instances[instance_count++];
    fs->base = (const uint8_t *)image;
    fs->header = (const OjfsHeader *)image;
    fs->entries = (const OjfsEntry *)(fs->base + sizeof(OjfsHeader));
    fs->strings = (const char *)(fs->base + fs->header->string_offset);
    fs->data = fs->base + fs->header->data_offset;

    /* Set as current instance */
    current_instance = fs;

    serial_printf("[OJFS] Initialized: %d entries, %d bytes\n",
        fs->header->entry_count, (int)fs->header->total_size);

    if (out_instance) {
        *out_instance = fs;
    }

    return &ojfs_ops;
}

/*
 * Get VFS operations
 */
VfsOps *ojfs_get_ops(void)
{
    return &ojfs_ops;
}

/*
 * Print filesystem tree (debug)
 */
void ojfs_print_tree(OjfsInstance *fs)
{
    if (!fs) return;

    console_printf("\n=== OJFS Contents ===\n");
    console_printf("Entries: %d\n\n", fs->header->entry_count);

    for (uint32_t i = 0; i < fs->header->entry_count; i++) {
        const OjfsEntry *e = &fs->entries[i];
        const char *name = get_entry_name(fs, e);
        const char *type = (e->type == OJFS_TYPE_DIR) ? "DIR " : "FILE";

        /* Calculate depth based on parent chain */
        int depth = 0;
        uint32_t parent = e->parent;
        while (parent != 0xFFFFFFFF && depth < 10) {
            depth++;
            parent = fs->entries[parent].parent;
        }

        /* Print with indentation */
        for (int j = 0; j < depth; j++) {
            console_printf("  ");
        }

        if (e->type == OJFS_TYPE_DIR) {
            console_printf("[%s] %s/\n", type, name);
        } else {
            console_printf("[%s] %s (%d bytes)\n", type, name, (int)e->size);
        }
    }

    console_printf("\n");
}
