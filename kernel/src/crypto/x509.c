/*
 * ojjyOS v3 - Strict DER reader and X.509 certificate parser (see x509.h)
 *
 * Everything here reads attacker-controlled bytes: every length is
 * checked against the bytes that remain before it is used, lengths must
 * use the minimal DER form, and a certificate must use its buffer exactly.
 * Object identifiers are kept as their DER content bytes (RFC 5280,
 * RFC 4055, RFC 5480, RFC 5758, RFC 8017 appendix A).
 *
 * ojjyOS - Created by Jonas Lee
 */
#include "crypto/x509.h"
#include "crypto/crypto.h"
#include "string.h"

/* ------------------------------------------------------------------ */
/* DER                                                                  */
/* ------------------------------------------------------------------ */

#define TAG_BOOLEAN    0x01
#define TAG_INTEGER    0x02
#define TAG_BITSTRING  0x03
#define TAG_OCTETS     0x04
#define TAG_NULL       0x05
#define TAG_OID        0x06
#define TAG_UTF8       0x0c
#define TAG_PRINTABLE  0x13
#define TAG_T61        0x14
#define TAG_IA5        0x16
#define TAG_UTCTIME    0x17
#define TAG_GENTIME    0x18
#define TAG_BMP        0x1e
#define TAG_SEQUENCE   0x30
#define TAG_SET        0x31

typedef struct {
    const uint8_t *p, *end;
} Der;

static Der der_of(const X509Span *s) { Der d = {s->p, s->p + s->len}; return d; }
static bool der_done(const Der *d) { return d->p == d->end; }

/* One TLV. value = contents, whole = header + contents (either may be NULL). */
static bool der_read(Der *d, uint8_t *tag, X509Span *value, X509Span *whole)
{
    size_t avail = (size_t)(d->end - d->p);
    if (avail < 2) return false;
    const uint8_t *start = d->p;
    if ((start[0] & 0x1f) == 0x1f) return false;   /* high tag numbers: unused by X.509 */
    size_t o = 2, len = start[1];
    if (len & 0x80) {
        size_t bytes = len & 0x7f;
        if (bytes == 0 || bytes > 3 || avail - 2 < bytes) return false;   /* indefinite / > 16 MB */
        if (start[2] == 0) return false;            /* leading zero: not minimal */
        len = 0;
        for (size_t i = 0; i < bytes; i++) len = (len << 8) | start[o++];
        if (len < 0x80) return false;               /* must have used the short form */
    }
    if (len > avail - o) return false;
    if (tag) *tag = start[0];
    if (value) { value->p = start + o; value->len = len; }
    if (whole) { whole->p = start; whole->len = o + len; }
    d->p = start + o + len;
    return true;
}

bool x509_der_next(X509Span *rest, uint8_t *tag, X509Span *value)
{
    Der d = der_of(rest);
    if (!der_read(&d, tag, value, NULL)) return false;
    rest->len -= (size_t)(d.p - rest->p);
    rest->p = d.p;
    return true;
}

static bool der_expect(Der *d, uint8_t tag, X509Span *value, X509Span *whole)
{
    uint8_t t;
    Der save = *d;
    if (!der_read(d, &t, value, whole) || t != tag) { *d = save; return false; }
    return true;
}

static bool der_peek(const Der *d, uint8_t tag)
{
    return d->p < d->end && d->p[0] == tag;
}

static bool span_is(const X509Span *s, const uint8_t *bytes, size_t len)
{
    return s->len == len && memcmp(s->p, bytes, len) == 0;
}
#define SPAN_IS(span, arr) span_is((span), (arr), sizeof(arr))

/* Positive INTEGER without the sign octet; rejects negatives and
 * non-minimal encodings. */
static bool der_uint(const X509Span *v, X509Span *out)
{
    if (v->len == 0 || (v->p[0] & 0x80)) return false;
    if (v->len > 1 && v->p[0] == 0 && !(v->p[1] & 0x80)) return false;
    out->p = v->p;
    out->len = v->len;
    if (out->len > 1 && out->p[0] == 0) { out->p++; out->len--; }
    return true;
}

