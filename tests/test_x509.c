/* X.509 tests: DER parsing, signatures for every supported algorithm,
 * chain checks, host names, the built-in roots, and a fuzz loop (every
 * truncation + 4000 random corruptions) - against real certificates made
 * by the OpenSSL CLI (tests/gen_x509_vectors.py). Created by Jonas Lee. */
#include "crypto/x509.h"
#include "crypto/crypto.h"
#include "x509_vectors.h"
#include "test_support.h"

void *malloc(unsigned long size);
void free(void *p);

static X509Cert parsed(const uint8_t *der, size_t len)
{
    X509Cert c;
    if (!x509_parse(der, len, &c)) memset(&c, 0, sizeof(c));
    return c;
}
#define CERT(name) parsed(x509_##name, x509_##name##_len)

static uint32_t rng_state = 20261027;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static X509Root test_roots[2];
static X509Policy policy;

static X509Root root_of(const uint8_t *der, size_t len, const char *label)
{
    X509Cert c;
    X509Root r = {0, 0, 0, 0, label};
    if (x509_parse(der, len, &c)) {
        r.subject = c.subject.p; r.subject_len = (uint16_t)c.subject.len;
        r.spki = c.spki.p; r.spki_len = (uint16_t)c.spki.len;
    }
    return r;
}

static X509Result last;
#define SPAN(name) {x509_##name, x509_##name##_len}
static X509Status verify(const char *host, const X509Span *list, size_t n)
{
    return x509_verify_chain(list, n, host, &policy, &last);
}
#define VERIFY(host, ...) ({ const X509Span l_[] = {__VA_ARGS__}; verify(host, l_, sizeof(l_) / sizeof(l_[0])); })

static bool contains(const char *s, const char *part)
{
    size_t n = strlen(part);
    for (; *s; s++) if (strncmp(s, part, n) == 0) return true;
    return false;
}

/* Parse a copy in an exact-size heap block so ASan sees any over-read,
 * then run everything that consumes a parsed certificate. */
static int fuzz_one(const uint8_t *der, size_t len, const X509Key *issuer)
{
    uint8_t *copy = (uint8_t *)malloc(len ? len : 1);
    memcpy(copy, der, len);
    X509Cert c;
    int ok = x509_parse(copy, len, &c);
    if (ok) {
        char text[64];
        x509_name_attr(&c.subject, X509_ATTR_CN, text, sizeof(text));
        x509_name_attr(&c.issuer, X509_ATTR_O, text, sizeof(text));
        (void)x509_signed_by(&c, issuer);
        (void)x509_key_bits(&c.key);
        (void)x509_host_matches(&c, "www.example.test");
        X509Span list[2] = {{copy, len}, {x509_int_a, x509_int_a_len}};
        X509Result r;
        (void)x509_verify_chain(list, 2, "www.example.test", &policy, &r);
        X509Span flipped[2] = {{x509_leaf_a, x509_leaf_a_len}, {copy, len}};
        (void)x509_verify_chain(flipped, 2, "www.example.test", &policy, &r);
    }
    free(copy);
    return ok;
}

