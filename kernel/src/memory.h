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
/* Boot-only entry: reserves the supplied physical kernel range. Do not
 * reinitialize a running allocator. Requires firmware mappings until it
 * has copied descriptor storage. Failure leaves allocation disabled. */
bool pmm_init_ranges(BootInfo *info, uint64_t kernel_start, uint64_t kernel_end);
typedef enum { PMM_LOW, PMM_HIGH, PMM_ANY } PmmZone;
typedef struct {
    /* All units are bytes. total includes firmware RAM types 1..10;
     * usable is initial eligible capacity after permanent reservations.
     * high_free is inactive until the architecture publishes a resolver. */
    uint64_t total_bytes, usable_bytes, low_free_bytes, high_free_bytes;
    uint64_t metadata_base, metadata_bytes, regions;
} PmmStats;
/* HIGH fails closed before mapping readiness; ANY prefers HIGH then LOW.
 * All legacy allocation APIs below remain strictly LOW. */
uint64_t pmm_alloc_page_zone(PmmZone zone);
uint64_t pmm_alloc_contiguous_zone(uint64_t pages, PmmZone zone);
PmmStats pmm_get_stats(void);
typedef void *(*PmmResolver)(uint64_t physical, size_t bytes);
bool pmm_enable_high(PmmResolver resolver);
bool pmm_high_ready(void);
bool pmm_region(uint64_t index, uint64_t *base, uint64_t *pages);
bool pmm_range_eligible(uint64_t physical, size_t bytes);
bool pmm_is_allocated(uint64_t physical);
void *pmm_phys_to_virt(uint64_t physical, size_t bytes);
uint64_t pmm_virt_to_phys(const void *pointer, size_t bytes);
void *pmm_alloc_mapped_bytes(size_t bytes);

/* Allocate a physical page (returns physical address, or 0 on failure) */
uint64_t pmm_alloc_page(void);
/* Physically contiguous, zeroed pages (identity-mapped); 0/NULL on failure. */
uint64_t pmm_alloc_contiguous(uint64_t pages);
void *pmm_alloc_bytes(size_t bytes);

/* Free a physical page */
void pmm_free_page(uint64_t addr);

/* Legacy statistics: described RAM total and immediately allocatable LOW free.
 * Use pmm_get_stats for managed capacity and inactive high free bytes. */
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