static bool der_small_uint(const X509Span *v, uint32_t max, uint32_t *out)
{
    X509Span u;
    if (!der_uint(v, &u) || u.len > 4) return false;
    uint64_t x = 0;
    for (size_t i = 0; i < u.len; i++) x = (x << 8) | u.p[i];
    if (x > max) return false;
    *out = (uint32_t)x;
    return true;
}

/* ------------------------------------------------------------------ */
/* Object identifiers (DER contents)                                    */
/* ------------------------------------------------------------------ */

static const uint8_t oid_rsa_encryption[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01};
static const uint8_t oid_mgf1[]           = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08};
static const uint8_t oid_rsassa_pss[]     = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0a};
static const uint8_t oid_sha256_rsa[]     = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b};
static const uint8_t oid_sha384_rsa[]     = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0c};
static const uint8_t oid_sha512_rsa[]     = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0d};
static const uint8_t oid_ec_public_key[]  = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01};
static const uint8_t oid_prime256v1[]     = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
static const uint8_t oid_secp384r1[]      = {0x2b, 0x81, 0x04, 0x00, 0x22};
static const uint8_t oid_ecdsa_sha256[]   = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02};
static const uint8_t oid_ecdsa_sha384[]   = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x03};
static const uint8_t oid_sha256[]         = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01};
static const uint8_t oid_sha384[]         = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02};
static const uint8_t oid_sha512[]         = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03};

/* certificate extensions: id-ce (2.5.29.x) */
#define CE_KEY_USAGE        0x0f
#define CE_SUBJECT_ALT_NAME 0x11
#define CE_BASIC            0x13
#define CE_NAME_CONSTRAINTS 0x1e
#define CE_POLICIES         0x20
#define CE_POLICY_CONSTR    0x24
#define CE_EXT_KEY_USAGE    0x25
#define CE_INHIBIT_ANY      0x36
static const uint8_t oid_kp_server_auth[] = {0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01};
static const uint8_t oid_any_eku[]        = {0x55, 0x1d, 0x25, 0x00};

/* ------------------------------------------------------------------ */
/* Algorithms                                                           */
/* ------------------------------------------------------------------ */

/* HashAlgorithm inside RSASSA-PSS params: SEQUENCE { OID, NULL? } */
static int pss_hash_of(const X509Span *alg_seq)
{
    Der d = der_of(alg_seq);
    X509Span oid, null;
    if (!der_expect(&d, TAG_OID, &oid, NULL)) return 0;
    if (der_expect(&d, TAG_NULL, &null, NULL) && null.len != 0) return 0;
    if (!der_done(&d)) return 0;
    if (SPAN_IS(&oid, oid_sha256)) return 256;
    if (SPAN_IS(&oid, oid_sha384)) return 384;
    if (SPAN_IS(&oid, oid_sha512)) return 512;
    return 0;
}

/* RSASSA-PSS-params (RFC 4055 3.1). Only SHA-2 with MGF1 over the same
 * hash and the standard trailer are supported (the SHA-1 defaults are not). */
static int parse_pss_params(const X509Span *params, int *salt)
{
    Der d = der_of(params);
    X509Span v, inner;
    int hash = 0, mgf_hash = 0;
    uint32_t salt_len = 20, trailer = 1;
    if (der_expect(&d, 0xa0, &v, NULL)) {
        Der e = der_of(&v);
        if (!der_expect(&e, TAG_SEQUENCE, &inner, NULL) || !der_done(&e)) return X509_SIG_NONE;
        hash = pss_hash_of(&inner);
    }
    if (der_expect(&d, 0xa1, &v, NULL)) {
        Der e = der_of(&v);
        X509Span mgf, oid, hash_seq;
        if (!der_expect(&e, TAG_SEQUENCE, &mgf, NULL) || !der_done(&e)) return X509_SIG_NONE;
        Der m = der_of(&mgf);
        if (!der_expect(&m, TAG_OID, &oid, NULL) || !SPAN_IS(&oid, oid_mgf1)) return X509_SIG_NONE;
        if (!der_expect(&m, TAG_SEQUENCE, &hash_seq, NULL) || !der_done(&m)) return X509_SIG_NONE;
        mgf_hash = pss_hash_of(&hash_seq);
    }
    if (der_expect(&d, 0xa2, &v, NULL)) {
        Der e = der_of(&v);
        X509Span n;
        if (!der_expect(&e, TAG_INTEGER, &n, NULL) || !der_done(&e) || !der_small_uint(&n, 512, &salt_len))
            return X509_SIG_NONE;
    }
    if (der_expect(&d, 0xa3, &v, NULL)) {
        Der e = der_of(&v);
        X509Span n;
        if (!der_expect(&e, TAG_INTEGER, &n, NULL) || !der_done(&e) || !der_small_uint(&n, 1, &trailer))
            return X509_SIG_NONE;
    }
    if (!der_done(&d) || trailer != 1 || hash == 0 || mgf_hash != hash) return X509_SIG_NONE;
    *salt = (int)salt_len;
    return hash == 256 ? X509_SIG_RSA_PSS_SHA256 : hash == 384 ? X509_SIG_RSA_PSS_SHA384 : X509_SIG_RSA_PSS_SHA512;
}

