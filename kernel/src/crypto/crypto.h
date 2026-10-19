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

/* SHA-512 / SHA-384 (FIPS 180-4) */
typedef struct {
    uint64_t h[8];
    uint64_t length;
    uint8_t block[128];
    size_t used;
    int digest_words;      /* 8 = SHA-512, 6 = SHA-384 */
} Sha512;
typedef Sha512 Sha384;

void sha512_init(Sha512 *s);
void sha512_update(Sha512 *s, const void *data, size_t len);
void sha512_final(Sha512 *s, uint8_t out[64]);
void sha512(const void *data, size_t len, uint8_t out[64]);
void sha384_init(Sha384 *s);
void sha384_update(Sha384 *s, const void *data, size_t len);
void sha384_final(Sha384 *s, uint8_t out[48]);
void sha384(const void *data, size_t len, uint8_t out[48]);

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

/* ------------------------------------------------------------------ */
/* RSA signature verification (kernel/src/crypto/rsa.c, RFC 8017)      */
/* ------------------------------------------------------------------ */
typedef enum {
    RSA_HASH_SHA256 = 0,
    RSA_HASH_SHA384,
    RSA_HASH_SHA512,
} RsaHashId;

/* PKCS#1 v1.5: EM = 0x00 0x01 FF.. 0x00 DigestInfo digest */
bool rsa_verify_pkcs1(const uint8_t *n_be, size_t n_len, uint32_t e,
                      const uint8_t *sig, size_t sig_len,
                      RsaHashId hash_id, const uint8_t *digest);
/* RSASSA-PSS with MGF1; salt_len < 0 means "hash length" */
bool rsa_verify_pss(const uint8_t *n_be, size_t n_len, uint32_t e,
                    const uint8_t *sig, size_t sig_len,
                    RsaHashId hash_id, const uint8_t *m_hash, int salt_len);

/* ------------------------------------------------------------------ */
/* ECDSA verification (kernel/src/crypto/ecdsa.c)                      */
/* ------------------------------------------------------------------ */
#define ECDSA_CURVE_P256 0
#define ECDSA_CURVE_P384 1
/* pub = 0x04 || X || Y (65 bytes for P-256, 97 for P-384); r and s are
 * fixed-length big-endian (32/48 bytes, left-padded by the DER parser).
 * Any hash length is accepted; the X.509 layer pairs curve and hash. */
bool ecdsa_verify_raw(int curve_id, const uint8_t *pub, size_t pub_len,
                      const uint8_t *r_be, const uint8_t *s_be,
                      const uint8_t *hash, size_t hash_len);

/* ------------------------------------------------------------------ */
/* Big integers (kernel/src/crypto/bignum.c). Verification only:      */
/* public data, no constant-time claims. Not reentrant (static scratch) */
/* ------------------------------------------------------------------ */
#define BN_LIMBS 132               /* 4224 bits: RSA-4096 with slack */

typedef struct {
    uint32_t v[BN_LIMBS];
    int n;                          /* significant limbs, little-endian */
} BigNum;

typedef struct {
    const BigNum *mod;
    BigNum r2;                      /* R^2 mod n, R = 2^(32*limbs) */
    uint32_t n0;                    /* -mod^-1 mod 2^32 */
    int limbs;
} BnMont;

void bn_zero(BigNum *r);
bool bn_is_zero(const BigNum *a);
int  bn_cmp(const BigNum *a, const BigNum *b);
void bn_set_u32(BigNum *r, uint32_t x);
void bn_set_bytes(BigNum *r, const uint8_t *big_endian, size_t len);
size_t bn_get_bytes(const BigNum *a, uint8_t *big_endian, size_t len);
int  bn_bits(const BigNum *a);
bool bn_bit(const BigNum *a, int i);
uint32_t bn_add(BigNum *r, const BigNum *a, const BigNum *b);     /* carry out */
void bn_sub(BigNum *r, const BigNum *a, const BigNum *b);         /* a >= b */

bool bn_mont_init(BnMont *m, const BigNum *mod);                  /* odd modulus */
void bn_mont_mul(BigNum *r, const BigNum *a, const BigNum *b, const BnMont *m);
void bn_mont_from(BigNum *r, const BigNum *a, const BnMont *m);   /* a -> a*R */
void bn_mont_to(BigNum *r, const BigNum *a, const BnMont *m);     /* a*R -> a */
bool bn_mod_exp(BigNum *r, const BigNum *base, const BigNum *exp, const BigNum *mod);
bool bn_mod_inv_prime(BigNum *r, const BigNum *a, const BigNum *p);
bool bn_mod_reduce(BigNum *r, const BigNum *a, const BigNum *m);  /* generic, slow */
void bn_mod_add(BigNum *r, const BigNum *a, const BigNum *b, const BigNum *m);
void bn_mod_sub(BigNum *r, const BigNum *a, const BigNum *b, const BigNum *m);

#endif /* _OJJY_CRYPTO_H */
