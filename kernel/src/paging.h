/*
 * ojjyOS v3 Kernel - Paging
 *
 * x86_64 4-level paging setup.
 */

#ifndef _OJJY_PAGING_H
#define _OJJY_PAGING_H

#include "types.h"

/* Page table entry flags */
#define PAGE_PRESENT        (1ULL << 0)
#define PAGE_WRITABLE       (1ULL << 1)
#define PAGE_USER           (1ULL << 2)
#define PAGE_WRITE_THROUGH  (1ULL << 3)
#define PAGE_CACHE_DISABLE  (1ULL << 4)
#define PAGE_ACCESSED       (1ULL << 5)
#define PAGE_DIRTY          (1ULL << 6)
#define PAGE_HUGE           (1ULL << 7)  /* 2MB page (in PD), 1GB page (in PDPT) */
#define PAGE_GLOBAL         (1ULL << 8)
#define PAGE_NO_EXECUTE     (1ULL << 63)

/* Initialize paging (identity map first 4GB for now) */
void paging_init(void);

/* Map a virtual address to a physical address */
void paging_map(uint64_t virt, uint64_t phys, uint64_t flags);

/* Unmap a virtual address */
void paging_unmap(uint64_t virt);

/* Flush TLB for a specific address */
void paging_invalidate(uint64_t virt);

/* Flush entire TLB */
void paging_flush_tlb(void);

#endif /* _OJJY_PAGING_H */
