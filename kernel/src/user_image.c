/* Strict static ELF64 loading and checked private-page copying. Jonas Lee. */
#include "user.h"
#include "memory.h"
#include "string.h"

typedef struct {
    uint8_t ident[16]; uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff; uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} PACKED ElfHeader;
typedef struct {
    uint32_t type, flags; uint64_t offset, address, physical, filesz, memsz, align;
} PACKED ElfSegment;
_Static_assert(sizeof(ElfHeader) == 64, "ELF64 header layout");
_Static_assert(sizeof(ElfSegment) == 56, "ELF64 program header layout");

bool user_image_validate(const void *file, size_t size, uint16_t machine, UserImage *out)
{
    if (!file || !out || size < sizeof(ElfHeader) || size > USER_MAX_FILE) return false;
    ElfHeader h; memcpy(&h, file, sizeof(h));
    if (memcmp(h.ident, "\177ELF", 4) || h.ident[4] != 2 || h.ident[5] != 1 || h.ident[6] != 1 ||
        h.type != 2 || h.machine != machine || h.version != 1 || h.flags || h.ehsize != sizeof(h) ||
        h.phentsize != sizeof(ElfSegment) || !h.phnum || h.phnum > USER_MAX_SEGMENTS ||
        h.phoff > size || (size_t)h.phnum * sizeof(ElfSegment) > size - h.phoff ||
        (machine == 183 && (h.entry & 3))) return false;
    UserImage plan; memset(&plan, 0, sizeof(plan)); plan.entry = h.entry;
    bool entry_ok = false;
    for (unsigned i = 0; i < h.phnum; i++) {
        ElfSegment p;
        memcpy(&p, (const uint8_t *)file + h.phoff + i * sizeof(p), sizeof(p));
        if (p.type == 2 || p.type == 3 || p.type == 7) return false; /* dynamic/interpreter/TLS */
        if (p.type == 0x6474E551U && (p.flags & USER_X)) return false;
        if (p.type != 1) continue;
        if (!p.memsz || p.filesz > p.memsz || p.offset > size || p.filesz > size - p.offset ||
            p.address < USER_IMAGE_LOW || p.address >= USER_IMAGE_HIGH ||
            p.memsz > USER_IMAGE_HIGH - p.address || p.physical != p.address ||
            !(p.flags & USER_R) || (p.flags & ~7U) || ((p.flags & USER_W) && (p.flags & USER_X))) return false;
        if (p.align > 1 && ((p.align & (p.align - 1)) || p.align > 0x200000 ||
            ((p.address ^ p.offset) & (p.align - 1)))) return false;
        uint64_t first = ALIGN_DOWN(p.address, PAGE_SIZE);
        uint64_t end = ALIGN_UP(p.address + p.memsz, PAGE_SIZE);
        for (unsigned j = 0; j < plan.count; j++) {
            UserSegment *s = &plan.segments[j];
            if (first < ALIGN_UP(s->address + s->memory_size, PAGE_SIZE) &&
                end > ALIGN_DOWN(s->address, PAGE_SIZE)) return false;
        }
        unsigned pages = (unsigned)((end - first) / PAGE_SIZE);
        if (pages > USER_MAX_PAGES - USER_STACK_PAGES - plan.pages) return false;
        plan.pages += pages;
        UserSegment *s = &plan.segments[plan.count++];
        s->address = p.address; s->offset = p.offset;
        s->file_size = p.filesz; s->memory_size = p.memsz; s->flags = p.flags;
        if ((p.flags & USER_X) && h.entry >= p.address && h.entry - p.address < p.filesz) entry_ok = true;
    }
    if (!entry_ok || !plan.count) return false;
    *out = plan;
    return true;
}

static const UserPage *find_page(const UserSpace *space, uint64_t address)
{
    uint64_t page = ALIGN_DOWN(address, PAGE_SIZE);
    if (!space || space->count > USER_MAX_PAGES) return NULL;
    for (unsigned i = 0; i < space->count; i++) if (space->pages[i].address == page) return &space->pages[i];
    return NULL;
}