/* AlgorithmIdentifier of a signature. Unknown algorithms give X509_SIG_NONE
 * (still well-formed); a broken structure returns false. */
static bool parse_sig_alg(const X509Span *seq, int *alg, int *salt)
{
    Der d = der_of(seq);
    X509Span oid, params;
    uint8_t tag = 0;
    if (!der_expect(&d, TAG_OID, &oid, NULL)) return false;
    bool have_params = !der_done(&d);
    if (have_params && !der_read(&d, &tag, &params, NULL)) return false;
    if (!der_done(&d)) return false;
    bool null_or_absent = !have_params || (tag == TAG_NULL && params.len == 0);
    *alg = X509_SIG_NONE;
    *salt = -1;
    if (SPAN_IS(&oid, oid_sha256_rsa) && null_or_absent) *alg = X509_SIG_RSA_PKCS1_SHA256;
    else if (SPAN_IS(&oid, oid_sha384_rsa) && null_or_absent) *alg = X509_SIG_RSA_PKCS1_SHA384;
    else if (SPAN_IS(&oid, oid_sha512_rsa) && null_or_absent) *alg = X509_SIG_RSA_PKCS1_SHA512;
    else if (SPAN_IS(&oid, oid_ecdsa_sha256) && !have_params) *alg = X509_SIG_ECDSA_SHA256;
    else if (SPAN_IS(&oid, oid_ecdsa_sha384) && !have_params) *alg = X509_SIG_ECDSA_SHA384;
    else if (SPAN_IS(&oid, oid_rsassa_pss) && have_params && tag == TAG_SEQUENCE) *alg = parse_pss_params(&params, salt);
    return true;
}

bool x509_parse_spki(const uint8_t *der, size_t len, X509Key *key)
{
    memset(key, 0, sizeof(*key));
    X509Span all = {der, len}, body, alg, bits;
    Der top = der_of(&all);
    if (!der_expect(&top, TAG_SEQUENCE, &body, NULL) || !der_done(&top)) return false;
    Der d = der_of(&body);
    if (!der_expect(&d, TAG_SEQUENCE, &alg, NULL)) return false;
    if (!der_expect(&d, TAG_BITSTRING, &bits, NULL) || !der_done(&d)) return false;
    if (bits.len < 1 || bits.p[0] != 0) return false;           /* whole octets only */
    X509Span keybits = {bits.p + 1, bits.len - 1};

    Der a = der_of(&alg);
    X509Span oid, param;
    uint8_t tag = 0;
    if (!der_expect(&a, TAG_OID, &oid, NULL)) return false;
    bool have_param = !der_done(&a);
    if (have_param && !der_read(&a, &tag, &param, NULL)) return false;
    if (!der_done(&a)) return false;

    if (SPAN_IS(&oid, oid_rsa_encryption)) {
        if (have_param && !(tag == TAG_NULL && param.len == 0)) return false;
        Der k = der_of(&keybits);
        X509Span seq, n, e;
        if (!der_expect(&k, TAG_SEQUENCE, &seq, NULL) || !der_done(&k)) return false;
        Der r = der_of(&seq);
        if (!der_expect(&r, TAG_INTEGER, &n, NULL) || !der_expect(&r, TAG_INTEGER, &e, NULL) || !der_done(&r))
            return false;
        if (!der_uint(&n, &key->n) || key->n.len < 64 || !(key->n.p[key->n.len - 1] & 1)) return false;
        if (!der_small_uint(&e, 0xffffffffu, &key->e) || key->e < 3 || !(key->e & 1)) return false;
        key->type = X509_KEY_RSA;
    } else if (SPAN_IS(&oid, oid_ec_public_key)) {
        if (!have_param || tag != TAG_OID) return false;            /* namedCurve only */
        if (SPAN_IS(&param, oid_prime256v1) && keybits.len == 65 && keybits.p[0] == 4) key->type = X509_KEY_EC_P256;
        else if (SPAN_IS(&param, oid_secp384r1) && keybits.len == 97 && keybits.p[0] == 4) key->type = X509_KEY_EC_P384;
        if (key->type) key->point = keybits;
    }
    /* other algorithms (Ed25519, P-521, PSS-only RSA keys): type NONE */
    return true;
}

