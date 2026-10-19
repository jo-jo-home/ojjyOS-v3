/*
 * ojjyOS v3 - Fixed-size unsigned big integers for signature VERIFICATION
 *
 * Written for public-key verification only: inputs are public data, so
 * constant time is NOT required; correctness and bounds safety are.
 * No heap: heavy temporaries live in file-static scratch (the kernel is
 * single-CPU and these helpers are not reentrant - documented, like the
 * rest of the crypto module).
 *
 *   - 32-bit limbs, little-endian limb order (big-endian byte conversion)
 *   - up to BN_LIMBS limbs (4224 bits: RSA-4096 with slack)
 *   - Montgomery multiplication (CIOS, Koç et al.), modular exponentiation,
 *     Fermat inverse, bit-by-bit modular reduction for hash->order inputs.
 *
 * ojjyOS - Created by Jonas Lee
 */
#include "crypto.h"

/* ------------------------------------------------------------------ */
/* Basic helpers                                                       */
/* ------------------------------------------------------------------ */

void bn_zero(BigNum *r)
{
    for (int i = 0; i < BN_LIMBS; i++) r->v[i] = 0;
    r->n = 0;
}

static void bn_trim(BigNum *r)
{
    while (r->n > 0 && r->v[r->n - 1] == 0) r->n--;
}

bool bn_is_zero(const BigNum *a)
{
    return a->n == 0;
}

int bn_cmp(const BigNum *a, const BigNum *b)
{
    if (a->n != b->n) return a->n > b->n ? 1 : -1;
    for (int i = a->n - 1; i >= 0; i--) {
        if (a->v[i] != b->v[i]) return a->v[i] > b->v[i] ? 1 : -1;
    }
    return 0;
}

void bn_set_u32(BigNum *r, uint32_t x)
{
    bn_zero(r);
    if (x) {
        r->v[0] = x;
        r->n = 1;
    }
}

void bn_set_bytes(BigNum *r, const uint8_t *be, size_t len)
{
    bn_zero(r);
    if (len == 0) return;
    if (len > (size_t)BN_LIMBS * 4) {
        /* Keep the low limbs; callers bound key sizes, and a truncated
         * modulus can only make verification fail, never succeed. */
        size_t skip = len - (size_t)BN_LIMBS * 4;
        be += skip;
        len -= skip;
    }
    size_t limbs = (len + 3) / 4;
    for (size_t i = 0; i < len; i++) {
        size_t limb = (len - 1 - i) / 4;
        size_t byte = (len - 1 - i) % 4;
        r->v[limb] |= (uint32_t)be[i] << (byte * 8);
    }
    r->n = (int)limbs;
    bn_trim(r);
}

size_t bn_get_bytes(const BigNum *a, uint8_t *be, size_t len)
{
    /* Left-pad with zeros; the value lands right-aligned. */
    size_t sig = a->n ? (size_t)a->n * 4 : 0;
    for (size_t i = 0; i < len; i++) be[i] = 0;
    for (int i = 0; i < a->n && (size_t)i * 4 < len; i++) {
        for (int j = 0; j < 4 && (size_t)(i * 4 + j) < len; j++) {
            be[len - 1 - (size_t)(i * 4 + j)] = (uint8_t)(a->v[i] >> (j * 8));
        }
    }
    return sig;
}

int bn_bits(const BigNum *a)
{
    if (a->n == 0) return 0;
    uint32_t top = a->v[a->n - 1];
    int bits = (a->n - 1) * 32;
    while (top) {
        bits++;
        top >>= 1;
    }
    return bits;
}

bool bn_bit(const BigNum *a, int i)
{
    int limb = i / 32;
    if (i < 0 || limb >= a->n) return false;
    return ((a->v[limb] >> (i % 32)) & 1u) != 0;
}

