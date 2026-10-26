/*
 * ojjyOS v3 - Notes library (see notes.h)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "ui/notes.h"
#include "fs/vfs.h"
#include "string.h"

#define NOTES_PATH_MAX      (NOTES_DIR_MAX + NOTES_NAME_MAX)
#define NOTES_PREVIEW_READ  256     /* bytes read per note for its preview */

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool has_txt(const char *name, size_t len)
{
    if (len < 4) return false;
    const char *ext = name + len - 4;
    return ext[0] == '.' && lower(ext[1]) == 't' && lower(ext[2]) == 'x' && lower(ext[3]) == 't';
}

bool notes_is_note_name(const char *name)
{
    if (!name || name[0] == '.') return false;
    size_t len = strlen(name);
    return len >= 5 && len < NOTES_NAME_MAX && has_txt(name, len);
}

void notes_title(const char *name, char *out, size_t size)
{
    if (!out || size == 0) return;
    out[0] = '\0';
    if (!name) return;
    size_t len = strlen(name);
    if (has_txt(name, len)) len -= 4;
    if (len > size - 1) len = size - 1;
    memcpy(out, name, len);
    out[len] = '\0';
}

void notes_preview(const char *text, size_t len, char *out, size_t size)
{
    if (!out || size == 0) return;
    out[0] = '\0';
    if (!text) return;
    size_t start = 0;
    while (start < len) {
        size_t end = start;
        while (end < len && text[end] != '\n') end++;
        size_t n = 0;
        for (size_t i = start; i < end && n + 1 < size; i++) {
            unsigned char c = (unsigned char)text[i];
            if (c == '\t') c = ' ';
            if (c < 32 || c > 126) continue;
            /* No leading spaces, and runs of spaces read as one. */
            if (c == ' ' && (n == 0 || out[n - 1] == ' ')) continue;
            out[n++] = (char)c;
        }
        while (n > 0 && out[n - 1] == ' ') n--;
        out[n] = '\0';
        if (n > 0) return;
        start = end + 1;
    }
}

bool notes_autosave_due(bool dirty, uint64_t last_edit_ms, uint64_t now_ms)
{
    return dirty && now_ms >= last_edit_ms && now_ms - last_edit_ms >= NOTES_AUTOSAVE_MS;
}

/* Title order: "New Note" before "New Note 2", case ignored; full names
 * break ties so the order is stable. Both names are valid note names. */
static int compare_titles(const char *a, const char *b)
{
    size_t la = strlen(a) - 4, lb = strlen(b) - 4;
    size_t n = la < lb ? la : lb;
    for (size_t i = 0; i < n; i++) {
        char ca = lower(a[i]), cb = lower(b[i]);
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
    }
    if (la != lb) return la < lb ? -1 : 1;
    return strcmp(a, b);
}