int main(void)
{
    test_roots[0] = root_of(x509_root_a, x509_root_a_len, "ojjyOS Test Root A");
    test_roots[1] = root_of(x509_root_b, x509_root_b_len, "ojjyOS Test Root B");
    policy.roots = test_roots;
    policy.root_count = 2;
    policy.now = X509_TEST_NOW;
    policy.slack = 0;
    /* --- every generated certificate parses with the expected fields --- */
    {
        X509Cert c;
        char cn[64], org[64];
        CHECK(x509_parse(x509_root_a, x509_root_a_len, &c), "root A parses");
        CHECK(c.ca && c.basic_present && c.path_len == -1, "root A is a CA without pathLen");
        CHECK(c.ku_present && (c.key_usage & X509_KU_KEY_CERT_SIGN), "root A keyCertSign");
        CHECK(c.key.e == 65537, "root A exponent");
        CHECK(x509_name_attr(&c.issuer, X509_ATTR_O, org, sizeof(org)) && strcmp(org, "ojjyOS Test") == 0, "root A O");
        CHECK(c.issuer.len == c.subject.len && memcmp(c.issuer.p, c.subject.p, c.issuer.len) == 0, "root A self-issued");
        CHECK(x509_name_attr(&c.subject, X509_ATTR_CN, cn, sizeof(cn)), "root A CN");
    }
#define CHECK_CERT(name, NAME) do { \
    X509Cert c = CERT(name); \
    char cn[64]; \
    CHECK(c.der.len == x509_##name##_len, #name " parses"); \
    CHECK(c.version == 3, #name " is v3"); \
    CHECK(c.sig_alg == X509_##NAME##_SIG, #name " signature algorithm"); \
    CHECK(c.key.type == X509_##NAME##_KEY, #name " key type"); \
    CHECK(x509_key_bits(&c.key) == X509_##NAME##_BITS, #name " key bits"); \
    CHECK(c.not_before == X509_##NAME##_NB && c.not_after == X509_##NAME##_NA, #name " validity"); \
    CHECK(x509_name_attr(&c.subject, X509_ATTR_CN, cn, sizeof(cn)) && strcmp(cn, X509_##NAME##_CN) == 0, \
          #name " subject CN"); \
} while (0)
    CHECK_CERT(root_a, ROOT_A); CHECK_CERT(int_a, INT_A); CHECK_CERT(leaf_a, LEAF_A);
    CHECK_CERT(root_b, ROOT_B); CHECK_CERT(int_b, INT_B); CHECK_CERT(leaf_b, LEAF_B);
    CHECK_CERT(leaf_b512, LEAF_B512); CHECK_CERT(leaf_b384, LEAF_B384);
    CHECK_CERT(int_c, INT_C); CHECK_CERT(leaf_c, LEAF_C);
    CHECK_CERT(leaf_expired, LEAF_EXPIRED); CHECK_CERT(leaf_future, LEAF_FUTURE);
    CHECK_CERT(leaf_gentime, LEAF_GENTIME); CHECK_CERT(leaf_wild, LEAF_WILD);
    CHECK_CERT(leaf_nosan, LEAF_NOSAN); CHECK_CERT(leaf_client, LEAF_CLIENT);
    CHECK_CERT(leaf_crit, LEAF_CRIT); CHECK_CERT(leaf_noncrit, LEAF_NONCRIT);
    CHECK_CERT(int_noca, INT_NOCA); CHECK_CERT(leaf_noca, LEAF_NOCA);
    CHECK_CERT(int_nokcs, INT_NOKCS); CHECK_CERT(leaf_nokcs, LEAF_NOKCS);
    CHECK_CERT(int_a2, INT_A2); CHECK_CERT(leaf_deep, LEAF_DEEP);
    CHECK_CERT(int_nc, INT_NC); CHECK_CERT(leaf_nc_ok, LEAF_NC_OK);
    CHECK_CERT(leaf_nc_dns, LEAF_NC_DNS); CHECK_CERT(leaf_nc_ip, LEAF_NC_IP);
    CHECK_CERT(leaf_nc_excl, LEAF_NC_EXCL); CHECK_CERT(int_eku, INT_EKU);
    CHECK_CERT(leaf_eku, LEAF_EKU); CHECK_CERT(root_x, ROOT_X);
    CHECK_CERT(leaf_untrusted, LEAF_UNTRUSTED); CHECK_CERT(leaf_self, LEAF_SELF);
    CHECK_CERT(leaf_rsa1024, LEAF_RSA1024);

    /* --- extensions --- */
    {
        X509Cert a = CERT(int_a), l = CERT(leaf_a), crit = CERT(leaf_crit), noncrit = CERT(leaf_noncrit);
        X509Cert client = CERT(leaf_client), nc = CERT(int_nc), noca = CERT(int_noca), nokcs = CERT(int_nokcs);
        CHECK(a.ca && a.path_len == 0, "intermediate A pathLen 0");
        CHECK(a.eku_present && a.eku_server_auth && !a.eku_any, "intermediate A EKU serverAuth");
        CHECK(!l.ca && l.basic_present, "leaf is not a CA");
        CHECK(l.ku_present && l.key_usage == X509_KU_DIGITAL_SIGNATURE, "leaf keyUsage digitalSignature only");
        CHECK(l.san_present && l.san.len > 0, "leaf has subjectAltName");
        CHECK(!l.unknown_critical && !noncrit.unknown_critical, "no unknown critical extension");
        CHECK(crit.unknown_critical, "unknown critical extension flagged");
        CHECK(client.eku_present && !client.eku_server_auth, "clientAuth-only EKU");
        CHECK(nc.nc_present && nc.name_constraints.len > 0, "name constraints kept");
        CHECK(noca.basic_present && !noca.ca, "CA:false intermediate");
        CHECK(nokcs.ca && nokcs.ku_present && !(nokcs.key_usage & X509_KU_KEY_CERT_SIGN), "no keyCertSign");
        X509Cert b = CERT(leaf_b);
        CHECK(b.pss_salt == 32, "PSS salt length from the parameters");
    }

    /* --- signatures: every child verifies with its issuer, not with others --- */
    {
        X509Cert ra = CERT(root_a), ia = CERT(int_a), la = CERT(leaf_a);
        X509Cert rb = CERT(root_b), ib = CERT(int_b), lb = CERT(leaf_b), lb5 = CERT(leaf_b512), lb3 = CERT(leaf_b384);
        X509Cert ic = CERT(int_c), lc = CERT(leaf_c), ls = CERT(leaf_self), weak = CERT(leaf_rsa1024);
        CHECK(x509_signed_by(&ra, &ra.key), "root A self-signature (RSA-4096 PKCS#1 SHA-256)");
        CHECK(x509_signed_by(&ia, &ra.key), "intermediate A by root A (PKCS#1 SHA-384)");
        CHECK(x509_signed_by(&la, &ia.key), "leaf A by intermediate A (ECDSA P-384 SHA-384)");
        CHECK(x509_signed_by(&rb, &rb.key), "root B self-signature (ECDSA P-384)");
        CHECK(x509_signed_by(&ib, &rb.key), "intermediate B by root B (ECDSA P-384 over RSA key)");
        CHECK(x509_signed_by(&lb, &ib.key), "leaf B by intermediate B (RSA-PSS SHA-256)");
        CHECK(x509_signed_by(&lb5, &ib.key), "leaf B512 (PKCS#1 SHA-512)");
        CHECK(x509_signed_by(&lb3, &ib.key), "leaf B384 (RSA-PSS SHA-384)");
        CHECK(x509_signed_by(&ic, &ra.key), "intermediate C by root A");
        CHECK(x509_signed_by(&lc, &ic.key), "leaf C: P-256 key with SHA-384 (hash truncated to the curve)");
        CHECK(x509_signed_by(&ls, &ls.key), "self-signed leaf verifies with itself");
        CHECK(x509_signed_by(&weak, &ib.key), "1024-bit leaf still has a valid signature");
        CHECK(!x509_signed_by(&la, &ra.key), "leaf A does not verify with root A");
        CHECK(!x509_signed_by(&la, &ic.key), "leaf A does not verify with a different P-256 key");
        CHECK(!x509_signed_by(&lb, &ia.key), "RSA signature with an EC key fails");
        CHECK(!x509_signed_by(&ia, &rb.key), "RSA-signed cert with an EC key fails");
        uint8_t *copy = (uint8_t *)malloc(x509_leaf_b_len);
        memcpy(copy, x509_leaf_b, x509_leaf_b_len);
        copy[x509_leaf_b_len - 7] ^= 1;                   /* inside the signature */
        X509Cert bad;
        CHECK(x509_parse(copy, x509_leaf_b_len, &bad) && !x509_signed_by(&bad, &ib.key), "flipped RSA signature bit fails");
        free(copy);
        copy = (uint8_t *)malloc(x509_leaf_a_len);
        memcpy(copy, x509_leaf_a, x509_leaf_a_len);
        copy[40] ^= 0x20;                                 /* inside tbsCertificate (serial area) */
        X509Cert bad2;
        if (x509_parse(copy, x509_leaf_a_len, &bad2)) CHECK(!x509_signed_by(&bad2, &ia.key), "changed tbs fails");
        else CHECK(1, "changed tbs fails");
        free(copy);
    }

    /* --- TLS 1.3 CertificateVerify signatures (RFC 8446 4.4.3) --- */
    {
        uint8_t content[64 + 33 + 1 + 32];
        memset(content, 0x20, 64);
        memcpy(content + 64, "TLS 1.3, server CertificateVerify", 33);
        content[97] = 0;
        memcpy(content + 98, cv_transcript, 32);
        X509Cert la = CERT(leaf_a), lc = CERT(leaf_c), lb = CERT(leaf_b);
        CHECK(x509_verify_message(X509_SIG_ECDSA_SHA256, -1, &la.key, content, sizeof(content),
                                  cv_sig_ecdsa_p256, cv_sig_ecdsa_p256_len), "CertificateVerify ecdsa_secp256r1_sha256");
        CHECK(x509_verify_message(X509_SIG_ECDSA_SHA384, -1, &lc.key, content, sizeof(content),
                                  cv_sig_ecdsa_p384, cv_sig_ecdsa_p384_len), "CertificateVerify ecdsa_secp384r1_sha384");
        CHECK(x509_verify_message(X509_SIG_RSA_PSS_SHA256, -1, &lb.key, content, sizeof(content),
                                  cv_sig_pss256, cv_sig_pss256_len), "CertificateVerify rsa_pss_rsae_sha256");
        CHECK(x509_verify_message(X509_SIG_RSA_PSS_SHA384, -1, &lb.key, content, sizeof(content),
                                  cv_sig_pss384, cv_sig_pss384_len), "CertificateVerify rsa_pss_rsae_sha384");
        CHECK(x509_verify_message(X509_SIG_RSA_PSS_SHA512, -1, &lb.key, content, sizeof(content),
                                  cv_sig_pss512, cv_sig_pss512_len), "CertificateVerify rsa_pss_rsae_sha512");
        CHECK(x509_verify_message(X509_SIG_RSA_PKCS1_SHA256, -1, &lb.key, content, sizeof(content),
                                  cv_sig_pkcs1, cv_sig_pkcs1_len), "PKCS#1 vector itself is valid");
        CHECK(!x509_verify_message(X509_SIG_RSA_PSS_SHA256, -1, &lb.key, content, sizeof(content),
                                   cv_sig_pkcs1, cv_sig_pkcs1_len), "PKCS#1 signature is not a PSS signature");
        content[120] ^= 1;
        CHECK(!x509_verify_message(X509_SIG_ECDSA_SHA256, -1, &la.key, content, sizeof(content),
                                   cv_sig_ecdsa_p256, cv_sig_ecdsa_p256_len), "other transcript fails (ECDSA)");
        CHECK(!x509_verify_message(X509_SIG_RSA_PSS_SHA256, -1, &lb.key, content, sizeof(content),
                                   cv_sig_pss256, cv_sig_pss256_len), "other transcript fails (PSS)");
    }

    /* --- chains: what verifies --- */
    {
        CHECK(VERIFY("www.example.test", SPAN(leaf_a), SPAN(int_a)) == X509_OK, "chain A verifies");
        CHECK(last.depth == 3 && strcmp(last.root, "ojjyOS Test Root A") == 0, "chain A anchored at root A");
        CHECK(strcmp(last.subject, "www.example.test") == 0 && strcmp(last.issuer, "ojjyOS Test") == 0, "chain A names");
        CHECK(VERIFY("example.test", SPAN(leaf_a), SPAN(int_a)) == X509_OK, "second DNS name");
        CHECK(VERIFY("WWW.Example.TEST.", SPAN(leaf_a), SPAN(int_a)) == X509_OK, "case and trailing dot ignored");
        CHECK(VERIFY("10.0.2.2", SPAN(leaf_a), SPAN(int_a)) == X509_OK, "IPv4 host against iPAddress SAN");
        CHECK(VERIFY("127.0.0.1", SPAN(leaf_a), SPAN(int_a)) == X509_OK, "second IP SAN");
        CHECK(VERIFY("rsa.example.test", SPAN(leaf_b), SPAN(int_b)) == X509_OK, "chain B (PSS leaf, EC root) verifies");
        CHECK(strcmp(last.root, "ojjyOS Test Root B") == 0, "chain B anchored at root B");
        CHECK(VERIFY("sha512.example.test", SPAN(leaf_b512), SPAN(int_b)) == X509_OK, "PKCS#1 SHA-512 leaf");
        CHECK(VERIFY("pss384.example.test", SPAN(leaf_b384), SPAN(int_b)) == X509_OK, "PSS SHA-384 leaf");
        CHECK(VERIFY("p256.example.test", SPAN(leaf_c), SPAN(int_c)) == X509_OK, "P-256 issuer with SHA-384");
        CHECK(VERIFY("www.example.test", SPAN(leaf_a), SPAN(int_c), SPAN(int_noca), SPAN(int_a)) == X509_OK,
              "out-of-order list with extras");
        CHECK(VERIFY("www.example.test", SPAN(leaf_a), SPAN(int_a), SPAN(root_a)) == X509_OK, "root sent along");
        CHECK(VERIFY("www.example.test", SPAN(leaf_a), SPAN(int_a), SPAN(int_a)) == X509_OK, "duplicate intermediate");
        CHECK(VERIFY("www.example.test", SPAN(leaf_gentime), SPAN(int_a)) == X509_OK, "GeneralizedTime notAfter");
        CHECK(VERIFY("a.wild.example.test", SPAN(leaf_wild), SPAN(int_a)) == X509_OK, "wildcard covers one label");
        CHECK(VERIFY("www.example.test", SPAN(leaf_noncrit), SPAN(int_a)) == X509_OK, "unknown non-critical ok");
        CHECK(VERIFY("a.allowed.test", SPAN(leaf_nc_ok), SPAN(int_nc)) == X509_OK, "inside name constraints");
        CHECK(VERIFY("10.0.2.2", SPAN(leaf_nc_ok), SPAN(int_nc)) == X509_OK, "IP inside name constraints");
        static const uint8_t garbage[] = {0x30, 0x03, 0x02, 0x01, 0x00};
        const X509Span with_garbage[] = {SPAN(leaf_a), {garbage, sizeof(garbage)}, SPAN(int_a)};
        CHECK(verify("www.example.test", with_garbage, 3) == X509_OK, "unparseable extra certificate skipped");
    }

    /* --- chains: what must fail, and the reason the user sees --- */
    {
        CHECK(VERIFY("other.test", SPAN(leaf_a), SPAN(int_a)) == X509_E_NAME, "wrong host");
        CHECK(strcmp(last.reason, "name mismatch: other.test vs www.example.test") == 0, "wrong host reason");
        CHECK(VERIFY("10.0.2.3", SPAN(leaf_a), SPAN(int_a)) == X509_E_NAME, "wrong IP");
        CHECK(VERIFY("example.test.evil", SPAN(leaf_a), SPAN(int_a)) == X509_E_NAME, "suffix trick");
        CHECK(VERIFY("wild.example.test", SPAN(leaf_wild), SPAN(int_a)) == X509_E_NAME, "wildcard needs a label");
        CHECK(VERIFY("a.b.wild.example.test", SPAN(leaf_wild), SPAN(int_a)) == X509_E_NAME, "wildcard is one label");
        CHECK(VERIFY("www.example.test", SPAN(leaf_nosan), SPAN(int_a)) == X509_E_NAME, "CN without SAN is not used");
        CHECK(strcmp(last.reason, "name mismatch: the certificate names no web server") == 0, "no-SAN reason");
        CHECK(VERIFY("www.example.test", SPAN(leaf_expired), SPAN(int_a)) == X509_E_EXPIRED, "expired");
        CHECK(strcmp(last.reason, "certificate expired on 2021-01-01") == 0, "expired reason");
        CHECK(VERIFY("www.example.test", SPAN(leaf_future), SPAN(int_a)) == X509_E_NOT_YET_VALID, "not yet valid");
        CHECK(strcmp(last.reason, "certificate not valid until 2030-01-01") == 0, "not yet valid reason");
        CHECK(VERIFY("www.example.test", SPAN(leaf_client), SPAN(int_a)) == X509_E_EKU, "clientAuth-only leaf");
        CHECK(VERIFY("www.example.test", SPAN(leaf_crit), SPAN(int_a)) == X509_E_CRITICAL, "unknown critical");
        CHECK(VERIFY("www.example.test", SPAN(leaf_noca), SPAN(int_noca)) == X509_E_NOT_CA, "CA:false issuer");
        CHECK(contains(last.reason, "not a certificate authority: ojjyOS Test Not A CA"), "CA:false reason names it");
        CHECK(VERIFY("www.example.test", SPAN(leaf_nokcs), SPAN(int_nokcs)) == X509_E_KEY_USAGE, "no keyCertSign");
        CHECK(VERIFY("www.example.test", SPAN(leaf_deep), SPAN(int_a2), SPAN(int_a)) == X509_E_PATH_LEN,
              "pathLen 0 exceeded");
        CHECK(VERIFY("a.allowed.test", SPAN(leaf_nc_dns), SPAN(int_nc)) == X509_E_NAME_CONSTRAINT,
              "DNS name outside permitted subtree");
        CHECK(VERIFY("b.allowed.test", SPAN(leaf_nc_ip), SPAN(int_nc)) == X509_E_NAME_CONSTRAINT,
              "IP outside permitted subtree");
        CHECK(VERIFY("x.bad.allowed.test", SPAN(leaf_nc_excl), SPAN(int_nc)) == X509_E_NAME_CONSTRAINT,
              "excluded subtree");
        CHECK(VERIFY("www.example.test", SPAN(leaf_eku), SPAN(int_eku)) == X509_E_EKU, "clientAuth-only issuer");
        CHECK(VERIFY("www.example.test", SPAN(leaf_a)) == X509_E_UNKNOWN_ISSUER, "missing intermediate");
        CHECK(strcmp(last.reason, "unknown issuer: ojjyOS Test Intermediate A") == 0, "missing intermediate reason");
        CHECK(VERIFY("www.example.test", SPAN(leaf_untrusted), SPAN(root_x)) == X509_E_UNKNOWN_ISSUER, "untrusted root");
        CHECK(strcmp(last.reason, "unknown issuer: ojjyOS Untrusted Root") == 0, "untrusted root reason");
        CHECK(VERIFY("www.example.test", SPAN(leaf_self)) == X509_E_SELF_SIGNED, "self-signed leaf");
        CHECK(strcmp(last.reason, "self-signed certificate") == 0, "self-signed reason");
        CHECK(VERIFY("weak.example.test", SPAN(leaf_rsa1024), SPAN(int_b)) == X509_E_WEAK_KEY, "1024-bit RSA leaf");
        CHECK(verify("www.example.test", NULL, 0) == X509_E_MALFORMED, "no certificate");
        static const uint8_t junk[] = {0x30, 0x01, 0x00};
        const X509Span bad_leaf[] = {{junk, sizeof(junk)}, SPAN(int_a)};
        CHECK(verify("www.example.test", bad_leaf, 2) == X509_E_MALFORMED, "malformed leaf");
        uint8_t *copy = (uint8_t *)malloc(x509_leaf_a_len);
        memcpy(copy, x509_leaf_a, x509_leaf_a_len);
        copy[x509_leaf_a_len - 10] ^= 0x40;
        const X509Span tampered[] = {{copy, x509_leaf_a_len}, SPAN(int_a)};
        CHECK(verify("www.example.test", tampered, 2) == X509_E_BAD_SIGNATURE, "tampered leaf signature");
        CHECK(strcmp(last.reason, "bad signature") == 0, "bad signature reason");
        free(copy);
        X509Policy saved = policy;
        policy.now = X509_LEAF_A_NB - 3600;
        CHECK(VERIFY("www.example.test", SPAN(leaf_a), SPAN(int_a)) == X509_E_NOT_YET_VALID, "an hour early, no slack");
        policy.slack = 48 * 3600;
        CHECK(VERIFY("www.example.test", SPAN(leaf_a), SPAN(int_a)) == X509_OK, "an hour early, unsynced slack");
        policy = saved;
        policy.root_count = 1;
        CHECK(VERIFY("rsa.example.test", SPAN(leaf_b), SPAN(int_b)) == X509_E_UNKNOWN_ISSUER, "root B removed");
        policy = saved;
    }

    /* --- malformed DER the parser must refuse --- */
    {
        static const uint8_t indefinite[] = {0x30, 0x80, 0x00, 0x00};
        static const uint8_t long_short[] = {0x30, 0x81, 0x05, 1, 2, 3, 4, 5};
        static const uint8_t leading0[] = {0x30, 0x82, 0x00, 0x85};
        X509Cert c;
        CHECK(!x509_parse(indefinite, sizeof(indefinite), &c), "indefinite length refused");
        CHECK(!x509_parse(long_short, sizeof(long_short), &c), "non-minimal length refused");
        CHECK(!x509_parse(leading0, sizeof(leading0), &c), "length with leading zero refused");
        CHECK(!x509_parse(x509_leaf_a, 0, &c), "empty input refused");
        uint8_t *extra = (uint8_t *)malloc(x509_leaf_a_len + 1);
        memcpy(extra, x509_leaf_a, x509_leaf_a_len);
        extra[x509_leaf_a_len] = 0;
        CHECK(!x509_parse(extra, x509_leaf_a_len + 1, &c), "trailing byte refused");
        free(extra);
    }

    /* --- fuzz: every truncation of every certificate, then corruptions --- */
    {
        X509Cert ia = CERT(int_a), ib = CERT(int_b);
        int truncated_ok = 0;
#define TRUNC(name) for (size_t n = 0; n < x509_##name##_len; n++) truncated_ok += fuzz_one(x509_##name, n, &ia.key);
        X509_CERTS(TRUNC)
        CHECK(truncated_ok == 0, "no truncated certificate parses");
        int survived = 0;
        const uint8_t *seeds[] = {x509_leaf_a, x509_leaf_b, x509_int_nc, x509_root_a};
        const size_t lens[] = {x509_leaf_a_len, x509_leaf_b_len, x509_int_nc_len, x509_root_a_len};
        uint8_t *buf = (uint8_t *)malloc(8192);
        for (int i = 0; i < 4000; i++) {
            int s = i % 4;
            size_t len = lens[s];
            memcpy(buf, seeds[s], len);
            int flips = 1 + (int)(rng() % 4);
            for (int f = 0; f < flips; f++) {
                uint32_t r = rng();
                size_t at = r % len;
                switch ((r >> 16) % 4) {
                    case 0: buf[at] ^= (uint8_t)(1u << ((r >> 8) % 8)); break;
                    case 1: buf[at] = (uint8_t)(r >> 24); break;
                    case 2: buf[at] = (uint8_t)(at < 8 ? 0x84 : 0x81); break;   /* length bytes */
                    default: if (len > 1) len -= 1 + (r >> 24) % (len / 2); break;
                }
            }
            survived += fuzz_one(buf, len, s == 1 ? &ib.key : &ia.key);
        }
        free(buf);
        CHECK(survived < 4000, "corruptions are mostly refused (and never crash)");
    }

    return finish();
}
