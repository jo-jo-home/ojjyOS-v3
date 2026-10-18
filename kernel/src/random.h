/*
 * ojjyOS v3 - Kernel randomness
 *
 * Entropy from virtio-rng (when present) and timer jitter is mixed into a
 * pool. NOTE: the current output function (xoshiro256**) is NOT a
 * cryptographic generator; it is fine for DHCP xids, ports and TCP ISNs.
 * TLS must use random_bytes() only after it is upgraded to a ChaCha20 DRBG.
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
