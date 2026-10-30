/* Checked x86-64 table walking; all dynamic tables remain LOW.
 * Created by Jonas Lee. HW-13/MEM-03 preparation, not an Intel EL0 runner. */
#include "paging.h"
#include "memory.h"
#include "serial.h"
#include "string.h"
#include "boot_info.h"
#include "panic.h"
#include "../mm/kvm.h"

typedef uint64_t PageTable[512];
static PageTable pml4 __attribute__((aligned(4096)));
static PageTable pdpt __attribute__((aligned(4096)));
static PageTable pd[4] __attribute__((aligned(4096)));
static unsigned pa_bits;
static bool nx_ready;
static uint64_t pa_mask(unsigned bits) { return ((1ULL<<bits)-1)&~PAGE_MASK; }
static bool canonical(uint64_t va) { return va<0x800000000000ULL || va>=0xffff800000000000ULL; }
static uint64_t *table_pointer(uint64_t pa)
{
#ifdef OJJY_PAGING_HOST_TEST
    extern void *paging_host_table(uint64_t physical);
    return paging_host_table(pa);
#else
    return (uint64_t *)(uintptr_t)pa;
#endif
}

/* NX/PA bits are capability checks, never guessed from requested RAM. */
static bool cpu_capabilities(void)
{
#ifdef OJJY_PAGING_HOST_TEST
    extern unsigned paging_host_bits;
    extern bool paging_host_nx;
    pa_bits=paging_host_bits; nx_ready=paging_host_nx;
#else
    uint32_t a,b,c,d;
    __asm__ volatile("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(0x80000000u),"c"(0));
    if (a<0x80000008u) return false;
    __asm__ volatile("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(0x80000008u),"c"(0));
    pa_bits=a&255;
    __asm__ volatile("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(0x80000001u),"c"(0));
    if (!(d&(1u<<20))) return false;
    __asm__ volatile("rdmsr" : "=a"(a),"=d"(d) : "c"(0xc0000080u));
    a|=1u<<11;
    __asm__ volatile("wrmsr" : : "a"(a),"d"(d),"c"(0xc0000080u) : "memory");
    __asm__ volatile("rdmsr" : "=a"(a),"=d"(d) : "c"(0xc0000080u));
    nx_ready=(a&(1u<<11))!=0;
#endif
    return pa_bits>=32 && pa_bits<=52 && nx_ready;
}

bool paging_tree_map(uint64_t *root,uint64_t va,uint64_t pa,unsigned level,
                     uint64_t flags,unsigned bits,bool nx)
{
    if (!root || !canonical(va) || bits<32 || bits>52 || (level!=2 && level!=3)) return false;
    uint64_t size=level==2 ? 0x200000 : PAGE_SIZE;
    const uint64_t allowed=PAGE_PRESENT|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_CACHE_DISABLE|PAGE_NO_EXECUTE;
    if ((va&(size-1)) || (pa&(size-1)) || pa>=(1ULL<<bits) || size>(1ULL<<bits)-pa ||
        (flags&~allowed) || ((flags&PAGE_NO_EXECUTE) && !nx)) return false;
    uint64_t *t=root,*links[3];uint64_t allocated[3];unsigned count=0;
    for (unsigned l=0;l<level;l++) {
        uint64_t *e=&t[(va>>(39-l*9))&511];
        if (!(*e&PAGE_PRESENT)) {
            if (*e) goto fail;
            uint64_t child=pmm_alloc_page();
            if (!child) goto fail;
            if ((child&PAGE_MASK) || child>=(1ULL<<32)) {pmm_free_page(child);goto fail;}
            uint64_t *p=table_pointer(child);
            if (!p) {pmm_free_page(child);goto fail;}
            memset(p,0,PAGE_SIZE);
            allocated[count]=child;links[count++]=e;
            *e=child|PAGE_PRESENT|PAGE_WRITABLE;
        }
        if ((*e&(PAGE_HUGE|PAGE_USER)) || (*e&~(pa_mask(bits)|0xfffULL|PAGE_NO_EXECUTE))) goto fail;
        t=table_pointer(*e&pa_mask(bits));
        if (!t) goto fail;
    }
    uint64_t *entry=&t[(va>>(39-level*9))&511];
    uint64_t desired=pa|flags|PAGE_PRESENT|(level==2 ? PAGE_HUGE : 0);
    if (*entry && *entry!=desired) goto fail;
    *entry=desired;return true;
fail:
    while (count) {--count;*links[count]=0;pmm_free_page(allocated[count]);}
    return false;
}
bool paging_tree_query(uint64_t *root,uint64_t va,unsigned bits,uint64_t *pa,uint64_t *flags)
{
    if (!root || !canonical(va) || bits<32 || bits>52 || !pa || !flags) return false;
    uint64_t *t=root;
    for (unsigned l=0;l<4;l++) {
        uint64_t e=t[(va>>(39-l*9))&511];
        if (!(e&PAGE_PRESENT)) return false;
        if (l==3 || (l==2 && (e&PAGE_HUGE))) {
            uint64_t mask=l==2 ? 0x1fffff : PAGE_MASK;
            *pa=(e&pa_mask(bits)&~mask)|(va&mask);*flags=e;return true;
        }
        if (e&PAGE_HUGE) return false;
        t=table_pointer(e&pa_mask(bits));if (!t)return false;
    }
    return false;
}
void paging_tree_destroy(uint64_t pa,unsigned level,unsigned bits)
{
    uint64_t *t=table_pointer(pa);
    if (t && level<3)for(unsigned i=0;i<512;i++)
        if ((t[i]&PAGE_PRESENT) && !(t[i]&PAGE_HUGE))paging_tree_destroy(t[i]&pa_mask(bits),level+1,bits);
    pmm_free_page(pa);
}
bool paging_install_high(const uint64_t *staged)
{
    if (!nx_ready || !staged) return false;
    for(unsigned i=0;i<256;i++)if(staged[i])return false;
    for(unsigned i=256;i<512;i++)if(staged[i] && pml4[i])return false;
    for(unsigned i=256;i<512;i++)if(staged[i])pml4[i]=staged[i];
    paging_flush_tlb();return true;
}
bool paging_map(uint64_t va,uint64_t pa,uint64_t flags)
{
    if (!paging_tree_map(pml4,va,pa,3,flags,pa_bits,nx_ready))return false;
    paging_invalidate(va);return true;
}
void paging_unmap(uint64_t va)
{
    if ((va&PAGE_MASK) || !canonical(va))return;
    uint64_t *t=pml4;
    for(unsigned l=0;l<3;l++) {
        uint64_t e=t[(va>>(39-l*9))&511];
        if (!(e&PAGE_PRESENT) || (e&PAGE_HUGE))return;
        t=table_pointer(e&pa_mask(pa_bits));if(!t)return;
    }
    t[(va>>12)&511]=0;paging_invalidate(va);
}
void paging_invalidate(uint64_t va)
{
#ifndef OJJY_PAGING_HOST_TEST
    __asm__ volatile("invlpg (%0)" : : "r"(va) : "memory");
#else
    (void)va;
#endif
}
void paging_flush_tlb(void)
{
#ifndef OJJY_PAGING_HOST_TEST
    write_cr3(read_cr3());
#endif
}
void paging_init(void)
{
    if (!cpu_capabilities())panic("Intel requires checked PA width and NXE");
    memset(pml4,0,sizeof(pml4));memset(pdpt,0,sizeof(pdpt));memset(pd,0,sizeof(pd));
    pml4[0]=(uint64_t)(uintptr_t)pdpt|PAGE_PRESENT|PAGE_WRITABLE;
    for(unsigned i=0;i<4;i++) {
        pdpt[i]=(uint64_t)(uintptr_t)pd[i]|PAGE_PRESENT|PAGE_WRITABLE;
        for(unsigned j=0;j<512;j++)pd[i][j]=((uint64_t)i<<30)|((uint64_t)j<<21)|PAGE_PRESENT|PAGE_WRITABLE|PAGE_HUGE;
    }
#ifndef OJJY_PAGING_HOST_TEST
    write_cr3((uint64_t)(uintptr_t)pml4);
#endif
    if (g_boot_info) {
        uint64_t fb=g_boot_info->fb_addr,size=(uint64_t)g_boot_info->fb_pitch*g_boot_info->fb_height;
        if (size && (fb>~0ULL-size || fb+size>~0ULL-PAGE_MASK))panic("Intel framebuffer range overflow");
        uint64_t end=ALIGN_UP(fb+size,PAGE_SIZE);
        for(uint64_t p=MAX(ALIGN_DOWN(fb,PAGE_SIZE),1ULL<<32);p<end;p+=PAGE_SIZE)
            if(!paging_map(p,p,PAGE_WRITABLE|PAGE_NO_EXECUTE|PAGE_CACHE_DISABLE))panic("Cannot map Intel framebuffer");
    }
    serial_printf("[PAGING] Page tables loaded, identity low 4GiB; PA_bits=%d NXE=%d\n",(int)pa_bits,(int)nx_ready);
    if(!kvm_x86_init(pa_bits,nx_ready) || !kvm_high_selftest())panic("Intel high RAM mapping failed");
}
