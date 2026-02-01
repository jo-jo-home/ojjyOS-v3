/*
 * ojjyOS v3 Kernel - Physical Memory Manager
 *
 * Simple bitmap-based physical page allocator.
 */

#ifndef _OJJY_MEMORY_H
#define _OJJY_MEMORY_H

#include "types.h"
#include "boot_info.h"

/* Initialize physical memory manager from UEFI memory map */
void pmm_init(BootInfo *info);

/* Allocate a physical page (returns physical address, or 0 on failure) */
uint64_t pmm_alloc_page(void);

/* Free a physical page */
void pmm_free_page(uint64_t addr);

/* Get memory statistics */
uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_free_memory(void);

/* Print memory map (for debugging) */
void pmm_print_map(BootInfo *info);

#endif /* _OJJY_MEMORY_H */
