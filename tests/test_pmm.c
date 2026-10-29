/* Runtime PMM boot/zone/reservation regressions. Created by Jonas Lee. */
#include "memory.h"
#include "string.h"
#include "test_support.h"

extern void *malloc(unsigned long);
extern void free(void *);
char _kernel_start[1], _kernel_end[1];
#define BASE 0x100000ULL
#define BACKING (32ULL<<20)
static uint8_t *ram;
static uint8_t low_top[8192];
static uint8_t high_store[64*4096] __attribute__((aligned(4096)));
uint64_t pmm_host_address(const void *p)
{
    uint64_t a=(uint64_t)(uintptr_t)p, b=(uint64_t)(uintptr_t)ram;
    uint64_t high=(uint64_t)(uintptr_t)high_store;
    if (a>=high && a-high<sizeof(high_store)) return (1ULL<<32)+a-high;
    return a>=b && a-b<BACKING ? BASE+a-b : 0;
}
void *pmm_host_pointer(uint64_t pa, size_t size)
{
    uint64_t edge=(1ULL<<32)-sizeof(low_top);
    if (pa>=edge && pa-edge<sizeof(low_top) && size<=sizeof(low_top)-(pa-edge))
        return low_top+(pa-edge);
    if (pa<BASE || pa-BASE>BACKING || size>BACKING-(pa-BASE)) return NULL;
    return ram+(pa-BASE);
}
static EfiMemoryDescriptor map[300];
static BootInfo info;
static void entry(unsigned i,uint64_t base,uint64_t pages,unsigned type)
{ map[i]=(EfiMemoryDescriptor){.type=type,.phys_addr=base,.num_pages=pages}; }
static void setup(unsigned count)
{
    memset(map,0,sizeof(map)); memset(&info,0,sizeof(info));
    memset(ram,0xcc,BACKING);
    info.mmap_addr=(uint64_t)(uintptr_t)map;
    info.mmap_size=count*sizeof(map[0]); info.mmap_desc_size=sizeof(map[0]);
}
static bool boot(void) { return pmm_init_ranges(&info,BASE+8192,BASE+16384); }
static uint32_t rng=1234567;
static uint32_t random32(void) { rng=rng*1664525u+1013904223u; return rng; }
static void basic(void)
{
    CHECK(pmm_alloc_page()==0,"no allocation before bootstrap");
    setup(6);
    entry(0,1ULL<<32, (64ULL<<30)/4096,7);
    entry(1,BASE,64,7); entry(2,BASE+128*4096,64,4);
    entry(3,BASE+64*4096,64,11); /* Device hole */
    entry(4,0x100000000000ULL,1,11); /* huge MMIO must not size metadata */
    entry(5,BASE+200*4096,0,7);
    info.fb_addr=BASE+20*4096+7; info.fb_pitch=64; info.fb_height=1;
    /* 64 GiB's bitmap needs more LOW storage than this small map supplies. */
    CHECK(!boot(),"metadata bootstrap exhaustion fails safely");
    CHECK(!pmm_alloc_page(),"failed bootstrap leaves allocator disabled");
    setup(4); entry(0,1ULL<<32,(64ULL<<30)/4096,7);
    entry(1,BASE,BACKING/4096,7);
    entry(2,0x100000000000ULL,1,11);
    entry(3,0x3000000000ULL,1,6); map[3].attribute=1ULL<<63;
    CHECK(boot(),"64 GiB high map bootstraps in real low backing");
    PmmStats s=pmm_get_stats();
    CHECK(s.total_bytes==(64ULL<<30)+BACKING+4096,"totals count RAM, not holes or MMIO span");
    CHECK(s.high_free_bytes==(64ULL<<30),"all 64 GiB high pages counted");
    CHECK(s.metadata_bytes<(5ULL<<20),"runtime two-bit metadata stays small");
    CHECK(info.mmap_addr!=(uint64_t)(uintptr_t)map,"descriptor copy is owned");
    CHECK(!pmm_alloc_page_zone(PMM_HIGH),"high allocation fails closed");
    CHECK(!pmm_alloc_contiguous_zone(1,(PmmZone)99),"invalid zone refused");
    uint64_t pa=pmm_alloc_page_zone(PMM_ANY);
    CHECK(pa && pa<(1ULL<<32),"ANY still returns LOW");
    uint8_t *p=pmm_host_pointer(pa,4096);
    bool zero=true; for (unsigned i=0;i<4096;i++) if (p[i]) zero=false;
    CHECK(zero,"allocated page is zeroed through translated host backing");
    pmm_free_page(pa);
    CHECK(pmm_get_free_memory()==s.low_free_bytes,"free restores low count");
    pmm_free_page(pa); pmm_free_page(pa+1); pmm_free_page(s.metadata_base);
    pmm_free_page(BASE+8192); pmm_free_page(1ULL<<32);
    CHECK(pmm_get_free_memory()==s.low_free_bytes,"bad/double/reserved/high frees cannot create pages");
    CHECK(!pmm_alloc_bytes(0) && !pmm_alloc_bytes(~(size_t)0),"zero/overflow byte allocation refused");
    CHECK(!pmm_alloc_contiguous(~0ULL),"overflow page allocation refused");
}
static void exhaust(void)
{
    setup(5); entry(0,BASE,64,7); entry(1,BASE+128*4096,64,3);
    entry(2,BASE+64*4096,64,11); entry(3,1ULL<<32,5,7);
    entry(4,BASE+200*4096,2,4); map[4].attribute=1ULL<<63;
    info.fb_addr=BASE+20*4096+1; info.fb_pitch=4096; info.fb_height=1;
    CHECK(boot(),"hole/framebuffer/runtime map bootstraps");
    PmmStats before=pmm_get_stats(); uint64_t held[256]; unsigned n=0; bool valid=true;
    CHECK(before.low_free_bytes==123*4096,"independent page accounting: 128 minus metadata/kernel/framebuffer");
    uint64_t pa;
    while ((pa=pmm_alloc_page()) && n<256) {
        held[n++]=pa;
        if (pa>=(1ULL<<32) || (pa>=BASE+64*4096 && pa<BASE+128*4096) ||
            pa==BASE+20*4096 || pa==BASE+21*4096 || pa==BASE+8192 || pa==BASE+12288) valid=false;
    }
    CHECK(valid && (uint64_t)n*4096==before.low_free_bytes,"allocate everything excludes every hole/reservation");
    CHECK(!pmm_alloc_contiguous(2),"exhaustion never returns high RAM");
    for (unsigned i=0;i<n;i++) pmm_free_page(held[i]);
    CHECK(pmm_get_free_memory()==before.low_free_bytes,"free everything restores exact count");
    pmm_free_page(BASE+70*4096);
    CHECK(pmm_get_free_memory()==before.low_free_bytes,"free inside Device hole ignored");
    CHECK(pmm_get_stats().high_free_bytes==5*4096,"low exhaustion does not change high count");
    pa=pmm_alloc_contiguous(4); CHECK(pa!=0,"contiguous allocation after frees");
    for (unsigned i=0;i<4;i++) pmm_free_page(pa+i*4096);
}
static void malformed(void)
{
    setup(1); entry(0,BASE,10,7); info.mmap_desc_size=0;
    CHECK(!boot(),"zero stride rejected");
    setup(1); entry(0,BASE,10,7); info.mmap_size--;
    CHECK(!boot(),"truncated record rejected");
    setup(1); entry(0,BASE+1,10,7);
    CHECK(!boot(),"unaligned physical interval rejected");
    setup(1); entry(0,BASE,~0ULL,7);
    CHECK(!boot(),"page count overflow rejected");
    setup(2); entry(0,BASE,10,7); entry(1,BASE+4096,2,11);
    CHECK(!boot(),"overlapping Device/RAM rejected before metadata write");
    setup(1); entry(0,BASE,0,7);
    CHECK(!boot(),"all-empty map refused");
    setup(1); entry(0,BASE,10,7); info.fb_addr=~0ULL-5; info.fb_pitch=100; info.fb_height=1;
    CHECK(!boot(),"framebuffer extent overflow rejected");
    /* Descriptor strides need not be naturally aligned or exactly sizeof. */
    uint8_t odd[2*49]; EfiMemoryDescriptor d={.type=7,.phys_addr=BASE,.num_pages=64};
    memcpy(odd,&d,sizeof(d)); d.phys_addr=1ULL<<32; d.num_pages=3;
    memcpy(odd+49,&d,sizeof(d)); memset(&info,0,sizeof(info));
    info.mmap_addr=(uint64_t)(uintptr_t)odd; info.mmap_size=98; info.mmap_desc_size=49;
    CHECK(boot(),"odd-sized padded descriptors read with memcpy");
}
static void boundary_and_storage(void)
{
    setup(3); entry(0,BASE,128,7);
    entry(1,(1ULL<<32)-8192,4,7); entry(2,BASE+128*4096,3,9);
    /* Put the incoming map in guest LOW storage. It and framebuffer pages
     * must survive bootstrap even if reported conventional by firmware. */
    uint8_t *saved=ram+32*4096;
    memcpy(saved,map,3*sizeof(map[0])); info.mmap_addr=(uint64_t)(uintptr_t)saved;
    CHECK(boot(),"cross-4-GiB interval split and descriptor retention");
    PmmStats s=pmm_get_stats();
    CHECK(s.high_free_bytes==8192 && s.regions==3,"cross-boundary high count exact");
    CHECK(memcmp((void *)(uintptr_t)info.mmap_addr,saved,3*sizeof(map[0]))==0,"owned map copy preserves all descriptor bytes");
    uint64_t held[128]; unsigned n=0; bool safe=true, end_seen=false; uint64_t pa;
    while (n<128 && (pa=pmm_alloc_page())) {
        if (pa==BASE+32*4096) safe=false;
        if (pa==(1ULL<<32)-4096) end_seen=true;
        if (pa>=(1ULL<<32)) safe=false;
        held[n++]=pa;
    }
    CHECK(safe,"allocator never reuses incoming map storage");
    CHECK(end_seen,"last LOW page at 4 GiB minus one page is allocatable, never the HIGH neighbor");
    for(unsigned i=0;i<n;i++) pmm_free_page(held[i]);
}
static void many_and_fuzz(void)
{
    bool ok=true;
    for (unsigned round=0;round<100;round++) {
        setup(260);
        for (unsigned i=0;i<260;i++)
            entry(i,BASE+(uint64_t)i*16*4096,8,(random32()%5==0)?11:7);
        entry(0,BASE,64,7); /* enough metadata bootstrap space */
        for (unsigned i=1;i<4;i++) entry(i,0,0,7);
        /* Shuffle descriptors to prove input ordering is irrelevant. */
        for (unsigned i=0;i<260;i++) {
            unsigned j=random32()%260;
            EfiMemoryDescriptor t=map[i]; map[i]=map[j]; map[j]=t;
        }
        if (!boot()) { ok=false; break; }
        uint64_t start=pmm_get_free_memory(), held[64]={0};
        for (unsigned op=0;op<2000;op++) {
            unsigned slot=random32()%64;
            if (held[slot]) { pmm_free_page(held[slot]); held[slot]=0; }
            else {
                uint64_t pa=pmm_alloc_page();
                if (!pa || pa>=(1ULL<<32)) { ok=false; break; }
                for (unsigned j=0;j<64;j++) if (held[j]==pa) ok=false;
                held[slot]=pa;
            }
        }
        for (unsigned i=0;i<64;i++) if (held[i]) pmm_free_page(held[i]);
        if (pmm_get_free_memory()!=start) ok=false;
    }
    CHECK(ok,"100 shuffled 260-region maps, 200000 random alloc/free operations conserve ownership/counts");
}
static void *high_pointer(uint64_t pa,size_t bytes)
{
    return pa>=(1ULL<<32) && pa-(1ULL<<32)<sizeof(high_store) &&
        bytes<=sizeof(high_store)-(pa-(1ULL<<32)) ? high_store+(pa-(1ULL<<32)) : NULL;
}
static void high_zones(void)
{
    setup(4); entry(0,BASE,128,7);entry(1,1ULL<<32,64,7);
    entry(2,(1ULL<<32)+sizeof(high_store),1,11);
    entry(3,(1ULL<<32)+sizeof(high_store)+4096,1,6);
    CHECK(boot(),"high backing map bootstraps");
    CHECK(!pmm_phys_to_virt(1ULL<<32,4096) && !pmm_high_ready(),"high translation fails before activation");
    CHECK(!pmm_enable_high(NULL) && pmm_enable_high(high_pointer) && !pmm_enable_high(high_pointer),
          "resolver readiness checked and one-way");
    PmmStats before=pmm_get_stats();
    memset(high_store,0xcc,sizeof(high_store));
    uint64_t pa=pmm_alloc_contiguous_zone(4,PMM_ANY);
    CHECK(pa==(1ULL<<32) && pmm_is_allocated(pa) && !pmm_is_allocated(pa+4*4096),"ANY chooses HIGH with ownership");
    bool zero=true;for(unsigned i=0;i<4*4096;i++) if(high_store[i])zero=false;
    CHECK(zero && high_store[4*4096]==0xcc,"HIGH zeroing stays in owned range");
    CHECK(pmm_phys_to_virt(pa+17,123)==high_store+17 && pmm_virt_to_phys(high_store+17,123)==pa+17,
          "checked translation roundtrip including offset");
    CHECK(!pmm_phys_to_virt(pa+sizeof(high_store)-1,2) && !pmm_phys_to_virt(~0ULL,4096) &&
          !pmm_phys_to_virt(pa,0) && !pmm_phys_to_virt((1ULL<<32)+sizeof(high_store),1),
          "range end, overflow, zero and Device rejected");
    uint64_t low=pmm_alloc_page(); CHECK(low && low<(1ULL<<32),"legacy remains LOW after readiness");
    pmm_free_page(low); for(unsigned i=0;i<4;i++)pmm_free_page(pa+i*4096);
    void *buf=pmm_alloc_mapped_bytes(8193); uint64_t addr=pmm_virt_to_phys(buf,8193);
    CHECK(buf && addr>=(1ULL<<32) && pmm_get_stats().high_free_bytes==before.high_free_bytes-3*4096,
          "mapped byte buffer selects HIGH and rounds pages");
    for(unsigned i=0;i<3;i++)pmm_free_page(addr+i*4096);
    uint64_t held[64];unsigned n=0;while(n<64 && (pa=pmm_alloc_page_zone(PMM_HIGH)))held[n++]=pa;
    CHECK(n==64 && !pmm_alloc_page_zone(PMM_HIGH),"HIGH exhaustion never falls back");
    low=pmm_alloc_page_zone(PMM_ANY); CHECK(low && low<(1ULL<<32),"ANY falls back LOW only after HIGH exhaustion");
    pmm_free_page(low);for(unsigned i=0;i<n;i++)pmm_free_page(held[i]);
    bool valid=true;uint64_t slots[32]={0};
    for(unsigned op=0;op<20000;op++) {
        unsigned j=random32()%32;
        if(slots[j]) {pmm_free_page(slots[j]);slots[j]=0;}
        else {
            slots[j]=pmm_alloc_page_zone((PmmZone)(random32()%3));
            if(!slots[j])valid=false;
            for(unsigned k=0;k<32;k++)if(k!=j && slots[j] && slots[j]==slots[k])valid=false;
        }
    }
    for(unsigned j=0;j<32;j++)if(slots[j])pmm_free_page(slots[j]);
    PmmStats after=pmm_get_stats();
    CHECK(valid && after.low_free_bytes==before.low_free_bytes && after.high_free_bytes==before.high_free_bytes,
          "20000 mixed-zone operations conserve both free counts and unique ownership");
    CHECK(pmm_get_free_memory()==before.low_free_bytes+before.high_free_bytes,"active free getter includes HIGH");
}
int main(void)
{
    ram=malloc(BACKING); if (!ram) return 2;
    basic(); exhaust(); malformed(); boundary_and_storage(); many_and_fuzz(); high_zones(); free(ram);
    return finish();
}
