/* Hold distinct 2MiB extents simultaneously, then verify every held page.
 * Runtime bounded LOW bookkeeping; no writes through physical casts.
 * Created by Jonas Lee. */
#include "memory_stress.h"
#include "memory.h"
#include "string.h"
#include "serial.h"
#define CHUNK (2ULL<<20)
#define MAX_CHUNKS 2048
static uint64_t pattern(uint64_t pa,unsigned word)
{return (pa^(0x9e3779b97f4a7c15ULL*(word+1)))+0x6a09e667f3bcc909ULL;}
bool memory_stress(uint64_t bytes,MemoryStressResult *out)
{
    if(!out)return false;
    memset(out,0,sizeof(*out));out->requested=bytes;
    PmmStats before=pmm_get_stats();
    if(!bytes || bytes%CHUNK || bytes/CHUNK>MAX_CHUNKS || !pmm_high_ready())return false;
    uint64_t count=bytes/CHUNK,meta_bytes=ALIGN_UP(count*sizeof(uint64_t),PAGE_SIZE);
    if(before.low_free_bytes<meta_bytes || before.low_free_bytes-meta_bytes+before.high_free_bytes<bytes)return false;
    uint64_t *held=pmm_alloc_bytes(meta_bytes);
    if(!held)return false;
    uint64_t meta=pmm_virt_to_phys(held,meta_bytes),n=0;
    if(!meta)return false; /* valid PMM allocation always has a LOW alias */
    bool ok=true;
    for(;n<count;n++) {
        uint64_t pa=pmm_alloc_contiguous_zone(CHUNK/PAGE_SIZE,PMM_ANY);
        if(!pa){ok=false;break;}
        bool unique=true;
        for(uint64_t j=0;j<n;j++)if(pa<held[j]+CHUNK && held[j]<pa+CHUNK)unique=false;
        if(!unique){ok=false;break;}
        held[n]=pa;
        out->peak_owned+=CHUNK;
        if(pa>=(1ULL<<32)) {
            out->high_peak+=CHUNK;
            if(!out->high_min || pa<out->high_min)out->high_min=pa;
            if(pa+CHUNK-1>out->high_max)out->high_max=pa+CHUNK-1;
        } else out->low_peak+=CHUNK;
        if(!(out->peak_owned% (256ULL<<20)))serial_printf("[MEMTEST] Held %llu MiB\n",out->peak_owned>>20);
    }
    PmmStats peak=pmm_get_stats();
    if(before.low_free_bytes<meta_bytes+out->low_peak || before.high_free_bytes<out->high_peak ||
       before.low_free_bytes-meta_bytes-out->low_peak!=peak.low_free_bytes ||
       before.high_free_bytes-out->high_peak!=peak.high_free_bytes)ok=false;
    /* All allocations remain owned through both complete passes. Different
     * PA-derived data catches duplicate aliases after the last writer runs.
     * Eight locations per page, including both endpoints, all volatile. */
    static const unsigned words[]={0,7,64,127,256,383,504,511};
    for(uint64_t b=0;ok && b<n;b++)for(uint64_t p=0;p<CHUNK/PAGE_SIZE;p++) {
        uint64_t pa=held[b]+p*PAGE_SIZE;
        volatile uint64_t *v=pmm_phys_to_virt(pa,PAGE_SIZE);
        if(!v){ok=false;break;}
        for(unsigned w=0;w<8;w++)v[words[w]]=pattern(pa,words[w]);
        out->written_pages++;
    }
#ifdef OJJY_STRESS_HOST_TEST
    extern void memory_stress_after_write(void);
    memory_stress_after_write();
#endif
    for(uint64_t b=0;ok && b<n;b++)for(uint64_t p=0;p<CHUNK/PAGE_SIZE;p++) {
        uint64_t pa=held[b]+p*PAGE_SIZE;
        volatile uint64_t *v=pmm_phys_to_virt(pa,PAGE_SIZE);
        if(!v){ok=false;break;}
        for(unsigned w=0;w<8;w++)if(v[words[w]]!=pattern(pa,words[w]))ok=false;
        if(!ok)break;
        out->verified_pages++;
    }
    for(uint64_t b=0;b<n;b++)for(uint64_t p=0;p<CHUNK/PAGE_SIZE;p++)pmm_free_page(held[b]+p*PAGE_SIZE);
    for(uint64_t p=0;p<meta_bytes;p+=PAGE_SIZE)pmm_free_page(meta+p);
    PmmStats after=pmm_get_stats();
    out->restored=before.low_free_bytes==after.low_free_bytes && before.high_free_bytes==after.high_free_bytes;
    out->passed=ok && out->peak_owned==bytes && out->verified_pages==bytes/PAGE_SIZE && out->restored;
    serial_printf("[MEMTEST] result=%s requested=%llu peak=%llu low=%llu high=%llu high_min=%p high_max=%p written=%llu verified=%llu restored=%d\n",
                  out->passed?"PASS":"FAIL",bytes,out->peak_owned,out->low_peak,out->high_peak,out->high_min,out->high_max,
                  out->written_pages,out->verified_pages,(int)out->restored);
    serial_printf("[MEMTEST] free low_before=%llu low_after=%llu high_before=%llu high_after=%llu\n",
                  before.low_free_bytes,after.low_free_bytes,before.high_free_bytes,after.high_free_bytes);
    return out->passed;
}
