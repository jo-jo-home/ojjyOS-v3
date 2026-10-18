/*
 * ojjyOS v3 - Cryptographic primitives (written from the specifications)
 *
 *   SHA-256 (FIPS 180-4), HMAC (RFC 2104), HKDF (RFC 5869) and the TLS 1.3
 *   HKDF-Expand-Label (RFC 8446 7.1), X25519 (RFC 7748), AES-128 (FIPS 197)
 *   with GCM (SP 800-38D), ChaCha20-Poly1305 (RFC 8439).
 *
 * Caveat: AES uses lookup tables and is not constant-time; GHASH is
 * bitwise. Fine for a hobby OS browser; documented, not hidden.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_CRYPTO_H
#define _OJJY_CRYPTO_H

#include "types.h"

/* SHA-256 */
typedef struct {
    uint32_t h[8];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} Sha256;

void sha256_init(Sha256 *s);
void sha256_update(Sha256 *s, const void *data, size_t len);
void sha256_final(Sha256 *s, uint8_t out[32]);
void sha256(const void *data, size_t len, uint8_t out[32]);

/* HMAC-SHA256 / HKDF-SHA256 */
void hmac_sha256(const uint8_t *key, size_t key_len, const void *data, size_t len, uint8_t out[32]);
void hkdf_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len, uint8_t prk[32]);
void hkdf_expand(const uint8_t prk[32], const uint8_t *info, size_t info_len, uint8_t *out, size_t out_len);
void hkdf_expand_label(const uint8_t secret[32], const char *label, const uint8_t *context, size_t context_len,
                       uint8_t *out, size_t out_len);

/* X25519 */
void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
void x25519_base(uint8_t out[32], const uint8_t scalar[32]);

/* AES-128-GCM (12-byte nonce, 16-byte tag). Open returns false on bad tag. */
typedef struct { uint32_t rk[44]; uint8_t h[16]; } Aes128Gcm;
void aes128_gcm_init(Aes128Gcm *g, const uint8_t key[16]);
void aes128_encrypt_block(const uint32_t rk[44], const uint8_t in[16], uint8_t out[16]);
void aes128_gcm_seal(const Aes128Gcm *g, const uint8_t nonce[12], const uint8_t *aad, size_t aad_len,
                     const uint8_t *in, size_t len, uint8_t *out, uint8_t tag[16]);
bool aes128_gcm_open(const Aes128Gcm *g, const uint8_t nonce[12], const uint8_t *aad, size_t aad_len,
                     const uint8_t *in, size_t len, const uint8_t tag[16], uint8_t *out);

/* ChaCha20 / Poly1305 / AEAD */
void chacha20_block(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12], uint8_t out[64]);
void chacha20_xor(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12],
                  const uint8_t *in, uint8_t *out, size_t len);
void poly1305(const uint8_t key[32], const uint8_t *msg, size_t len, uint8_t tag[16]);
void chacha20_poly1305_seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t aad_len,
                            const uint8_t *in, size_t len, uint8_t *out, uint8_t tag[16]);
bool chacha20_poly1305_open(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t aad_len,
                            const uint8_t *in, size_t len, const uint8_t tag[16], uint8_t *out);

/* Constant-time comparison */
bool crypto_equal(const void *a, const void *b, size_t len);
void crypto_wipe(void *p, size_t len);

#endif /* _OJJY_CRYPTO_H */
