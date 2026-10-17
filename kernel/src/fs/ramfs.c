/*
 * ojjyOS v3 Kernel - RAMFS Implementation
 *
 * In-memory read/write filesystem. Each instance owns its own node table
 * and a free-list-managed data pool, so mount points are fully
 * independent (e.g. /Users and /Library do not alias each other).
 */

#include "ramfs.h"
#include "../string.h"
#include "../serial.h"

#define RAMFS_MAX_FILES    64
#define RAMFS_MAX_DIRS     32
#define RAMFS_MAX_INSTANCES 4

/*
 * Data pool block header for the first-fit allocator.
 * Layout: [size][free] [data...] - data offsets are offsets of `data`.
 */
typedef struct {
    uint32_t size;      /* Size of the data area */
    uint32_t free;      /* 1 if the block is free */
} RamfsBlock;

#define RAMFS_BLOCK_HDR   sizeof(RamfsBlock)
#define RAMFS_BLOCK_MIN   32          /* Minimum useful block (hdr + 24 data) */
#define RAMFS_BLOCK_ALIGN 16

/*
 * Per-instance filesystem state
 */
typedef struct {
    RamfsNode nodes[RAMFS_MAX_NODES];
    int node_count;

    uint8_t data_pool[RAMFS_DATA_SIZE];

    bool dirty;     /* Modified since the last persistence save */
} RamfsInstance;

_Static_assert(sizeof(RamfsInstance) <= RAMFS_SNAPSHOT_MAX_BYTES, "snapshot capacity");

static RamfsInstance ramfs_instances[RAMFS_MAX_INSTANCES];
static int ramfs_instance_count = 0;

/*
 * File/dir handles carry their instance so read/write/seek/readdir
 * work without globals.
 */
typedef struct {
    RamfsInstance *fs;
    RamfsNode *node;
    uint64_t position;
} RamfsFile;

typedef struct {
    RamfsInstance *fs;
    uint32_t parent;
    uint32_t current;
} RamfsDir;

static RamfsFile file_pool[RAMFS_MAX_FILES];
static bool file_used[RAMFS_MAX_FILES];

static RamfsDir dir_pool[RAMFS_MAX_DIRS];
static bool dir_used[RAMFS_MAX_DIRS];

/* ------------------------------------------------------------------ */
/* Data pool: first-fit free-list allocator                            */
/* ------------------------------------------------------------------ */

static RamfsBlock *block_at(RamfsInstance *fs, uint64_t data_offset)
{
    return (RamfsBlock *)(fs->data_pool + data_offset - RAMFS_BLOCK_HDR);
}

/*
 * Allocate a data block of at least `size` bytes.
 * Returns the data-area offset, or -1 when the pool is exhausted.
 */
static int64_t ramfs_alloc_data(RamfsInstance *fs, uint64_t size)
{
    /* Round the request up for alignment hygiene */
    uint64_t need = (size + RAMFS_BLOCK_ALIGN - 1) & ~((uint64_t)RAMFS_BLOCK_ALIGN - 1);
    if (need == 0) need = RAMFS_BLOCK_ALIGN;

    /* First-fit scan of the block list */
    uint64_t off = 0;
    while (off + RAMFS_BLOCK_HDR <= RAMFS_DATA_SIZE) {
        RamfsBlock *b = (RamfsBlock *)(fs->data_pool + off);
        uint64_t data_off = off + RAMFS_BLOCK_HDR;

        if (b->free && (uint64_t)b->size >= need) {
            uint64_t rem = b->size - need;
            if (rem >= RAMFS_BLOCK_MIN) {
                /* Split: use the front part, keep the tail free */
                uint32_t old_size = b->size;
                b->size = (uint32_t)need;
                b->free = 0;

                RamfsBlock *tail = (RamfsBlock *)(fs->data_pool + data_off + need);
                tail->size = (uint32_t)(old_size - need - RAMFS_BLOCK_HDR);
                tail->free = 1;
            } else {
                b->free = 0;  /* Keep the whole block (small remainder) */
            }
            return (int64_t)data_off;
        }

        /* Move to the next block */
        off = data_off + b->size;
    }

    serial_printf("[RAMFS] Data pool exhausted (need %d bytes)\n", (int)need);
    return -1;
}