bool user_range_valid(const UserSpace *space, uint64_t address, size_t size, uint32_t flags)
{
    if (!space || space->count > USER_MAX_PAGES) return false;
    if (!size) return true;
    if (address < USER_BASE || address >= USER_STACK_TOP || size > USER_STACK_TOP - address) return false;
    while (size) {
        const UserPage *page = find_page(space, address);
        if (!page || (page->flags & flags) != flags) return false;
        size_t chunk = MIN(size, PAGE_SIZE - (address & PAGE_MASK));
        address += chunk; size -= chunk;
    }
    return true;
}

bool user_copy_from(const UserSpace *space, void *dest, uint64_t source, size_t size)
{
    if ((!dest && size) || !user_range_valid(space, source, size, USER_R)) return false;
    uint8_t *to = dest;
    while (size) {
        const UserPage *page = find_page(space, source);
        size_t offset = source & PAGE_MASK, chunk = MIN(size, PAGE_SIZE - offset);
        memcpy(to, (const void *)(uintptr_t)(page->physical + offset), chunk);
        to += chunk; source += chunk; size -= chunk;
    }
    return true;
}

void user_space_destroy(UserSpace *space)
{
    if (!space) return;
    for (unsigned i = 0; i < space->count && i < USER_MAX_PAGES; i++) {
        memset((void *)(uintptr_t)space->pages[i].physical, 0, PAGE_SIZE);
        pmm_free_page(space->pages[i].physical);
    }
    memset(space, 0, sizeof(*space));
}

static bool add_page(UserSpace *space, uint64_t address, uint32_t flags)
{
    if (space->count == USER_MAX_PAGES) return false;
    uint64_t physical = pmm_alloc_page();
    if (!physical) return false;
    memset((void *)(uintptr_t)physical, 0, PAGE_SIZE);
    space->pages[space->count++] = (UserPage){address, physical, flags};
    return true;
}

bool user_space_load(UserSpace *space, const void *file, size_t size, const UserImage *image)
{
    /* Revalidate the plan instead of trusting caller-supplied segment metadata. */
    UserImage checked;
    if (!space || !image || space->count ||
        !user_image_validate(file, size, 183, &checked) ||
        checked.entry != image->entry || checked.count != image->count || checked.pages != image->pages) return false;
    /* Compare fields, not struct padding (assignment need not preserve it). */
    for (unsigned i = 0; i < checked.count; i++) {
        const UserSegment *a = &checked.segments[i], *b = &image->segments[i];
        if (a->address != b->address || a->offset != b->offset || a->file_size != b->file_size ||
            a->memory_size != b->memory_size || a->flags != b->flags) return false;
    }
    image = &checked;
    for (unsigned i = 0; i < image->count; i++) {
        const UserSegment *s = &image->segments[i];
        uint64_t end = ALIGN_UP(s->address + s->memory_size, PAGE_SIZE);
        for (uint64_t a = ALIGN_DOWN(s->address, PAGE_SIZE); a < end; a += PAGE_SIZE) {
            if (!add_page(space, a, s->flags)) goto fail;
        }
        size_t done = 0;
        while (done < s->file_size) {
            uint64_t address = s->address + done;
            const UserPage *page = find_page(space, address);
            size_t offset = address & PAGE_MASK;
            size_t chunk = MIN(s->file_size - done, PAGE_SIZE - offset);
            memcpy((void *)(uintptr_t)(page->physical + offset), (const uint8_t *)file + s->offset + done, chunk);
            done += chunk;
        }
    }
    for (unsigned i = 0; i < USER_STACK_PAGES; i++) {
        if (!add_page(space, USER_STACK_TOP - (i + 1) * PAGE_SIZE, USER_R | USER_W)) goto fail;
    }
    return true;
fail:
    user_space_destroy(space);
    return false;
}
