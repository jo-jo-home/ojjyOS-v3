/*
 * ojjyOS v3 Kernel - Physical Memory Manager Implementation
 *
 * Uses a bitmap to track free/used 4KB pages.
 */

#include "memory.h"
#include "serial.h"
#include "string.h"
#include "boot_info.h"

/* Bitmap for tracking physical pages */
#define MAX_MEMORY_GB       4ULL
#define MAX_MEMORY_BYTES    (MAX_MEMORY_GB * 1024ULL * 1024ULL * 1024ULL)
#define MAX_PAGES           (MAX_MEMORY_BYTES / PAGE_SIZE)
#define BITMAP_SIZE         (MAX_PAGES / 8)

/* Static bitmap (4GB max = 128KB bitmap) */
static uint8_t page_bitmap[BITMAP_SIZE];

/* Start and end of kernel image (including .bss), provided by linker script */
extern char _kernel_start[];
extern char _kernel_end[];

/* Memory statistics */
static uint64_t total_memory = 0;
static uint64_t free_memory = 0;
static uint64_t bitmap_pages = 0;
static bool pmm_ready = false;   /* No allocations before pmm_init */

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

    if (desc_size == 0) {
        serial_printf("[PMM] ERROR: Invalid memory map descriptor size!\n");
        return;
    }

    uint64_t entries = mmap_size / desc_size;

    /* First pass: calculate usable memory and mark free regions */
    for (uint64_t i = 0; i < entries; i++) {
        EfiMemoryDescriptor *desc = (EfiMemoryDescriptor *)(mmap_addr + i * desc_size);

        uint64_t size = desc->num_pages * PAGE_SIZE;

        /* Count only memory the OS can actually use */
        if (desc->type == EFI_CONVENTIONAL_MEMORY ||
            desc->type == EFI_LOADER_CODE ||
            desc->type == EFI_LOADER_DATA ||
            desc->type == EFI_BOOT_SERVICES_CODE ||
            desc->type == EFI_BOOT_SERVICES_DATA ||
            desc->type == EFI_ACPI_RECLAIM_MEMORY) {
            total_memory += size;
        }

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

    /* Reserve first 1MB (legacy area, BIOS, VGA memory, boot info at 0x500) */
    for (uint64_t page = 0; page < 256; page++) {
        if (!pmm_is_page_used(page)) {
            pmm_set_page_used(page);
            free_memory -= PAGE_SIZE;
        }
    }

    /* Reserve the actual kernel image (_kernel_start .. _kernel_end, .bss included).
     * The kernel BSS is several MB (wallpaper buffers, ramfs pools), so we use
     * linker symbols on both x86_64 (0x100000) and aarch64 (0x71000000). */
    uint64_t kernel_start_page = (uint64_t)(uintptr_t)_kernel_start / PAGE_SIZE;
    uint64_t kernel_end_page = ((uint64_t)(uintptr_t)_kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    if (kernel_start_page < 256) {
        kernel_start_page = 256;
    }
    if (kernel_end_page < kernel_start_page) {
        kernel_end_page = kernel_start_page;
    }
    for (uint64_t page = kernel_start_page; page < kernel_end_page; page++) {
        if (page < MAX_PAGES && !pmm_is_page_used(page)) {
            pmm_set_page_used(page);
            free_memory -= PAGE_SIZE;
        }
    }

    /* Never hand out the scanout: some firmware reports the framebuffer
     * inside boot-services memory, which is otherwise reclaimed above. */
    if (info->fb_addr && info->fb_pitch && info->fb_height) {
        uint64_t first = info->fb_addr / PAGE_SIZE;
        uint64_t last = (info->fb_addr + (uint64_t)info->fb_pitch * info->fb_height + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint64_t page = first; page < last && page < MAX_PAGES; page++) {
            if (!pmm_is_page_used(page)) { pmm_set_page_used(page); free_memory -= PAGE_SIZE; }
        }
    }

    bitmap_pages = sizeof(page_bitmap) * 8;
    pmm_ready = true;

    serial_printf("[PMM] Total usable memory: %d MB\n",
        (int)(total_memory / (1024 * 1024)));
    serial_printf("[PMM] Free memory:  %d MB\n",
        (int)(free_memory / (1024 * 1024)));
    serial_printf("[PMM] Kernel reserved: %p - %p\n",
        (uint64_t)(uintptr_t)_kernel_start, (uint64_t)(uintptr_t)_kernel_end);
    serial_printf("[PMM] Page bitmap covers %d pages\n", (int)bitmap_pages);
}

/*
 * Allocate a physical page
 */
uint64_t pmm_alloc_page(void)
{
    /* The boot splash allocates its back buffer before pmm_init; with the
     * zero-cleared bitmap every page would look free and the allocator
     * would hand out page 256 (which the owned page tables map Device). */
    if (!pmm_ready) return 0;

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
 * Allocate `pages` physically contiguous, zeroed pages (first fit, below
 * 4 GiB, which both architectures identity-map). Used for large buffers
 * so they need not live in the kernel image's BSS. Returns 0 on failure.
 */
uint64_t pmm_alloc_contiguous(uint64_t pages)
{
    if (!pages || !pmm_ready) return 0;
    uint64_t run = 0;
    for (uint64_t page = 256; page < MAX_PAGES; page++) {
        if (pmm_is_page_used(page)) { run = 0; continue; }
        if (++run == pages) {
            uint64_t start = page + 1 - pages;
            for (uint64_t p = start; p <= page; p++) pmm_set_page_used(p);
            free_memory -= pages * PAGE_SIZE;
            memset((void *)(uintptr_t)(start * PAGE_SIZE), 0, pages * PAGE_SIZE);
            return start * PAGE_SIZE;
        }
    }
    serial_printf("[PMM] ERROR: no %d contiguous pages\n", (int)pages);
    return 0;
}

void *pmm_alloc_bytes(size_t bytes)
{
    return (void *)(uintptr_t)pmm_alloc_contiguous((bytes + PAGE_SIZE - 1) / PAGE_SIZE);
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

/* ============================================================================
 * Kernel Heap Allocator (16-byte aligned coalescing free-list)
 * ============================================================================ */

#define KHEAP_MAGIC         0x4F4A4850U /* "OJHP" */
#define KHEAP_ALIGN         16ULL
#define KHEAP_MIN_SPLIT     16ULL

typedef struct KHeapBlock {
    uint32_t magic;
    uint32_t is_free;
    uint64_t size;
    struct KHeapBlock *next;
    struct KHeapBlock *prev;
} KHeapBlock;

static uint8_t kheap_arena[KHEAP_ARENA_SIZE] __attribute__((aligned(16)));
static KHeapBlock *kheap_head = NULL;
static bool kheap_ready = false;
static uint64_t kheap_used_bytes = 0;
static uint32_t kheap_alloc_count = 0;

static uint64_t kheap_align_up(uint64_t n)
{
    return (n + (KHEAP_ALIGN - 1ULL)) & ~(KHEAP_ALIGN - 1ULL);
}

void kheap_init(void)
{
    memset(kheap_arena, 0, sizeof(kheap_arena));
    kheap_head = (KHeapBlock *)kheap_arena;
    kheap_head->magic = KHEAP_MAGIC;
    kheap_head->is_free = 1;
    kheap_head->size = (uint64_t)sizeof(kheap_arena) - (uint64_t)sizeof(KHeapBlock);
    kheap_head->next = NULL;
    kheap_head->prev = NULL;
    kheap_used_bytes = 0;
    kheap_alloc_count = 0;
    kheap_ready = true;
}

void *kmalloc(size_t size)
{
    if (size == 0) return NULL;
    if (!kheap_ready) kheap_init();

    uint64_t req = kheap_align_up((uint64_t)size);
    if (req == 0 || req > (uint64_t)sizeof(kheap_arena)) return NULL;

    KHeapBlock *curr = kheap_head;
    while (curr) {
        if (curr->magic != KHEAP_MAGIC) {
            serial_printf("[KHEAP] ERROR: Corrupted heap block header at 0x%p\n", curr);
            return NULL;
        }
        if (curr->is_free && curr->size >= req) {
            uint64_t rem = curr->size - req;
            if (rem >= (uint64_t)sizeof(KHeapBlock) + KHEAP_MIN_SPLIT) {
                uint8_t *split_addr = (uint8_t *)curr + sizeof(KHeapBlock) + req;
                KHeapBlock *next_blk = (KHeapBlock *)split_addr;
                next_blk->magic = KHEAP_MAGIC;
                next_blk->is_free = 1;
                next_blk->size = rem - (uint64_t)sizeof(KHeapBlock);
                next_blk->next = curr->next;
                next_blk->prev = curr;
                if (curr->next) {
                    curr->next->prev = next_blk;
                }
                curr->next = next_blk;
                curr->size = req;
            }
            curr->is_free = 0;
            kheap_used_bytes += curr->size;
            kheap_alloc_count++;
            return (void *)((uint8_t *)curr + sizeof(KHeapBlock));
        }
        curr = curr->next;
    }

    serial_printf("[KHEAP] WARNING: Out of heap memory (requested %d bytes)\n", (int)size);
    return NULL;
}

void *kcalloc(size_t count, size_t size)
{
    if (count == 0 || size == 0) return NULL;
    if (count > ((size_t)-1) / size) return NULL;
    size_t total = count * size;
    void *ptr = kmalloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void kfree(void *ptr)
{
    if (!ptr || !kheap_ready) return;

    uintptr_t p = (uintptr_t)ptr;
    uintptr_t start = (uintptr_t)kheap_arena + sizeof(KHeapBlock);
    uintptr_t end = (uintptr_t)kheap_arena + sizeof(kheap_arena);
    if (p < start || p >= end) {
        serial_printf("[KHEAP] WARNING: kfree pointer 0x%p outside heap arena\n", ptr);
        return;
    }

    KHeapBlock *blk = (KHeapBlock *)((uint8_t *)ptr - sizeof(KHeapBlock));
    if (blk->magic != KHEAP_MAGIC) {
        serial_printf("[KHEAP] ERROR: Invalid block magic on kfree(0x%p)\n", ptr);
        return;
    }
    if (blk->is_free) {
        serial_printf("[KHEAP] WARNING: Double kfree(0x%p)\n", ptr);
        return;
    }

    blk->is_free = 1;
    if (kheap_used_bytes >= blk->size) kheap_used_bytes -= blk->size;
    else kheap_used_bytes = 0;
    if (kheap_alloc_count > 0) kheap_alloc_count--;

    /* Coalesce forward with next block if free */
    if (blk->next && blk->next->magic == KHEAP_MAGIC && blk->next->is_free) {
        KHeapBlock *nxt = blk->next;
        blk->size += (uint64_t)sizeof(KHeapBlock) + nxt->size;
        blk->next = nxt->next;
        if (nxt->next) {
            nxt->next->prev = blk;
        }
        nxt->magic = 0;
    }

    /* Coalesce backward with prev block if free */
    if (blk->prev && blk->prev->magic == KHEAP_MAGIC && blk->prev->is_free) {
        KHeapBlock *prv = blk->prev;
        prv->size += (uint64_t)sizeof(KHeapBlock) + blk->size;
        prv->next = blk->next;
        if (blk->next) {
            blk->next->prev = prv;
        }
        blk->magic = 0;
    }
}

void *krealloc(void *ptr, size_t new_size)
{
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    KHeapBlock *blk = (KHeapBlock *)((uint8_t *)ptr - sizeof(KHeapBlock));
    if (blk->magic != KHEAP_MAGIC) return NULL;
    if (blk->size >= (uint64_t)new_size) {
        return ptr;
    }

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, (size_t)blk->size);
    kfree(ptr);
    return new_ptr;
}

uint64_t kheap_get_total(void)
{
    return (uint64_t)sizeof(kheap_arena);
}

uint64_t kheap_get_used(void)
{
    return kheap_used_bytes;
}

uint64_t kheap_get_free(void)
{
    if (!kheap_ready) return (uint64_t)sizeof(kheap_arena) - (uint64_t)sizeof(KHeapBlock);
    uint64_t total_free = 0;
    KHeapBlock *curr = kheap_head;
    while (curr && curr->magic == KHEAP_MAGIC) {
        if (curr->is_free) total_free += curr->size;
        curr = curr->next;
    }
    return total_free;
}

uint32_t kheap_get_alloc_count(void)
{
    return kheap_alloc_count;
}

bool kheap_validate(void)
{
    if (!kheap_ready) kheap_init();
    KHeapBlock *curr = kheap_head;
    KHeapBlock *prev = NULL;
    uint32_t count = 0;
    while (curr) {
        if (curr->magic != KHEAP_MAGIC) return false;
        if (curr->prev != prev) return false;
        if (!curr->is_free) count++;
        prev = curr;
        curr = curr->next;
    }
    return (count == kheap_alloc_count);
}
