/*
 * Host test for the Notes library (kernel/src/ui/notes.c) on a real RAMFS
 * mounted at /Users through the real VFS: note names, titles, previews
 * (with a fuzz loop), autosave timing, the first-run import of
 * Documents/Notes.txt, listing order, selection, new-note names, the
 * NOTES_MAX cap and a folder blocked by a file.
 *
 * ojjyOS - Created by Jonas Lee
 */
#include "ui/notes.h"
#include "fs/ramfs.h"
#include "fs/vfs.h"
#include "test_support.h"

/* VFS access-control hooks; users.c owns the real ones. Unauthenticated
 * sessions are unrestricted, which is what these tests want. */
bool users_is_authenticated(void) { return false; }
bool users_current_is_admin(void) { return false; }
const char *users_current_username(void) { return "guest"; }

static void put(const char *path, const char *text)
{
    VfsFile *f = vfs_open(path, VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
    CHECK(f != NULL, "create test file");
    if (!f) return;
    CHECK(vfs_write(f, text, strlen(text)) == (ssize_t)strlen(text), "write test file");
    vfs_close(f);
}

static bool same(const char *path, const char *text)
{
    char buffer[256] = {0};
    VfsFile *f = vfs_open(path, VFS_O_READ);
    if (!f) return false;
    ssize_t length = vfs_read(f, buffer, sizeof(buffer) - 1);
    vfs_close(f);
    return length == (ssize_t)strlen(text) && strcmp(buffer, text) == 0;
}

static bool names_are(const NotesLibrary *lib, const char *const *names, int count)
{
    if (lib->count != count) return false;
    for (int i = 0; i < count; i++)
        if (strcmp(lib->items[i].name, names[i]) != 0) return false;
    return true;
}

static void test_names(void)
{
    CHECK(notes_is_note_name("a.txt"), "a.txt is a note");
    CHECK(notes_is_note_name("Shopping List.TXT"), "upper-case extension is a note");
    CHECK(!notes_is_note_name(".txt"), "bare extension is not a note");
    CHECK(!notes_is_note_name(".hidden.txt"), "hidden file is not a note");
    CHECK(!notes_is_note_name("readme.md"), "other extension is not a note");
    CHECK(!notes_is_note_name("txt"), "short name is not a note");
    CHECK(!notes_is_note_name(""), "empty name is not a note");
    CHECK(!notes_is_note_name(NULL), "NULL is not a note");
    char longname[NOTES_NAME_MAX + 8];
    memset(longname, 'n', sizeof(longname));
    strcpy(longname + NOTES_NAME_MAX - 4, ".txt");      /* strlen == NOTES_NAME_MAX */
    CHECK(!notes_is_note_name(longname), "name that does not fit is skipped");
    longname[NOTES_NAME_MAX - 5] = '\0';
    strcpy(longname + NOTES_NAME_MAX - 9, ".txt");      /* strlen == NOTES_NAME_MAX - 5 */
    CHECK(notes_is_note_name(longname), "longest fitting name is a note");

    char title[16];
    notes_title("Groceries.txt", title, sizeof(title));
    CHECK(strcmp(title, "Groceries") == 0, "title drops .txt");
    notes_title("Plan.TXT", title, sizeof(title));
    CHECK(strcmp(title, "Plan") == 0, "title drops .TXT");
    notes_title("A very long note title.txt", title, sizeof(title));
    CHECK(strcmp(title, "A very long not") == 0, "title is cut to the buffer");
    notes_title("x.txt", title, 1);
    CHECK(title[0] == '\0', "one-byte buffer gives an empty title");
}

static void test_preview(void)
{
    char out[NOTES_PREVIEW_MAX];
    const char *text = "\n \t \n   Buy   milk\tand eggs  \nsecond line\n";
    notes_preview(text, strlen(text), out, sizeof(out));
    CHECK(strcmp(out, "Buy milk and eggs") == 0, "preview: first non-blank line, trimmed, spaces folded");
    text = "no newline at the end";
    notes_preview(text, strlen(text), out, sizeof(out));
    CHECK(strcmp(out, text) == 0, "preview: last line without newline");
    text = "\r\n\x01\x02\n\xc3\xa9t\xc3\xa9 caf\xc3\xa9\r\n";
    notes_preview(text, strlen(text), out, sizeof(out));
    CHECK(strcmp(out, "t caf") == 0, "preview: control and non-ASCII bytes dropped");
    notes_preview("   \n\t\n", 6, out, sizeof(out));
    CHECK(out[0] == '\0', "preview: blank note gives empty preview");
    notes_preview("", 0, out, sizeof(out));
    CHECK(out[0] == '\0', "preview: empty note");
    notes_preview("abcdef", 6, out, 4);
    CHECK(strcmp(out, "abc") == 0, "preview: cut to buffer");
    notes_preview("abc\ndef", 2, out, sizeof(out));
    CHECK(strcmp(out, "ab") == 0, "preview: honours the length, not the NUL");

    /* Fuzz: any bytes give a terminated, printable, trimmed preview. */
    uint32_t seed = 0x5eed1234u;
    bool ok = true;
    char buf[300];
    for (int round = 0; round < 4000 && ok; round++) {
        size_t len = 0;
        seed = seed * 1103515245u + 12345u;
        len = (seed >> 8) % sizeof(buf);
        for (size_t i = 0; i < len; i++) {
            seed = seed * 1103515245u + 12345u;
            uint8_t b = (uint8_t)(seed >> 16);
            if ((b & 7) == 0) b = '\n';
            else if ((b & 7) == 1) b = ' ';
            buf[i] = (char)b;
        }
        size_t size = 1 + round % NOTES_PREVIEW_MAX;
        memset(out, 0x7f, sizeof(out));
        notes_preview(buf, len, out, size);
        size_t n = 0;
        while (n < size && out[n]) n++;
        if (n >= size) { ok = false; break; }
        for (size_t i = 0; i < n; i++)
            if (out[i] < 32 || out[i] > 126) ok = false;
        if (n > 0 && (out[0] == ' ' || out[n - 1] == ' ')) ok = false;
        for (size_t i = size; i < sizeof(out); i++)
            if ((unsigned char)out[i] != 0x7f) ok = false;      /* nothing past `size` */
    }
    CHECK(ok, "preview fuzz: 4000 random buffers stay bounded, printable and trimmed");
}

static void test_autosave(void)
{
    CHECK(!notes_autosave_due(false, 1000, 9000), "clean note is never due");
    CHECK(!notes_autosave_due(true, 1000, 1000 + NOTES_AUTOSAVE_MS - 1), "dirty note waits for quiet time");
    CHECK(notes_autosave_due(true, 1000, 1000 + NOTES_AUTOSAVE_MS), "dirty note is due after quiet time");
    CHECK(!notes_autosave_due(true, 5000, 4000), "clock behind the edit is not due");
}

static void test_library(void)
{
    CHECK(vfs_mkdir("/Users/jonas") == 0, "mkdir home");
    CHECK(vfs_mkdir("/Users/jonas/Documents") == 0, "mkdir Documents");
    const char *old_notes = "Jonas's Notes\n- Press Cmd+S to save changes\n";
    put("/Users/jonas/Documents/Notes.txt", old_notes);

    NotesLibrary lib;
    CHECK(notes_open(&lib, "/Users/jonas") == 1, "first open imports Documents/Notes.txt");
    CHECK(strcmp(lib.dir, "/Users/jonas/Notes") == 0, "library folder is ~/Notes");
    CHECK(vfs_isdir("/Users/jonas/Notes"), "~/Notes created");
    CHECK(lib.count == 1 && strcmp(lib.items[0].name, "Notes.txt") == 0, "imported note listed");
    CHECK(strcmp(lib.items[0].preview, "Jonas's Notes") == 0, "imported note preview");
    CHECK(same("/Users/jonas/Notes/Notes.txt", old_notes), "import is a byte-exact copy");
    CHECK(same("/Users/jonas/Documents/Notes.txt", old_notes), "original stays in Documents");
    CHECK(lib.selected == -1, "nothing selected after open");

    CHECK(notes_open(&lib, "/Users/jonas") == 0, "second open does not import again");
    CHECK(lib.count == 1, "still one note");
    CHECK(vfs_unlink("/Users/jonas/Notes/Notes.txt") == 0, "delete the imported note");
    CHECK(notes_open(&lib, "/Users/jonas") == 0 && lib.count == 0, "deleted note does not come back");

    char name[NOTES_NAME_MAX];
    CHECK(notes_new_name(&lib, name, sizeof(name)) == 0 && strcmp(name, "New Note.txt") == 0, "first new name");
    int a = notes_create(&lib);
    CHECK(a == 0 && lib.selected == 0 && strcmp(lib.items[0].name, "New Note.txt") == 0, "create New Note.txt");
    CHECK(vfs_isfile("/Users/jonas/Notes/New Note.txt"), "new note file exists");
    int b = notes_create(&lib);
    CHECK(b >= 0 && strcmp(lib.items[b].name, "New Note 2.txt") == 0 && lib.selected == b, "create New Note 2.txt");
    CHECK(lib.items[b].preview[0] == '\0', "new note has an empty preview");
    CHECK(notes_new_name(&lib, name, 5) == -1, "new name refuses a small buffer");

    put("/Users/jonas/Notes/b.txt", "bee\n");
    put("/Users/jonas/Notes/A.txt", "\n\n  ay\n");
    put("/Users/jonas/Notes/c.TXT", "see");
    put("/Users/jonas/Notes/skip.md", "not a note");
    put("/Users/jonas/Notes/.hidden.txt", "hidden");
    CHECK(vfs_mkdir("/Users/jonas/Notes/folder.txt") == 0, "folder named like a note");
    lib.selected = notes_find(&lib, "b.txt");
    CHECK(lib.selected == -1, "unlisted note is not found before a scan");
    CHECK(notes_scan(&lib) == 5, "scan lists the five notes");
    static const char *const order[] = { "A.txt", "b.txt", "c.TXT", "New Note.txt", "New Note 2.txt" };
    CHECK(names_are(&lib, order, 5), "title order, case ignored, shorter title first");
    CHECK(strcmp(lib.items[0].preview, "ay") == 0 && strcmp(lib.items[2].preview, "see") == 0, "previews read");

    lib.selected = notes_find(&lib, "B.TXT");
    CHECK(lib.selected == 1, "find ignores case");
    put("/Users/jonas/Notes/a0.txt", "x");
    CHECK(notes_scan(&lib) == 6 && lib.selected == 2 && strcmp(lib.items[2].name, "b.txt") == 0,
          "selection follows its note across a scan");
    CHECK(vfs_unlink("/Users/jonas/Notes/b.txt") == 0, "delete the selected note");
    CHECK(notes_scan(&lib) == 5 && lib.selected == -1, "selection cleared when its note is gone");

    put("/Users/jonas/Notes/A.txt", "rewritten first line\n");
    notes_refresh(&lib, notes_find(&lib, "A.txt"));
    CHECK(strcmp(lib.items[notes_find(&lib, "A.txt")].preview, "rewritten first line") == 0, "refresh one preview");

    char path[160];
    CHECK(strcmp(lib.items[0].name, "A.txt") == 0 && strcmp(lib.items[1].name, "a0.txt") == 0,
          "title 'A' sorts before 'a0'");
    CHECK(notes_path(&lib, 1, path, sizeof(path)) == 0 && strcmp(path, "/Users/jonas/Notes/a0.txt") == 0, "note path");
    CHECK(notes_path(&lib, -1, path, sizeof(path)) == -1 && notes_path(&lib, lib.count, path, sizeof(path)) == -1,
          "note path rejects bad indexes");
    CHECK(notes_path(&lib, 0, path, 8) == -1, "note path rejects a small buffer");

    /* Fill up: creation stops at NOTES_MAX, extra files only set `more`. */
    int created = 0;
    while (notes_create(&lib) >= 0 && created < 2 * NOTES_MAX) created++;
    CHECK(lib.count == NOTES_MAX && created == NOTES_MAX - 5, "creation stops at NOTES_MAX");
    CHECK(!lib.more, "exactly full is not 'more'");
    put("/Users/jonas/Notes/zz extra.txt", "extra");
    CHECK(notes_scan(&lib) == NOTES_MAX && lib.more, "notes past NOTES_MAX are counted as more");
    CHECK(notes_create(&lib) == -1, "full library refuses a new note");

    /* A file called Notes blocks the folder. */
    CHECK(vfs_mkdir("/Users/sam") == 0, "mkdir second home");
    put("/Users/sam/Notes", "a file in the way");
    CHECK(notes_open(&lib, "/Users/sam") == -1 && lib.count == 0, "file named Notes gives no library");
    CHECK(notes_scan(&lib) == -1 && notes_create(&lib) == -1, "unusable library refuses scan and create");

    /* A home without Documents/Notes.txt just gets an empty folder. */
    CHECK(vfs_mkdir("/Users/kim") == 0, "mkdir third home");
    CHECK(notes_open(&lib, "/Users/kim") == 0 && lib.count == 0 && vfs_isdir("/Users/kim/Notes"),
          "new home gets an empty Notes folder");
}

int main(void)
{
    vfs_init();
    void *users_fs = ramfs_create();
    CHECK(users_fs != NULL, "ramfs instance");
    CHECK(vfs_mount("/Users", ramfs_get_ops(), users_fs, false) == 0, "mount /Users");
    test_names();
    test_preview();
    test_autosave();
    test_library();
    return finish();
}