uint32_t bn_add(BigNum *r, const BigNum *a, const BigNum *b)
{
    /* r may alias a or b. Returns the carry out (1 only on overflow of
     * the fixed size; callers keep operands small enough that it is 0). */
    int n = a->n > b->n ? a->n : b->n;
    uint32_t carry = 0;
    for (int i = 0; i < n; i++) {
        uint64_t s = carry;
        if (i < a->n) s += a->v[i];
        if (i < b->n) s += b->v[i];
        r->v[i] = (uint32_t)s;
        carry = (uint32_t)(s >> 32);
    }
    if (carry && n < BN_LIMBS) {
        r->v[n] = carry;
        n++;
        carry = 0;
    }
    r->n = n;
    bn_trim(r);
    return carry;
}

void bn_sub(BigNum *r, const BigNum *a, const BigNum *b)
{
    /* r = a - b; requires a >= b (r may alias). */
    uint32_t borrow = 0;
    for (int i = 0; i < a->n; i++) {
        uint32_t bi = (i < b->n) ? b->v[i] : 0;
        uint64_t d = (uint64_t)a->v[i] - bi - borrow;
        if ((int64_t)d < 0) {
            d += ((uint64_t)1 << 32);
            borrow = 1;
        } else {
            borrow = 0;
        }
        r->v[i] = (uint32_t)d;
    }
    r->n = a->n;
    bn_trim(r);
}

/* ------------------------------------------------------------------ */
/* Montgomery multiplication (CIOS). File-static scratch; not reentrant */
/* ------------------------------------------------------------------ */

static uint32_t mont_t[BN_LIMBS + 2];

static void mont_mul(BigNum *r, const BigNum *a, const BigNum *b, const BnMont *m)
{
    int k = m->limbs;
    for (int i = 0; i < k + 2; i++) mont_t[i] = 0;

    for (int i = 0; i < k; i++) {
        /* t += a * b[i] */
        uint64_t carry = 0;
        for (int j = 0; j < k; j++) {
            uint64_t s = (uint64_t)mont_t[j] + (uint64_t)a->v[j] * b->v[i] + carry;
            mont_t[j] = (uint32_t)s;
            carry = s >> 32;
        }
        {
            uint64_t s = (uint64_t)mont_t[k] + carry;
            mont_t[k] = (uint32_t)s;
            mont_t[k + 1] = (uint32_t)(s >> 32);   /* assign: single overflow word */
        }

        /* t += n * (t[0] * n0 mod 2^32); shift down one limb */
        uint32_t m_i = mont_t[0] * m->n0;             /* 32-bit wraparound */
        uint64_t carry2 = ((uint64_t)mont_t[0] + (uint64_t)m_i * m->mod->v[0]) >> 32;
        for (int j = 1; j < k; j++) {
            uint64_t s = (uint64_t)mont_t[j] + (uint64_t)m_i * m->mod->v[j] + carry2;
            mont_t[j - 1] = (uint32_t)s;
            carry2 = s >> 32;
        }
        {
            uint64_t s = (uint64_t)mont_t[k] + carry2;
            mont_t[k - 1] = (uint32_t)s;
            mont_t[k] = mont_t[k + 1] + (uint32_t)(s >> 32);
            mont_t[k + 1] = 0;
        }
    }

    BigNum t;
    t.n = k + 1;
    for (int i = 0; i <= k; i++) t.v[i] = mont_t[i];
    bn_trim(&t);

    /* Final subtraction (at most two: CIOS leaves t < 2n). */
    if (bn_cmp(&t, m->mod) >= 0) bn_sub(&t, &t, m->mod);
    if (bn_cmp(&t, m->mod) >= 0) bn_sub(&t, &t, m->mod);
    *r = t;
}

bool bn_mont_init(BnMont *m, const BigNum *mod)
{
    if (mod->n == 0 || mod->n + 1 > BN_LIMBS || (mod->v[0] & 1u) == 0) return false;
    m->mod = mod;
    m->limbs = mod->n;

    /* n0 = -mod^{-1} mod 2^32 (Newton iteration, converges over 2^32). */
    uint32_t inv = 1;
    for (int i = 0; i < 5; i++) inv *= 2u - mod->v[0] * inv;
    m->n0 = (uint32_t)(0u - inv);

    /* r2 = R^2 mod n, R = 2^(32*limbs): double 64*limbs times mod n. */
    static BigNum acc, twice;
    bn_set_u32(&acc, 1);
    for (int i = 0; i < 64 * m->limbs; i++) {
        bn_add(&twice, &acc, &acc);
        if (bn_cmp(&twice, mod) >= 0) bn_sub(&twice, &twice, mod);
        acc = twice;
    }
    m->r2 = acc;
    return true;
}

