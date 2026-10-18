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
/* Physically contiguous, zeroed pages (identity-mapped); 0/NULL on failure. */
uint64_t pmm_alloc_contiguous(uint64_t pages);
void *pmm_alloc_bytes(size_t bytes);

/* Free a physical page */
void pmm_free_page(uint64_t addr);

/* Get memory statistics */
uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_free_memory(void);

/* Print memory map (for debugging) */
void pmm_print_map(BootInfo *info);

/*
 * Kernel Heap Allocator (16-byte aligned coalescing free-list)
 */
#define KHEAP_ARENA_SIZE    (512 * 1024)

void kheap_init(void);
void *kmalloc(size_t size);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *ptr, size_t new_size);
void kfree(void *ptr);

uint64_t kheap_get_total(void);
uint64_t kheap_get_used(void);
uint64_t kheap_get_free(void);
uint32_t kheap_get_alloc_count(void);
bool kheap_validate(void);

#endif /* _OJJY_MEMORY_H */
