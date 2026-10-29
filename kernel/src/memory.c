/*
 * ojjyOS v3 Kernel - Physical Memory Manager Implementation
 *
 * Uses a bitmap to track free/used 4KB pages.
 */

#include "memory.h"
#include "serial.h"
#include "string.h"
#include "boot_info.h"

/* Runtime PMM metadata: two bits per reclaimable page, no physical-span cap.
 * Existing callers remain LOW until the architecture has high RAM mappings.
 * Created by Jonas Lee. */
#define LOW_END (1ULL << 32)
#define LIMIT64 (~0ULL)
typedef struct {
    uint64_t base, pages, cursor;
    uint8_t *eligible, *allocated;
} PmmRegion;
extern char _kernel_start[], _kernel_end[];
static PmmRegion *regions;
static uint64_t region_count;
static PmmStats stats;
static bool pmm_ready;

#ifdef OJJY_PMM_HOST_TEST
extern void *pmm_host_pointer(uint64_t physical, size_t bytes);
extern uint64_t pmm_host_address(const void *pointer);
#define physical_pointer(a,n) pmm_host_pointer((a),(n))
#define boot_address(p) pmm_host_address((const void *)(uintptr_t)(p))
#else
#define physical_pointer(a,n) ((void *)(uintptr_t)(a))
#define boot_address(p) ((uint64_t)(uintptr_t)(p))
#endif

static bool overlap(uint64_t a, uint64_t end, uint64_t b, uint64_t bend)
{ return a < end && b < bend && a < bend && b < end; }
static bool bit(const uint8_t *b, uint64_t n) { return (b[n/8] >> (n%8)) & 1; }
static void set_bit(uint8_t *b, uint64_t n, bool value)
{ if (value) b[n/8] |= 1u << (n%8); else b[n/8] &= ~(1u << (n%8)); }
static bool reclaimable(const EfiMemoryDescriptor *d)
{
    return !(d->attribute & (1ULL << 63)) &&
        (d->type == 3 || d->type == 4 || d->type == 7);
}
static void descriptor(const BootInfo *info, uint64_t i, EfiMemoryDescriptor *d)
{ memcpy(d, (const uint8_t *)(uintptr_t)info->mmap_addr + i*info->mmap_desc_size, sizeof(*d)); }

/* All boot exclusions are byte ranges, rounded implicitly by overlap with
 * whole pages. Original loader-owned objects remain reserved too. */
static bool excluded(uint64_t start, uint64_t end, const BootInfo *info,
                     uint64_t ks, uint64_t ke, uint64_t meta, uint64_t mend)
{
    return start < 0x100000 || overlap(start,end,ks,ke) ||
        overlap(start,end,boot_address(info),boot_address(info)+sizeof(*info)) ||
        overlap(start,end,boot_address(info->mmap_addr),boot_address(info->mmap_addr)+info->mmap_size) ||
        overlap(start,end,info->fb_addr,info->fb_addr+(uint64_t)info->fb_pitch*info->fb_height) ||
        overlap(start,end,meta,mend);
}

/* A separate entry makes bootstrap reservations host-testable without
 * pretending host linker symbols are guest physical addresses. */
