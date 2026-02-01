/*
 * ojjyOS v3 Kernel - RAMFS Implementation
 */

#include "ramfs.h"
#include "../string.h"
#include "../serial.h"

#define RAMFS_MAX_NODES    256
#define RAMFS_MAX_FILES    64
#define RAMFS_MAX_DIRS     32
#define RAMFS_DATA_SIZE    (256 * 1024)

typedef struct {
    RamfsNode *node;
    uint64_t position;
} RamfsFile;

typedef struct {
    uint32_t parent;
    uint32_t current;
} RamfsDir;

static RamfsNode nodes[RAMFS_MAX_NODES];
static int node_count = 0;

static uint8_t data_pool[RAMFS_DATA_SIZE];
static uint64_t data_offset = 0;

static RamfsFile file_pool[RAMFS_MAX_FILES];
static bool file_used[RAMFS_MAX_FILES];

static RamfsDir dir_pool[RAMFS_MAX_DIRS];
static bool dir_used[RAMFS_MAX_DIRS];

static RamfsNode *alloc_node(void)
{
    if (node_count >= RAMFS_MAX_NODES) {
        return NULL;
    }
    RamfsNode *node = &nodes[node_count++];
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

static int find_child(uint32_t parent, const char *name)
{
    for (int i = 0; i < node_count; i++) {
        if (nodes[i].type != VFS_TYPE_UNKNOWN && nodes[i].parent == parent && strcmp(nodes[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_node(const char *path)
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
        component[len] = '\0';

        int idx = find_child(parent, component);
        if (idx < 0) return -1;
        parent = (uint32_t)idx;

        p += len;
        if (*p == '/') p++;
    }

    return (int)parent;
}

static int ensure_dir(const char *path)
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
        component[len] = '\0';

        int idx = find_child(parent, component);
        if (idx < 0) {
            RamfsNode *node = alloc_node();
            if (!node) return -1;
            strncpy(node->name, component, VFS_NAME_MAX);
            node->parent = parent;
            node->type = VFS_TYPE_DIR;
            node->permissions = VFS_PERM_READ | VFS_PERM_WRITE;
            idx = (int)(node - nodes);
        }
        parent = (uint32_t)idx;

        p += len;
        if (*p == '/') p++;
    }

    return (int)parent;
}

static int create_node(const char *path, VfsFileType type)
{
    if (!path || path[0] == '\0') return -1;

    char dir[256];
    if (vfs_dirname(path, dir, sizeof(dir)) != 0) return -1;

    const char *base = vfs_basename(path);
    int parent = ensure_dir(dir[0] ? dir : "/");
    if (parent < 0) return -1;

    if (find_child((uint32_t)parent, base) >= 0) return -1;

    RamfsNode *node = alloc_node();
    if (!node) return -1;
    strncpy(node->name, base, VFS_NAME_MAX);
    node->parent = (uint32_t)parent;
    node->type = type;
    node->permissions = VFS_PERM_READ | VFS_PERM_WRITE;

    return (int)(node - nodes);
}

static int64_t ramfs_alloc_data(uint64_t size)
{
    if (data_offset + size > RAMFS_DATA_SIZE) return -1;
    uint64_t offset = data_offset;
    data_offset += size;
    return (int64_t)offset;
}

static VfsFile *ramfs_open(const char *path, uint32_t mode)
{
    int idx = find_node(path);

    if (idx < 0) {
        if (mode & VFS_O_CREATE) {
            idx = create_node(path, VFS_TYPE_FILE);
        }
    }

    if (idx < 0) return NULL;
    RamfsNode *node = &nodes[idx];
    if (node->type != VFS_TYPE_FILE) return NULL;

    if ((mode & VFS_O_TRUNC) != 0) {
        node->size = 0;
    }

    if (node->capacity == 0) {
        int64_t offset = ramfs_alloc_data(4096);
        if (offset < 0) return NULL;
        node->data_offset = (uint64_t)offset;
        node->capacity = 4096;
    }

    RamfsFile *file = alloc_file();
    if (!file) return NULL;
    file->node = node;
    file->position = (mode & VFS_O_APPEND) ? node->size : 0;
    return (VfsFile *)file;
}

static void ramfs_close(VfsFile *file)
{
    free_file((RamfsFile *)file);
}

static ssize_t ramfs_read(VfsFile *vfile, void *buf, size_t count)
{
    RamfsFile *file = (RamfsFile *)vfile;
    if (!file || !file->node) return -1;

    RamfsNode *node = file->node;
    if (file->position >= node->size) return 0;

    uint64_t remaining = node->size - file->position;
    if (count > remaining) count = remaining;

    memcpy(buf, data_pool + node->data_offset + file->position, count);
    file->position += count;
    return count;
}

static ssize_t ramfs_write(VfsFile *vfile, const void *buf, size_t count)
{
    RamfsFile *file = (RamfsFile *)vfile;
    if (!file || !file->node) return -1;

    RamfsNode *node = file->node;
    if (node->capacity == 0) return -1;

    uint64_t needed = file->position + count;
    if (needed > node->capacity) {
        uint64_t new_capacity = node->capacity + 4096;
        if (new_capacity < needed) new_capacity = needed;
        int64_t new_offset = ramfs_alloc_data(new_capacity);
        if (new_offset < 0) {
            return -1;
        }
        memcpy(data_pool + (uint64_t)new_offset, data_pool + node->data_offset, node->size);
        node->data_offset = (uint64_t)new_offset;
        node->capacity = new_capacity;
    }

    memcpy(data_pool + node->data_offset + file->position, buf, count);
    file->position += count;
    if (file->position > node->size) node->size = file->position;
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

static int ramfs_stat(const char *path, VfsStat *stat)
{
    if (!stat) return -1;
    int idx = find_node(path);
    if (idx < 0) return -1;
    RamfsNode *node = &nodes[idx];

    stat->type = node->type;
    stat->size = node->size;
    stat->permissions = node->permissions;
    stat->uid = 1000;
    stat->created = 0;
    stat->modified = 0;
    stat->inode = idx;
    return 0;
}

static VfsDir *ramfs_opendir(const char *path)
{
    int idx = find_node(path);
    if (idx < 0) return NULL;
    if (nodes[idx].type != VFS_TYPE_DIR) return NULL;

    RamfsDir *dir = alloc_dir();
    if (!dir) return NULL;
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
    if (!dir || !entry) return -1;

    while (dir->current < (uint32_t)node_count) {
        RamfsNode *node = &nodes[dir->current++];
        if (node->type != VFS_TYPE_UNKNOWN && node->parent == dir->parent) {
            strncpy(entry->name, node->name, VFS_NAME_MAX);
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

static int ramfs_exists(const char *path)
{
    return find_node(path) >= 0;
}

static int ramfs_isdir(const char *path)
{
    int idx = find_node(path);
    if (idx < 0) return 0;
    return nodes[idx].type == VFS_TYPE_DIR;
}

static int ramfs_isfile(const char *path)
{
    int idx = find_node(path);
    if (idx < 0) return 0;
    return nodes[idx].type == VFS_TYPE_FILE;
}

static int ramfs_mkdir(const char *path)
{
    if (!path || path[0] == '\0') return -1;
    if (find_node(path) >= 0) return -1;
    return create_node(path, VFS_TYPE_DIR);
}

static int ramfs_unlink(const char *path)
{
    int idx = find_node(path);
    if (idx <= 0) return -1;
    RamfsNode *node = &nodes[idx];

    if (node->type == VFS_TYPE_DIR) {
        for (int i = 0; i < node_count; i++) {
            if (nodes[i].type != VFS_TYPE_UNKNOWN && nodes[i].parent == (uint32_t)idx) {
                return -1;
            }
        }
    }

    node->type = VFS_TYPE_UNKNOWN;
    node->name[0] = '\0';
    node->size = 0;
    node->capacity = 0;
    node->data_offset = 0;
    return 0;
}

static int ramfs_rename(const char *from, const char *to)
{
    int idx = find_node(from);
    if (idx < 0) return -1;

    char to_dir[256];
    if (vfs_dirname(to, to_dir, sizeof(to_dir)) != 0) return -1;
    const char *to_base = vfs_basename(to);

    int parent = find_node(to_dir[0] ? to_dir : "/");
    if (parent < 0) return -1;
    if (find_child((uint32_t)parent, to_base) >= 0) return -1;

    RamfsNode *node = &nodes[idx];
    node->parent = (uint32_t)parent;
    strncpy(node->name, to_base, VFS_NAME_MAX);
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

VfsOps *ramfs_init(void)
{
    node_count = 0;
    data_offset = 0;
    memset(nodes, 0, sizeof(nodes));
    memset(file_used, 0, sizeof(file_used));
    memset(dir_used, 0, sizeof(dir_used));

    RamfsNode *root = alloc_node();
    if (!root) return NULL;
    strcpy(root->name, "");
    root->parent = 0xFFFFFFFF;
    root->type = VFS_TYPE_DIR;
    root->permissions = VFS_PERM_READ | VFS_PERM_WRITE;

    serial_printf("[RAMFS] Initialized (%d nodes)\n", RAMFS_MAX_NODES);
    return &ramfs_ops;
}

int ramfs_create_dir(const char *path)
{
    return ramfs_mkdir(path);
}

int ramfs_create_file(const char *path)
{
    if (!path) return -1;
    if (find_node(path) >= 0) return -1;
    return create_node(path, VFS_TYPE_FILE);
}
