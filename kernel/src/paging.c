/*
 * ojjyOS v3 Kernel - Paging Implementation
 *
 * Sets up 4-level paging for x86_64.
 * For MVP, we just identity-map the first 4GB using 2MB huge pages.
 */

#include "paging.h"
#include "memory.h"
#include "serial.h"
#include "string.h"

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

    serial_printf("[PAGING] Page tables loaded (CR3 = 0x%p)\n", pml4_addr);
    serial_printf("[PAGING] Identity mapped first 4GB with 2MB pages\n");
}

/*
 * Map a single 4KB page (for future use)
 * Note: Currently we use 2MB pages, so this is not fully implemented
 */
void paging_map(uint64_t virt, uint64_t phys, uint64_t flags)
{
    (void)virt;
    (void)phys;
    (void)flags;

    /* TODO: Implement 4KB page mapping when needed */
    serial_printf("[PAGING] Warning: 4KB page mapping not implemented\n");
}

/*
 * Unmap a virtual address
 */
void paging_unmap(uint64_t virt)
{
    (void)virt;

    /* TODO: Implement when needed */
    serial_printf("[PAGING] Warning: Page unmapping not implemented\n");
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
