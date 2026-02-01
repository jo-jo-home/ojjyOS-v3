/*
 * ojjyOS v3 Kernel - Physical Memory Manager Implementation
 *
 * Uses a bitmap to track free/used 4KB pages.
 */

#include "memory.h"
#include "serial.h"
#include "string.h"

/* Bitmap for tracking physical pages */
#define MAX_MEMORY_GB       4ULL
#define MAX_MEMORY_BYTES    (MAX_MEMORY_GB * 1024ULL * 1024ULL * 1024ULL)
#define MAX_PAGES           (MAX_MEMORY_BYTES / PAGE_SIZE)
#define BITMAP_SIZE         (MAX_PAGES / 8)

/* Static bitmap (4GB max = 128KB bitmap) */
static uint8_t page_bitmap[BITMAP_SIZE];

/* Memory statistics */
static uint64_t total_memory = 0;
static uint64_t free_memory = 0;
static uint64_t bitmap_pages = 0;

/*
 * Mark a page as used
 */
static void pmm_set_page_used(uint64_t page_index)
{
    page_bitmap[page_index / 8] |= (1 << (page_index % 8));
}

/*
 * Mark a page as free
 */
static void pmm_set_page_free(uint64_t page_index)
{
    page_bitmap[page_index / 8] &= ~(1 << (page_index % 8));
}

/*
 * Check if a page is used
 */
static bool pmm_is_page_used(uint64_t page_index)
{
    return (page_bitmap[page_index / 8] & (1 << (page_index % 8))) != 0;
}

/*
 * Convert UEFI memory type to string
 */
static const char *memory_type_string(uint32_t type)
{
    switch (type) {
        case EFI_RESERVED_MEMORY_TYPE: return "Reserved";
        case EFI_LOADER_CODE: return "Loader Code";
        case EFI_LOADER_DATA: return "Loader Data";
        case EFI_BOOT_SERVICES_CODE: return "Boot Services Code";
        case EFI_BOOT_SERVICES_DATA: return "Boot Services Data";
        case EFI_RUNTIME_SERVICES_CODE: return "Runtime Services Code";
        case EFI_RUNTIME_SERVICES_DATA: return "Runtime Services Data";
        case EFI_CONVENTIONAL_MEMORY: return "Conventional";
        case EFI_UNUSABLE_MEMORY: return "Unusable";
        case EFI_ACPI_RECLAIM_MEMORY: return "ACPI Reclaim";
        case EFI_ACPI_MEMORY_NVS: return "ACPI NVS";
        case EFI_MEMORY_MAPPED_IO: return "MMIO";
        case EFI_MEMORY_MAPPED_IO_PORT_SPACE: return "MMIO Port";
        case EFI_PAL_CODE: return "PAL Code";
        default: return "Unknown";
    }
}

/*
 * Print memory map (for debugging)
 */
void pmm_print_map(BootInfo *info)
{
    serial_printf("[PMM] Memory Map:\n");

    uint64_t mmap_addr = info->mmap_addr;
    uint64_t mmap_size = info->mmap_size;
    uint64_t desc_size = info->mmap_desc_size;

    uint64_t entries = mmap_size / desc_size;

    for (uint64_t i = 0; i < entries; i++) {
        EfiMemoryDescriptor *desc = (EfiMemoryDescriptor *)(mmap_addr + i * desc_size);

        uint64_t size = desc->num_pages * PAGE_SIZE;
        const char *type = memory_type_string(desc->type);

        serial_printf("  [%2d] 0x%p - 0x%p (%6d KB) %s\n",
            i,
            desc->phys_addr,
            desc->phys_addr + size,
            size / 1024,
            type);
    }
}

/*
 * Initialize physical memory manager
 */
