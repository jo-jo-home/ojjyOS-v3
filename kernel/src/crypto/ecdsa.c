/*
 * ojjyOS v3 - ECDSA signature verification over NIST P-256 and P-384
 *
 * Public-key verification only (SEC 1 / FIPS 186). Field arithmetic mod p
 * goes through the generic Montgomery engine; points are Jacobian with
 * the general dbl-2007-bl / add-2007-bl formulas (valid for any a, so no
 * a = -3 special cases); the group order n of both curves is prime, so
 * inverses mod n use Fermat. Static scratch; not reentrant.
 *
 * ojjyOS - Created by Jonas Lee
 */
#include "crypto.h"

typedef struct {
    int bytes;                     /* 32 (P-256) or 48 (P-384) */
    const uint8_t *p, *b;          /* field prime and curve coefficient */
    const uint8_t *gx, *gy;        /* generator */
    const uint8_t *n;              /* group order (prime) */
} EcdsaCurve;

/* NIST P-256 (FIPS 186-4 D.1.2.3) */
static const uint8_t p256_p[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
static const uint8_t p256_b[32] = {
    0x5A, 0xC6, 0x35, 0xD8, 0xAA, 0x3A, 0x93, 0xE7, 0xB3, 0xEB, 0xBD, 0x55, 0x76,
    0x98, 0x86, 0xBC, 0x65, 0x1D, 0x06, 0xB0, 0xCC, 0x53, 0xB0, 0xF6, 0x3B, 0xCE,
    0x3C, 0x3E, 0x27, 0xD2, 0x60, 0x4B,
};
static const uint8_t p256_gx[32] = {
    0x6B, 0x17, 0xD1, 0xF2, 0xE1, 0x2C, 0x42, 0x47, 0xF8, 0xBC, 0xE6, 0xE5, 0x63,
    0xA4, 0x40, 0xF2, 0x77, 0x03, 0x7D, 0x81, 0x2D, 0xEB, 0x33, 0xA0, 0xF4, 0xA1,
    0x39, 0x45, 0xD8, 0x98, 0xC2, 0x96,
};
static const uint8_t p256_gy[32] = {
    0x4F, 0xE3, 0x42, 0xE2, 0xFE, 0x1A, 0x7F, 0x9B, 0x8E, 0xE7, 0xEB, 0x4A, 0x7C,
    0x0F, 0x9E, 0x16, 0x2B, 0xCE, 0x33, 0x57, 0x6B, 0x31, 0x5E, 0xCE, 0xCB, 0xB6,
    0x40, 0x68, 0x37, 0xBF, 0x51, 0xF5,
};
static const uint8_t p256_n[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xBC, 0xE6, 0xFA, 0xAD, 0xA7, 0x17, 0x9E, 0x84, 0xF3, 0xB9,
    0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x51,
};

/* NIST P-384 (FIPS 186-4 D.1.2.5) */
static const uint8_t p384_p[48] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
};
static const uint8_t p384_b[48] = {
    0xB3, 0x31, 0x2F, 0xA7, 0xE2, 0x3E, 0xE7, 0xE4, 0x98, 0x8E, 0x05, 0x6B, 0xE3,
    0xF8, 0x2D, 0x19, 0x18, 0x1D, 0x9C, 0x6E, 0xFE, 0x81, 0x41, 0x12, 0x03, 0x14,
    0x08, 0x8F, 0x50, 0x13, 0x87, 0x5A, 0xC6, 0x56, 0x39, 0x8D, 0x8A, 0x2E, 0xD1,
    0x9D, 0x2A, 0x85, 0xC8, 0xED, 0xD3, 0xEC, 0x2A, 0xEF,
};
static const uint8_t p384_gx[48] = {
    0xAA, 0x87, 0xCA, 0x22, 0xBE, 0x8B, 0x05, 0x37, 0x8E, 0xB1, 0xC7, 0x1E, 0xF3,
    0x20, 0xAD, 0x74, 0x6E, 0x1D, 0x3B, 0x62, 0x8B, 0xA7, 0x9B, 0x98, 0x59, 0xF7,
    0x41, 0xE0, 0x82, 0x54, 0x2A, 0x38, 0x55, 0x02, 0xF2, 0x5D, 0xBF, 0x55, 0x29,
    0x6C, 0x3A, 0x54, 0x5E, 0x38, 0x72, 0x76, 0x0A, 0xB7,
};
static const uint8_t p384_gy[48] = {
    0x36, 0x17, 0xDE, 0x4A, 0x96, 0x26, 0x2C, 0x6F, 0x5D, 0x9E, 0x98, 0xBF, 0x92,
    0x92, 0xDC, 0x29, 0xF8, 0xF4, 0x1D, 0xBD, 0x28, 0x9A, 0x14, 0x7C, 0xE9, 0xDA,
    0x31, 0x13, 0xB5, 0xF0, 0xB8, 0xC0, 0x0A, 0x60, 0xB1, 0xCE, 0x1D, 0x7E, 0x81,
    0x9D, 0x7A, 0x43, 0x1D, 0x7C, 0x90, 0xEA, 0x0E, 0x5F,
};
static const uint8_t p384_n[48] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC7, 0x63,
    0x4D, 0x81, 0xF4, 0x37, 0x2D, 0xDF, 0x58, 0x1A, 0x0D, 0xB2, 0x48, 0xB0, 0xA7,
    0x7A, 0xEC, 0xEC, 0x19, 0x6A, 0xCC, 0xC5, 0x29, 0x73,
};

