/*
 * Host-side unit test for the ojjyOS kernel OJFS reader.
 *
 * Compiles the REAL kernel sources (fs/ojfs.c, string.c) and runs them
 * against the real image produced by tools/mkojfs, verifying the
 * root-lookup fix (find_entry must start traversal at the root entry).
 *
 * No host headers are included in this TU: the kernel's types.h defines
 * its own fixed-width types, which would collide with <stdint.h>.
 * libc entry points are declared manually instead.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "fs/ojfs.h"
#include "fs/vfs.h"
#include "string.h"

/* Minimal libc declarations (no headers - avoid typedef collisions) */
extern int open(const char *path, int flags, ...);
extern long read(int fd, void *buf, unsigned long count);
extern int close(int fd);
extern long write(int fd, const void *buf, unsigned long count);

#define O_RDONLY 0

/* Test results */
static int tests_run = 0;
static int tests_failed = 0;

static void emit(const char *s)
{
    unsigned long len = 0;
    while (s[len]) len++;
    write(1, s, len);
    write(1, "\n", 1);
}

static void emit_str(const char *label, const char *value)
{
    unsigned long len = 0;
    write(1, label, 0);
    (void)label;
    while (label[len]) len++;
    write(1, label, len);
    len = 0;
    while (value[len]) len++;
    write(1, value, len);
    write(1, "\n", 1);
}

/* Kernel stubs: serial/console output goes nowhere on the host */
void serial_printf(const char *fmt, ...) { (void)fmt; }
void serial_puts(const char *s) { (void)s; }
void serial_putc(char c) { (void)c; }
void console_printf(const char *fmt, ...) { (void)fmt; }
void console_puts(const char *s) { (void)s; }
void console_putc(char c) { (void)c; }

