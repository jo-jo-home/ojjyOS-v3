/*
 * Host-side unit test for the ojjyOS kernel RAMFS.
 *
 * Verifies:
 *  - write / grow / read-back / seek / append
 *  - truncate semantics
 *  - unlink frees memory (the data pool is reusable, not a one-way bump)
 *  - rename including cycle rejection
 *  - two instances are fully independent (the /Users vs /Library fix)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "fs/ramfs.h"
#include "fs/vfs.h"
#include "string.h"

extern long write(int fd, const void *buf, unsigned long count);

static int tests_run = 0;
static int tests_failed = 0;

static void emit(const char *s)
{
    unsigned long len = 0;
    while (s[len]) len++;
    write(1, s, len);
    write(1, "\n", 1);
}

static void emit_num(const char *label, long v)
{
    char num[24];
    write(1, label, strlen(label));
    if (v < 0) { write(1, "-", 1); v = -v; }
    utoa((uint64_t)v, num, 10);
    write(1, num, strlen(num));
    write(1, "\n", 1);
}

/* Kernel stubs */
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

int main(void)
{
    VfsOps *ops = ramfs_get_ops();
    CHECK(ops != NULL, "ramfs_get_ops");

    void *fsa = ramfs_create();
    void *fsb = ramfs_create();
    CHECK(fsa != NULL && fsb != NULL, "two instances created");

    /* mkdir + create + write */
    CHECK(ops->mkdir(fsa, "/Users") == 0, "mkdir /Users");
    CHECK(ops->mkdir(fsa, "/Users/guest") == 0, "mkdir /Users/guest");
    CHECK(ops->mkdir(fsa, "/Users") != 0, "duplicate mkdir rejected");
    CHECK(ops->mkdir(fsa, "/Users/guest/..") != 0, "mkdir .. rejected");

    VfsFile *f = ops->open(fsa, "/Users/guest/a.txt", VFS_O_CREATE | VFS_O_WRITE);
    CHECK(f != NULL, "create file");

    if (f) {
        /* Write 100 KB in 4 KB chunks - old bump allocator died at 256KB pool */
        char chunk[4096];
        memset(chunk, 'A', sizeof(chunk));
        long written = 0;
        int fail = 0;
        for (int i = 0; i < 25; i++) {
            ssize_t n = ops->write(f, chunk, sizeof(chunk));
            if (n != (ssize_t)sizeof(chunk)) { fail = 1; break; }
            written += (long)n;
        }
        CHECK(!fail && written == 25 * 4096, "write 100KB in 4KB chunks (grow + reuse)");

        /* Read back a slice */
        CHECK(ops->seek(f, 100000, VFS_SEEK_SET) == 100000, "seek to 100000");
        char back[16];
        ssize_t n = ops->read(f, back, 8);
        CHECK(n == 8, "read 8 bytes at offset");
        int all_a = 1;
        for (int i = 0; i < 8; i++) if (back[i] != 'A') all_a = 0;
        CHECK(all_a, "read-back data matches");
        ops->close(f);
    }

    /* File exists with the right size */
    VfsStat st;
    CHECK(ops->stat(fsa, "/Users/guest/a.txt", &st) == 0 && st.size == 25 * 4096, "stat size 102400");

    /* unlink frees the pool: write 100KB again in a new file (old code
     * could not: growth leaked, 256KB pool would be exhausted) */
    CHECK(ops->unlink(fsa, "/Users/guest/a.txt") == 0, "unlink file");
    VfsFile *g = ops->open(fsa, "/Users/guest/b.txt", VFS_O_CREATE | VFS_O_WRITE);
    CHECK(g != NULL, "create second file after unlink");
    if (g) {
        char chunk[4096];
        memset(chunk, 'B', sizeof(chunk));
        int fail = 0;
        for (int i = 0; i < 24; i++) {
            if (ops->write(g, chunk, sizeof(chunk)) != (ssize_t)sizeof(chunk)) { fail = 1; break; }
        }
        CHECK(!fail, "write ~96KB again after unlink (pool reclaimed)");
        ops->close(g);
    }

    /* unlink of non-empty dir rejected; empty dir works */
    CHECK(ops->mkdir(fsa, "/tmp") == 0, "mkdir /tmp");
    CHECK(ops->unlink(fsa, "/Users/guest") != 0, "unlink non-empty dir rejected");
    CHECK(ops->unlink(fsa, "/tmp") == 0, "unlink empty dir");
    CHECK(ops->unlink(fsa, "/") != 0, "unlink root rejected");

    /* rename */
    CHECK(ops->rename(fsa, "/Users/guest/b.txt", "/Users/guest/c.txt") == 0, "rename file");
    CHECK(ops->isfile(fsa, "/Users/guest/c.txt") != 0, "renamed file exists");
    CHECK(ops->isfile(fsa, "/Users/guest/b.txt") == 0, "old name gone");

    /* rename dir into own descendant must be rejected (cycle) */
    CHECK(ops->mkdir(fsa, "/dir") == 0, "mkdir /dir");
    CHECK(ops->mkdir(fsa, "/dir/sub") == 0, "mkdir /dir/sub");
    CHECK(ops->rename(fsa, "/dir", "/dir/sub/x") != 0, "rename into own descendant rejected");
    CHECK(ops->rename(fsa, "/", "/root") != 0, "rename root rejected");

    /* overwrite protection */
    CHECK(ops->rename(fsa, "/Users/guest/c.txt", "/dir/sub") != 0, "rename over existing rejected");

    /* --- instance independence --- */
    CHECK(ops->mkdir(fsb, "/Preferences") == 0, "mkdir in instance B");
    CHECK(ops->isdir(fsb, "/Preferences") != 0, "B sees its own dir");
    CHECK(ops->isdir(fsa, "/Preferences") == 0, "A does NOT see B's dir (no aliasing)");
    CHECK(ops->isdir(fsb, "/Users") == 0, "B does NOT see A's dir");

    /* O_TRUNC without write does not destroy data */
    f = ops->open(fsa, "/Users/guest/c.txt", VFS_O_READ | VFS_O_TRUNC);
    CHECK(f != NULL, "open READ|TRUNC succeeds");
    if (f) {
        ops->close(f);
        CHECK(ops->stat(fsa, "/Users/guest/c.txt", &st) == 0 && st.size == 24 * 4096,
              "O_TRUNC without write does not truncate");
    }

    emit_num("tests run: ", tests_run);
    if (tests_failed == 0) {
        emit("ALL RAMFS TESTS PASSED");
        return 0;
    }
    emit_num("TESTS FAILED: ", tests_failed);
    return 1;
}