/*
 * Free a data block previously returned by ramfs_alloc_data.
 * Coalesces with an immediately-following free block to limit
 * fragmentation from incremental file growth.
 */
static void ramfs_free_data(RamfsInstance *fs, uint64_t data_offset)
{
    if (data_offset == 0 || data_offset + RAMFS_BLOCK_HDR > RAMFS_DATA_SIZE) {
        return;
    }

    RamfsBlock *b = block_at(fs, data_offset);
    b->free = 1;

    /* Coalesce forward */
    uint64_t next_off = data_offset + b->size;
    if (next_off + RAMFS_BLOCK_HDR <= RAMFS_DATA_SIZE) {
        RamfsBlock *next = (RamfsBlock *)(fs->data_pool + next_off);
        if (next->free) {
            b->size += RAMFS_BLOCK_HDR + next->size;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Node management                                                     */
/* ------------------------------------------------------------------ */

static RamfsNode *alloc_node(RamfsInstance *fs)
{
    /* Reuse slots freed by unlink */
    for (int i = 0; i < fs->node_count; i++) {
        if (fs->nodes[i].type == VFS_TYPE_UNKNOWN) {
            memset(&fs->nodes[i], 0, sizeof(RamfsNode));
            return &fs->nodes[i];
        }
    }
    if (fs->node_count >= RAMFS_MAX_NODES) {
        return NULL;
    }
    RamfsNode *node = &fs->nodes[fs->node_count++];
    memset(node, 0, sizeof(*node));
    return node;
}

static RamfsFile *alloc_file(void)
{
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!file_used[i]) {
            file_used[i] = true;
            memset(&file_pool[i], 0, sizeof(RamfsFile));
            return &file_pool[i];
        }
    }
    return NULL;
}

static void free_file(RamfsFile *file)
{
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (&file_pool[i] == file) {
            file_used[i] = false;
            return;
        }
    }
}

static RamfsDir *alloc_dir(void)
{
    for (int i = 0; i < RAMFS_MAX_DIRS; i++) {
        if (!dir_used[i]) {
            dir_used[i] = true;
            memset(&dir_pool[i], 0, sizeof(RamfsDir));
            return &dir_pool[i];
        }
    }
    return NULL;
}

static void free_dir(RamfsDir *dir)
{
    for (int i = 0; i < RAMFS_MAX_DIRS; i++) {
        if (&dir_pool[i] == dir) {
            dir_used[i] = false;
            return;
        }
    }
}