#define CHECK(cond, name) do { \
    tests_run++; \
    if (!(cond)) { tests_failed++; emit("FAIL: " name); } \
    else { emit("ok: " name); } \
} while (0)

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    const char *image_path = "build/system.ojfs";

    int fd = open(image_path, O_RDONLY);
    if (fd < 0) {
        emit("FATAL: cannot open build/system.ojfs (run scripts/mkfs.sh first)");
        return 2;
    }

    /* Read the whole image */
    static unsigned char image[16 * 1024 * 1024];
    long total = 0;
    for (;;) {
        long n = read(fd, image + total, sizeof(image) - (unsigned long)total);
        if (n <= 0) break;
        total += n;
        if ((unsigned long)total >= sizeof(image)) break;
    }
    close(fd);

    if (total <= 0) {
        emit("FATAL: image read failed");
        return 2;
    }

    /* --- Validate --- */
    CHECK(ojfs_validate(image, (unsigned long)total), "ojfs_validate accepts the mkojfs image");

    OjfsInstance *inst = NULL;
    VfsOps *ops = ojfs_init(image, (unsigned long)total, &inst);
    CHECK(ops != NULL, "ojfs_init returns ops");
    CHECK(inst != NULL, "ojfs_init returns instance");
    if (!ops || !inst) return 2;

    /* --- The critical regression: paths BELOW the root must resolve --- */
    CHECK(ops->isdir(inst, "/") != 0, "isdir /");
    CHECK(ops->isdir(inst, "/Applications") != 0, "isdir /Applications (root-child lookup)");
    CHECK(ops->isdir(inst, "/System") != 0, "isdir /System");
    CHECK(ops->isdir(inst, "/System/Wallpapers") != 0, "isdir /System/Wallpapers (nested)");
    CHECK(ops->isdir(inst, "/Applications/About.app") != 0, "isdir About.app bundle");
    CHECK(ops->isfile(inst, "/System/version.txt") != 0, "isfile /System/version.txt");
    CHECK(ops->isfile(inst, "/Applications/About.app/manifest.json") != 0, "isfile deep manifest.json");

    /* Bundle typing */
    VfsStat st;
    CHECK(ops->stat(inst, "/Applications/About.app", &st) == 0 &&
          st.type == VFS_TYPE_BUNDLE, "stat types .app as BUNDLE");
    CHECK(ops->stat(inst, "/System/version.txt", &st) == 0 && st.size > 0, "stat file has size");

    /* Reading a file's content */
    VfsFile *f = ops->open(inst, "/System/version.txt", VFS_O_READ);
    CHECK(f != NULL, "open /System/version.txt");
    if (f) {
        char buf[256];
        memset(buf, 0, sizeof(buf));
        ssize_t n = ops->read(f, buf, sizeof(buf) - 1);
        CHECK(n > 0, "read version.txt returns data");
        CHECK(n > 10 && n < 200, "version.txt size plausible");
        emit_str("     version.txt first bytes: ", buf);
        ops->close(f);
    }

    /* Deep file read: About.app resources */
    VfsFile *about = ops->open(inst, "/Applications/About.app/resources/about.txt", VFS_O_READ);
    CHECK(about != NULL, "open About.app/resources/about.txt (deep path)");
    if (about) ops->close(about);

    /* Web app pages & new apps */
    CHECK(ops->isfile(inst, "/System/Web/home.html") != 0, "isfile /System/Web/home.html");
    CHECK(ops->isfile(inst, "/System/Web/accounts.html") != 0, "isfile /System/Web/accounts.html");
    CHECK(ops->isfile(inst, "/Applications/Web.app/manifest.json") != 0, "isfile Web.app manifest");
    CHECK(ops->isfile(inst, "/Applications/Calculator.app/manifest.json") != 0, "isfile Calculator.app manifest");
    CHECK(ops->isfile(inst, "/Applications/Monitor.app/manifest.json") != 0, "isfile Monitor.app manifest");
    VfsFile *home = ops->open(inst, "/System/Web/home.html", VFS_O_READ);
    CHECK(home != NULL, "open /System/Web/home.html");
    if (home) {
        char buf[256];
        memset(buf, 0, sizeof(buf));
        ssize_t n = ops->read(home, buf, 40);
        CHECK(n == 40 && strncmp(buf, "<html><head><title>Start Page</title>", 37) == 0,
              "home.html start page content");
        ops->close(home);
    }

    /* Directory listing of /Applications */
    VfsDir *dir = ops->opendir(inst, "/Applications");
    CHECK(dir != NULL, "opendir /Applications");
    if (dir) {
        int found_about = 0, found_finder = 0, found_calc = 0, found_mon = 0, count = 0;
        VfsDirEntry entry;
        while (ops->readdir(dir, &entry) == 0) {
            count++;
            if (strcmp(entry.name, "About.app") == 0) found_about = 1;
            if (strcmp(entry.name, "Finder.app") == 0) found_finder = 1;
            if (strcmp(entry.name, "Calculator.app") == 0) found_calc = 1;
            if (strcmp(entry.name, "Monitor.app") == 0) found_mon = 1;
        }
        ops->closedir(dir);
        CHECK(count >= 11, "opendir /Applications lists >= 11 bundles");
        CHECK(found_about, "listing includes About.app");
        CHECK(found_finder, "listing includes Finder.app");
        CHECK(found_calc, "listing includes Calculator.app");
        CHECK(found_mon, "listing includes Monitor.app");
    }

    /* Non-existent paths must fail (no false positives) */
    CHECK(ops->isfile(inst, "/Applications/About.app/nope.txt") == 0, "missing file returns 0");
    CHECK(ops->isdir(inst, "/Nope") == 0, "missing dir returns 0");

    /* --- strncpy regression (n+1 overflow fix) --- */
    {
        char buf[8];
        memset(buf, 'X', sizeof(buf));
        strncpy(buf, "ab", 4);          /* must write EXACTLY 4 bytes */
        CHECK(buf[0] == 'a' && buf[1] == 'b' && buf[2] == '\0' && buf[3] == '\0',
              "strncpy pads to exactly n");
        CHECK(buf[4] == 'X', "strncpy does not write past n");
        strncpy(buf, "abcdefghij", 4);   /* truncation: no NUL within n */
        CHECK(buf[0] == 'a' && buf[3] == 'd', "strncpy truncates at n");
        buf[7] = '\0';
    }

    /* --- path normalization --- */
    {
        char norm[256];
        CHECK(vfs_normalize_path("/Applications//About.app/../Finder.app", norm, sizeof(norm)) == 0 &&
              strcmp(norm, "/Applications/Finder.app") == 0, "normalize resolves . and ..");
        CHECK(vfs_normalize_path("/a/b/../../../c", norm, sizeof(norm)) == 0 &&
              strcmp(norm, "/c") == 0, "normalize clamps .. at root");
        CHECK(vfs_normalize_path("///", norm, sizeof(norm)) == 0 &&
              strcmp(norm, "/") == 0, "normalize collapses slashes to /");
    }

    /* Report */
    char numbuf[16];
    utoa((uint64_t)tests_run, numbuf, 10);
    emit_str("tests run: ", numbuf);
    if (tests_failed == 0) {
        emit("ALL OJFS/STRING TESTS PASSED");
        return 0;
    }
    utoa((uint64_t)tests_failed, numbuf, 10);
    emit_str("TESTS FAILED: ", numbuf);
    return 1;
}
