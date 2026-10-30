/* Stress diagnostics on translated physical storage. Created by Jonas Lee. */
#include "memory_stress.h"
#include "memory.h"
#include "string.h"
#include "test_support.h"
#define HIGH 0x100000000ULL
#define SIZE (8u<<20)
static uint8_t data[SIZE],meta[4096];
static bool pages[SIZE/4096],metadata,ready=true,corrupt,duplicate;
static unsigned calls,fail_at=~0u;
PmmStats pmm_get_stats(void)
{
    uint64_t free=0;for(unsigned i=0;i<SIZE/4096;i++)if(!pages[i])free+=4096;
    return(PmmStats){.low_free_bytes=(1u<<20)-(metadata?4096:0),.high_free_bytes=free};
}
bool pmm_high_ready(void){return ready;}
void *pmm_alloc_bytes(size_t n)
{if(n>sizeof(meta)||metadata)return NULL;metadata=true;memset(meta,0,sizeof(meta));return meta;}
uint64_t pmm_virt_to_phys(const void *p,size_t n){return p==meta&&n<=sizeof(meta)?0x200000:0;}
uint64_t pmm_alloc_contiguous_zone(uint64_t n,PmmZone z)
{
    (void)z;
    if(calls++==fail_at)return 0;
    if(duplicate&&calls==2)return HIGH;
    if(n!=512)return 0;
    for(unsigned b=0;b<SIZE/4096;b+=512)if(!pages[b]) {
        for(unsigned i=0;i<512;i++)pages[b+i]=true;
        memset(data+b*4096,0,2u<<20);return HIGH+b*4096;
    }
    return 0;
}
void *pmm_phys_to_virt(uint64_t pa,size_t n)
{return pa>=HIGH&&pa-HIGH<SIZE&&n<=SIZE-(pa-HIGH)?data+(pa-HIGH):NULL;}
void pmm_free_page(uint64_t pa)
{
    if(pa==0x200000){metadata=false;return;}
    if(pa>=HIGH&&pa-HIGH<SIZE&&!(pa&4095))pages[(pa-HIGH)/4096]=false;
}
void memory_stress_after_write(void){if(corrupt)data[0]^=1;}
static void reset(void)
{memset(pages,0,sizeof(pages));metadata=false;ready=true;corrupt=duplicate=false;calls=0;fail_at=~0u;}
int main(void)
{
    MemoryStressResult r;reset();
    CHECK(memory_stress(SIZE,&r)&&r.peak_owned==SIZE&&r.high_peak==SIZE&&r.low_peak==0&&r.restored,
          "all distinct extents simultaneously held and freed");
    CHECK(r.written_pages==SIZE/4096&&r.verified_pages==SIZE/4096&&r.high_min==HIGH&&r.high_max==HIGH+SIZE-1,
          "every held page patterned and verified, actual high PA bounds");
    CHECK(pmm_get_stats().high_free_bytes==SIZE&&!metadata,"all pages plus LOW metadata restored");
    reset();corrupt=true;CHECK(!memory_stress(SIZE,&r)&&r.restored&&!r.passed&&r.verified_pages==0,
                             "injected corruption detected after all writes; no leaks");
    bool ok=true;
    for(unsigned n=0;n<4;n++) {
        reset();fail_at=n;
        if(memory_stress(SIZE,&r)||!r.restored||metadata||pmm_get_stats().high_free_bytes!=SIZE)ok=false;
    }
    CHECK(ok,"each partial allocation failure reclaims held extents and bookkeeping");
    reset();duplicate=true;CHECK(!memory_stress(SIZE,&r)&&r.restored,"duplicate PA ownership cannot fake peak");
    reset();ready=false;CHECK(!memory_stress(SIZE,&r)&&!calls,"no HIGH readiness refuses diagnostic");
    reset();CHECK(!memory_stress(16u<<20,&r)&&!calls&&!metadata,"insufficient real capacity refused without claiming success");
    CHECK(!memory_stress(0,&r)&&!memory_stress(123,&r)&&!memory_stress(~0ULL,&r),"zero, unaligned and huge request refused");
    return finish();
}