void bn_mont_from(BigNum *r, const BigNum *a, const BnMont *m)
{
    mont_mul(r, a, &m->r2, m);      /* a * R^2 * R^-1 = aR */
}

void bn_mont_to(BigNum *r, const BigNum *a, const BnMont *m)
{
    static BigNum one;
    bn_set_u32(&one, 1);
    mont_mul(r, a, &one, m);        /* a * R^-1 = a */
}

void bn_mont_mul(BigNum *r, const BigNum *a, const BigNum *b, const BnMont *m)
{
    mont_mul(r, a, b, m);
}

bool bn_mod_exp(BigNum *r, const BigNum *base, const BigNum *exp, const BigNum *mod)
{
    BnMont m;
    if (!bn_mont_init(&m, mod)) return false;
    if (bn_cmp(base, mod) >= 0) return false;   /* caller reduces first */

    static BigNum result, base_m, one, tmp;
    bn_set_u32(&one, 1);
    bn_mont_from(&result, &one, &m);
    bn_mont_from(&base_m, base, &m);

    for (int i = bn_bits(exp) - 1; i >= 0; i--) {
        mont_mul(&tmp, &result, &result, &m);
        result = tmp;
        if (bn_bit(exp, i)) {
            mont_mul(&tmp, &result, &base_m, &m);
            result = tmp;
        }
    }
    bn_mont_to(r, &result, &m);
    return true;
}

bool bn_mod_inv_prime(BigNum *r, const BigNum *a, const BigNum *p)
{
    /* p prime, 0 < a < p: a^(p-2) mod p. */
    if (bn_is_zero(a) || bn_cmp(a, p) >= 0) return false;
    static BigNum pm2;
    static const BigNum two = { { 2, 0, 0, 0 }, 1 };
    pm2 = *p;
    bn_sub(&pm2, &pm2, &two);
    return bn_mod_exp(r, a, &pm2, p);
}

bool bn_mod_reduce(BigNum *r, const BigNum *a, const BigNum *m)
{
    /* Bit-by-bit shift-subtract reduce. Requires m->n + 1 <= BN_LIMBS
     * (true for the EC orders we use; RSA paths never call this). */
    if (m->n == 0 || m->n + 1 > BN_LIMBS) return false;
    static BigNum cur, tmp;
    bn_zero(&cur);
    for (int i = bn_bits(a) - 1; i >= 0; i--) {
        /* cur = (cur << 1) | bit(i) */
        tmp = cur;
        uint32_t carry = bn_bit(a, i) ? 1u : 0u;
        for (int j = 0; j < BN_LIMBS; j++) {
            uint32_t orig = j < tmp.n ? tmp.v[j] : 0;
            cur.v[j] = (orig << 1) | carry;
            carry = orig >> 31;
        }
        cur.n = tmp.n < BN_LIMBS ? tmp.n + 1 : BN_LIMBS;
        bn_trim(&cur);
        if (bn_cmp(&cur, m) >= 0) bn_sub(&cur, &cur, m);
    }
    *r = cur;
    return true;
}

void bn_mod_add(BigNum *r, const BigNum *a, const BigNum *b, const BigNum *m)
{
    /* 0 <= a,b < m  =>  a+b < 2m: one conditional subtract. */
    static BigNum t;
    uint32_t carry = bn_add(&t, a, b);
    if (carry || bn_cmp(&t, m) >= 0) bn_sub(&t, &t, m);
    *r = t;
}

void bn_mod_sub(BigNum *r, const BigNum *a, const BigNum *b, const BigNum *m)
{
    static BigNum t;
    if (bn_cmp(a, b) >= 0) {
        bn_sub(&t, a, b);
    } else {
        bn_sub(&t, m, b);
        bn_add(&t, &t, a);
        if (bn_cmp(&t, m) >= 0) bn_sub(&t, &t, m);
    }
    *r = t;
}
