/*
 * ojjyOS v3 - Certificate path building and server identity (see x509.h)
 *
 * The leaf is checked first (host name, dates, key, usage), then a path is
 * searched depth-first through the certificates the server sent, in any
 * order, until some certificate's issuer is a trusted root whose key
 * verifies it. When every path fails, the reason reported is the one
 * found furthest along a path, so a broken intermediate is named instead
 * of a vague "unknown issuer".
 *
 * Parsed certificates live in a static table (kernel stacks are small);
 * like the bignum code this is not reentrant.
 *
 * ojjyOS - Created by Jonas Lee
 */
#include "crypto/x509.h"
#include "string.h"

static X509Cert chain[X509_MAX_CHAIN];

/* ------------------------------------------------------------------ */
/* Text helpers                                                         */
/* ------------------------------------------------------------------ */

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool ieq(const uint8_t *a, const char *b, size_t len)
{
    for (size_t i = 0; i < len; i++)
        if (lower((char)a[i]) != lower(b[i])) return false;
    return true;
}

static void put(char *out, size_t size, const char *a, const char *b, const char *c)
{
    size_t k = 0;
    const char *parts[3] = {a, b, c};
    for (int p = 0; p < 3; p++)
        for (const char *s = parts[p]; s && *s && k + 1 < size; s++) out[k++] = *s;
    out[k] = '\0';
}

/* "YYYY-MM-DD" (civil-from-days, the inverse of x509.c's conversion) */
static void format_date(int64_t t, char out[11])
{
    int64_t z = (t >= 0 ? t : t - 86399) / 86400 + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp + (mp < 10 ? 3 : -9);
    int64_t y = yoe + era * 400 + (m <= 2);
    if (y < 0 || y > 9999) y = 0;
    out[0] = (char)('0' + y / 1000); out[1] = (char)('0' + y / 100 % 10);
    out[2] = (char)('0' + y / 10 % 10); out[3] = (char)('0' + y % 10);
    out[4] = '-'; out[5] = (char)('0' + m / 10); out[6] = (char)('0' + m % 10);
    out[7] = '-'; out[8] = (char)('0' + d / 10); out[9] = (char)('0' + d % 10);
    out[10] = '\0';
}

static void display_name(const X509Span *name, char *out, size_t size)
{
    if (!x509_name_attr(name, X509_ATTR_CN, out, size) || !out[0])
        if (!x509_name_attr(name, X509_ATTR_O, out, size) || !out[0]) put(out, size, "(unnamed)", NULL, NULL);
}

/* ------------------------------------------------------------------ */
/* Host names (RFC 6125) and name constraints (RFC 5280 4.2.1.10)       */
/* ------------------------------------------------------------------ */

static bool parse_ipv4(const char *s, size_t len, uint8_t out[4])
{
    int part = 0, digits = 0, value = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || s[i] == '.') {
            if (!digits || part > 3) return false;
            out[part++] = (uint8_t)value;
            digits = value = 0;
        } else if (s[i] >= '0' && s[i] <= '9' && digits < 3) {
            value = value * 10 + (s[i] - '0');
            if (value > 255) return false;
            digits++;
        } else {
            return false;
        }
    }
    return part == 4;
}

/* A SAN dNSName against the host: exact, or "*." + at least two labels
 * matching everything after the host's first (non-empty) label. */
static bool dns_match(const uint8_t *pat, size_t plen, const char *host, size_t hlen)
{
    for (size_t i = 0; i < plen; i++)
        if (pat[i] == '*' && !(i == 0 && plen > 2 && pat[1] == '.')) return false;   /* only "*." prefix */
    if (plen && pat[plen - 1] == '.') plen--;
    if (pat[0] != '*') return plen == hlen && ieq(pat, host, hlen);
    const uint8_t *rest = pat + 2;
    size_t rlen = plen - 2, dots = 0;
    for (size_t i = 0; i < rlen; i++) dots += rest[i] == '.';
    if (!dots || !rlen) return false;                 /* "*.com" is never acceptable */
    size_t first = 0;
    while (first < hlen && host[first] != '.') first++;
    if (first == 0 || first >= hlen) return false;
    return hlen - first - 1 == rlen && ieq(rest, host + first + 1, rlen);
}

