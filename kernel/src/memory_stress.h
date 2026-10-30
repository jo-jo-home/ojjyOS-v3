/* Bounded diagnostic, not a background service. Created by Jonas Lee. */
#ifndef OJJY_MEMORY_STRESS_H
#define OJJY_MEMORY_STRESS_H
#include "types.h"
typedef struct {
    uint64_t requested,peak_owned,low_peak,high_peak,high_min,high_max;
    uint64_t written_pages,verified_pages;
    bool restored,passed;
} MemoryStressResult;
bool memory_stress(uint64_t bytes,MemoryStressResult *result);
#endif