static bool equal_ci(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

int notes_find(const NotesLibrary *lib, const char *name)
{
    if (!lib || !name) return -1;
    for (int i = 0; i < lib->count; i++)
        if (strcmp(lib->items[i].name, name) == 0) return i;
    for (int i = 0; i < lib->count; i++)
        if (equal_ci(lib->items[i].name, name)) return i;
    return -1;
}

int notes_path(const NotesLibrary *lib, int index, char *out, size_t size)
{
    if (!lib || !out || size == 0 || index < 0 || index >= lib->count) return -1;
    return vfs_join_path(out, size, lib->dir, lib->items[index].name);
}

void notes_refresh(NotesLibrary *lib, int index)
{
    char path[NOTES_PATH_MAX];
    if (notes_path(lib, index, path, sizeof(path)) != 0) return;
    char buf[NOTES_PREVIEW_READ];
    size_t got = 0;
    VfsFile *file = vfs_open(path, VFS_O_READ);
    if (file) {
        ssize_t n;
        while (got < sizeof(buf) && (n = vfs_read(file, buf + got, sizeof(buf) - got)) > 0)
            got += (size_t)n;
        vfs_close(file);
    }
    notes_preview(buf, got, lib->items[index].preview, sizeof(lib->items[index].preview));
}

int notes_scan(NotesLibrary *lib)
{
    if (!lib || !lib->dir[0]) return -1;
    char keep[NOTES_NAME_MAX];
    keep[0] = '\0';
    if (lib->selected >= 0 && lib->selected < lib->count) {
        strncpy(keep, lib->items[lib->selected].name, sizeof(keep) - 1);
        keep[sizeof(keep) - 1] = '\0';
    }
    lib->count = 0;
    lib->more = false;
    lib->selected = -1;

    VfsDir *dir = vfs_opendir(lib->dir);
    if (!dir) return -1;
    VfsDirEntry entry;
    while (vfs_readdir(dir, &entry) == 0) {
        if (entry.type != VFS_TYPE_FILE || !notes_is_note_name(entry.name)) continue;
        if (lib->count >= NOTES_MAX) {
            lib->more = true;
            continue;
        }
        NoteEntry *note = &lib->items[lib->count++];
        strncpy(note->name, entry.name, sizeof(note->name) - 1);
        note->name[sizeof(note->name) - 1] = '\0';
        note->preview[0] = '\0';
    }
    vfs_closedir(dir);

    for (int i = 1; i < lib->count; i++) {      /* insertion sort, <= 48 notes */
        NoteEntry moving = lib->items[i];
        int j = i - 1;
        while (j >= 0 && compare_titles(lib->items[j].name, moving.name) > 0) {
            lib->items[j + 1] = lib->items[j];
            j--;
        }
        lib->items[j + 1] = moving;
    }
    for (int i = 0; i < lib->count; i++) notes_refresh(lib, i);
    if (keep[0]) lib->selected = notes_find(lib, keep);
    return lib->count;
}

/* Copies a whole file; a partial copy (data pool full) is removed again so
 * a cut-short note never appears. */
static bool copy_file(const char *from, const char *to)
{
    VfsFile *src = vfs_open(from, VFS_O_READ);
    if (!src) return false;
    VfsFile *dst = vfs_open(to, VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
    if (!dst) {
        vfs_close(src);
        return false;
    }
    char buf[256];
    bool ok = true;
    ssize_t n;
    while ((n = vfs_read(src, buf, sizeof(buf))) > 0) {
        if (vfs_write(dst, buf, (size_t)n) != n) {
            ok = false;
            break;
        }
    }
    if (n < 0) ok = false;
    vfs_close(dst);
    vfs_close(src);
    if (!ok) vfs_unlink(to);
    return ok;
}

int notes_open(NotesLibrary *lib, const char *home)
{
    if (!lib || !home) return -1;
    memset(lib, 0, sizeof(*lib));
    lib->selected = -1;
    if (vfs_join_path(lib->dir, sizeof(lib->dir), home, "Notes") != 0) return -1;

    int imported = 0;
    if (!vfs_isdir(lib->dir)) {
        if (vfs_exists(lib->dir) || vfs_mkdir(lib->dir) != 0) {
            lib->dir[0] = '\0';
            return -1;
        }
        /* First run: bring the old single-file notes along (copied, so
         * Documents/Notes.txt stays where it was). Only when the folder
         * is new, so deleting every note later does not bring it back. */
        char old_path[NOTES_PATH_MAX], new_path[NOTES_PATH_MAX];
        if (vfs_join_path(old_path, sizeof(old_path), home, "Documents/Notes.txt") == 0 &&
            vfs_isfile(old_path) &&
            vfs_join_path(new_path, sizeof(new_path), lib->dir, "Notes.txt") == 0 &&
            copy_file(old_path, new_path)) {
            imported = 1;
        }
    }
    return notes_scan(lib) < 0 ? -1 : imported;
}

static void append(char *out, size_t size, const char *text)
{
    size_t len = strlen(out);
    while (*text && len + 1 < size) out[len++] = *text++;
    out[len] = '\0';
}

int notes_new_name(const NotesLibrary *lib, char *out, size_t size)
{
    if (!lib || !out || size == 0 || !lib->dir[0]) return -1;
    for (int n = 1; n < 1000; n++) {
        char name[NOTES_NAME_MAX] = "New Note";
        if (n > 1) {
            char num[8];
            utoa((uint64_t)n, num, 10);
            append(name, sizeof(name), " ");
            append(name, sizeof(name), num);
        }
        append(name, sizeof(name), ".txt");
        if (notes_find(lib, name) >= 0) continue;
        char path[NOTES_PATH_MAX];
        if (vfs_join_path(path, sizeof(path), lib->dir, name) != 0) return -1;
        if (vfs_exists(path)) continue;
        if (strlen(name) + 1 > size) return -1;
        strcpy(out, name);
        return 0;
    }
    return -1;
}

int notes_create(NotesLibrary *lib)
{
    if (!lib || !lib->dir[0] || lib->count >= NOTES_MAX) return -1;
    char name[NOTES_NAME_MAX], path[NOTES_PATH_MAX];
    if (notes_new_name(lib, name, sizeof(name)) != 0 ||
        vfs_join_path(path, sizeof(path), lib->dir, name) != 0) {
        return -1;
    }
    VfsFile *file = vfs_open(path, VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
    if (!file) return -1;
    vfs_close(file);
    lib->selected = -1;
    if (notes_scan(lib) < 0) return -1;
    lib->selected = notes_find(lib, name);
    return lib->selected;
}
