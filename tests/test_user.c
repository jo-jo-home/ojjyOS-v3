/* Loader, cleanup and actual syscall-policy tests. Created by Jonas Lee. */
#include "user.h"
#include "memory.h"
#include "proc.h"
#include "fs/vfs.h"
#include "string.h"
#include "test_support.h"

static uint8_t elf[8192];
static uint8_t pages[USER_MAX_PAGES][PAGE_SIZE] __attribute__((aligned(4096)));
static bool used[USER_MAX_PAGES], authenticated = true, supported = true, prepare_ok = true;
static unsigned live, attempts, fail_at = ~0U, mode;
static uint64_t now;
static unsigned spawned, killed;
static struct { unsigned offset; } handle;
typedef struct {
    uint8_t ident[16]; uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff; uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} PACKED Header;
typedef struct { uint32_t type, flags; uint64_t offset, address, physical, filesz, memsz, align; } PACKED Segment;
static Header *h = (Header *)elf;
static Segment *p = (Segment *)(elf + 64);

static void fixture(void)
{
    memset(elf, 0, sizeof(elf)); memcpy(h->ident, "\177ELF\2\1\1", 7);
    h->type = 2; h->machine = 183; h->version = 1; h->entry = USER_IMAGE_LOW;
    h->phoff = 64; h->ehsize = 64; h->phentsize = 56; h->phnum = 1;
    *p = (Segment){1, USER_R | USER_X, 4096, USER_IMAGE_LOW, USER_IMAGE_LOW, 16, 8192, 4096};
    memcpy(elf + 4096, "sample\n", 7);
}
uint64_t pmm_alloc_page(void)
{
    if (attempts++ == fail_at) return 0;
    for (unsigned i = 0; i < USER_MAX_PAGES; i++) if (!used[i]) {
        used[i] = true; live++; memset(pages[i], 0xAA, PAGE_SIZE);
        return 0x100000000ULL+i*PAGE_SIZE;
    }
    return 0;
}
uint64_t pmm_get_free_memory(void) { return (USER_MAX_PAGES - live) * PAGE_SIZE; }
uint64_t pmm_alloc_page_zone(PmmZone zone) { (void)zone; return pmm_alloc_page(); }
void *pmm_phys_to_virt(uint64_t pa, size_t bytes)
{
    uint64_t start=0x100000000ULL;
    return pa>=start && pa-start<sizeof(pages) && bytes<=sizeof(pages)-(pa-start)
        ? (uint8_t *)pages+(pa-start) : NULL;
}
void pmm_free_page(uint64_t addr)
{
    for (unsigned i = 0; i < USER_MAX_PAGES; i++) if (0x100000000ULL+i*PAGE_SIZE == addr) {
        CHECK(used[i], "free only allocated user page");
        bool zero = true;
        for (unsigned j = 0; j < PAGE_SIZE; j++) if (pages[i][j]) zero = false;
        CHECK(zero, "private pages wiped on free");
        used[i] = false; live--; return;
    }
    CHECK(false, "free belongs to pool");
}
bool users_is_authenticated(void) { return authenticated; }
int users_current_index(void) { return 0; }
const char *users_current_username(void) { return "tester"; }
int proc_spawn(const char *n, uint32_t parent, uint32_t uid, const char *owner, int app, uint64_t bytes, bool system)
{ (void)n; (void)parent; (void)uid; (void)owner; (void)app; (void)bytes; (void)system; spawned++; return 42; }
int proc_kill(uint32_t pid, int *app) { (void)pid; (void)app; killed++; return 0; }
int vfs_stat(const char *path, VfsStat *stat)
{ (void)path; memset(stat, 0, sizeof(*stat)); stat->type = VFS_TYPE_FILE; stat->size = sizeof(elf); return 0; }
VfsFile *vfs_open(const char *path, uint32_t flags)
{ (void)path; (void)flags; handle.offset = 0; return (VfsFile *)&handle; }
ssize_t vfs_read(VfsFile *f, void *dest, size_t count)
{
    (void)f; count = MIN(count, 137); count = MIN(count, sizeof(elf) - handle.offset);
    memcpy(dest, elf + handle.offset, count); handle.offset += count; return count;
}
void vfs_close(VfsFile *f) { (void)f; }
void user_arch_init(void) {}
bool user_arch_supported(void) { return supported; }
bool user_arch_prepare(const UserSpace *space) { (void)space; return prepare_ok; }
uint64_t user_arch_millis(void) { return now; }
void user_arch_run(uint64_t entry, uint64_t stack)
{
    CHECK(entry == USER_IMAGE_LOW && stack == USER_STACK_TOP, "validated entry and guarded stack");
    CHECK(user_syscall(USER_SYS_GETPID, 0, 0, 0) == 42, "PID syscall");
    CHECK(user_syscall(USER_SYS_GETUID, 0, 0, 0) == 501, "UID syscall");
    CHECK(user_syscall(9, 0x41000000, 0, 0) == -38, "kernel filesystem syscall not exposed");
    CHECK(user_syscall(USER_SYS_WRITE, 1, 0x41000000, 8) == -14, "kernel pointer rejected");
    CHECK(user_syscall(USER_SYS_WRITE, 1, ~0ULL - 2, 8) == -14, "overflow pointer rejected");
    CHECK(user_syscall(USER_SYS_WRITE, 1, USER_STACK_TOP - 1, 2) == -14, "cross-page hole rejected");
    CHECK(user_syscall(USER_SYS_WRITE, 3, USER_IMAGE_LOW, 1) == -9, "descriptor checked");
    CHECK(user_syscall(USER_SYS_WRITE, 1, USER_IMAGE_LOW, 513) == -22, "write budget checked");
    CHECK(user_syscall(USER_SYS_WRITE, 1, USER_IMAGE_LOW, 7) == 7, "checked output copied");
    if (mode == 1) { user_fault(entry, 0x41000000, 0x9200000f); return; }
    if (mode == 2) { now += USER_TIME_MS; user_timer_tick(); return; }
    if (mode == 3) {
        for (unsigned i = 0; i < 300 && user_is_running(); i++) user_syscall(1, 0, 0, 0);
        return;
    }
    CHECK(user_syscall(USER_SYS_EXIT, 0, 0, 0) == 0 && !user_is_running(), "exit stops execution");
}