static const EcdsaCurve ec_curves[2] = {
    { 32, p256_p, p256_b, p256_gx, p256_gy, p256_n },
    { 48, p384_p, p384_b, p384_gx, p384_gy, p384_n },
};

/* Jacobian projective point; Z = 0 is the point at infinity. */
typedef struct {
    BigNum x, y, z;
} EcPoint;

/* Working context. Everything static: single-CPU kernel, not reentrant.
 * FIELD ELEMENTS ARE STORED IN THE MONTGOMERY DOMAIN: mont_mul(a,b)
 * computes a*b*R^-1, so operands must carry the factor R for the result
 * to stay in the domain (plain values only for parsing, range checks and
 * the order-n arithmetic, which converts properly). */
static struct {
    const EcdsaCurve *c;
    BigNum p, a, b, n;             /* plain curve constants */
    BigNum a_m, b_m;               /* a, b in the Montgomery domain */
    BnMont pm;                     /* Montgomery context for the field */
    EcPoint g;                     /* generator (Montgomery domain) */
    BnMont nm;                     /* Montgomery context for the order */
} ec;

static void ec_mul(BigNum *r, const BigNum *a, const BigNum *b)
{
    bn_mont_mul(r, a, b, &ec.pm);
}

static void ec_sqr(BigNum *r, const BigNum *a)
{
    bn_mont_mul(r, a, a, &ec.pm);
}

static void ec_add(BigNum *r, const BigNum *a, const BigNum *b)
{
    bn_mod_add(r, a, b, &ec.p);
}

static void ec_sub(BigNum *r, const BigNum *a, const BigNum *b)
{
    bn_mod_sub(r, a, b, &ec.p);
}

static bool ec_load(const EcdsaCurve *c)
{
    if (ec.c == c) return true;
    ec.c = c;
    bn_set_bytes(&ec.p, c->p, (size_t)c->bytes);
    bn_set_bytes(&ec.b, c->b, (size_t)c->bytes);
    bn_set_bytes(&ec.n, c->n, (size_t)c->bytes);
    static const BigNum three = { { 3, 0, 0, 0 }, 1 };
    BigNum a_plain = ec.p;
    bn_sub(&a_plain, &a_plain, &three);            /* a = p - 3 */
    if (!bn_mont_init(&ec.pm, &ec.p)) return false;
    if (!bn_mont_init(&ec.nm, &ec.n)) return false;
    bn_mont_from(&ec.a_m, &a_plain, &ec.pm);
    bn_mont_from(&ec.b_m, &ec.b, &ec.pm);
    BigNum gx, gy;
    bn_set_bytes(&gx, c->gx, (size_t)c->bytes);
    bn_set_bytes(&gy, c->gy, (size_t)c->bytes);
    bn_mont_from(&ec.g.x, &gx, &ec.pm);
    bn_mont_from(&ec.g.y, &gy, &ec.pm);
    BigNum one;
    bn_set_u32(&one, 1);
    bn_mont_from(&ec.g.z, &one, &ec.pm);           /* Z = 1 in the domain */
    return true;
}

