/*
 * mkojfs - Create OJFS filesystem images
 *
 * Usage: mkojfs <output.ojfs> <root_dir>
 *
 * Recursively packs a directory into an OJFS image.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

/* Match kernel definitions */
#define OJFS_MAGIC      0x53464A4F  /* "OJFS" */
#define OJFS_VERSION    1
#define OJFS_TYPE_FILE  1
#define OJFS_TYPE_DIR   2

#define VFS_PERM_READ       (1 << 0)
#define VFS_PERM_WRITE      (1 << 1)
#define VFS_PERM_EXECUTE    (1 << 2)
#define VFS_PERM_SYSTEM     (1 << 7)

#pragma pack(push, 1)
typedef struct {
    uint32_t    magic;
    uint32_t    version;
    uint32_t    entry_count;
    uint32_t    string_offset;
    uint32_t    string_size;
    uint32_t    data_offset;
    uint64_t    total_size;
} OjfsHeader;

typedef struct {
    uint32_t    name_offset;
    uint32_t    parent;
    uint32_t    type;
    uint32_t    permissions;
    uint64_t    data_offset;
    uint64_t    size;
} OjfsEntry;
#pragma pack(pop)

/* Entry list */
#define MAX_ENTRIES     1024
#define MAX_STRINGS     (64 * 1024)
#define MAX_DATA        (4 * 1024 * 1024)

static OjfsEntry entries[MAX_ENTRIES];
static int entry_count = 0;

static char string_table[MAX_STRINGS];
static int string_offset = 0;

static uint8_t data_section[MAX_DATA];
static uint64_t data_offset = 0;

/* Add string to table */
static uint32_t add_string(const char *str)
{
    int len = strlen(str) + 1;
    if (string_offset + len > MAX_STRINGS) {
        fprintf(stderr, "String table full\n");
        exit(1);
    }

    uint32_t offset = string_offset;
    strcpy(string_table + string_offset, str);
    string_offset += len;
    return offset;
}

/* Add file data */
static uint64_t add_data(const char *path, uint64_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open file: %s\n", path);
        *size = 0;
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (data_offset + file_size > MAX_DATA) {
        fprintf(stderr, "Data section full\n");
        fclose(f);
        exit(1);
    }

    uint64_t offset = data_offset;
    fread(data_section + data_offset, 1, file_size, f);
    data_offset += file_size;
    fclose(f);

    *size = file_size;
    return offset;  /* Will be adjusted to absolute offset later */
}

/* Add entry */
static int add_entry(const char *name, uint32_t parent, uint32_t type,
                     uint64_t data_off, uint64_t size)
{
    if (entry_count >= MAX_ENTRIES) {
        fprintf(stderr, "Too many entries\n");
        exit(1);
    }

    int idx = entry_count++;
    entries[idx].name_offset = add_string(name);
    entries[idx].parent = parent;
    entries[idx].type = type;
    entries[idx].permissions = VFS_PERM_READ | VFS_PERM_SYSTEM;
    entries[idx].data_offset = data_off;
    entries[idx].size = size;

    return idx;
}

/* Recursively scan directory */
static void scan_directory(const char *path, uint32_t parent_idx)
{
    DIR *dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "Cannot open directory: %s (%s)\n", path, strerror(errno));
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        /* Build full path */
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) {
            fprintf(stderr, "Cannot stat: %s\n", full_path);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            /* Add directory entry */
            int dir_idx = add_entry(ent->d_name, parent_idx, OJFS_TYPE_DIR, 0, 0);
            printf("  DIR  %s (parent=%d, idx=%d)\n", ent->d_name, parent_idx, dir_idx);

            /* Recurse */
            scan_directory(full_path, dir_idx);

        } else if (S_ISREG(st.st_mode)) {
            /* Add file entry */
            uint64_t size;
            uint64_t data_off = add_data(full_path, &size);
            add_entry(ent->d_name, parent_idx, OJFS_TYPE_FILE, data_off, size);
            printf("  FILE %s (%llu bytes)\n", ent->d_name, (unsigned long long)size);
        }
    }

    closedir(dir);
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <output.ojfs> <root_dir>\n", argv[0]);
        return 1;
    }

    const char *output_path = argv[1];
    const char *root_dir = argv[2];

    printf("Creating OJFS image: %s from %s\n", output_path, root_dir);

    /* Add root directory */
    int root_idx = add_entry("", 0xFFFFFFFF, OJFS_TYPE_DIR, 0, 0);
    printf("Root directory: idx=%d\n", root_idx);

    /* Scan directory tree */
    scan_directory(root_dir, root_idx);

    printf("\nTotal: %d entries, %d bytes strings, %llu bytes data\n",
        entry_count, string_offset, (unsigned long long)data_offset);

    /* Calculate offsets */
    uint32_t header_size = sizeof(OjfsHeader);
    uint32_t entries_size = entry_count * sizeof(OjfsEntry);
    uint32_t strings_start = header_size + entries_size;
    uint32_t data_start = strings_start + string_offset;
    /* Align data to 8 bytes */
    data_start = (data_start + 7) & ~7;

    uint64_t total_size = data_start + data_offset;

    /* Adjust file data offsets to be absolute */
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].type == OJFS_TYPE_FILE) {
            entries[i].data_offset += data_start;
        }
    }

    /* Build header */
    OjfsHeader header = {
        .magic = OJFS_MAGIC,
        .version = OJFS_VERSION,
        .entry_count = entry_count,
        .string_offset = strings_start,
        .string_size = string_offset,
        .data_offset = data_start,
        .total_size = total_size
    };

    /* Write output file */
    FILE *out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "Cannot create output: %s\n", output_path);
        return 1;
    }

    /* Write header */
    fwrite(&header, sizeof(header), 1, out);

    /* Write entries */
    fwrite(entries, sizeof(OjfsEntry), entry_count, out);

    /* Write string table */
    fwrite(string_table, 1, string_offset, out);

    /* Pad to data alignment */
    long pos = ftell(out);
    while (pos < data_start) {
        fputc(0, out);
        pos++;
    }

    /* Write data */
    fwrite(data_section, 1, data_offset, out);

    fclose(out);

    printf("Created %s (%llu bytes)\n", output_path, (unsigned long long)total_size);

    return 0;
}
