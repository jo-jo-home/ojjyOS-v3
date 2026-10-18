/*
 * ojjyOS v3 - Kernel randomness
 *
 * ChaCha20 DRBG with fast key erasure (random.c). Entropy from virtio-rng
 * (when present), the NIC MAC and timer jitter is hashed into the key.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_RANDOM_H
#define _OJJY_RANDOM_H

#include "types.h"

void random_add_entropy(const void *data, size_t len);
uint64_t random_u64(void);
uint32_t random_u32(void);
void random_bytes(void *out, size_t len);
bool random_has_hw_entropy(void);   /* true once virtio-rng contributed */
void random_mark_hw_entropy(void);

#endif /* _OJJY_RANDOM_H */