static int find_child(RamfsInstance *fs, uint32_t parent, const char *name)
{
    for (int i = 0; i < fs->node_count; i++) {
        if (fs->nodes[i].type != VFS_TYPE_UNKNOWN &&
            fs->nodes[i].parent == parent &&
            strcmp(fs->nodes[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static bool valid_component(const char *name)
{
    if (!name || name[0] == '\0') return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    return true;
}

static int find_node(RamfsInstance *fs, const char *path)
{
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) {
        return 0;
    }

    const char *p = path;
    if (*p == '/') p++;

    uint32_t parent = 0;
    char component[VFS_NAME_MAX + 1];

    while (*p) {
        size_t len = 0;
        while (p[len] && p[len] != '/') {
            if (len < VFS_NAME_MAX) component[len] = p[len];
            len++;
        }
        component[len < VFS_NAME_MAX ? len : VFS_NAME_MAX] = '\0';

        /* Skip empty components (double slashes) */
        if (component[0] == '\0') {
            p += len;
            if (*p == '/') p++;
            continue;
        }

        int idx = find_child(fs, parent, component);
        if (idx < 0) return -1;
        parent = (uint32_t)idx;

        p += len;
        if (*p == '/') p++;
    }

    return (int)parent;
}

static int ensure_dir(RamfsInstance *fs, const char *path)
{
    if (!path || path[0] == '\0') return -1;

    char temp[256];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    if (strcmp(temp, "/") == 0) return 0;

    if (temp[0] == '/') {
        memmove(temp, temp + 1, strlen(temp));
    }

    uint32_t parent = 0;
    char component[VFS_NAME_MAX + 1];
    char *p = temp;
    while (*p) {
        size_t len = 0;
        while (p[len] && p[len] != '/') {
            if (len < VFS_NAME_MAX) component[len] = p[len];
            len++;
        }
        component[len < VFS_NAME_MAX ? len : VFS_NAME_MAX] = '\0';

        /* Skip empty components */
        if (component[0] == '\0') {
            p += len;
            if (*p == '/') p++;
            continue;
        }

        if (!valid_component(component)) {
            return -1;
        }

        int idx = find_child(fs, parent, component);
        if (idx < 0) {
            RamfsNode *node = alloc_node(fs);
            if (!node) return -1;
            strncpy(node->name, component, VFS_NAME_MAX);
            node->name[VFS_NAME_MAX] = '\0';
            node->parent = parent;
            node->type = VFS_TYPE_DIR;
            node->permissions = VFS_PERM_READ | VFS_PERM_WRITE;
            idx = (int)(node - fs->nodes);
        } else if (fs->nodes[idx].type != VFS_TYPE_DIR) {
            return -1;  /* A non-directory is in the way */
        }
        parent = (uint32_t)idx;

        p += len;
        if (*p == '/') p++;
    }

    return (int)parent;
}

static int create_node(RamfsInstance *fs, const char *path, VfsFileType type)
{
    if (!path || path[0] == '\0') return -1;

    char dir[256];
    if (vfs_dirname(path, dir, sizeof(dir)) != 0) return -1;

    const char *base = vfs_basename(path);
    if (!valid_component(base)) return -1;

    int parent = ensure_dir(fs, dir[0] ? dir : "/");
    if (parent < 0) return -1;

    if (find_child(fs, (uint32_t)parent, base) >= 0) return -1;

    RamfsNode *node = alloc_node(fs);
    if (!node) return -1;
    strncpy(node->name, base, VFS_NAME_MAX);
    node->name[VFS_NAME_MAX] = '\0';
    node->parent = (uint32_t)parent;
    node->type = type;
    node->permissions = VFS_PERM_READ | VFS_PERM_WRITE;

    return (int)(node - fs->nodes);
}

/* ------------------------------------------------------------------ */
/* VFS operations                                                      */
/* ------------------------------------------------------------------ */

static VfsFile *ramfs_open(void *fs_data, const char *path, uint32_t mode)
{
    RamfsInstance *fs = (RamfsInstance *)fs_data;
    if (!fs) return NULL;
    bool modified = false;

    int idx = find_node(fs, path);

    if (idx < 0) {
        if (mode & VFS_O_CREATE) {
            idx = create_node(fs, path, VFS_TYPE_FILE);
            if (idx >= 0) modified = true;
        }
    }

    if (idx < 0) return NULL;
    RamfsNode *node = &fs->nodes[idx];
    if (node->type != VFS_TYPE_FILE) return NULL;

    /* O_TRUNC only applies when writing */
    if ((mode & VFS_O_TRUNC) != 0 && (mode & VFS_O_WRITE) != 0) {
        node->size = 0;
        modified = true;
    }

    if (node->capacity == 0) {
        int64_t offset = ramfs_alloc_data(fs, 4096);
        if (offset < 0) return NULL;
        node->data_offset = (uint64_t)offset;
        node->capacity = 4096;
    }

    RamfsFile *file = alloc_file();
    if (!file) return NULL;
    file->fs = fs;
    file->node = node;
    file->position = (mode & VFS_O_APPEND) ? node->size : 0;
    if (modified) fs->dirty = true;
    return (VfsFile *)file;
}

static void ramfs_close(VfsFile *file)
{
    free_file((RamfsFile *)file);
}

static ssize_t ramfs_read(VfsFile *vfile, void *buf, size_t count)
{
    RamfsFile *file = (RamfsFile *)vfile;
    if (!file || !file->node || !file->fs) return -1;

    RamfsNode *node = file->node;
    if (file->position >= node->size) return 0;

    uint64_t remaining = node->size - file->position;
    if (count > remaining) count = remaining;

    memcpy(buf, file->fs->data_pool + node->data_offset + file->position, count);
    file->position += count;
    return count;
}

static ssize_t ramfs_write(VfsFile *vfile, const void *buf, size_t count)
{
    RamfsFile *file = (RamfsFile *)vfile;
    if (!file || !file->node || !file->fs) return -1;

    RamfsInstance *fs = file->fs;
    RamfsNode *node = file->node;
    if (node->capacity == 0) return -1;

    uint64_t needed = file->position + count;
    if (needed > node->capacity) {
        /* Grow: allocate a new block, copy, free the old one.
         * (The free list makes this reusable instead of a leak.) */
        uint64_t new_capacity = node->capacity * 2;
        if (new_capacity < needed) new_capacity = needed;
        int64_t new_offset = ramfs_alloc_data(fs, new_capacity);
        if (new_offset < 0) {
            /* Fall back: try to satisfy the exact request */
            new_capacity = needed;
            new_offset = ramfs_alloc_data(fs, new_capacity);
            if (new_offset < 0) {
                return -1;
            }
        }
        memcpy(fs->data_pool + (uint64_t)new_offset,
               fs->data_pool + node->data_offset, node->size);
        uint64_t old_offset = node->data_offset;
        node->data_offset = (uint64_t)new_offset;
        node->capacity = new_capacity;
        ramfs_free_data(fs, old_offset);
    }

    memcpy(fs->data_pool + node->data_offset + file->position, buf, count);
    file->position += count;
    if (file->position > node->size) node->size = file->position;
    fs->dirty = true;
    return count;
}

static int64_t ramfs_seek(VfsFile *vfile, int64_t offset, int whence)
{
    RamfsFile *file = (RamfsFile *)vfile;
    if (!file || !file->node) return -1;

    int64_t new_pos;
    switch (whence) {
        case VFS_SEEK_SET: new_pos = offset; break;
        case VFS_SEEK_CUR: new_pos = (int64_t)file->position + offset; break;
        case VFS_SEEK_END: new_pos = (int64_t)file->node->size + offset; break;
        default: return -1;
    }

    if (new_pos < 0 || new_pos > (int64_t)file->node->size) return -1;
    file->position = (uint64_t)new_pos;
    return new_pos;
}

static int64_t ramfs_tell(VfsFile *vfile)
{
    RamfsFile *file = (RamfsFile *)vfile;
    if (!file) return -1;
    return (int64_t)file->position;
}

static int ramfs_stat(void *fs_data, const char *path, VfsStat *stat)
{
    RamfsInstance *fs = (RamfsInstance *)fs_data;
    if (!fs || !stat) return -1;
    int idx = find_node(fs, path);
    if (idx < 0) return -1;
    RamfsNode *node = &fs->nodes[idx];

    stat->type = node->type;
    stat->size = node->size;
    stat->permissions = node->permissions;
    stat->uid = 1000;
    stat->created = 0;
    stat->modified = 0;
    stat->inode = idx;
    return 0;
}

static VfsDir *ramfs_opendir(void *fs_data, const char *path)
{
    RamfsInstance *fs = (RamfsInstance *)fs_data;
    if (!fs) return NULL;
    int idx = find_node(fs, path);
    if (idx < 0) return NULL;
    if (fs->nodes[idx].type != VFS_TYPE_DIR) return NULL;

    RamfsDir *dir = alloc_dir();
    if (!dir) return NULL;
    dir->fs = fs;
    dir->parent = (uint32_t)idx;
    dir->current = 0;
    return (VfsDir *)dir;
}

static void ramfs_closedir(VfsDir *vdir)
{
    free_dir((RamfsDir *)vdir);
}

static int ramfs_readdir(VfsDir *vdir, VfsDirEntry *entry)
{
    RamfsDir *dir = (RamfsDir *)vdir;
    if (!dir || !entry || !dir->fs) return -1;

    RamfsInstance *fs = dir->fs;

    while (dir->current < (uint32_t)fs->node_count) {
        RamfsNode *node = &fs->nodes[dir->current++];
        if (node->type != VFS_TYPE_UNKNOWN && node->parent == dir->parent) {
            strncpy(entry->name, node->name, VFS_NAME_MAX);
            entry->name[VFS_NAME_MAX] = '\0';
            entry->type = node->type;
            entry->size = node->size;
            entry->inode = dir->current - 1;
            return 0;
        }
    }
    return -1;
}

static int ramfs_rewinddir(VfsDir *vdir)
{
    RamfsDir *dir = (RamfsDir *)vdir;
    if (!dir) return -1;
    dir->current = 0;
    return 0;
}

static int ramfs_exists(void *fs_data, const char *path)
{
    RamfsInstance *fs = (RamfsInstance *)fs_data;
    if (!fs) return 0;
    return find_node(fs, path) >= 0;
}

static int ramfs_isdir(void *fs_data, const char *path)
{
    RamfsInstance *fs = (RamfsInstance *)fs_data;
    if (!fs) return 0;
    int idx = find_node(fs, path);
    if (idx < 0) return 0;
    return fs->nodes[idx].type == VFS_TYPE_DIR;
}

static int ramfs_isfile(void *fs_data, const char *path)
{
    RamfsInstance *fs = (RamfsInstance *)fs_data;
    if (!fs) return 0;
    int idx = find_node(fs, path);
    if (idx < 0) return 0;
    return fs->nodes[idx].type == VFS_TYPE_FILE;
}

static int ramfs_mkdir(void *fs_data, const char *path)
{
    RamfsInstance *fs = (RamfsInstance *)fs_data;
    if (!fs || !path || path[0] == '\0') return -1;
    if (find_node(fs, path) >= 0) return -1;
    /* create_node returns the node index; VFS contract is 0/-1 */
    int idx = create_node(fs, path, VFS_TYPE_DIR);
    if (idx >= 0) fs->dirty = true;
    return idx >= 0 ? 0 : -1;
}

static int ramfs_unlink(void *fs_data, const char *path)
{
    RamfsInstance *fs = (RamfsInstance *)fs_data;
    if (!fs) return -1;
    int idx = find_node(fs, path);
    if (idx <= 0) return -1;  /* 0 is the root; not unlikable */
    RamfsNode *node = &fs->nodes[idx];

    if (node->type == VFS_TYPE_DIR) {
        for (int i = 0; i < fs->node_count; i++) {
            if (fs->nodes[i].type != VFS_TYPE_UNKNOWN &&
                fs->nodes[i].parent == (uint32_t)idx) {
                return -1;  /* Directory not empty */
            }
        }
    }

    /* Release the data block back to the pool */
    if (node->capacity > 0 && node->data_offset > 0) {
        ramfs_free_data(fs, node->data_offset);
    }

    node->type = VFS_TYPE_UNKNOWN;
    node->name[0] = '\0';
    node->size = 0;
    node->capacity = 0;
    node->data_offset = 0;
    fs->dirty = true;
    return 0;
}

/*
 * Recursively check whether `candidate` is `node` itself or one of its
 * descendants - used to reject renames that would create a cycle.
 */
static bool is_descendant_of(RamfsInstance *fs, uint32_t candidate, uint32_t node)
{
    uint32_t cur = candidate;
    while (cur != 0xFFFFFFFF) {
        if (cur == node) return true;
        if (cur >= (uint32_t)fs->node_count) return false;
        cur = fs->nodes[cur].parent;
    }
    return false;
}

static int ramfs_rename(void *fs_data, const char *from, const char *to)
{
    RamfsInstance *fs = (RamfsInstance *)fs_data;
    if (!fs) return -1;

    int idx = find_node(fs, from);
    if (idx <= 0) return -1;  /* Not found, or is the root */

    char to_dir[256];
    if (vfs_dirname(to, to_dir, sizeof(to_dir)) != 0) return -1;
    const char *to_base = vfs_basename(to);
    if (!valid_component(to_base)) return -1;

    int parent = find_node(fs, to_dir[0] ? to_dir : "/");
    if (parent < 0) return -1;
    if (fs->nodes[parent].type != VFS_TYPE_DIR) return -1;

    /* No-op rename */
    if (parent == (int)fs->nodes[idx].parent &&
        strcmp(fs->nodes[idx].name, to_base) == 0) {
        return 0;
    }

    /* Reject overwriting an existing destination */
    if (find_child(fs, (uint32_t)parent, to_base) >= 0) return -1;

    /* Reject moving a directory into itself or one of its descendants */
    if (fs->nodes[idx].type == VFS_TYPE_DIR &&
        is_descendant_of(fs, (uint32_t)parent, (uint32_t)idx)) {
        return -1;
    }

    RamfsNode *node = &fs->nodes[idx];
    node->parent = (uint32_t)parent;
    strncpy(node->name, to_base, VFS_NAME_MAX);
    node->name[VFS_NAME_MAX] = '\0';
    fs->dirty = true;
    return 0;
}

static VfsOps ramfs_ops = {
    .name = "ramfs",
    .open = ramfs_open,
    .close = ramfs_close,
    .read = ramfs_read,
    .write = ramfs_write,
    .seek = ramfs_seek,
    .tell = ramfs_tell,
    .stat = ramfs_stat,
    .opendir = ramfs_opendir,
    .closedir = ramfs_closedir,
    .readdir = ramfs_readdir,
    .rewinddir = ramfs_rewinddir,
    .exists = ramfs_exists,
    .isdir = ramfs_isdir,
    .isfile = ramfs_isfile,
    .mkdir = ramfs_mkdir,
    .unlink = ramfs_unlink,
    .rename = ramfs_rename,
};

/*
 * Create a new independent RAMFS instance.
 */
void *ramfs_create(void)
{
    if (ramfs_instance_count >= RAMFS_MAX_INSTANCES) {
        serial_printf("[RAMFS] Too many instances\n");
        return NULL;
    }

    RamfsInstance *fs = &ramfs_instances[ramfs_instance_count++];
    memset(fs, 0, sizeof(*fs));

    /* Seed the data pool as one big free block */
    RamfsBlock *init = (RamfsBlock *)fs->data_pool;
    init->size = RAMFS_DATA_SIZE - RAMFS_BLOCK_HDR;
    init->free = 1;

    /* Create the root node */
    RamfsNode *root = alloc_node(fs);
    if (!root) return NULL;
    strcpy(root->name, "");
    root->parent = 0xFFFFFFFF;
    root->type = VFS_TYPE_DIR;
    root->permissions = VFS_PERM_READ | VFS_PERM_WRITE;

    serial_printf("[RAMFS] Instance %d initialized (%d nodes, %d KB pool)\n",
        ramfs_instance_count - 1, RAMFS_MAX_NODES, RAMFS_DATA_SIZE / 1024);
    return fs;
}

VfsOps *ramfs_get_ops(void)
{
    return &ramfs_ops;
}

/* Size of one instance (used by the persistence layer to snapshot the
 * whole filesystem state to disk) */
size_t ramfs_instance_size(void)
{
    return sizeof(RamfsInstance);
}

bool ramfs_is_dirty(void *fs_data)
{
    RamfsInstance *fs = (RamfsInstance *)fs_data;
    return fs ? fs->dirty : false;
}

void ramfs_clear_dirty(void *fs_data)
{
    RamfsInstance *fs = (RamfsInstance *)fs_data;
    if (fs) fs->dirty = false;
}

void ramfs_mark_dirty(void *fs_data)
{
    RamfsInstance *fs = (RamfsInstance *)fs_data;
    if (fs) fs->dirty = true;
}

/* Check untrusted disk bytes before replacing a live filesystem. This also
 * makes the legacy (unchecksummed) checkpoint migration fail safely. */
bool ramfs_validate_snapshot(const void *snapshot, size_t size)
{
    if (!snapshot || size != sizeof(RamfsInstance)) return false;
    const RamfsInstance *fs = snapshot;
    if (fs->node_count < 1 || fs->node_count > RAMFS_MAX_NODES ||
        fs->nodes[0].type != VFS_TYPE_DIR || fs->nodes[0].parent != 0xFFFFFFFF ||
        fs->nodes[0].name[0] != '\0') return false;

    uint64_t off = 0;
    while (off < RAMFS_DATA_SIZE) {
        if (RAMFS_DATA_SIZE - off < RAMFS_BLOCK_HDR) return false;
        const RamfsBlock *block = (const RamfsBlock *)(fs->data_pool + off);
        if (block->free > 1 || block->size == 0 ||
            block->size > RAMFS_DATA_SIZE - off - RAMFS_BLOCK_HDR) return false;
        off += RAMFS_BLOCK_HDR + block->size;
        if (off % 4 != 0) return false;
    }

    for (int i = 0; i < fs->node_count; i++) {
        const RamfsNode *node = &fs->nodes[i];
        if (node->type == VFS_TYPE_UNKNOWN) continue;
        if (node->type != VFS_TYPE_FILE && node->type != VFS_TYPE_DIR &&
            node->type != VFS_TYPE_BUNDLE) return false;
        unsigned len = 0;
        while (len <= VFS_NAME_MAX && node->name[len]) {
            if (node->name[len] == '/') return false;
            len++;
        }
        if (len > VFS_NAME_MAX || (i && !valid_component(node->name))) return false;
        if (i) {
            uint32_t parent = node->parent;
            for (int depth = 0; ; depth++) {
                if (parent >= (uint32_t)fs->node_count || depth >= fs->node_count ||
                    (fs->nodes[parent].type != VFS_TYPE_DIR &&
                     fs->nodes[parent].type != VFS_TYPE_BUNDLE)) return false;
                if (parent == 0) break;
                parent = fs->nodes[parent].parent;
            }
            for (int j = 1; j < i; j++) {
                if (fs->nodes[j].type != VFS_TYPE_UNKNOWN &&
                    fs->nodes[j].parent == node->parent &&
                    strcmp(fs->nodes[j].name, node->name) == 0) return false;
            }
        }
        if (node->type != VFS_TYPE_FILE) {
            if (node->size || node->capacity || node->data_offset) return false;
            continue;
        }
        if (node->size > node->capacity) return false;
        if (!node->capacity) {
            if (node->data_offset) return false;
            continue;
        }
        bool found = false;
        for (off = 0; off < RAMFS_DATA_SIZE; ) {
            const RamfsBlock *block = (const RamfsBlock *)(fs->data_pool + off);
            if (off + RAMFS_BLOCK_HDR == node->data_offset && !block->free &&
                node->capacity <= block->size) found = true;
            off += RAMFS_BLOCK_HDR + block->size;
        }
        if (!found) return false;
        for (int j = 1; j < i; j++) {
            if (fs->nodes[j].type == VFS_TYPE_FILE && fs->nodes[j].capacity &&
                fs->nodes[j].data_offset == node->data_offset) return false;
        }
    }
    return true;
}

int ramfs_create_dir(void *fs_data, const char *path)
{
    return ramfs_mkdir(fs_data, path);
}

int ramfs_create_file(void *fs_data, const char *path)
{
    RamfsInstance *fs = (RamfsInstance *)fs_data;
    if (!fs || !path) return -1;
    if (find_node(fs, path) >= 0) return -1;
    if (create_node(fs, path, VFS_TYPE_FILE) < 0) return -1;
    fs->dirty = true;
    return 0;
}