/* r = 2 * pt. EFD dbl-2007-bl (general a). */
static void ec_double(EcPoint *r, const EcPoint *pt)
{
    static BigNum a2, b2, c2, d, e, f, t, z3;
    static EcPoint out;
    if (bn_is_zero(&pt->z)) {
        *r = *pt;
        return;
    }
    /* Z3 = 2*Y*Z first: r may alias pt, so nothing may overwrite the
     * inputs before every read of them is done. */
    ec_mul(&z3, &pt->y, &pt->z);
    ec_add(&z3, &z3, &z3);
    ec_sqr(&a2, &pt->x);                                  /* A = X^2 */
    ec_sqr(&b2, &pt->y);                                  /* B = Y^2 */
    ec_sqr(&c2, &b2);                                     /* C = B^2 */
    /* D = 2 * ((X + B)^2 - A - C) */
    ec_add(&t, &pt->x, &b2);
    ec_sqr(&d, &t);
    ec_sub(&d, &d, &a2);
    ec_sub(&d, &d, &c2);
    ec_add(&d, &d, &d);
    /* E = 3A + a*Z^4 */
    ec_sqr(&t, &pt->z);                                   /* Z^2 */
    ec_sqr(&t, &t);                                       /* Z^4 */
    ec_mul(&t, &t, &ec.a_m);
    ec_add(&e, &a2, &a2);
    ec_add(&e, &e, &a2);
    ec_add(&e, &e, &t);
    ec_sqr(&f, &e);                                       /* F = E^2 */
    /* X3 = F - 2D; Y3 = E*(D - X3) - 8C */
    ec_sub(&out.x, &f, &d);
    ec_sub(&out.x, &out.x, &d);
    ec_sub(&t, &d, &out.x);
    ec_mul(&t, &e, &t);
    ec_add(&c2, &c2, &c2);                                /* 2C */
    ec_add(&c2, &c2, &c2);                                /* 4C */
    ec_add(&c2, &c2, &c2);                                /* 8C */
    ec_sub(&out.y, &t, &c2);
    out.z = z3;
    *r = out;
}

/* r = p1 + p2. EFD add-2007-bl (general). Assumes p1 != p2 symbolically;
 * equal-coordinate cases handled by the h == 0 check. */
static void ec_add_points(EcPoint *r, const EcPoint *p1, const EcPoint *p2)
{
    static BigNum z1z1, z2z2, u1, u2, s1, s2, h, rr, hh, hhh, v, t;
    if (bn_is_zero(&p1->z)) {
        *r = *p2;
        return;
    }
    if (bn_is_zero(&p2->z)) {
        *r = *p1;
        return;
    }
    ec_sqr(&z1z1, &p1->z);
    ec_sqr(&z2z2, &p2->z);
    ec_mul(&u1, &p1->x, &z2z2);
    ec_mul(&u2, &p2->x, &z1z1);
    ec_mul(&s1, &p1->y, &p2->z);
    ec_mul(&s1, &s1, &z2z2);
    ec_mul(&s2, &p2->y, &p1->z);
    ec_mul(&s2, &s2, &z1z1);
    ec_sub(&h, &u2, &u1);
    ec_sub(&rr, &s2, &s1);                                /* r = S2 - S1 */
    if (bn_is_zero(&h)) {
        if (bn_is_zero(&rr)) {
            ec_double(r, p1);                             /* same point */
        } else {
            bn_zero(&r->x);
            bn_zero(&r->y);
            bn_zero(&r->z);                               /* P + (-P) = infinity */
        }
        return;
    }
    ec_sqr(&hh, &h);                                       /* H^2 */
    ec_mul(&hhh, &h, &hh);                                 /* H^3 */
    ec_mul(&v, &u1, &hh);                                  /* U1*H^2 */
    /* X3 = r^2 - H^3 - 2*U1*H^2 */
    ec_sqr(&r->x, &rr);
    ec_sub(&r->x, &r->x, &hhh);
    ec_sub(&r->x, &r->x, &v);
    ec_sub(&r->x, &r->x, &v);
    /* Y3 = r*(U1*H^2 - X3) - S1*H^3 */
    ec_sub(&t, &v, &r->x);
    ec_mul(&t, &rr, &t);
    ec_mul(&s1, &s1, &hhh);
    ec_sub(&t, &t, &s1);
    r->y = t;
    /* Z3 = Z1*Z2*H */
    ec_mul(&r->z, &p1->z, &p2->z);
    ec_mul(&r->z, &r->z, &h);
}

/* r = k * pt (MSB-first double-and-add). */
static void ec_scalar_mul(EcPoint *r, const BigNum *k, const EcPoint *pt)
{
    static EcPoint acc, tmp;
    bn_zero(&acc.x);
    bn_zero(&acc.y);
    bn_zero(&acc.z);                                      /* infinity */
    for (int i = bn_bits(k) - 1; i >= 0; i--) {
        ec_double(&acc, &acc);
        if (bn_bit(k, i)) {
            ec_add_points(&tmp, &acc, pt);
            acc = tmp;
        }
    }
    *r = acc;
}

/* Affine x of a Jacobian point, converted back to a plain value.
 * Z is inverted as a plain number; the zinv^2 chain runs in the domain
 * and mont_to lands on x/z^2. */
static bool ec_affine_x(BigNum *x_out, const EcPoint *pt)
{
    if (bn_is_zero(&pt->z)) return false;
    static BigNum z_plain, zinv, zinv_m, zinv2_m;
    bn_mont_to(&z_plain, &pt->z, &ec.pm);
    if (!bn_mod_inv_prime(&zinv, &z_plain, &ec.p)) return false;
    bn_mont_from(&zinv_m, &zinv, &ec.pm);
    bn_mont_mul(&zinv2_m, &zinv_m, &zinv_m, &ec.pm);
    bn_mont_mul(&zinv2_m, &pt->x, &zinv2_m, &ec.pm);
    bn_mont_to(x_out, &zinv2_m, &ec.pm);
    return true;
}

