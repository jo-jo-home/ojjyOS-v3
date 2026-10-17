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
#include "users.h"
#include "drivers/rtc.h"
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
uint64_t timer_get_ticks(void) { return 424242; }
void rtc_read_time(RtcTime *time)
{
    if (!time) return;
    time->year = 2026;
    time->month = 10;
    time->day = 17;
    time->hour = 12;
    time->minute = 30;
    time->second = 0;
    time->weekday = 6;
}
void system_request_sync(void) {}

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

    /* --- User Accounts & Salted SHA-256 Credentials Tests --- */
    vfs_init();
    void *u_fs = ramfs_create();
    void *l_fs = ramfs_create();
    CHECK(vfs_mount("/Users", ops, u_fs, false) == 0, "mount /Users for accounts test");
    CHECK(vfs_mount("/Library", ops, l_fs, false) == 0, "mount /Library for accounts test");

    users_init();
    CHECK(users_count() == 0, "fresh boot has 0 accounts (triggers Setup Assistant)");
    CHECK(!users_is_authenticated(), "fresh boot starts unauthenticated");

    CHECK(users_create("jonas", "Jonas Lee", "tahoe2026", true) == 0, "create admin account jonas");
    CHECK(users_count() == 1, "users_count is 1 after create");
    CHECK(users_create("jonas", "Duplicate", "pass", false) != 0, "duplicate username rejected");
    CHECK(users_create("bad user!", "Bad", "pass", false) != 0, "invalid username chars rejected");

    /* Verify home directory provisioning */
    CHECK(vfs_isdir("/Users/jonas") != 0, "provisioned /Users/jonas");
    CHECK(vfs_isdir("/Users/jonas/Desktop") != 0, "provisioned /Users/jonas/Desktop");
    CHECK(vfs_isdir("/Users/jonas/Documents") != 0, "provisioned /Users/jonas/Documents");
    CHECK(vfs_isfile("/Users/jonas/Desktop/Welcome.txt") != 0, "seeded Welcome.txt in user Desktop");
    CHECK(vfs_isfile("/Library/Accounts/users.db") != 0, "persisted /Library/Accounts/users.db");

    /* Verify salted SHA-256 password authentication */
    CHECK(!users_verify_credentials("jonas", "wrongpass"), "wrong password rejected");
    CHECK(users_verify_credentials("jonas", "tahoe2026"), "correct password verified via SHA-256");
    CHECK(users_login("jonas", "wrongpass") != 0, "users_login rejects wrong password");
    CHECK(users_login("jonas", "tahoe2026") == 0, "users_login succeeds with valid password");
    CHECK(users_is_authenticated(), "session is authenticated after login");
    CHECK(strcmp(users_current_username(), "jonas") == 0, "active username is jonas");
    CHECK(strcmp(users_current_home(), "/Users/jonas") == 0, "active home is /Users/jonas");

    /* Verify password change */
    CHECK(users_change_password("jonas", "wrongold", "newsecret") != 0, "password change rejects bad old password");
    CHECK(users_change_password("jonas", "tahoe2026", "newsecret") == 0, "password change succeeds");
    CHECK(!users_verify_credentials("jonas", "tahoe2026"), "old password no longer valid");
    CHECK(users_verify_credentials("jonas", "newsecret"), "new password valid");

    /* Verify reload from /Library/Accounts/users.db */
    users_init();
    CHECK(users_count() == 1, "users_init reloads account from users.db");
    CHECK(users_verify_credentials("jonas", "newsecret"), "reloaded account preserves salted SHA-256 hash");

    emit_num("tests run: ", tests_run);
    if (tests_failed == 0) {
        emit("ALL RAMFS & USERS TESTS PASSED");
        return 0;
    }
    emit_num("TESTS FAILED: ", tests_failed);
    return 1;
}