int main(void)
{
    fixture(); UserImage image; UserSpace space = {0};
    CHECK(user_image_validate(elf, sizeof(elf), 183, &image), "valid static ELF accepted");
    CHECK(image.pages == 2, "BSS pages accounted");
    image.segments[0].offset++;
    CHECK(!user_space_load(&space, elf, sizeof(elf), &image) && !live, "modified loader plan rejected before allocation");
    image.segments[0].offset--;
    CHECK(user_space_load(&space, elf, sizeof(elf), &image), "private pages loaded");
    char copy[8];
    CHECK(user_copy_from(&space, copy, USER_IMAGE_LOW, 7) && !memcmp(copy, "sample\n", 7), "segment data copied");
    CHECK(user_copy_from(&space, copy, USER_IMAGE_LOW + 4095, 8) && copy[0] == 0 && copy[7] == 0, "cross-page BSS zero");
    CHECK(!user_range_valid(&space, USER_IMAGE_LOW, 1, USER_W), "code not writable");
    CHECK(!user_range_valid(&space, USER_STACK_TOP - 8, 8, USER_X), "stack not executable");
    CHECK(!user_range_valid(&space, USER_STACK_TOP - 5 * PAGE_SIZE, 1, USER_R), "stack guard unmapped");
    CHECK(user_range_valid(&space, ~0ULL, 0, USER_R), "zero-length copy ignores pointer");
    memset(copy, 0x55, sizeof(copy));
    CHECK(!user_copy_from(&space, copy, USER_STACK_TOP - 1, 8) && copy[0] == 0x55,
          "bad full range rejected before partial copy");
    user_space_destroy(&space); CHECK(live == 0, "all pages reclaimed");
    for (unsigned fail = 0; fail < 6; fail++) {
        attempts = 0; fail_at = fail;
        CHECK(!user_space_load(&space, elf, sizeof(elf), &image) && !live && !space.count, "allocation failure rolls back");
    }
    fail_at = ~0U;
#define BAD(change, message) do { fixture(); change; CHECK(!user_image_validate(elf, sizeof(elf), 183, &image), message); } while (0)
    BAD(h->ident[0] = 0, "magic rejected");
    BAD(h->ident[4] = 1, "ELF32 rejected");
    BAD(h->ident[5] = 2, "big endian rejected");
    BAD(h->machine = 62, "wrong machine rejected");
    BAD(h->type = 3, "PIE rejected");
    BAD(h->flags = 1, "unsupported ELF flags rejected");
    BAD(h->ehsize = 63, "header size rejected");
    BAD(h->phentsize = 55, "program header size rejected");
    BAD(h->phnum = 9, "header count bounded");
    BAD(h->phoff = ~0ULL, "header overflow rejected");
    BAD(h->entry++, "unaligned entry rejected");
    BAD(h->entry += 32, "entry in BSS rejected");
    BAD(p->type = 2, "dynamic rejected");
    BAD(p->type = 3, "interpreter rejected");
    BAD(p->type = 7, "TLS rejected");
    BAD(p->flags = 7, "W+X rejected");
    BAD(p->flags = 1, "non-readable code rejected");
    BAD(p->offset = ~0ULL, "file offset overflow rejected");
    BAD(p->filesz = ~0ULL, "file length overflow rejected");
    BAD(p->memsz = ~0ULL, "memory overflow rejected");
    BAD(p->memsz = PAGE_SIZE * 61, "page budget enforced");
    BAD(p->address = 0x41000000, "kernel segment rejected");
    BAD(p->align = 3, "alignment power checked");
    BAD(p->align = 8192, "offset congruence checked");
    BAD(h->phnum = 2; p[1] = *p, "overlapping pages rejected");
    fixture(); CHECK(!user_image_validate(elf, 63, 183, &image), "truncated ELF rejected");
    CHECK(!user_image_validate(elf, USER_MAX_FILE + 1, 183, &image), "oversize ELF rejected before read");
    CHECK(!user_image_validate(NULL, 8192, 183, &image) &&
          !user_image_validate(elf, 8192, 183, NULL), "null validation parameters rejected");
    /* Deterministic mutation corpus: validation must never read beyond the
     * provided file, even with arbitrary header/segment integers. ASan/UBSan
     * instruments the actual validator and these varied size limits. */
    uint32_t seed = 1234567;
    for (unsigned trial = 0; trial < 4000; trial++) {
        fixture();
        for (unsigned j = 0; j < 12; j++) {
            seed = seed * 1664525U + 1013904223U;
            unsigned offset = seed % 176;
            elf[offset] ^= (uint8_t)(seed >> 24);
        }
        user_image_validate(elf, trial % 2 ? sizeof(elf) : trial % 176, 183, &image);
    }
    CHECK(true, "4000 malformed header mutations completed safely");
    fixture(); UserResult result;
    supported = false; CHECK(user_run("hello", &result) == -1, "Intel unsupported explicit"); supported = true;
    authenticated = false; CHECK(user_run("hello", &result) == -2, "unauthenticated launch denied"); authenticated = true;
    CHECK(user_run("../escape", &result) == -3 && user_run("", &result) == -3, "name traversal rejected");
    prepare_ok = false; CHECK(user_run("hello", &result) == -4 && !live && spawned == killed, "backend failure cleans pages and PID"); prepare_ok = true;
    for (mode = 0; mode < 4; mode++) {
        CHECK(user_run("hello", &result) == 0 && !live && spawned == killed, "completed run reclaimed");
        CHECK(!strcmp(result.output, "sample\n"), "output captured exactly");
        UserStatus expected[] = {USER_EXITED, USER_FAULT, USER_TIMEOUT, USER_LIMIT};
        CHECK(result.status == expected[mode], "exit/fault/watchdog/syscall limit reported");
    }
    return finish();
}