bool ecdsa_verify_raw(int curve_id, const uint8_t *pub, size_t pub_len,
                      const uint8_t *r_be, const uint8_t *s_be,
                      const uint8_t *hash, size_t hash_len)
{
    if (curve_id != ECDSA_CURVE_P256 && curve_id != ECDSA_CURVE_P384) return false;
    const EcdsaCurve *c = &ec_curves[curve_id == ECDSA_CURVE_P384];
    int cb = c->bytes;
    if (!hash || hash_len == 0 || !pub || !r_be || !s_be) return false;
    if (pub_len != (size_t)(1 + 2 * cb)) return false;
    if (!ec_load(c)) return false;

    /* Public key Q: uncompressed only, on-curve, 0 < x,y < p (plain
     * range checks first), then the point is converted to the Montgomery
     * domain for the scalar multiplication. */
    if (pub[0] != 0x04) return false;
    static BigNum qx, qy, t1, t2, t3;
    bn_set_bytes(&qx, pub + 1, (size_t)cb);
    bn_set_bytes(&qy, pub + 1 + (size_t)cb, (size_t)cb);
    if (bn_is_zero(&qx) || bn_is_zero(&qy)) return false;
    if (bn_cmp(&qx, &ec.p) >= 0 || bn_cmp(&qy, &ec.p) >= 0) return false;
    /* On-curve in the domain: (yR)^2 * R^-1 = y^2 * R, and the right
     * side accumulates the same R factor, so the comparison is valid. */
    static BigNum qx_m, qy_m;
    bn_mont_from(&qx_m, &qx, &ec.pm);
    bn_mont_from(&qy_m, &qy, &ec.pm);
    ec_sqr(&t1, &qy_m);                                   /* y^2 * R */
    ec_sqr(&t2, &qx_m);
    ec_mul(&t2, &t2, &qx_m);                              /* x^3 * R */
    ec_mul(&t3, &ec.a_m, &qx_m);                          /* a*x * R */
    ec_add(&t2, &t2, &t3);
    ec_add(&t2, &t2, &ec.b_m);
    if (bn_cmp(&t1, &t2) != 0) return false;

    /* r, s: fixed-length big-endian, 1 <= value < n. */
    static BigNum r, s, e, w, u1, u2;
    bn_set_bytes(&r, r_be, (size_t)cb);
    bn_set_bytes(&s, s_be, (size_t)cb);
    if (bn_is_zero(&r) || bn_cmp(&r, &ec.n) >= 0) return false;
    if (bn_is_zero(&s) || bn_cmp(&s, &ec.n) >= 0) return false;

    /* e = the leftmost bit-length-of-n bits of the hash, mod n (FIPS 186-4
     * 6.4; both orders are whole bytes long, so truncation is by bytes). */
    static BigNum hash_bn;
    if (hash_len > (size_t)cb) hash_len = (size_t)cb;
    bn_set_bytes(&hash_bn, hash, hash_len);
    if (!bn_mod_reduce(&e, &hash_bn, &ec.n)) return false;

    /* w = s^-1 mod n (n is prime); u1 = e*w; u2 = r*w (mod n) */
    if (!bn_mod_inv_prime(&w, &s, &ec.n)) return false;
    static BigNum am, bm;
    bn_mont_from(&am, &e, &ec.nm);
    bn_mont_from(&bm, &w, &ec.nm);
    bn_mont_mul(&u1, &am, &bm, &ec.nm);
    bn_mont_to(&u1, &u1, &ec.nm);
    bn_mont_from(&am, &r, &ec.nm);
    bn_mont_mul(&u2, &am, &bm, &ec.nm);
    bn_mont_to(&u2, &u2, &ec.nm);

    /* R = u1*G + u2*Q (both points in the Montgomery domain) */
    static EcPoint q, r1, r2, result;
    q.x = qx_m;
    q.y = qy_m;
    q.z = ec.g.z;                                          /* Z = 1 in the domain */
    ec_scalar_mul(&r1, &u1, &ec.g);
    ec_scalar_mul(&r2, &u2, &q);
    ec_add_points(&result, &r1, &r2);
    if (bn_is_zero(&result.z)) return false;

    static BigNum rx;
    if (!ec_affine_x(&rx, &result)) return false;
    if (bn_cmp(&rx, &ec.n) >= 0) bn_mod_reduce(&rx, &rx, &ec.n);
    return bn_cmp(&rx, &r) == 0;
}
