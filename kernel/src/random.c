/*
 * ojjyOS v3 - Kernel randomness: ChaCha20 DRBG with fast key erasure
 *
 * Entropy (virtio-rng blocks, MAC address, timer jitter) is folded into a
 * 256-bit key with SHA-256(key || input). Output comes from ChaCha20 blocks
 * under that key; after every request the key is replaced with fresh
 * keystream, so earlier outputs cannot be recomputed from a later state.
 * Quality depends on the entropy fed in: random_has_hw_entropy() tells TLS
 * whether a hardware source (virtio-rng) contributed.
 *
 * ojjyOS - Created by Jonas Lee
 */
#include "random.h"
#include "crypto/crypto.h"
#include "string.h"

static uint8_t key[32] = {
    0x6a, 0x09, 0xe6, 0x67, 0xf3, 0xbc, 0xc9, 0x08, 0xbb, 0x67, 0xae, 0x85, 0x84, 0xca, 0xa7, 0x3b,
    0x3c, 0x6e, 0xf3, 0x72, 0xfe, 0x94, 0xf8, 0x2b, 0xa5, 0x4f, 0xf5, 0x3a, 0x5f, 0x1d, 0x36, 0xf1
};
static uint64_t counter;
static bool hw_entropy;

void random_add_entropy(const void *data, size_t len)
{
    Sha256 s;
    sha256_init(&s);
    sha256_update(&s, key, sizeof(key));
    sha256_update(&s, &counter, sizeof(counter));
    sha256_update(&s, data, len);
    sha256_final(&s, key);
    counter++;
}

void random_bytes(void *out, size_t len)
{
    uint8_t nonce[12] = {0}, block[64];
    uint8_t *o = (uint8_t *)out;
    memcpy(nonce, &counter, sizeof(counter));
    counter++;
    uint32_t blk = 0;
    chacha20_block(key, blk++, nonce, block);       /* block 0 -> next key */
    memcpy(key, block, 32);
    while (len) {
        chacha20_block(key, blk++, nonce, block);
        size_t n = len < 64 ? len : 64;
        memcpy(o, block, n);
        o += n; len -= n;
    }
    chacha20_block(key, blk, nonce, block);         /* erase: rekey again */
    memcpy(key, block, 32);
    crypto_wipe(block, sizeof(block));
}

uint64_t random_u64(void) { uint64_t v; random_bytes(&v, sizeof(v)); return v; }
uint32_t random_u32(void) { uint32_t v; random_bytes(&v, sizeof(v)); return v; }
bool random_has_hw_entropy(void) { return hw_entropy; }
void random_mark_hw_entropy(void) { hw_entropy = true; }
