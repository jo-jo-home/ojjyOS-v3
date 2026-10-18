/*
 * ojjyOS v3 - SHA-256, HMAC-SHA256, HKDF and TLS 1.3 HKDF-Expand-Label
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "crypto/crypto.h"
#include "string.h"

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void compress(uint32_t h[8], const uint8_t b[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)b[4 * i] << 24) | ((uint32_t)b[4 * i + 1] << 16) | ((uint32_t)b[4 * i + 2] << 8) | b[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha256_init(Sha256 *s)
{
    static const uint32_t iv[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    memcpy(s->h, iv, sizeof(iv));
    s->length = 0;
    s->used = 0;
}

void sha256_update(Sha256 *s, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    s->length += len;
    while (len) {
        size_t n = 64 - s->used < len ? 64 - s->used : len;
        memcpy(s->block + s->used, p, n);
        s->used += n; p += n; len -= n;
        if (s->used == 64) { compress(s->h, s->block); s->used = 0; }
    }
}

void sha256_final(Sha256 *s, uint8_t out[32])
{
    uint64_t bits = s->length * 8;
    uint8_t pad = 0x80;
    sha256_update(s, &pad, 1);
    uint8_t zero = 0;
    while (s->used != 56) sha256_update(s, &zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(s, len, 8);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(s->h[i] >> 24); out[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(s->h[i] >> 8); out[4 * i + 3] = (uint8_t)s->h[i];
    }
}

void sha256(const void *data, size_t len, uint8_t out[32])
{
    Sha256 s;
    sha256_init(&s);
    sha256_update(&s, data, len);
    sha256_final(&s, out);
}

void hmac_sha256(const uint8_t *key, size_t key_len, const void *data, size_t len, uint8_t out[32])
{
    uint8_t k[64], pad[64], inner[32];
    memset(k, 0, sizeof(k));
    if (key_len > 64) sha256(key, key_len, k); else memcpy(k, key, key_len);
    Sha256 s;
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    sha256_init(&s); sha256_update(&s, pad, 64); sha256_update(&s, data, len); sha256_final(&s, inner);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    sha256_init(&s); sha256_update(&s, pad, 64); sha256_update(&s, inner, 32); sha256_final(&s, out);
    crypto_wipe(k, sizeof(k));
}

void hkdf_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len, uint8_t prk[32])
{
    uint8_t zero[32] = {0};
    if (!salt || !salt_len) { salt = zero; salt_len = 32; }
    hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
}

void hkdf_expand(const uint8_t prk[32], const uint8_t *info, size_t info_len, uint8_t *out, size_t out_len)
{
    uint8_t t[32], buf[32 + 255 + 1];
    size_t t_len = 0, done = 0;
    if (info_len > 255) info_len = 255;
    for (uint8_t counter = 1; done < out_len; counter++) {
        memcpy(buf, t, t_len);
        memcpy(buf + t_len, info, info_len);
        buf[t_len + info_len] = counter;
        hmac_sha256(prk, 32, buf, t_len + info_len + 1, t);
        t_len = 32;
        size_t n = out_len - done < 32 ? out_len - done : 32;
        memcpy(out + done, t, n);
        done += n;
    }
}

void hkdf_expand_label(const uint8_t secret[32], const char *label, const uint8_t *context, size_t context_len,
                       uint8_t *out, size_t out_len)
{
    uint8_t info[2 + 1 + 6 + 64 + 1 + 64];
    size_t l = strlen(label);
    if (l > 64) l = 64;
    if (context_len > 64) context_len = 64;
    size_t o = 0;
    info[o++] = (uint8_t)(out_len >> 8);
    info[o++] = (uint8_t)out_len;
    info[o++] = (uint8_t)(6 + l);
    memcpy(info + o, "tls13 ", 6); o += 6;
    memcpy(info + o, label, l); o += l;
    info[o++] = (uint8_t)context_len;
    if (context_len) { memcpy(info + o, context, context_len); o += context_len; }
    hkdf_expand(secret, info, o, out, out_len);
}

bool crypto_equal(const void *a, const void *b, size_t len)
{
    const volatile uint8_t *x = (const volatile uint8_t *)a, *y = (const volatile uint8_t *)b;
    uint8_t d = 0;
    for (size_t i = 0; i < len; i++) d |= (uint8_t)(x[i] ^ y[i]);
    return d == 0;
}

void crypto_wipe(void *p, size_t len)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (len--) *v++ = 0;
}