int x509_key_bits(const X509Key *key)
{
    if (key->type == X509_KEY_EC_P256) return 256;
    if (key->type == X509_KEY_EC_P384) return 384;
    if (key->type != X509_KEY_RSA || !key->n.len) return 0;
    int bits = (int)key->n.len * 8;
    for (uint8_t top = key->n.p[0]; !(top & 0x80); top <<= 1) bits--;
    return bits;
}

/* ------------------------------------------------------------------ */
/* Signatures                                                           */
/* ------------------------------------------------------------------ */

/* hash 'msg' as the algorithm needs; returns the digest length */
static size_t digest_for(int sig_alg, const uint8_t *msg, size_t len, uint8_t out[64], RsaHashId *id)
{
    switch (sig_alg) {
        case X509_SIG_RSA_PKCS1_SHA256: case X509_SIG_RSA_PSS_SHA256: case X509_SIG_ECDSA_SHA256:
            sha256(msg, len, out); *id = RSA_HASH_SHA256; return 32;
        case X509_SIG_RSA_PKCS1_SHA384: case X509_SIG_RSA_PSS_SHA384: case X509_SIG_ECDSA_SHA384:
            sha384(msg, len, out); *id = RSA_HASH_SHA384; return 48;
        case X509_SIG_RSA_PKCS1_SHA512: case X509_SIG_RSA_PSS_SHA512:
            sha512(msg, len, out); *id = RSA_HASH_SHA512; return 64;
        default:
            return 0;
    }
}

/* ECDSA-Sig-Value ::= SEQUENCE { r INTEGER, s INTEGER } -> fixed width */
static bool ecdsa_sig_split(const uint8_t *sig, size_t len, size_t width, uint8_t *r, uint8_t *s)
{
    X509Span all = {sig, len}, seq, ri, si, ru, su;
    Der top = der_of(&all);
    if (!der_expect(&top, TAG_SEQUENCE, &seq, NULL) || !der_done(&top)) return false;
    Der d = der_of(&seq);
    if (!der_expect(&d, TAG_INTEGER, &ri, NULL) || !der_expect(&d, TAG_INTEGER, &si, NULL) || !der_done(&d))
        return false;
    if (!der_uint(&ri, &ru) || !der_uint(&si, &su) || ru.len > width || su.len > width) return false;
    memset(r, 0, width);
    memset(s, 0, width);
    memcpy(r + width - ru.len, ru.p, ru.len);
    memcpy(s + width - su.len, su.p, su.len);
    return true;
}

