/* Staged RAM-only kernel aliases, HW-13/MEM-04. Created by Jonas Lee. */
#ifndef OJJY_KVM_H
#define OJJY_KVM_H
#include "memory.h"
#define KPHYS_BASE 0xffff800000000000ULL
/* ARM preparatory mapper. Build before table activation; tables stay LOW.
 * Failure frees every table it allocated. No Device/firmware mappings here. */
uint64_t kvm_arm_build(unsigned pa_bits);
bool kvm_arm_activate(uint64_t active_root);
bool kvm_arm_query(uint64_t virtual_address, uint64_t *physical, uint64_t *flags);
bool kvm_arm_leaf(uint64_t physical, unsigned level, unsigned pa_bits, uint64_t *entry);
bool kvm_high_selftest(void);
#endif
