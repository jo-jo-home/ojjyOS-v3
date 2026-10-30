/* Real Intel walker/backend with LOW translated table storage. Jonas Lee. */
#define OJJY_PAGING_HOST_TEST
#include "../kernel/src/paging.c"
#include "../kernel/mm/kvm.c"
#include "test_support.h"
BootInfo *g_boot_info;
unsigned paging_host_bits=40;
bool paging_host_nx=true;
static uint8_t backing[32][4096] __attribute__((aligned(4096)));
static bool owned[32];
static unsigned live,calls,fail_at=~0u;
static PmmResolver published;
static uint64_t ram_base;
#define LOW 0x10000000ULL
#define HIGH 0x100000000ULL
void panic(const char *s) { (void)s;__builtin_trap(); }
void *paging_host_table(uint64_t pa)
{return pa>=LOW && pa-LOW<sizeof(backing) ? (uint8_t *)backing+(pa-LOW):NULL;}
void *pmm_phys_to_virt(uint64_t pa,size_t n)
{return n && pa>=LOW && pa-LOW<sizeof(backing) && n<=sizeof(backing)-(pa-LOW)?paging_host_table(pa):NULL;}
uint64_t pmm_alloc_page(void)
{
    if(calls++==fail_at)return 0;
    for(unsigned i=0;i<32;i++)if(!owned[i]) {
        owned[i]=true;live++;memset(backing[i],0,4096);return LOW+i*4096;
    }
    return 0;
}
void pmm_free_page(uint64_t pa)
{unsigned i=(pa-LOW)/4096;if(i<32&&owned[i]){owned[i]=false;live--;}}
bool pmm_region(uint64_t i,uint64_t *base,uint64_t *pages)
{if(i)return false;*base=ram_base;*pages=1537;return true;}
bool pmm_range_eligible(uint64_t pa,size_t n)
{return n&&pa>=ram_base&&pa-ram_base<1537*4096ULL&&n<=1537*4096ULL-(pa-ram_base)&&!(pa<ram_base+0x201000&&ram_base+0x200000<pa+n);}
bool pmm_high_ready(void){return published!=NULL;}
bool pmm_enable_high(PmmResolver r){if(published)return false;published=r;return true;}
PmmStats pmm_get_stats(void){return(PmmStats){0};}
uint64_t pmm_alloc_page_zone(PmmZone z){(void)z;return 0;}
uint64_t pmm_virt_to_phys(const void *p,size_t n){(void)p;(void)n;return 0;}
static void reset(void)
{
    memset(backing,0,sizeof(backing));memset(owned,0,sizeof(owned));memset(pml4,0,sizeof(pml4));
    live=calls=0;fail_at=~0u;published=NULL;root=0;active=false;pa_bits=40;nx_ready=true;ram_base=HIGH;
}
int main(void)
{
    reset();uint64_t staged[512]={0},pa,flags;
    paging_host_nx=false;CHECK(!cpu_capabilities(),"NX absent fails closed");
    paging_host_nx=true;paging_host_bits=53;CHECK(!cpu_capabilities(),"invalid PA width rejected");
    paging_host_bits=40;CHECK(cpu_capabilities(),"valid NX/PA capabilities accepted");
    CHECK(!paging_tree_map(staged,KPHYS_BASE,HIGH,3,PAGE_USER,40,true)&&!live,"USER bits rejected before allocation");
    CHECK(!paging_tree_map(staged,KPHYS_BASE,HIGH,3,PAGE_NO_EXECUTE,40,false)&&!live,"NX cannot be set before NXE");
    CHECK(!paging_tree_map(staged,KPHYS_BASE,HIGH,3,PAGE_WRITABLE,32,true)&&!live,"high PA beyond CPU width rejected");
    CHECK(!paging_tree_map(staged,0x800000000000ULL,HIGH,3,0,40,true)&&!live,"noncanonical VA rejected");
    CHECK(!paging_tree_map(staged,KPHYS_BASE+1,HIGH,3,0,40,true)&&!live,"unaligned VA refused not rounded");
    bool rolled=true;
    for(unsigned n=0;n<3;n++) {
        fail_at=n;calls=0;
        if(paging_tree_map(staged,KPHYS_BASE+HIGH,HIGH,3,PAGE_NO_EXECUTE,40,true)||live||staged[256])rolled=false;
    }
    CHECK(rolled,"each missing PDPT/PD/PT failure rolls back every link and page");
    fail_at=~0u;
    CHECK(paging_tree_map(staged,KPHYS_BASE+HIGH,HIGH,3,PAGE_WRITABLE|PAGE_NO_EXECUTE,40,true)&&live==3,
          "missing final PT really allocated");
    CHECK(paging_tree_query(staged,KPHYS_BASE+HIGH+123,40,&pa,&flags)&&pa==HIGH+123&&(flags&PAGE_NO_EXECUTE)&&!(flags&PAGE_USER),
          "4KiB query strips NX from physical address and retains supervisor flags");
    CHECK(!paging_tree_map(staged,KPHYS_BASE+HIGH,HIGH+4096,3,PAGE_NO_EXECUTE,40,true),"conflicting existing leaf not overwritten");
    fail_at=calls;
    CHECK(!paging_tree_map(staged,KPHYS_BASE+HIGH+0x200000,HIGH+0x200000,3,PAGE_NO_EXECUTE,40,true)&&live==3&&
          paging_tree_query(staged,KPHYS_BASE+HIGH,40,&pa,&flags)&&pa==HIGH,
          "missing PT failure under existing PD preserves prior mapping and ownership");
    fail_at=~0u;
    CHECK(paging_tree_map(staged,KPHYS_BASE+HIGH+0x200000,HIGH+0x200000,3,PAGE_NO_EXECUTE,40,true)&&live==4,
          "existing PD gains the previously missing final PT");
    CHECK(paging_tree_map(staged,KPHYS_BASE+HIGH+0x400000,HIGH+0x400000,2,PAGE_NO_EXECUTE,40,true)&&
          !paging_tree_map(staged,KPHYS_BASE+HIGH+0x401000,HIGH+0x401000,3,PAGE_NO_EXECUTE,40,true),
          "4KiB mapping cannot overwrite part of live 2MiB leaf");
    for(unsigned i=256;i<512;i++)if(staged[i])paging_tree_destroy(staged[i]&pa_mask(40),1,40);
    CHECK(!live,"unpublished complete subtree freed");reset();
    CHECK(!kvm_x86_init(40,false)&&!live&&!published,"high backend refuses missing NXE");
    CHECK(!kvm_x86_init(32,true)&&!live&&!published,"unrepresentable high RAM rolls back");reset();
    rolled=true;
    for(unsigned n=0;n<5;n++) {
        fail_at=n;
        if(kvm_x86_init(40,true)||live||published||pml4[256])rolled=false;
        reset();
    }
    CHECK(rolled,"every staged high-map OOM reclaims LOW tables without publishing readiness");
    pml4[256]=PAGE_PRESENT;
    CHECK(!kvm_x86_init(40,true)&&!live&&!published&&pml4[256]==PAGE_PRESENT,
          "occupied upper PML4 slot preserves old mapping and rolls staged tree back");
    pml4[256]=0;
    CHECK(kvm_x86_init(40,true)&&live==5&&published,"staged Intel high subtree activated only when complete");
    CHECK(paging_tree_query(pml4,KPHYS_BASE+HIGH+123,40,&pa,&flags)&&pa==HIGH+123&&(flags&PAGE_HUGE)&&
          (flags&PAGE_NO_EXECUTE)&&!(flags&PAGE_USER),"full eligible 2MiB block maps supervisor NX");
    CHECK(!paging_tree_query(pml4,KPHYS_BASE+HIGH+0x200000,40,&pa,&flags),"reserved 4KiB page unmapped");
    CHECK(paging_tree_query(pml4,KPHYS_BASE+HIGH+0x201000,40,&pa,&flags)&&!(flags&PAGE_HUGE),"RAM edge uses 4KiB leaf");
    CHECK(!paging_tree_query(pml4,KPHYS_BASE+0x280000000ULL,40,&pa,&flags),"Device BAR has no cacheable alias");
    CHECK(resolve_high(HIGH,4096)==(void *)(uintptr_t)(KPHYS_BASE+HIGH)&&!resolve_high(HIGH+0x1ff000,8192),
          "checked high alias excludes reservation-crossing span");
    reset();ram_base=1ULL<<47;
    CHECK(!kvm_x86_init(52,true)&&!live&&!published,"CPU-supported PA outside canonical alias span rejected with rollback");
    reset();
    CHECK(kvm_x86_init(52,true)&&published,"52-bit-capable CPU accepted when actual RAM fits alias span");
    return finish();
}