bool pmm_init_ranges(BootInfo *info, uint64_t ks, uint64_t ke)
{
    pmm_ready = false; regions = NULL; region_count = 0;
    memset(&stats,0,sizeof(stats));
    if (!info || !info->mmap_addr || info->mmap_desc_size < sizeof(EfiMemoryDescriptor) ||
        !info->mmap_size || info->mmap_size % info->mmap_desc_size ||
        info->mmap_addr > LIMIT64-info->mmap_size || ks > ke ||
        info->fb_addr > LIMIT64-(uint64_t)info->fb_pitch*info->fb_height) return false;
    uint64_t count = info->mmap_size / info->mmap_desc_size;
    uint64_t bytes = info->mmap_size, nr = 0, total = 0;
    /* Validate before modifying any candidate RAM. Unknown/Device types
     * are not RAM. Zero-length entries are ignored; nonzero must align. */
    for (uint64_t i=0; i<count; i++) {
        EfiMemoryDescriptor d; descriptor(info,i,&d);
        if (!d.num_pages) continue;
        if ((d.phys_addr & PAGE_MASK) || d.num_pages > (LIMIT64-d.phys_addr)/PAGE_SIZE) return false;
        uint64_t end=d.phys_addr+d.num_pages*PAGE_SIZE;
        for (uint64_t j=0; j<i; j++) {
            EfiMemoryDescriptor other; descriptor(info,j,&other);
            if (other.num_pages && overlap(d.phys_addr,end,other.phys_addr,
                                           other.phys_addr+other.num_pages*PAGE_SIZE)) return false;
        }
        if (d.type >= 1 && d.type <= 10) {
            uint64_t size=d.num_pages*PAGE_SIZE;
            if (total > LIMIT64-size) return false;
            total += size;
        }
        if (!reclaimable(&d)) continue;
        uint64_t pieces=(d.phys_addr < LOW_END && end > LOW_END) ? 2 : 1;
        uint64_t bitmap=(d.num_pages+7)/8;
        /* Splitting at the zone boundary can cost one padding byte. */
        uint64_t overhead=pieces*sizeof(PmmRegion)+2;
        if (bytes>LIMIT64-overhead || bitmap>(LIMIT64-bytes-overhead)/2) return false;
        bytes += pieces*sizeof(PmmRegion)+2*bitmap+2;
        nr += pieces;
    }
    if (!nr || bytes > LIMIT64-PAGE_MASK) return false;
    bytes=(bytes+PAGE_MASK)&~PAGE_MASK;
    uint64_t chosen=0;
    for (uint64_t i=0; i<count && !chosen; i++) {
        EfiMemoryDescriptor d; descriptor(info,i,&d);
        if (d.type != 7 || !reclaimable(&d)) continue;
        uint64_t end=d.phys_addr+d.num_pages*PAGE_SIZE;
        if (end > LOW_END) end=LOW_END;
        uint64_t a=d.phys_addr < 0x100000 ? 0x100000 : d.phys_addr;
        for (; a <= end && bytes <= end-a; a+=PAGE_SIZE) {
            if (!excluded(a,a+bytes,info,ks,ke,0,0)) { chosen=a; break; }
        }
    }
    if (!chosen) return false;
    uint8_t *arena=physical_pointer(chosen,bytes);
    if (!arena) return false;
    memset(arena,0,bytes);
    stats.total_bytes=total;
    /* Regions first for native alignment; copied map last need not align. */
    regions=(PmmRegion *)arena;
    uint8_t *bits=arena+nr*sizeof(PmmRegion);
    for (uint64_t i=0; i<count; i++) {
        EfiMemoryDescriptor d; descriptor(info,i,&d);
        if (!d.num_pages || !reclaimable(&d)) continue;
        uint64_t a=d.phys_addr, end=a+d.num_pages*PAGE_SIZE;
        while (a<end) {
            uint64_t stop=(a<LOW_END && end>LOW_END) ? LOW_END : end;
            PmmRegion *r=&regions[region_count++];
            r->base=a; r->pages=(stop-a)/PAGE_SIZE;
            uint64_t n=(r->pages+7)/8;
            r->eligible=bits; r->allocated=bits+n; bits+=2*n;
            /* Most pages are eligible; only exclusions need page inspection. */
            memset(r->eligible,0xff,n);
            for (uint64_t p=0; p<r->pages; p++) {
                uint64_t pa=a+p*PAGE_SIZE;
                if (excluded(pa,pa+PAGE_SIZE,info,ks,ke,chosen,chosen+bytes))
                    set_bit(r->eligible,p,false);
                else if (a<LOW_END) stats.low_free_bytes+=PAGE_SIZE;
                else stats.high_free_bytes+=PAGE_SIZE;
            }
            a=stop;
        }
    }
    memcpy(bits,(const void *)(uintptr_t)info->mmap_addr,info->mmap_size);
    info->mmap_addr=chosen+(uint64_t)(bits-arena);
#ifdef OJJY_PMM_HOST_TEST
    /* Host consumers use the translated copy; real BootInfo keeps its PA. */
    info->mmap_addr=(uint64_t)(uintptr_t)bits;
#endif
    stats.metadata_base=chosen; stats.metadata_bytes=bytes;
    stats.usable_bytes=stats.low_free_bytes+stats.high_free_bytes;
    stats.regions=region_count;
    pmm_ready=true;
    return true;
}