bool x509_host_matches(const X509Cert *leaf, const char *host)
{
    if (!leaf->san_present || !host) return false;
    size_t hlen = strlen(host);
    if (hlen && host[hlen - 1] == '.') hlen--;
    if (!hlen || hlen > 253) return false;
    uint8_t ip[4];
    bool is_ip = parse_ipv4(host, hlen, ip);
    X509Span rest = leaf->san, v;
    uint8_t tag;
    while (rest.len && x509_der_next(&rest, &tag, &v)) {
        if (tag == 0x82 && !is_ip && v.len && dns_match(v.p, v.len, host, hlen)) return true;
        if (tag == 0x87 && is_ip && v.len == 4 && memcmp(v.p, ip, 4) == 0) return true;
    }
    return false;
}

/* name inside a dNSName constraint: "example.com" covers itself and its
 * subdomains, ".example.com" only subdomains, "" everything. */
static bool dns_within(const uint8_t *name, size_t nlen, const uint8_t *base, size_t blen)
{
    if (blen == 0) return true;
    if (nlen && name[nlen - 1] == '.') nlen--;
    if (base[0] == '.') {
        if (nlen <= blen) return false;
        for (size_t i = 0; i < blen; i++) if (lower((char)name[nlen - blen + i]) != lower((char)base[i])) return false;
        return true;
    }
    if (nlen < blen) return false;
    for (size_t i = 0; i < blen; i++) if (lower((char)name[nlen - blen + i]) != lower((char)base[i])) return false;
    return nlen == blen || name[nlen - blen - 1] == '.';
}

static bool ip_within(const uint8_t *ip, size_t len, const uint8_t *base, size_t blen)
{
    if (len != 4 || blen != 8) return false;
    for (int i = 0; i < 4; i++) if ((ip[i] & base[4 + i]) != (base[i] & base[4 + i])) return false;
    return true;
}

/* One name against a GeneralSubtrees list of the same type. *seen tells
 * whether the list constrains that type at all. False = bad DER. */
static bool subtrees_match(const X509Span *subtrees, uint8_t type, const uint8_t *name, size_t len,
                           bool *seen, bool *hit)
{
    X509Span rest = *subtrees, subtree;
    uint8_t tag;
    *seen = *hit = false;
    while (rest.len) {
        if (!x509_der_next(&rest, &tag, &subtree) || tag != 0x30) return false;
        X509Span inner = subtree, base;
        uint8_t btag;
        if (!x509_der_next(&inner, &btag, &base)) return false;
        if (btag != type) continue;
        *seen = true;
        if (type == 0x82 ? dns_within(name, len, base.p, base.len) : ip_within(name, len, base.p, base.len)) *hit = true;
    }
    return true;
}

