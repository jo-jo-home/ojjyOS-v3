/* Owned EL1/EL0 mappings for the supported QEMU virt machine. Jonas Lee. */
#include "user_arch.h"
#include "arch64.h"
#include "serial.h"
#include "string.h"
#include "panic.h"
#include "memory.h"

#define TABLE 3ULL
#define AF (1ULL << 10)
#define SH_INNER (3ULL << 8)
#define NORMAL (1ULL << 2)
#define PXN (1ULL << 53)
#define UXN (1ULL << 54)
typedef uint64_t Table[512];
static Table kernel_l0 __attribute__((aligned(4096)));
static Table kernel_l1 __attribute__((aligned(4096)));
static Table app_l0 __attribute__((aligned(4096)));
static Table app_l1 __attribute__((aligned(4096)));
static Table app_l2 __attribute__((aligned(4096)));
static Table app_l3 __attribute__((aligned(4096)));
static uint8_t exception_stack[16384] __attribute__((aligned(16)));
static bool ready;
extern void a64_user_enter(uint64_t entry, uint64_t stack, uint64_t kernel_stack);

static uint64_t descriptor(const void *table) { return (uint64_t)(uintptr_t)table | TABLE; }

static void switch_root(uint64_t root)
{
    __asm__ volatile("dsb ishst; msr ttbr0_el1, %0; isb; tlbi vmalle1; dsb ish; isb"
                     : : "r"(root) : "memory");
}

void user_arch_init(void)
{
    kernel_l0[0] = descriptor(kernel_l1);
    /* EL1-only identity map: device MMIO/ROM below 1GiB, RAM up to 4GiB.
     * User code never receives these privileged aliases. */
    kernel_l1[0] = 1 | AF | PXN | UXN;
    for (unsigned i = 1; i < 4; i++) kernel_l1[i] = ((uint64_t)i << 30) | 1 | AF | SH_INNER | NORMAL | UXN;
    uint64_t old, cpacr;
    __asm__ volatile("mrs %0, sctlr_el1; mrs %1, cpacr_el1" : "=r"(old), "=r"(cpacr));
    uint64_t off = old & ~1ULL;
    uint64_t mair = 0xFF00; /* index 0 Device-nGnRnE, index 1 normal WB/WA */
    /* T0SZ=16 (48-bit VA), 4 KiB granule, WB/WA walks, inner shareable,
     * IPS=0 (32-bit physical), EPD1=1. User VAs above 4GiB are not PAs. */
    uint64_t tcr = 16 | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) |
                   (16ULL << 16) | (1ULL << 23);
    /* Preserve architectural RES1 bits. Little-endian, no EL0 cache/DAIF
     * control or WFI/WFE escape, no strict unaligned-data trapping. */
    uint64_t on = old | 1 | (1ULL << 2) | (1ULL << 12) | (1ULL << 3) | (1ULL << 4);
    on &= ~((1ULL << 1) | (1ULL << 9) | (1ULL << 16) | (1ULL << 18) |
            (1ULL << 19) | (1ULL << 24) | (1ULL << 25) | (1ULL << 26));
    cpacr = (cpacr & ~(3ULL << 20)) | (1ULL << 20); /* FP/SIMD traps at EL0 */
    __asm__ volatile("dsb sy; msr sctlr_el1, %0; isb; msr mair_el1, %1; msr tcr_el1, %2; "
                     "msr ttbr0_el1, %3; msr ttbr1_el1, xzr; dsb sy; tlbi vmalle1; dsb sy; isb; "
                     "msr sctlr_el1, %4; isb; msr cpacr_el1, %5; msr cntkctl_el1, xzr; isb"
                     : : "r"(off), "r"(mair), "r"(tcr), "r"((uint64_t)(uintptr_t)kernel_l0),
                         "r"(on), "r"(cpacr) : "memory");
    ready = true;
    serial_printf("[USER] Owned ARM64 tables ready; kernel/MMIO EL1-only\n");
}

bool user_arch_supported(void) { return ready; }

bool user_arch_prepare(const UserSpace *space)
{
    if (!ready || !space || !space->count || space->count > USER_MAX_PAGES) return false;
    memset(app_l0, 0, sizeof(app_l0)); memset(app_l1, 0, sizeof(app_l1));
    memset(app_l2, 0, sizeof(app_l2)); memset(app_l3, 0, sizeof(app_l3));
    app_l0[0] = descriptor(app_l1);
    memcpy(app_l1, kernel_l1, sizeof(kernel_l1));
    app_l1[4] = descriptor(app_l2); app_l2[0] = descriptor(app_l3);
    uint64_t ctr; __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    uint64_t line = 4ULL << ((ctr >> 16) & 15);
    for (unsigned i = 0; i < space->count; i++) {
        const UserPage *p = &space->pages[i];
        if (p->address < USER_BASE || p->address >= USER_BASE + 0x200000 ||
            (p->address & PAGE_MASK) || !p->physical || (p->physical & PAGE_MASK) ||
            p->physical >= 0x100000000ULL || !(p->flags & USER_R) || (p->flags & ~7U) ||
            ((p->flags & USER_X) && (p->flags & USER_W))) return false;
        unsigned slot = (unsigned)((p->address - USER_BASE) / PAGE_SIZE);
        if (app_l3[slot]) return false;
        uint64_t ap = (p->flags & USER_W) ? (1ULL << 6) : (3ULL << 6);
        app_l3[slot] = p->physical | TABLE | AF | SH_INNER | NORMAL | ap | PXN |
                       ((p->flags & USER_X) ? 0 : UXN);
        for (uint64_t a = p->physical; a < p->physical + PAGE_SIZE; a += line) {
            __asm__ volatile("dc cvau, %0" : : "r"(a) : "memory");
        }
    }
    __asm__ volatile("dsb ish; ic iallu; dsb ish; isb" ::: "memory");
    return true;
}

void user_arch_run(uint64_t entry, uint64_t stack)
{
    uint64_t flags;
    __asm__ volatile("mrs %0, daif; msr daifset, #0xf" : "=r"(flags) : : "memory");
    switch_root((uint64_t)(uintptr_t)app_l0);
    a64_user_enter(entry, stack, (uint64_t)(uintptr_t)(exception_stack + sizeof(exception_stack)));
    switch_root((uint64_t)(uintptr_t)kernel_l0);
    __asm__ volatile("msr daif, %0; isb" : : "r"(flags) : "memory");
}

uint64_t user_arch_millis(void)
{
    uint64_t counter, frequency;
    __asm__ volatile("mrs %0, cntpct_el0; mrs %1, cntfrq_el0" : "=r"(counter), "=r"(frequency));
    if (!frequency) return 0;
    return (counter / frequency) * 1000 + ((counter % frequency) * 1000) / frequency;
}

int a64_user_sync(A64UserFrame *frame)
{
    if (!user_is_running() || (frame->pstate & 15) != 0) panic("Unexpected user trap");
    if (((frame->esr >> 26) & 63) == 0x15 && (frame->esr & 0xFFFF) == 0) {
        frame->x[0] = (uint64_t)user_syscall(frame->x[8], frame->x[0], frame->x[1], frame->x[2]);
    } else {
        user_fault(frame->pc, frame->far, frame->esr);
    }
    return user_is_running() ? 0 : 1;
}

int a64_user_irq(A64UserFrame *frame)
{
    (void)frame;
    a64_irq_handler(); /* Always acknowledge/re-arm/EOI before leaving the app. */
    user_timer_tick();
    return user_is_running() ? 0 : 1;
}
