/* Bounded ARM RAM-only TTBR1 builder; not the full MEM-03 API.
 * All owned table pages are LOW; HIGH frames are never cast to pointers.
 * Created by Jonas Lee. */
#include "kvm.h"
#include "string.h"
#include "serial.h"

#if defined(OJJY_KVM_ARM_TEST) || (defined(__aarch64__) && !defined(OJJY_PAGING_HOST_TEST))
#define TABLE 3ULL
#define AF (1ULL<<10)
#define NORMAL (1ULL<<2)
#define INNER (3ULL<<8)
#define PXN (1ULL<<53)
#define UXN (1ULL<<54)
static uint64_t root, limit;
static unsigned address_bits;
static bool active;

bool kvm_arm_leaf(uint64_t pa, unsigned level, unsigned bits, uint64_t *entry)
{
    if (!entry || (level!=2 && level!=3) || bits<32 || bits>48) return false;
    uint64_t size=level==2 ? 0x200000ULL : PAGE_SIZE;
    if ((pa&(size-1)) || pa>=(1ULL<<bits) || size>(1ULL<<bits)-pa) return false;
    *entry=pa | (level==2 ? 1 : TABLE) | AF | NORMAL | INNER | PXN | UXN;
    return true;
}
static uint64_t table_address(uint64_t entry) { return entry & (limit-1) & ~PAGE_MASK; }
static uint64_t *table(uint64_t pa) { return pmm_phys_to_virt(pa,PAGE_SIZE); }

