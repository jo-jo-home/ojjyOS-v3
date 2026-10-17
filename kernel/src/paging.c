/*
 * ojjyOS v3 Kernel - Paging Implementation
 *
 * Sets up 4-level paging for x86_64.
 * Identity-maps the first 4GB using 2MB huge pages, and provides an
 * on-demand 4KB page mapper (paging_map) for anything outside that range
 * (e.g. framebuffers placed above 4GB by the firmware).
 */

#include "paging.h"
#include "memory.h"
#include "serial.h"
#include "string.h"
#include "boot_info.h"

/* Page table structure (512 entries * 8 bytes = 4KB) */
typedef uint64_t PageTable[512] __attribute__((aligned(4096)));

/* Static page tables for initial setup */
/* We need: 1 PML4, 1 PDPT, 4 PDs (for 4GB with 2MB pages) */
static PageTable pml4 __attribute__((aligned(4096)));
static PageTable pdpt __attribute__((aligned(4096)));
static PageTable pd[4] __attribute__((aligned(4096)));  /* 4 page directories for 4GB */

/*
 * Extract page table indices from virtual address
 */
#define PML4_INDEX(addr)    (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr)    (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)      (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)      (((addr) >> 12) & 0x1FF)

/* Global boot info (set in main.c) */
extern BootInfo *g_boot_info;

/*
 * Map the framebuffer if it lives outside the identity-mapped 4GB.
 * Called after write_cr3 so the mapping survives the switch.
 */
static void map_framebuffer_if_needed(void)
{
    if (!g_boot_info) return;

    uint64_t fb_addr = g_boot_info->fb_addr;
    uint64_t fb_size = (uint64_t)g_boot_info->fb_pitch * g_boot_info->fb_height;
    uint64_t fb_end = fb_addr + fb_size;

    /* Fully inside the identity-mapped first 4GB? */
    if (fb_end <= 4ULL * 1024ULL * 1024ULL * 1024ULL) {
        return;
    }

    serial_printf("[PAGING] Framebuffer above 4GB (0x%p), mapping %d KB...\n",
        fb_addr, (int)((fb_size + 1023) / 1024));

    /* Map every page the framebuffer occupies (identity) */
    uint64_t page = fb_addr & ~0xFFFULL;
    while (page < ((fb_end + 0xFFFULL) & ~0xFFFULL)) {
        paging_map(page, page, PAGE_PRESENT | PAGE_WRITABLE);
        page += PAGE_SIZE;
    }

    serial_printf("[PAGING] Framebuffer mapped\n");
}

/*
 * Initialize paging with identity mapping
 */
void paging_init(void)
{
    serial_printf("[PAGING] Setting up page tables...\n");

    /* Clear all page tables */
    memset(pml4, 0, sizeof(pml4));
    memset(pdpt, 0, sizeof(pdpt));
    memset(pd, 0, sizeof(pd));

    /* Set up PML4 -> PDPT */
    pml4[0] = ((uint64_t)pdpt) | PAGE_PRESENT | PAGE_WRITABLE;

    /* Set up PDPT -> PDs (each PD covers 1GB) */
    for (int i = 0; i < 4; i++) {
        pdpt[i] = ((uint64_t)&pd[i]) | PAGE_PRESENT | PAGE_WRITABLE;
    }

    /* Set up PDs with 2MB huge pages (identity mapping) */
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 512; j++) {
            uint64_t phys_addr = ((uint64_t)i * 512 + j) * (2 * 1024 * 1024);
            pd[i][j] = phys_addr | PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE;
        }
    }

    /* Load new page tables */
    uint64_t pml4_addr = (uint64_t)pml4;
    write_cr3(pml4_addr);

    /* Make sure a >4GB framebuffer is still reachable */
    map_framebuffer_if_needed();

    serial_printf("[PAGING] Page tables loaded (CR3 = 0x%p)\n", pml4_addr);
    serial_printf("[PAGING] Identity mapped first 4GB with 2MB pages\n");
}

/*
 * Walk the page table hierarchy for a virtual address, allocating
 * intermediate tables on demand. Returns a pointer to the final
 * page-table entry (NULL on allocation failure).
 */
static uint64_t *walk_to_pte(uint64_t virt, bool create)
{
    uint64_t pml4_idx = PML4_INDEX(virt);
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    uint64_t pd_idx = PD_INDEX(virt);
    uint64_t pt_idx = PT_INDEX(virt);

    /* PML4 entry */
    uint64_t *entry = &pml4[pml4_idx];
    if (!(*entry & PAGE_PRESENT)) {
        if (!create) return NULL;
        uint64_t table = pmm_alloc_page();
        if (!table) return NULL;
        /* Zero the new table BEFORE linking it in */
        memset((void *)table, 0, PAGE_SIZE);
        *entry = table | PAGE_PRESENT | PAGE_WRITABLE;
        paging_invalidate(virt);
    }

    /* PDPT entry */
    uint64_t *table = (uint64_t *)(*entry & ~0xFFFULL);
    entry = &table[pdpt_idx];
    if (*entry & PAGE_HUGE) {
        serial_printf("[PAGING] ERROR: cannot 4K-map inside 1GB huge page at 0x%p\n", virt);
        return NULL;
    }
    if (!(*entry & PAGE_PRESENT)) {
        if (!create) return NULL;
        uint64_t newt = pmm_alloc_page();
        if (!newt) return NULL;
        memset((void *)newt, 0, PAGE_SIZE);
        *entry = newt | PAGE_PRESENT | PAGE_WRITABLE;
        paging_invalidate(virt);
    }

    /* PD entry */
    table = (uint64_t *)(*entry & ~0xFFFULL);
    entry = &table[pd_idx];
    if (*entry & PAGE_HUGE) {
        serial_printf("[PAGING] ERROR: cannot 4K-map inside 2MB huge page at 0x%p\n", virt);
        return NULL;
    }

    /* PT entry */
    table = (uint64_t *)(*entry & ~0xFFFULL);
    return &table[pt_idx];
}

/*
 * Map a single 4KB page.
 * Creates intermediate page tables on demand using the physical
 * memory manager.
 */
void paging_map(uint64_t virt, uint64_t phys, uint64_t flags)
{
    virt &= ~0xFFFULL;
    phys &= ~0xFFFULL;

    uint64_t *pte = walk_to_pte(virt, true);
    if (!pte) {
        serial_printf("[PAGING] ERROR: out of memory mapping 0x%p\n", virt);
        return;
    }

    *pte = phys | (flags & (PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER)) | PAGE_PRESENT;
    paging_invalidate(virt);
}

/*
 * Unmap a virtual address (removes the 4KB page mapping)
 */
void paging_unmap(uint64_t virt)
{
    virt &= ~0xFFFULL;

    uint64_t *pte = walk_to_pte(virt, false);
    if (!pte) return;

    *pte = 0;
    paging_invalidate(virt);
}

/*
 * Invalidate TLB entry for a specific virtual address
 */
void paging_invalidate(uint64_t virt)
{
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

/*
 * Flush entire TLB by reloading CR3
 */
void paging_flush_tlb(void)
{
    uint64_t cr3 = read_cr3();
    write_cr3(cr3);
}
