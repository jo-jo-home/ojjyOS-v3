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
#include "memory.h"
#include "proc.h"
#include "drivers/rtc.h"
#include "string.h"

char _kernel_start[16];
char _kernel_end[16];

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
    time->year = 2026; time->month = 10; time->day = 18;
    time->hour = 12; time->minute = 34; time->second = 56; time->weekday = 7;
}
/* users.c draws salts from the kernel CSPRNG; deterministic for tests. */
void random_bytes(void *out, size_t len)
{
    static uint8_t b = 1;
    uint8_t *p = (uint8_t *)out;
    for (size_t i = 0; i < len; i++) { p[i] = (uint8_t)(0x5A ^ b) + (uint8_t)i; b++; }
}
void system_request_sync(void) {}

#define CHECK(cond, name) do { \
    tests_run++; \
    if (!(cond)) { tests_failed++; emit("FAIL: " name); } \
    else { emit("ok: " name); } \
} while (0)

static bool has_char(const char *s, char c) { for (; *s; s++) if (*s == c) return true; return false; }

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
    CHECK(users_login("jonas", "tahoe2026") == 0, "admin logs in before further account creation");
    CHECK(users_create("jonas", "Duplicate", "pass", false) != 0, "duplicate username rejected");
    CHECK(users_create("bad user!", "Bad", "pass", false) != 0, "invalid username chars rejected");
    CHECK(users_create("..", "Dots", "pass", false) != 0, "'..' username rejected (home would be /)");
    CHECK(users_create(".", "Dot", "pass", false) != 0, "'.' username rejected");
    CHECK(users_create(".hidden", "Hidden", "pass", false) != 0, "leading-dot username rejected");
    CHECK(users_create("abcdefghijklmnopqrstuvwxyz0123456789", "Long", "pass", false) != 0,
          "over-long username rejected instead of truncated");
    CHECK(users_create("nl", "Evil\nroot|x|y", "pass", false) == 0 &&
          users_find("nl") && !has_char(users_find("nl")->full_name, '\n') &&
          !has_char(users_find("nl")->full_name, '|'), "full name cannot inject users.db records");
    CHECK(users_delete("nl") == 0, "cleanup injected-name account");

    /* Verify home directory provisioning */
    CHECK(vfs_isdir("/Users/jonas") != 0, "provisioned /Users/jonas");
    CHECK(vfs_isdir("/Users/jonas/Desktop") != 0, "provisioned /Users/jonas/Desktop");
    CHECK(vfs_isdir("/Users/jonas/Documents") != 0, "provisioned /Users/jonas/Documents");
    CHECK(vfs_isfile("/Users/jonas/Desktop/Welcome.txt") != 0, "seeded Welcome.txt in user Desktop");
    CHECK(vfs_isfile("/Library/Accounts/users.db") != 0, "persisted /Library/Accounts/users.db");

    /* Verify salted PBKDF2 password authentication */
    CHECK(!users_verify_credentials("jonas", "wrongpass"), "wrong password rejected");
    CHECK(users_verify_credentials("jonas", "tahoe2026"), "correct password verified via PBKDF2");
    CHECK(strncmp(users_find("jonas")->pass_hash, "pbkdf2$10000$", 13) == 0, "hash stored in PBKDF2 format");
    CHECK(strlen(users_find("jonas")->salt_hex) == 16, "salt is 16 hex chars from the CSPRNG");
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
    CHECK(users_verify_credentials("jonas", "newsecret"), "reloaded account preserves PBKDF2 hash");
    CHECK(users_login("jonas", "newsecret") == 0, "re-authenticate jonas after reload");

    /* --- Security pass (BETTER_OS_PLAN.md S1-S5) --- */
    CHECK(users_create("visitor", "Visitor", "visitorpw", false) == 0,
          "admin session can create a non-admin account");
    CHECK(users_login("visitor", "visitorpw") == 0, "log in as the non-admin visitor");
    CHECK(users_create("sneaky", "Sneaky", "pw12345", true) == -4,
          "non-admin cannot create accounts (S1)");
    CHECK(users_delete("jonas") != 0, "non-admin cannot delete accounts (S1)");
    CHECK(vfs_open("/Users/jonas/Desktop/Welcome.txt", VFS_O_READ) == NULL,
          "non-admin cannot read another user's files (S4)");
    CHECK(vfs_isdir("/Users/jonas") == 0, "non-admin cannot stat another user's home (S4)");
    CHECK(vfs_exists("/Users/jonas/Documents") == 0,
          "non-admin cannot probe another user's tree (S4)");
    CHECK(vfs_open("/Library/Accounts/users.db", VFS_O_READ) == NULL,
          "non-admin cannot read the credential database (S5)");
    VfsFile *own = vfs_open("/Users/visitor/Desktop/Welcome.txt", VFS_O_READ);
    CHECK(own != NULL, "own home stays fully readable (S4)");
    if (own) vfs_close(own);
    CHECK(users_login("jonas", "newsecret") == 0, "log back in as the administrator");
    VfsFile *other = vfs_open("/Users/visitor/Desktop/Welcome.txt", VFS_O_READ);
    CHECK(other != NULL, "admin can read any user's home");
    if (other) vfs_close(other);
    VfsFile *dbf = vfs_open("/Library/Accounts/users.db", VFS_O_READ);
    CHECK(dbf != NULL, "admin can read the credential database");
    if (dbf) vfs_close(dbf);
    CHECK(users_delete("jonas") != 0, "admin cannot delete their own logged-in account");
    CHECK(users_delete("visitor") == 0, "admin can delete another account");

    /* Legacy single-SHA-256 accounts (pre-PBKDF2 disks) still log in and
     * are transparently upgraded (S3 migration). Vector:
     * sha256("ojjyOS-v3-auth:deadbeef:legacy-pass"). */
    VfsFile *db = vfs_open("/Library/Accounts/users.db", VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
    CHECK(db != NULL, "open users.db to plant a legacy-format record");
    const char *legacy_line =
        "legacy|Legacy User|deadbeef|c044c85b8233c1cf215d3da891b47e61fd4dc2c18e3f7b97bed5c95e943e6697|1|/Users/legacy\n";
    vfs_write(db, legacy_line, strlen(legacy_line));
    vfs_close(db);
    users_init();
    CHECK(users_count() == 1, "legacy-format account loads from users.db");
    CHECK(!users_is_authenticated(), "reload clears the session");
    CHECK(users_verify_credentials("legacy", "legacy-pass"), "legacy fast hash still verifies");
    CHECK(!users_verify_credentials("legacy", "wrong"), "legacy account rejects wrong password");
    CHECK(users_login("legacy", "legacy-pass") == 0, "legacy account logs in");
    CHECK(strncmp(users_find("legacy")->pass_hash, "pbkdf2$", 7) == 0,
          "login transparently upgrades the hash to PBKDF2");
    CHECK(users_verify_credentials("legacy", "legacy-pass"), "upgraded credentials still verify");
    CHECK(users_current_is_admin(), "legacy vector account is an admin");

    /* --- Kernel Heap Allocator (kmalloc/kcalloc/krealloc/kfree + coalescing) --- */
    kheap_init();
    CHECK(kheap_validate(), "fresh kheap is valid");
    CHECK(kheap_get_used() == 0, "fresh kheap has 0 bytes used");

    void *b1 = kmalloc(128);
    void *b2 = kcalloc(16, 16);
    void *b3 = kmalloc(512);
    CHECK(b1 != NULL && b2 != NULL && b3 != NULL, "kmalloc and kcalloc succeed");
    CHECK(((uintptr_t)b1 & 0xF) == 0 && ((uintptr_t)b2 & 0xF) == 0, "kmalloc returns 16-byte aligned pointers");
    CHECK(kheap_get_alloc_count() == 3, "kheap tracks 3 active allocations");
    CHECK(kheap_validate(), "kheap valid with 3 blocks");

    memset(b1, 0xAB, 128);
    void *b1_grown = krealloc(b1, 300);
    CHECK(b1_grown != NULL && ((uint8_t *)b1_grown)[0] == 0xAB && ((uint8_t *)b1_grown)[127] == 0xAB,
          "krealloc preserves existing payload");

    kfree(b2);
    kfree(b1_grown);
    kfree(b3);
    CHECK(kheap_get_used() == 0 && kheap_get_alloc_count() == 0, "kfree releases all blocks");
    CHECK(kheap_validate(), "kheap coalesces back to valid state");

    /* --- Kernel Process Table & Syscall Dispatcher --- */
    proc_init();
    CHECK(proc_count() == 4, "proc_init spawns 4 core system daemons");
    int calc_pid = proc_ensure_app(10 /* APP_CALCULATOR */, "Calculator", 256 * 1024);
    CHECK(calc_pid >= 5, "proc_ensure_app spawns Calculator process");
    CHECK(proc_count() == 5, "proc_count is 5 after app spawn");
    const ProcEntry *cpe = proc_get_by_pid((uint32_t)calc_pid);
    CHECK(cpe != NULL && strcmp(cpe->owner, users_current_username()) == 0,
          "spawned app process is owned by the active user");

    proc_tick(50, 10);
    CHECK(cpe->cpu_ticks > 1 && cpe->state == PROC_RUNNING, "proc_tick accounts CPU ticks to focused app");

    CHECK(proc_kill(1, NULL) == -2, "proc_kill refuses to kill protected kernel_task (PID 1)");
    int killed_app = -1;
    CHECK(proc_kill((uint32_t)calc_pid, &killed_app) == 0 && killed_app == 10, "proc_kill terminates user app process");
    CHECK(proc_count() == 4, "proc_count returns to 4 after kill");

    void *sys_mem = (void *)(uintptr_t)syscall_dispatch(SYS_MALLOC, 64, 0, 0, 0);
    CHECK(sys_mem != NULL, "syscall SYS_MALLOC allocates memory");
    CHECK(syscall_dispatch(SYS_FREE, (uint64_t)(uintptr_t)sys_mem, 0, 0, 0) == 0, "syscall SYS_FREE frees memory");
    CHECK(syscall_dispatch(SYS_GETUID, 0, 0, 0, 0) == 501, "syscall SYS_GETUID returns active user UID");
    CHECK(proc_get_total_syscalls() >= 3, "syscall counter increments on dispatch");

    emit_num("tests run: ", tests_run);
    if (tests_failed == 0) {
        emit("ALL RAMFS, USERS, KHEAP & PROC TESTS PASSED");
        return 0;
    }
    emit_num("TESTS FAILED: ", tests_failed);
    return 1;
}