static void destroy(uint64_t pa, unsigned level)
{
    uint64_t *t=table(pa);
    if (level<3) for (unsigned i=0;i<512;i++)
        if ((t[i]&3)==TABLE) destroy(table_address(t[i]),level+1);
    pmm_free_page(pa);
}
static bool map_leaf(uint64_t pa, unsigned leaf_level)
{
    uint64_t va=KPHYS_BASE+pa, leaf;
    if (!kvm_arm_leaf(pa,leaf_level,address_bits,&leaf)) return false;
    uint64_t *t=table(root);
    for (unsigned level=0;level<leaf_level;level++) {
        unsigned slot=(unsigned)((va>>(39-level*9))&511);
        if (!t[slot]) {
            uint64_t child=pmm_alloc_page();
            if (!child) return false;
            t[slot]=child|TABLE;
        }
        if ((t[slot]&3)!=TABLE) return false;
        t=table(table_address(t[slot]));
    }
    unsigned slot=(unsigned)((va>>(39-leaf_level*9))&511);
    if (t[slot]) return false;
    t[slot]=leaf;
    return true;
}
uint64_t kvm_arm_build(unsigned bits)
{
    if (root || active || bits<32 || bits>47) return 0;
    address_bits=bits; limit=1ULL<<bits;
    root=pmm_alloc_page();
    if (!root) return 0;
    uint64_t base,pages;
    for (uint64_t i=0;pmm_region(i,&base,&pages);i++) {
        if (base<(1ULL<<32)) continue;
        uint64_t end=base+pages*PAGE_SIZE;
        if (end>limit) goto fail;
        for (uint64_t pa=base;pa<end;) {
            if (!pmm_range_eligible(pa,PAGE_SIZE)) { pa+=PAGE_SIZE; continue; }
            unsigned level=3;
            if (!(pa&0x1fffff) && end-pa>=0x200000 && pmm_range_eligible(pa,0x200000)) level=2;
            if (!map_leaf(pa,level)) goto fail;
            pa+=level==2 ? 0x200000 : PAGE_SIZE;
        }
    }
    return root;
fail:
    destroy(root,0); root=0; limit=0; return 0;
}
bool kvm_arm_query(uint64_t va, uint64_t *pa, uint64_t *flags)
{
    if (!root || va<KPHYS_BASE || va-KPHYS_BASE>=limit || !pa || !flags) return false;
    uint64_t *t=table(root);
    for (unsigned level=0;level<4;level++) {
        uint64_t e=t[(va>>(39-level*9))&511];
        if (!(e&1)) return false;
        if (level==3 || (level==2 && (e&3)==1)) {
            uint64_t mask=level==2 ? 0x1fffff : PAGE_MASK;
            *pa=(table_address(e)&~mask)|(va&mask); *flags=e; return true;
        }
        if ((e&3)!=TABLE) return false;
        t=table(table_address(e));
    }
    return false;
}
static void *resolve_high(uint64_t pa, size_t bytes)
{
    if (!active || pa<(1ULL<<32) || pa>=limit || !bytes || bytes>limit-pa ||
        !pmm_range_eligible(pa,bytes)) return NULL;
    uint64_t found,flags;
    if (!kvm_arm_query(KPHYS_BASE+pa,&found,&flags) || found!=pa ||
        !kvm_arm_query(KPHYS_BASE+pa+bytes-1,&found,&flags) || found!=pa+bytes-1) return NULL;
    return (void *)(uintptr_t)(KPHYS_BASE+pa);
}
bool kvm_arm_activate(uint64_t active_root)
{
    if (!root || active || active_root!=root) return false;
    active=true;
    if (!pmm_enable_high(resolve_high)) { active=false; return false; }
    serial_printf("[KVM] ARM RAM-only alias ready base=%p PA_bits=%d root=%p\n",
                  KPHYS_BASE,(int)address_bits,root);
    return true;
}
#else
#include "paging.h"
static uint64_t root;
static unsigned address_bits;
static bool active;
static void *resolve_high(uint64_t pa,size_t bytes)
{
    if(!active || pa<(1ULL<<32) || !bytes || pa>=(1ULL<<47) || bytes>(1ULL<<47)-pa ||
       pa>=(1ULL<<address_bits) || bytes>(1ULL<<address_bits)-pa ||
       !pmm_range_eligible(pa,bytes))return NULL;
    uint64_t found,flags;uint64_t *r=pmm_phys_to_virt(root,PAGE_SIZE);
    if(!paging_tree_query(r,KPHYS_BASE+pa,address_bits,&found,&flags) || found!=pa ||
       !paging_tree_query(r,KPHYS_BASE+pa+bytes-1,address_bits,&found,&flags) || found!=pa+bytes-1)return NULL;
    return (void *)(uintptr_t)(KPHYS_BASE+pa);
}
bool kvm_x86_init(unsigned bits,bool nx)
{
    if(root || active || !nx || bits<32 || bits>52 || pmm_high_ready())return false;
    address_bits=bits;
    root=pmm_alloc_page();if(!root)return false;
    uint64_t *r=pmm_phys_to_virt(root,PAGE_SIZE);
    if(!r)goto fail;
    uint64_t base,pages;
    for(uint64_t i=0;pmm_region(i,&base,&pages);i++) {
        if(base<(1ULL<<32))continue;
        uint64_t end=base+pages*PAGE_SIZE;
        if(end>(1ULL<<bits) || end>(1ULL<<47))goto fail;
        for(uint64_t pa=base;pa<end;) {
            if(!pmm_range_eligible(pa,PAGE_SIZE)){pa+=PAGE_SIZE;continue;}
            unsigned level=(!(pa&0x1fffff) && end-pa>=0x200000 && pmm_range_eligible(pa,0x200000))?2:3;
            if(!paging_tree_map(r,KPHYS_BASE+pa,pa,level,PAGE_WRITABLE|PAGE_NO_EXECUTE,bits,nx))goto fail;
            pa+=level==2?0x200000:PAGE_SIZE;
        }
    }
    if(!paging_install_high(r))goto fail;
    active=true;
    if(!pmm_enable_high(resolve_high))return false; /* boot invariant: PMM initialized, not previously enabled */
    serial_printf("[KVM] Intel RAM-only alias ready base=%p PA_bits=%d root=%p NXE=1\n",KPHYS_BASE,(int)bits,root);
    return true;
fail:
    paging_tree_destroy(root,0,bits);root=0;return false;
}
#endif
bool kvm_high_selftest(void)
{
    PmmStats before=pmm_get_stats();
    if (!before.high_free_bytes) { serial_puts("[KVM] No HIGH RAM on this boot\n"); return true; }
    uint64_t pa=pmm_alloc_page_zone(PMM_HIGH);
    if (!pa) return false;
    volatile uint64_t *p=pmm_phys_to_virt(pa,PAGE_SIZE);
    bool ok=p && pmm_virt_to_phys((const void *)p,PAGE_SIZE)==pa;
    if (ok) for (unsigned i=0;i<PAGE_SIZE/sizeof(*p);i++) if (p[i]) ok=false;
    if (p) {
        for (unsigned i=0;i<PAGE_SIZE/sizeof(*p);i++) p[i]=pa ^ (0x9e3779b97f4a7c15ULL*i);
        for (unsigned i=0;i<PAGE_SIZE/sizeof(*p);i++) if (p[i]!=(pa ^ (0x9e3779b97f4a7c15ULL*i))) ok=false;
        memset((void *)p,0,PAGE_SIZE);
    }
    pmm_free_page(pa);
    PmmStats after=pmm_get_stats();
    ok=ok && before.low_free_bytes==after.low_free_bytes && before.high_free_bytes==after.high_free_bytes;
    serial_printf("[KVM] HIGH zero/write/read/free pa=%p result=%s\n",pa,ok ? "PASS" : "FAIL");
    return ok;
}
