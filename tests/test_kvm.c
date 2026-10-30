/* Actual ARM builder on translated host tables. Created by Jonas Lee. */
#define OJJY_KVM_ARM_TEST
#include "../kernel/mm/kvm.c"
#include "test_support.h"
static uint8_t backing[32][PAGE_SIZE] __attribute__((aligned(4096)));
static bool used[32];
static unsigned calls, fail_at=~0u, live;
static PmmResolver published;
#define LOW 0x10000000ULL
#define HIGH 0x100000000ULL
uint64_t pmm_alloc_page(void)
{
    if (calls++==fail_at) return 0;
    for (unsigned i=0;i<32;i++) if (!used[i]) {
        used[i]=true; live++; memset(backing[i],0,PAGE_SIZE); return LOW+i*PAGE_SIZE;
    }
    return 0;
}
void pmm_free_page(uint64_t pa)
{
    unsigned i=(pa-LOW)/PAGE_SIZE;
    if (i<32 && used[i]) { used[i]=false; live--; }
}
void *pmm_phys_to_virt(uint64_t pa,size_t bytes)
{
    return pa>=LOW && pa-LOW<sizeof(backing) && bytes<=sizeof(backing)-(pa-LOW)
        ? (uint8_t *)backing+(pa-LOW) : NULL;
}
uint64_t pmm_virt_to_phys(const void *p,size_t n) { (void)p;(void)n;return 0; }
bool pmm_region(uint64_t i,uint64_t *base,uint64_t *pages)
{
    if (i>1) return false;
    *base=i ? HIGH : LOW; *pages=i ? 1537 : 32; return true;
}
bool pmm_range_eligible(uint64_t pa,size_t n)
{
    if (pa>=LOW && pa-LOW<sizeof(backing) && n<=sizeof(backing)-(pa-LOW)) return true;
    /* One reserved 4KiB page in the middle block; unaligned end page. */
    return n && pa>=HIGH && pa-HIGH<1537*PAGE_SIZE && n<=1537*PAGE_SIZE-(pa-HIGH) &&
        !(pa<HIGH+0x201000 && HIGH+0x200000<pa+n);
}
bool pmm_enable_high(PmmResolver r) { published=r;return true; }
PmmStats pmm_get_stats(void) { return (PmmStats){0}; }
uint64_t pmm_alloc_page_zone(PmmZone z) { (void)z;return 0; }
static void reset(void)
{
    if (root) destroy(root,0);
    root=limit=0;active=false;published=NULL;calls=live=0;fail_at=~0u;
    memset(used,0,sizeof(used));
}
int main(void)
{
    uint64_t e,pa,f;
    CHECK(!kvm_arm_leaf(HIGH+1,3,36,&e),"unaligned leaf rejected");
    CHECK(!kvm_arm_leaf(HIGH,2,32,&e),"PA width enforced");
    CHECK(!kvm_arm_leaf(HIGH,1,36,&e),"unsupported leaf level rejected");
    CHECK(kvm_arm_leaf(HIGH,2,36,&e) && (e&3)==1,"2MiB leaf encodes PA and block type");
    CHECK((e&(PXN|UXN|AF|NORMAL|INNER))==(PXN|UXN|AF|NORMAL|INNER) && !(e&(3ULL<<6)),
          "kernel RW Normal shareable XN, no user permission");
    CHECK(!kvm_arm_build(48) && !live,"alias VA span cannot wrap");
    CHECK(!kvm_arm_build(32) && !live && !published,"impossible PA rolls tables back without enabling");
    reset();
    bool rollback=true;
    for (unsigned n=0;n<5;n++) {
        fail_at=n;
        if (kvm_arm_build(36) || live || root || published) rollback=false;
        reset();
    }
    CHECK(rollback,"each intermediate allocation failure rolls back LOW pages");
    uint64_t built=kvm_arm_build(36);
    CHECK(built && live==5,"LOW intermediate tables constructed including boundary PTs");
    CHECK(kvm_arm_query(KPHYS_BASE+HIGH+123,&pa,&f) && pa==HIGH+123 && (f&3)==1,
          "aligned RAM gets 2MiB block and checked offset translation");
    CHECK(!kvm_arm_query(KPHYS_BASE+HIGH+0x200000,&pa,&f),"reserved page stays unmapped");
    CHECK(kvm_arm_query(KPHYS_BASE+HIGH+0x201000,&pa,&f) && (f&3)==3,"partial block uses 4KiB leaves");
    CHECK(kvm_arm_query(KPHYS_BASE+HIGH+1537*PAGE_SIZE-1,&pa,&f) && pa==HIGH+1537*PAGE_SIZE-1,
          "exact final RAM byte translates");
    CHECK(!kvm_arm_query(KPHYS_BASE+HIGH+1537*PAGE_SIZE,&pa,&f),"end is not rounded into hole");
    CHECK(!kvm_arm_query(KPHYS_BASE+0x280000000ULL,&pa,&f),"VZ BAR not cache mapped");
    CHECK(!kvm_arm_query(HIGH,&pa,&f) && !kvm_arm_query(KPHYS_BASE+LOW,&pa,&f),"no identity or low alias here");
    CHECK(!resolve_high(HIGH,PAGE_SIZE) && !kvm_arm_activate(built+PAGE_SIZE),"readiness fails closed");
    CHECK(kvm_arm_activate(built) && published && !kvm_arm_activate(built),"activation is one-way and exact-root checked");
    CHECK(resolve_high(HIGH+17,100)==(void *)(uintptr_t)(KPHYS_BASE+HIGH+17),"active alias resolver");
    CHECK(!resolve_high(HIGH+0x1ff000,8192) && !resolve_high(~0ULL,8192) && !resolve_high(HIGH,0),
          "cross-reservation, overflow and empty ranges refused");
    reset(); CHECK(!live,"test tables fully reclaimed");
    return finish();
}