bool x509_verify_message(int sig_alg, int pss_salt, const X509Key *key,
                         const uint8_t *msg, size_t msg_len,
                         const uint8_t *sig, size_t sig_len)
{
    uint8_t digest[64];
    RsaHashId id = RSA_HASH_SHA256;
    size_t dlen = digest_for(sig_alg, msg, msg_len, digest, &id);
    if (!dlen || !key) return false;
    switch (sig_alg) {
        case X509_SIG_RSA_PKCS1_SHA256: case X509_SIG_RSA_PKCS1_SHA384: case X509_SIG_RSA_PKCS1_SHA512:
            return key->type == X509_KEY_RSA &&
                   rsa_verify_pkcs1(key->n.p, key->n.len, key->e, sig, sig_len, id, digest);
        case X509_SIG_RSA_PSS_SHA256: case X509_SIG_RSA_PSS_SHA384: case X509_SIG_RSA_PSS_SHA512:
            return key->type == X509_KEY_RSA &&
                   rsa_verify_pss(key->n.p, key->n.len, key->e, sig, sig_len, id, digest, pss_salt);
        case X509_SIG_ECDSA_SHA256: case X509_SIG_ECDSA_SHA384: {
            if (key->type != X509_KEY_EC_P256 && key->type != X509_KEY_EC_P384) return false;
            size_t width = key->type == X509_KEY_EC_P256 ? 32 : 48;
            uint8_t r[48], s[48];
            if (!ecdsa_sig_split(sig, sig_len, width, r, s)) return false;
            return ecdsa_verify_raw(key->type == X509_KEY_EC_P256 ? ECDSA_CURVE_P256 : ECDSA_CURVE_P384,
                                    key->point.p, key->point.len, r, s, digest, dlen);
        }
        default:
            return false;
    }
}

bool x509_signed_by(const X509Cert *child, const X509Key *issuer_key)
{
    return x509_verify_message(child->sig_alg, child->pss_salt, issuer_key,
                               child->tbs.p, child->tbs.len, child->sig.p, child->sig.len);
}

/* ------------------------------------------------------------------ */
/* Names and time                                                       */
/* ------------------------------------------------------------------ */

static void copy_text(uint8_t tag, const X509Span *v, char *out, size_t size)
{
    size_t k = 0;
    bool bmp = tag == TAG_BMP;
    for (size_t i = 0; i < v->len && k + 1 < size; i++) {
        uint32_t c = v->p[i];
        if (bmp) {                                   /* UCS-2: keep ASCII, '?' otherwise */
            if (i + 1 >= v->len) break;
            c = ((uint32_t)v->p[i] << 8) | v->p[i + 1];
            i++;
            if (c >= 0x80) c = '?';
        }
        if (c < 0x20 || c == 0x7f) c = '?';
        out[k++] = (char)c;
    }
    out[k] = '\0';
}

bool x509_name_attr(const X509Span *name, int attr, char *out, size_t size)
{
    if (!size) return false;
    out[0] = '\0';
    Der top = der_of(name);
    X509Span rdns, set;
    if (!der_expect(&top, TAG_SEQUENCE, &rdns, NULL)) return false;
    Der d = der_of(&rdns);
    while (der_expect(&d, TAG_SET, &set, NULL)) {
        Der s = der_of(&set);
        X509Span atv;
        while (der_expect(&s, TAG_SEQUENCE, &atv, NULL)) {
            Der a = der_of(&atv);
            X509Span oid, value;
            uint8_t tag;
            if (!der_expect(&a, TAG_OID, &oid, NULL) || !der_read(&a, &tag, &value, NULL)) break;
            const uint8_t want[3] = {0x55, 0x04, (uint8_t)attr};    /* id-at (2.5.4.x) */
            if (!SPAN_IS(&oid, want)) continue;
            if (tag != TAG_UTF8 && tag != TAG_PRINTABLE && tag != TAG_IA5 && tag != TAG_T61 && tag != TAG_BMP) continue;
            copy_text(tag, &value, out, size);
            return true;
        }
    }
    return false;
}

static int64_t days_from_civil(int64_t y, int m, int d)
{
    /* Proleptic Gregorian, March-based year so the leap day is last. */
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int digits(const uint8_t *p, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9') return -1;
        v = v * 10 + (p[i] - '0');
    }
    return v;
}

