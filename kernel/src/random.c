/*
 * ojjyOS v3 - Kernel randomness (see random.h for the security caveat)
 *
 * ojjyOS - Created by Jonas Lee
 */
#include "random.h"

static uint64_t pool[4] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL
};
static bool hw_entropy;
static uint64_t mix_counter;

static uint64_t splitmix(uint64_t *x)
{
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static inline uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

void random_add_entropy(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        uint64_t seed = pool[i & 3] ^ ((uint64_t)p[i] << ((i & 7) * 8)) ^ (++mix_counter);
        pool[i & 3] ^= splitmix(&seed);
    }
    if (!(pool[0] | pool[1] | pool[2] | pool[3])) pool[0] = 1;
}

uint64_t random_u64(void)
{
    /* xoshiro256** */
    uint64_t result = rotl(pool[1] * 5, 7) * 9;
    uint64_t t = pool[1] << 17;
    pool[2] ^= pool[0]; pool[3] ^= pool[1];
    pool[1] ^= pool[2]; pool[0] ^= pool[3];
    pool[2] ^= t; pool[3] = rotl(pool[3], 45);
    return result;
}

uint32_t random_u32(void) { return (uint32_t)(random_u64() >> 32); }

void random_bytes(void *out, size_t len)
{
    uint8_t *o = (uint8_t *)out;
    while (len) {
        uint64_t v = random_u64();
        for (int i = 0; i < 8 && len; i++, len--) { *o++ = (uint8_t)v; v >>= 8; }
    }
}

bool random_has_hw_entropy(void) { return hw_entropy; }
void random_mark_hw_entropy(void) { hw_entropy = true; }
