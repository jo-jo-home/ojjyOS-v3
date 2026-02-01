/*
 * ojjyOS v3 Kernel - Global Descriptor Table Implementation
 *
 * In long mode, segmentation is mostly disabled, but we still need:
 * - Code segments for kernel (ring 0) and user (ring 3)
 * - Data segments for kernel and user
 * - TSS for interrupt stack switching
 */

#include "gdt.h"
#include "string.h"

/* GDT entry structure */
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} PACKED GdtEntry;

/* TSS entry (16 bytes in long mode) */
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} PACKED TssEntry;

/* Task State Segment */
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;          /* Stack pointer for ring 0 */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;          /* Interrupt stack table */
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} PACKED Tss;

/* GDT pointer structure */
typedef struct {
    uint16_t limit;
    uint64_t base;
} PACKED GdtPointer;

/* Our GDT (6 entries: null, kernel code, kernel data, user data, user code, TSS) */
static struct {
    GdtEntry null;
    GdtEntry kernel_code;
    GdtEntry kernel_data;
    GdtEntry user_data;
    GdtEntry user_code;
    TssEntry tss;
} PACKED gdt;

static Tss tss;
static GdtPointer gdt_ptr;

/*
 * Load GDT
 */
static void gdt_load(GdtPointer *ptr)
{
    __asm__ volatile(
        "lgdt (%0)\n"
        "push $0x08\n"           /* Kernel code segment */
        "lea 1f(%%rip), %%rax\n"
        "push %%rax\n"
        "lretq\n"                /* Far return to reload CS */
        "1:\n"
        "mov $0x10, %%ax\n"      /* Kernel data segment */
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "mov $0x00, %%ax\n"      /* Clear FS and GS */
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        :
        : "r"(ptr)
        : "rax", "memory"
    );
}

/*
 * Load TSS
 */
static void tss_load(uint16_t selector)
{
    __asm__ volatile("ltr %0" : : "r"(selector));
}

/*
 * Set a GDT entry
 */
static void gdt_set_entry(GdtEntry *entry, uint8_t access, uint8_t granularity)
{
    entry->limit_low = 0;
    entry->base_low = 0;
    entry->base_mid = 0;
    entry->access = access;
    entry->granularity = granularity;
    entry->base_high = 0;
}

/*
 * Set TSS entry in GDT
 */
static void gdt_set_tss(TssEntry *entry, uint64_t base, uint32_t limit)
{
    entry->limit_low = limit & 0xFFFF;
    entry->base_low = base & 0xFFFF;
    entry->base_mid = (base >> 16) & 0xFF;
    entry->access = 0x89;       /* Present, TSS (available) */
    entry->granularity = ((limit >> 16) & 0x0F);
    entry->base_high = (base >> 24) & 0xFF;
    entry->base_upper = (base >> 32) & 0xFFFFFFFF;
    entry->reserved = 0;
}

/*
 * Initialize GDT
 */
void gdt_init(void)
{
    /* Clear structures */
    memset(&gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    /* Null descriptor */
    gdt_set_entry(&gdt.null, 0, 0);

    /* Kernel code (64-bit, ring 0) */
    /* Access: Present(1) DPL(00) Type(1) Code(1) Conform(0) Read(1) Accessed(0) */
    /* = 0b10011010 = 0x9A */
    /* Granularity: Long mode (L=1, D=0), granularity doesn't matter */
    /* = 0b00100000 = 0x20 */
    gdt_set_entry(&gdt.kernel_code, 0x9A, 0x20);

    /* Kernel data (64-bit, ring 0) */
    /* Access: Present(1) DPL(00) Type(1) Data(0) Direction(0) Write(1) Accessed(0) */
    /* = 0b10010010 = 0x92 */
    gdt_set_entry(&gdt.kernel_data, 0x92, 0x00);

    /* User data (64-bit, ring 3) */
    /* Access: Present(1) DPL(11) Type(1) Data(0) Direction(0) Write(1) Accessed(0) */
    /* = 0b11110010 = 0xF2 */
    gdt_set_entry(&gdt.user_data, 0xF2, 0x00);

    /* User code (64-bit, ring 3) */
    /* Access: Present(1) DPL(11) Type(1) Code(1) Conform(0) Read(1) Accessed(0) */
    /* = 0b11111010 = 0xFA */
    gdt_set_entry(&gdt.user_code, 0xFA, 0x20);

    /* TSS */
    tss.iopb_offset = sizeof(Tss);
    gdt_set_tss(&gdt.tss, (uint64_t)&tss, sizeof(Tss) - 1);

    /* Set up GDT pointer */
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    /* Load GDT */
    gdt_load(&gdt_ptr);

    /* Load TSS */
    tss_load(GDT_TSS);
}

/*
 * Set kernel stack for interrupts from usermode
 */
void gdt_set_kernel_stack(uint64_t stack)
{
    tss.rsp0 = stack;
}