void pmm_init(BootInfo *info)
{
    if (!pmm_init_ranges(info,(uint64_t)(uintptr_t)_kernel_start,(uint64_t)(uintptr_t)_kernel_end)) {
        serial_puts("[PMM] ERROR: invalid map or no low bootstrap metadata storage\n"); return;
    }
    serial_printf("[PMM] %llu MB RAM, %llu MB usable\n",stats.total_bytes>>20,stats.usable_bytes>>20);
    serial_printf("[PMM] Low free=%llu pages, high tracked=%llu pages (disabled); metadata=%llu bytes at %p\n",
                  stats.low_free_bytes/PAGE_SIZE,stats.high_free_bytes/PAGE_SIZE,stats.metadata_bytes,stats.metadata_base);
    /* Compatibility receipt used by existing boot tests (managed capacity). */
    serial_printf("[PMM] Total usable memory: %llu MB\n",stats.usable_bytes>>20);
    serial_printf("[PMM] Free memory:  %llu MB\n",stats.low_free_bytes>>20);
}

uint64_t pmm_alloc_contiguous_zone(uint64_t pages, PmmZone zone)
{
    /* No architecture readiness hook exists yet: HIGH always fails closed. */
    if (!pmm_ready || !pages || pages > LOW_END/PAGE_SIZE ||
        (zone!=PMM_LOW && zone!=PMM_ANY)) return 0;
    for (uint64_t i=0; i<region_count; i++) {
        PmmRegion *r=&regions[i];
        if (r->base>=LOW_END || pages>r->pages) continue;
        /* Next-fit, two non-wrapping ranges. No contiguous run crosses a hole. */
        for (unsigned pass=0; pass<2; pass++) {
            uint64_t first=pass ? 0 : r->cursor, end=r->pages;
            uint64_t run=0;
            for (uint64_t p=first; p<end; p++) {
                if (!bit(r->eligible,p) || bit(r->allocated,p)) { run=0; continue; }
                if (++run<pages) continue;
                uint64_t start=p+1-pages, pa=r->base+start*PAGE_SIZE;
                void *ptr=physical_pointer(pa,pages*PAGE_SIZE);
                if (!ptr) return 0;
                for (uint64_t n=start; n<=p; n++) set_bit(r->allocated,n,true);
                r->cursor=(p+1)%r->pages;
                stats.low_free_bytes-=pages*PAGE_SIZE;
                memset(ptr,0,pages*PAGE_SIZE);
                return pa;
            }
        }
    }
    return 0;
}
uint64_t pmm_alloc_page_zone(PmmZone zone) { return pmm_alloc_contiguous_zone(1,zone); }
uint64_t pmm_alloc_page(void) { return pmm_alloc_page_zone(PMM_LOW); }
uint64_t pmm_alloc_contiguous(uint64_t pages) { return pmm_alloc_contiguous_zone(pages,PMM_LOW); }
void *pmm_alloc_bytes(size_t bytes)
{
    if (!bytes || bytes>LIMIT64-PAGE_MASK) return NULL;
    return (void *)(uintptr_t)pmm_alloc_contiguous((bytes+PAGE_MASK)/PAGE_SIZE);
}
void pmm_free_page(uint64_t pa)
{
    if (!pmm_ready || (pa&PAGE_MASK)) return;
    for (uint64_t i=0; i<region_count; i++) {
        PmmRegion *r=&regions[i];
        if (pa<r->base || (pa-r->base)/PAGE_SIZE>=r->pages) continue;
        uint64_t n=(pa-r->base)/PAGE_SIZE;
        if (!bit(r->eligible,n) || !bit(r->allocated,n)) return;
        set_bit(r->allocated,n,false);
        if (pa<LOW_END) stats.low_free_bytes+=PAGE_SIZE;
        else stats.high_free_bytes+=PAGE_SIZE;
        return;
    }
}
PmmStats pmm_get_stats(void) { return stats; }
uint64_t pmm_get_total_memory(void) { return stats.total_bytes; }
/* Only immediately allocatable pages are exposed through the legacy getter. */
uint64_t pmm_get_free_memory(void) { return stats.low_free_bytes; }
void pmm_print_map(BootInfo *info)
{
    if (!info || info->mmap_desc_size<sizeof(EfiMemoryDescriptor)) return;
    for (uint64_t i=0;i<info->mmap_size/info->mmap_desc_size;i++) {
        EfiMemoryDescriptor d; descriptor(info,i,&d);
        serial_printf("[PMM] base=%p pages=%llu type=%u\n",d.phys_addr,d.num_pages,d.type);
    }
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