void pmm_init(BootInfo *info)
{
    serial_printf("[PMM] Initializing physical memory manager...\n");

    /* Clear bitmap (mark all pages as used initially) */
    memset(page_bitmap, 0xFF, sizeof(page_bitmap));

    uint64_t mmap_addr = info->mmap_addr;
    uint64_t mmap_size = info->mmap_size;
    uint64_t desc_size = info->mmap_desc_size;

    uint64_t entries = mmap_size / desc_size;

    /* First pass: calculate total memory and find usable regions */
    for (uint64_t i = 0; i < entries; i++) {
        EfiMemoryDescriptor *desc = (EfiMemoryDescriptor *)(mmap_addr + i * desc_size);

        uint64_t size = desc->num_pages * PAGE_SIZE;
        total_memory += size;

        /* Mark conventional memory (and boot services memory) as free */
        if (desc->type == EFI_CONVENTIONAL_MEMORY ||
            desc->type == EFI_BOOT_SERVICES_CODE ||
            desc->type == EFI_BOOT_SERVICES_DATA) {

            /* Mark each page as free */
            uint64_t start_page = desc->phys_addr / PAGE_SIZE;
            uint64_t end_page = start_page + desc->num_pages;

            for (uint64_t page = start_page; page < end_page; page++) {
                if (page < sizeof(page_bitmap) * 8) {
                    pmm_set_page_free(page);
                    free_memory += PAGE_SIZE;
                }
            }
        }
    }

    /* Reserve first 1MB (legacy area, BIOS, etc.) */
    for (uint64_t page = 0; page < 256; page++) {
        if (!pmm_is_page_used(page)) {
            pmm_set_page_used(page);
            free_memory -= PAGE_SIZE;
        }
    }

    /* Reserve kernel area (1MB - 4MB to be safe) */
    for (uint64_t page = 256; page < 1024; page++) {
        if (!pmm_is_page_used(page)) {
            pmm_set_page_used(page);
            free_memory -= PAGE_SIZE;
        }
    }

    bitmap_pages = sizeof(page_bitmap) * 8;

    serial_printf("[PMM] Total memory: %d MB\n", total_memory / (1024 * 1024));
    serial_printf("[PMM] Free memory:  %d MB\n", free_memory / (1024 * 1024));
    serial_printf("[PMM] Page bitmap covers %d pages\n", bitmap_pages);
}

/*
 * Allocate a physical page
 */
uint64_t pmm_alloc_page(void)
{
    /* Simple linear search for a free page */
    for (uint64_t i = 0; i < sizeof(page_bitmap); i++) {
        if (page_bitmap[i] != 0xFF) {
            /* Found a byte with at least one free page */
            for (int bit = 0; bit < 8; bit++) {
                if (!(page_bitmap[i] & (1 << bit))) {
                    /* Found a free page */
                    uint64_t page_index = i * 8 + bit;
                    pmm_set_page_used(page_index);
                    free_memory -= PAGE_SIZE;

                    uint64_t addr = page_index * PAGE_SIZE;

                    /* Zero the page */
                    memset((void *)addr, 0, PAGE_SIZE);

                    return addr;
                }
            }
        }
    }

    /* Out of memory */
    serial_printf("[PMM] ERROR: Out of physical memory!\n");
    return 0;
}

/*
 * Free a physical page
 */
void pmm_free_page(uint64_t addr)
{
    uint64_t page_index = addr / PAGE_SIZE;

    if (page_index >= sizeof(page_bitmap) * 8) {
        serial_printf("[PMM] WARNING: Trying to free page outside bitmap range\n");
        return;
    }

    if (!pmm_is_page_used(page_index)) {
        serial_printf("[PMM] WARNING: Double-free of page 0x%x\n", addr);
        return;
    }

    pmm_set_page_free(page_index);
    free_memory += PAGE_SIZE;
}

/*
 * Get total memory
 */
uint64_t pmm_get_total_memory(void)
{
    return total_memory;
}

/*
 * Get free memory
 */
uint64_t pmm_get_free_memory(void)
{
    return free_memory;
}
