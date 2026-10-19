/*
 * ojjyOS v3 - SHA-512 / SHA-384 (FIPS 180-4, written from the specification)
 *
 * SHA-512 is needed for sha512WithRSA certificates and TLS 1.3 suites;
 * SHA-384 for P-384 ECDSA and sha384WithRSA. 64-bit words, 128-byte blocks.
 *
 * ojjyOS - Created by Jonas Lee
 */
#include "crypto.h"

static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

static inline uint64_t rotr64(uint64_t v, unsigned n)
{
    return (v >> n) | (v << (64 - n));
}

static inline uint64_t rd64be(const uint8_t *p)
{
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) |
           ((uint64_t)p[3] << 32) | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) | (uint64_t)p[7];
}

static void sha512_transform(Sha512 *ctx, const uint8_t block[128])
{
    uint64_t w[80];
    for (int i = 0; i < 16; i++) w[i] = rd64be(block + i * 8);
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3];
    uint64_t e = ctx->h[4], f = ctx->h[5], g = ctx->h[6], h = ctx->h[7];
    for (int i = 0; i < 80; i++) {
        uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t t1 = h + S1 + ch + sha512_k[i] + w[i];
        uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
    ctx->h[4] += e; ctx->h[5] += f; ctx->h[6] += g; ctx->h[7] += h;
}

static void sha512_init_common(Sha512 *ctx, int digest_words)
{
    static const uint64_t h512[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
    };
    static const uint64_t h384[8] = {
        0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
        0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL,
    };
    for (int i = 0; i < 8; i++) ctx->h[i] = digest_words == 8 ? h512[i] : h384[i];
    ctx->length = 0;
    ctx->used = 0;
    ctx->digest_words = digest_words;
}

void sha512_init(Sha512 *s) { sha512_init_common((Sha512 *)s, 8); }
void sha384_init(Sha384 *s) { sha512_init_common((Sha512 *)s, 6); }

static void sha512_update_common(Sha512 *ctx, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    ctx->length += len;
    while (len) {
        if (ctx->used == 0 && len >= 128) {
            sha512_transform(ctx, p);
            p += 128;
            len -= 128;
            continue;
        }
        size_t take = 128 - ctx->used;
        if (take > len) take = len;
        for (size_t i = 0; i < take; i++) ctx->block[ctx->used + i] = p[i];
        ctx->used += take;
        p += take;
        len -= take;
        if (ctx->used == 128) {
            sha512_transform(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

void sha512_update(Sha512 *s, const void *data, size_t len)
{
    sha512_update_common((Sha512 *)s, data, len);
}

void sha384_update(Sha384 *s, const void *data, size_t len)
{
    sha512_update_common((Sha512 *)s, data, len);
}

static void sha512_final_common(Sha512 *ctx, uint8_t *out, int words)
{
    uint64_t bits = ctx->length * 8ULL;
    uint8_t pad = 0x80;
    sha512_update_common(ctx, &pad, 1);
    uint8_t zero = 0;
    while (ctx->used != 112) sha512_update_common(ctx, &zero, 1);
    /* SHA-512/384 carry a 128-bit (16-byte) length field. `bits` is a
     * 64-bit value, so the top 8 bytes are zero; write the low bytes with
     * in-range shifts (a shift of the full width would be UB). */
    uint8_t len_be[16];
    for (int i = 0; i < 8; i++) len_be[i] = 0;
    for (int i = 0; i < 8; i++) len_be[15 - i] = (uint8_t)(bits >> (i * 8));
    sha512_update_common(ctx, len_be, 16);
    for (int i = 0; i < words; i++) {
        for (int j = 0; j < 8; j++) out[i * 8 + j] = (uint8_t)(ctx->h[i] >> (56 - j * 8));
    }
}

void sha512_final(Sha512 *s, uint8_t out[64]) { sha512_final_common((Sha512 *)s, out, 8); }
void sha384_final(Sha384 *s, uint8_t out[48]) { sha512_final_common((Sha512 *)s, out, 6); }

void sha512(const void *data, size_t len, uint8_t out[64])
{
    Sha512 s;
    sha512_init(&s);
    sha512_update(&s, data, len);
    sha512_final(&s, out);
}

void sha384(const void *data, size_t len, uint8_t out[48])
{
    Sha384 s;
    sha384_init(&s);
    sha384_update(&s, data, len);
    sha384_final(&s, out);
}