/* UTCTime "YYMMDDHHMMSSZ" or GeneralizedTime "YYYYMMDDHHMMSSZ" (DER forms) */
static bool parse_time(uint8_t tag, const X509Span *v, int64_t *out)
{
    int year;
    const uint8_t *p = v->p;
    if (tag == TAG_UTCTIME && v->len == 13) {
        year = digits(p, 2);
        if (year < 0) return false;
        year += year >= 50 ? 1900 : 2000;
        p += 2;
    } else if (tag == TAG_GENTIME && v->len == 15) {
        year = digits(p, 4);
        if (year < 0) return false;
        p += 4;
    } else {
        return false;
    }
    int month = digits(p, 2), day = digits(p + 2, 2), hour = digits(p + 4, 2);
    int minute = digits(p + 6, 2), second = digits(p + 8, 2);
    if (p[10] != 'Z' || month < 1 || month > 12 || day < 1 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59)
        return false;
    static const uint8_t mdays[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (day > mdays[month - 1] || (month == 2 && day == 29 && !leap)) return false;
    *out = days_from_civil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
    return true;
}

/* ------------------------------------------------------------------ */
/* Extensions                                                           */
/* ------------------------------------------------------------------ */

static bool parse_basic(const X509Span *v, X509Cert *c)
{
    Der top = der_of(v);
    X509Span seq, b, n;
    if (!der_expect(&top, TAG_SEQUENCE, &seq, NULL) || !der_done(&top)) return false;
    Der d = der_of(&seq);
    c->basic_present = true;
    if (der_expect(&d, TAG_BOOLEAN, &b, NULL)) {
        if (b.len != 1 || (b.p[0] != 0xff && b.p[0] != 0)) return false;
        c->ca = b.p[0] == 0xff;
    }
    if (der_expect(&d, TAG_INTEGER, &n, NULL)) {
        uint32_t limit;
        if (!der_small_uint(&n, 255, &limit)) return false;
        c->path_len = (int)limit;
    }
    return der_done(&d);
}

static bool parse_key_usage(const X509Span *v, X509Cert *c)
{
    Der top = der_of(v);
    X509Span bits;
    if (!der_expect(&top, TAG_BITSTRING, &bits, NULL) || !der_done(&top)) return false;
    if (bits.len < 1 || bits.p[0] > 7 || (bits.len == 1 && bits.p[0] != 0)) return false;
    c->ku_present = true;
    c->key_usage = 0;
    for (size_t i = 1; i < bits.len && i <= 2; i++)
        for (int b = 0; b < 8; b++)
            if (bits.p[i] & (0x80 >> b)) c->key_usage |= 1u << ((i - 1) * 8 + (size_t)b);
    return true;
}

static bool parse_eku(const X509Span *v, X509Cert *c)
{
    Der top = der_of(v);
    X509Span seq, oid;
    if (!der_expect(&top, TAG_SEQUENCE, &seq, NULL) || !der_done(&top)) return false;
    Der d = der_of(&seq);
    c->eku_present = true;
    while (!der_done(&d)) {
        if (!der_expect(&d, TAG_OID, &oid, NULL)) return false;
        if (SPAN_IS(&oid, oid_kp_server_auth)) c->eku_server_auth = true;
        if (SPAN_IS(&oid, oid_any_eku)) c->eku_any = true;
    }
    return true;
}

static bool parse_extensions(const X509Span *v, X509Cert *c)
{
    Der top = der_of(v);
    X509Span list, ext;
    if (!der_expect(&top, TAG_SEQUENCE, &list, NULL) || !der_done(&top) || list.len == 0) return false;
    Der d = der_of(&list);
    uint64_t seen = 0;
    while (!der_done(&d)) {
        if (!der_expect(&d, TAG_SEQUENCE, &ext, NULL)) return false;
        Der e = der_of(&ext);
        X509Span oid, flag, value;
        bool critical = false;
        if (!der_expect(&e, TAG_OID, &oid, NULL)) return false;
        if (der_expect(&e, TAG_BOOLEAN, &flag, NULL)) {
            if (flag.len != 1 || (flag.p[0] != 0xff && flag.p[0] != 0)) return false;
            critical = flag.p[0] == 0xff;
        }
        if (!der_expect(&e, TAG_OCTETS, &value, NULL) || !der_done(&e)) return false;

        int id = oid.len == 3 && oid.p[0] == 0x55 && oid.p[1] == 0x1d ? oid.p[2] : -1;
        if (id >= 0 && id < 64) {               /* one instance per extension (RFC 5280 4.2) */
            if (seen & (1ull << id)) return false;
            seen |= 1ull << id;
        }
        bool ok = true;
        switch (id) {
            case CE_BASIC: ok = parse_basic(&value, c); break;
            case CE_KEY_USAGE: ok = parse_key_usage(&value, c); break;
            case CE_EXT_KEY_USAGE: ok = parse_eku(&value, c); break;
            case CE_SUBJECT_ALT_NAME: {
                Der s = der_of(&value);
                ok = der_expect(&s, TAG_SEQUENCE, &c->san, NULL) && der_done(&s);
                c->san_present = ok;
                break;
            }
            case CE_NAME_CONSTRAINTS: {
                Der s = der_of(&value);
                ok = der_expect(&s, TAG_SEQUENCE, &c->name_constraints, NULL) && der_done(&s);
                c->nc_present = ok;
                break;
            }
            case CE_POLICIES: case CE_POLICY_CONSTR: case CE_INHIBIT_ANY:
                break;                          /* understood; policies are not enforced */
            default:
                if (critical) c->unknown_critical = true;
                break;
        }
        if (!ok) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Certificate                                                          */
/* ------------------------------------------------------------------ */

bool x509_parse(const uint8_t *der, size_t len, X509Cert *c)
{
    memset(c, 0, sizeof(*c));
    c->path_len = -1;
    c->pss_salt = -1;
    X509Span all = {der, len}, cert, tbs_body, outer_alg, outer_alg_whole, sigbits;
    Der top = der_of(&all);
    if (!der_expect(&top, TAG_SEQUENCE, &cert, &c->der) || !der_done(&top)) return false;
    Der d = der_of(&cert);
    if (!der_expect(&d, TAG_SEQUENCE, &tbs_body, &c->tbs)) return false;
    if (!der_expect(&d, TAG_SEQUENCE, &outer_alg, &outer_alg_whole)) return false;
    if (!der_expect(&d, TAG_BITSTRING, &sigbits, NULL) || !der_done(&d)) return false;
    if (sigbits.len < 2 || sigbits.p[0] != 0) return false;
    c->sig.p = sigbits.p + 1;
    c->sig.len = sigbits.len - 1;

    Der t = der_of(&tbs_body);
    X509Span v, inner_alg, inner_alg_whole, validity, ext;
    c->version = 1;
    if (der_expect(&t, 0xa0, &v, NULL)) {
        Der e = der_of(&v);
        X509Span n;
        uint32_t ver;
        if (!der_expect(&e, TAG_INTEGER, &n, NULL) || !der_done(&e) || !der_small_uint(&n, 2, &ver)) return false;
        if (ver == 0) return false;                 /* DEFAULT v1 must be omitted in DER */
        c->version = (int)ver + 1;
    }
    X509Span serial;
    if (!der_expect(&t, TAG_INTEGER, &serial, NULL) || serial.len == 0 || serial.len > 21) return false;
    c->serial = serial;
    if (!der_expect(&t, TAG_SEQUENCE, &inner_alg, &inner_alg_whole)) return false;
    /* the signed and unsigned algorithm identifiers must be identical */
    if (inner_alg_whole.len != outer_alg_whole.len ||
        memcmp(inner_alg_whole.p, outer_alg_whole.p, inner_alg_whole.len) != 0)
        return false;
    if (!parse_sig_alg(&outer_alg, &c->sig_alg, &c->pss_salt)) return false;
    if (!der_expect(&t, TAG_SEQUENCE, NULL, &c->issuer)) return false;
    if (!der_expect(&t, TAG_SEQUENCE, &validity, NULL)) return false;
    {
        Der e = der_of(&validity);
        X509Span tv;
        uint8_t tag;
        if (!der_read(&e, &tag, &tv, NULL) || !parse_time(tag, &tv, &c->not_before)) return false;
        if (!der_read(&e, &tag, &tv, NULL) || !parse_time(tag, &tv, &c->not_after)) return false;
        if (!der_done(&e)) return false;
    }
    if (!der_expect(&t, TAG_SEQUENCE, NULL, &c->subject)) return false;
    if (!der_expect(&t, TAG_SEQUENCE, NULL, &c->spki)) return false;
    if (!x509_parse_spki(c->spki.p, c->spki.len, &c->key)) return false;
    if (der_peek(&t, 0x81) && !der_read(&t, NULL, NULL, NULL)) return false;    /* unique IDs: skipped */
    if (der_peek(&t, 0x82) && !der_read(&t, NULL, NULL, NULL)) return false;
    if (der_expect(&t, 0xa3, &ext, NULL)) {
        if (c->version != 3 || !parse_extensions(&ext, c)) return false;
    }
    return der_done(&t);
}