static bool name_constraints_ok(const X509Cert *ca, const X509Cert *leaf)
{
    X509Span rest = ca->name_constraints, value, permitted = {0, 0}, excluded = {0, 0};
    uint8_t tag;
    while (rest.len) {
        if (!x509_der_next(&rest, &tag, &value)) return false;
        if (tag == 0xa0) permitted = value;
        else if (tag == 0xa1) excluded = value;
        else return false;
    }
    if (!leaf->san_present) return true;
    X509Span names = leaf->san, name;
    while (names.len) {
        if (!x509_der_next(&names, &tag, &name)) return false;
        if (tag != 0x82 && tag != 0x87) continue;
        bool seen, hit;
        if (!subtrees_match(&excluded, tag, name.p, name.len, &seen, &hit) || hit) return false;
        if (!subtrees_match(&permitted, tag, name.p, name.len, &seen, &hit) || (seen && !hit)) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Path search                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    const X509Policy *policy;
    size_t count;
    bool used[X509_MAX_CHAIN];
    X509Status best;
    int best_depth;
    char reason[96];
    const X509Root *anchor;
    int depth;
} Search;

/* Keep the failure found furthest along a path. */
static void note(Search *s, int depth, X509Status status, const char *a, const char *b, const char *c)
{
    if (s->best != X509_OK && depth <= s->best_depth) return;
    s->best = status;
    s->best_depth = depth;
    put(s->reason, sizeof(s->reason), a, b, c);
}

static bool same_name(const X509Span *a, const uint8_t *b, size_t blen)
{
    return a->len == blen && memcmp(a->p, b, blen) == 0;
}

static bool time_ok(Search *s, int depth, const X509Cert *c, const char *what)
{
    char date[11];
    if (s->policy->now + s->policy->slack < c->not_before) {
        format_date(c->not_before, date);
        note(s, depth, X509_E_NOT_YET_VALID, what, " not valid until ", date);
        return false;
    }
    if (s->policy->now - s->policy->slack > c->not_after) {
        format_date(c->not_after, date);
        note(s, depth, X509_E_EXPIRED, what, " expired on ", date);
        return false;
    }
    return true;
}

/* May this certificate issue the one below it (below = CAs under it)? */
static bool ca_ok(Search *s, const X509Cert *ca, int depth, int below)
{
    char name[48];
    display_name(&ca->subject, name, sizeof(name));
    if (!time_ok(s, depth, ca, "intermediate certificate")) return false;
    if (ca->version != 3 || !ca->basic_present || !ca->ca) {
        note(s, depth, X509_E_NOT_CA, "issuer is not a certificate authority: ", name, NULL);
        return false;
    }
    if (ca->path_len >= 0 && below > ca->path_len) {
        note(s, depth, X509_E_PATH_LEN, "certificate chain too long for ", name, " (pathLen)");
        return false;
    }
    if (ca->ku_present && !(ca->key_usage & X509_KU_KEY_CERT_SIGN)) {
        note(s, depth, X509_E_KEY_USAGE, "issuer may not sign certificates: ", name, NULL);
        return false;
    }
    if (ca->eku_present && !ca->eku_server_auth && !ca->eku_any) {
        note(s, depth, X509_E_EKU, "issuer is not for web servers: ", name, NULL);
        return false;
    }
    if (ca->unknown_critical) {
        note(s, depth, X509_E_CRITICAL, "issuer has an unsupported critical extension: ", name, NULL);
        return false;
    }
    if (ca->key.type == X509_KEY_NONE) {
        note(s, depth, X509_E_UNSUPPORTED, "unsupported issuer key: ", name, NULL);
        return false;
    }
    if (ca->key.type == X509_KEY_RSA && x509_key_bits(&ca->key) < 2048) {
        note(s, depth, X509_E_WEAK_KEY, "issuer key is too weak: ", name, NULL);
        return false;
    }
    if (ca->nc_present && !name_constraints_ok(ca, &chain[0])) {
        note(s, depth, X509_E_NAME_CONSTRAINT, "name not permitted by issuer ", name, NULL);
        return false;
    }
    return true;
}

static bool signed_ok(Search *s, int depth, const X509Cert *child, const X509Key *key)
{
    if (child->sig_alg == X509_SIG_NONE) {
        note(s, depth, X509_E_UNSUPPORTED, "unsupported signature algorithm", NULL, NULL);
        return false;
    }
    if (!x509_signed_by(child, key)) {
        note(s, depth, X509_E_BAD_SIGNATURE, "bad signature", NULL, NULL);
        return false;
    }
    return true;
}

/* Find an issuer for chain[node], which sits at position depth (leaf = 0). */
static bool extend(Search *s, int node, int depth)
{
    const X509Cert *c = &chain[node];
    bool matched = false;
    for (size_t r = 0; r < s->policy->root_count; r++) {
        const X509Root *root = &s->policy->roots[r];
        if (!same_name(&c->issuer, root->subject, root->subject_len)) continue;
        X509Key key;
        if (!x509_parse_spki(root->spki, root->spki_len, &key) || key.type == X509_KEY_NONE) continue;
        matched = true;
        if (signed_ok(s, depth + 1, c, &key)) {
            s->anchor = root;
            s->depth = depth + 2;                       /* leaf .. root, inclusive */
            return true;
        }
    }
    if (depth + 1 >= X509_MAX_CHAIN) {
        note(s, depth + 1, X509_E_TOO_MANY, "certificate chain is too long", NULL, NULL);
        return false;
    }
    for (size_t i = 1; i < s->count; i++) {
        const X509Cert *ca = &chain[i];
        if (s->used[i] || !ca->der.len || !same_name(&c->issuer, ca->subject.p, ca->subject.len)) continue;
        matched = true;
        if (!ca_ok(s, ca, depth + 1, depth)) continue;
        if (!signed_ok(s, depth + 1, c, &ca->key)) continue;
        s->used[i] = true;
        if (extend(s, (int)i, depth + 1)) return true;
        s->used[i] = false;
    }
    if (!matched) {
        char name[48];
        display_name(&c->issuer, name, sizeof(name));
        note(s, depth + 1, X509_E_UNKNOWN_ISSUER, "unknown issuer: ", name, NULL);
    }
    return false;
}

X509Status x509_verify_chain(const X509Span *certs, size_t count, const char *host,
                             const X509Policy *policy, X509Result *res)
{
    memset(res, 0, sizeof(*res));
    Search s;
    memset(&s, 0, sizeof(s));
    s.policy = policy;
    s.count = count > X509_MAX_CHAIN ? X509_MAX_CHAIN : count;

#define FAIL(st, a, b, c) do { res->status = (st); put(res->reason, sizeof(res->reason), a, b, c); \
                                   return res->status; } while (0)
    if (!count) FAIL(X509_E_MALFORMED, "the server sent no certificate", NULL, NULL);
    for (size_t i = 0; i < s.count; i++) {
        if (x509_parse(certs[i].p, certs[i].len, &chain[i])) continue;
        if (i == 0) FAIL(X509_E_MALFORMED, "malformed certificate", NULL, NULL);
        memset(&chain[i], 0, sizeof(chain[i]));          /* unusable extra: skipped */
    }
    const X509Cert *leaf = &chain[0];
    display_name(&leaf->subject, res->subject, sizeof(res->subject));
    if (!x509_name_attr(&leaf->issuer, X509_ATTR_O, res->issuer, sizeof(res->issuer)) || !res->issuer[0])
        display_name(&leaf->issuer, res->issuer, sizeof(res->issuer));

    /* the leaf: what the user can act on first */
    if (leaf->key.type == X509_KEY_NONE) FAIL(X509_E_UNSUPPORTED, "unsupported server key type", NULL, NULL);
    if (leaf->key.type == X509_KEY_RSA && x509_key_bits(&leaf->key) < 2048)
        FAIL(X509_E_WEAK_KEY, "the server's RSA key is too weak", NULL, NULL);
    if (!x509_host_matches(leaf, host)) {
        char shown[48] = "";
        X509Span rest = leaf->san, v;
        uint8_t tag;
        while (leaf->san_present && rest.len && x509_der_next(&rest, &tag, &v)) {
            if (tag != 0x82 || !v.len) continue;
            size_t n = v.len < sizeof(shown) - 1 ? v.len : sizeof(shown) - 1;
            for (size_t k = 0; k < n; k++) shown[k] = (v.p[k] >= 0x20 && v.p[k] < 0x7f) ? (char)v.p[k] : '?';
            shown[n] = '\0';
            break;
        }
        if (!shown[0]) FAIL(X509_E_NAME, "name mismatch: the certificate names no web server", NULL, NULL);
        char want[40];
        put(want, sizeof(want), host, " vs ", NULL);
        FAIL(X509_E_NAME, "name mismatch: ", want, shown);
    }
    if (!time_ok(&s, 0, leaf, "certificate")) FAIL(s.best, s.reason, NULL, NULL);
    if (leaf->eku_present && !leaf->eku_server_auth && !leaf->eku_any)
        FAIL(X509_E_EKU, "certificate is not for web servers", NULL, NULL);
    if (leaf->ku_present && !(leaf->key_usage & X509_KU_DIGITAL_SIGNATURE))
        FAIL(X509_E_KEY_USAGE, "certificate key may not sign the handshake", NULL, NULL);
    if (leaf->unknown_critical) FAIL(X509_E_CRITICAL, "certificate has an unsupported critical extension", NULL, NULL);

    s.used[0] = true;
    if (extend(&s, 0, 0)) {
        res->status = X509_OK;
        res->depth = s.depth;
        put(res->root, sizeof(res->root), s.anchor->label, NULL, NULL);
        return X509_OK;
    }
    bool self_issued = leaf->issuer.len == leaf->subject.len &&
                       memcmp(leaf->issuer.p, leaf->subject.p, leaf->issuer.len) == 0;
    if (s.best == X509_E_UNKNOWN_ISSUER && s.best_depth == 1 && self_issued)
        FAIL(X509_E_SELF_SIGNED, "self-signed certificate", NULL, NULL);
    FAIL(s.best ? s.best : X509_E_UNKNOWN_ISSUER, s.best ? s.reason : "unknown issuer", NULL, NULL);
#undef FAIL
}
