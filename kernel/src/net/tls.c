/*
 * ojjyOS v3 - TLS 1.3 client (see tls.h)
 *
 * All peer input is untrusted: every length is checked against the bytes
 * actually present before use.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "net/tls.h"
#include "crypto/x509.h"
#include "clock.h"
#include "random.h"
#include "serial.h"
#include "timer.h"
#include "string.h"

#define CT_CCS        20
#define CT_ALERT      21
#define CT_HANDSHAKE  22
#define CT_APP        23

#define HS_CLIENT_HELLO        1
#define HS_SERVER_HELLO        2
#define HS_NEW_SESSION_TICKET  4
#define HS_ENCRYPTED_EXT       8
#define HS_CERTIFICATE         11
#define HS_CERT_REQUEST        13
#define HS_CERT_VERIFY         15
#define HS_FINISHED            20
#define HS_KEY_UPDATE          24

#define SUITE_AES128_GCM   0x1301
#define SUITE_CHACHA20     0x1303

#define ALERT_BAD_CERTIFICATE      42
#define ALERT_CERTIFICATE_EXPIRED  45
#define ALERT_ILLEGAL_PARAMETER    47
#define ALERT_UNKNOWN_CA           48
#define ALERT_DECRYPT_ERROR        51

/* Until the first NTP sync the RTC is only the firmware's guess. */
#define CLOCK_SLACK_UNSYNCED  (48 * 3600)

static const uint8_t hrr_random[32] = {
    0xcf, 0x21, 0xad, 0x74, 0xe5, 0x9a, 0x61, 0x11, 0xbe, 0x1d, 0x8c, 0x02, 0x1e, 0x65, 0xb8, 0x91,
    0xc2, 0xa2, 0x11, 0x16, 0x7a, 0xbb, 0x8c, 0x5e, 0x07, 0x9e, 0x09, 0xe2, 0xc8, 0xa8, 0x33, 0x9c
};

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t rd24(const uint8_t *p) { return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]; }
static inline void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void wr24(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 16); p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)v; }

const char *tls_suite_name(uint16_t suite)
{
    return suite == SUITE_AES128_GCM ? "TLS_AES_128_GCM_SHA256" :
           suite == SUITE_CHACHA20 ? "TLS_CHACHA20_POLY1305_SHA256" : "none";
}

static void fail(TlsConn *c, const char *msg)
{
    if (c->state == TLS_FAILED) return;
    strncpy(c->error, msg, sizeof(c->error) - 1);
    c->error[sizeof(c->error) - 1] = '\0';
    c->state = TLS_FAILED;
    crypto_wipe(c->priv, sizeof(c->priv));
}

static const char *alert_text(uint8_t desc)
{
    switch (desc) {
        case 40: return "the server refused the handshake (handshake_failure)";
        case 42: case 43: case 44: case 45: case 46: return "the server rejected a certificate";
        case 47: return "the server rejected a handshake parameter";
        case 50: return "the server could not decode our handshake";
        case 51: return "the server could not decrypt our message";
        case 70: return "the server does not support TLS 1.3 (protocol_version)";
        case 71: return "the server needs stronger security settings";
        case 80: return "the server had an internal error";
        case 109: return "the server requires an extension we did not send";
        case 112: return "the server does not recognise this host name (SNI)";
        case 120: return "the server requires a different application protocol";
        default: return "the server sent a TLS alert";
    }
}

static void transcript_hash(const TlsConn *c, uint8_t out[32])
{
    Sha256 copy = c->transcript;
    sha256_final(&copy, out);
}

static void set_keys(TlsConn *c, TlsKeys *k, const uint8_t secret[32])
{
    size_t key_len = c->suite == SUITE_AES128_GCM ? 16 : 32;
    hkdf_expand_label(secret, "key", NULL, 0, k->key, key_len);
    hkdf_expand_label(secret, "iv", NULL, 0, k->iv, 12);
    k->seq = 0;
    if (c->suite == SUITE_AES128_GCM) aes128_gcm_init(&k->gcm, k->key);
}

static void nonce_for(const TlsKeys *k, uint8_t nonce[12])
{
    memcpy(nonce, k->iv, 12);
    for (int i = 0; i < 8; i++) nonce[11 - i] ^= (uint8_t)(k->seq >> (8 * i));
}

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */

static void flush(TlsConn *c)
{
    while (c->out_off < c->out_len) {
        int n = c->io.send(c->io.ctx, c->out + c->out_off, c->out_len - c->out_off);
        if (n < 0) { fail(c, "the connection closed during the secure exchange"); return; }
        if (n == 0) return;
        c->out_off += (size_t)n;
    }
    c->out_off = c->out_len = 0;
}

static bool out_room(const TlsConn *c, size_t need) { return c->out_len + need <= sizeof(c->out); }

static void put_plain(TlsConn *c, uint8_t type, const uint8_t *data, size_t len)
{
    if (!out_room(c, len + 5)) { fail(c, "internal: TLS output overflow"); return; }
    uint8_t *r = c->out + c->out_len;
    r[0] = type;
    r[1] = 3; r[2] = type == CT_HANDSHAKE ? 1 : 3;     /* ClientHello record uses 0x0301 */
    wr16(r + 3, (uint16_t)len);
    memcpy(r + 5, data, len);
    c->out_len += len + 5;
}

static void put_encrypted(TlsConn *c, uint8_t inner_type, const uint8_t *data, size_t len)
{
    size_t inner = len + 1, total = inner + 16;
    if (len > 16384 || !out_room(c, total + 5)) { fail(c, "internal: TLS output overflow"); return; }
    uint8_t *r = c->out + c->out_len;
    r[0] = CT_APP; r[1] = 3; r[2] = 3;
    wr16(r + 3, (uint16_t)total);
    uint8_t *body = r + 5;
    memcpy(body, data, len);
    body[len] = inner_type;
    uint8_t nonce[12];
    nonce_for(&c->wr, nonce);
    if (c->suite == SUITE_AES128_GCM)
        aes128_gcm_seal(&c->wr.gcm, nonce, r, 5, body, inner, body, body + inner);
    else
        chacha20_poly1305_seal(c->wr.key, nonce, r, 5, body, inner, body, body + inner);
    c->wr.seq++;
    c->out_len += total + 5;
}

/* Tell the server why we stop (encrypted fatal alert), then fail. */
static void fail_alert(TlsConn *c, uint8_t desc, const char *msg)
{
    if (c->state == TLS_FAILED) return;
    if (c->rd_encrypted) {
        uint8_t alert[2] = {2, desc};
        put_encrypted(c, CT_ALERT, alert, sizeof(alert));
        flush(c);
    }
    fail(c, msg);
}

static void send_handshake(TlsConn *c, const uint8_t *msg, size_t len, bool encrypted)
{
    sha256_update(&c->transcript, msg, len);
    if (encrypted) put_encrypted(c, CT_HANDSHAKE, msg, len); else put_plain(c, CT_HANDSHAKE, msg, len);
}

/* ------------------------------------------------------------------ */
/* ClientHello                                                         */
/* ------------------------------------------------------------------ */

static bool host_is_ip(const char *h)
{
    for (; *h; h++) if (!((*h >= '0' && *h <= '9') || *h == '.')) return false;
    return true;
}

void tls_start(TlsConn *c, const char *host, TlsIo io)
{
    tls_start_ex(c, host, io, 0);
}

void tls_start_ex(TlsConn *c, const char *host, TlsIo io, unsigned flags)
{
    /* Everything before the big buffers, then the buffer fill levels. */
    memset(c, 0, __builtin_offsetof(TlsConn, in));
    c->in_len = c->hs_len = c->app_off = c->app_len = c->out_off = c->out_len = 0;
    c->state = TLS_HANDSHAKE;
    c->io = io;
    c->flags = flags;
    c->cert_status = -1;
    strncpy(c->host, host, sizeof(c->host) - 1);
    c->host[sizeof(c->host) - 1] = '\0';
    sha256_init(&c->transcript);

    uint8_t pub[32], random_bytes_buf[32], session[32];
    random_bytes(c->priv, 32);
    x25519_base(pub, c->priv);
    random_bytes(random_bytes_buf, 32);
    random_bytes(session, 32);

    static uint8_t m[512];
    size_t o = 4;
    wr16(m + o, 0x0303); o += 2;
    memcpy(m + o, random_bytes_buf, 32); o += 32;
    m[o++] = 32; memcpy(m + o, session, 32); o += 32;          /* middlebox compatibility */
    wr16(m + o, 4); o += 2;
    wr16(m + o, SUITE_CHACHA20); o += 2;                        /* fast in software */
    wr16(m + o, SUITE_AES128_GCM); o += 2;                      /* mandatory-to-implement */
    m[o++] = 1; m[o++] = 0;                                     /* null compression */
    size_t ext_len_at = o;
    o += 2;

    size_t hl = strlen(c->host);
    if (hl && !host_is_ip(c->host)) {                           /* server_name */
        wr16(m + o, 0x0000); wr16(m + o + 2, (uint16_t)(hl + 5)); wr16(m + o + 4, (uint16_t)(hl + 3));
        m[o + 6] = 0; wr16(m + o + 7, (uint16_t)hl); memcpy(m + o + 9, c->host, hl);
        o += 9 + hl;
    }
    static const uint8_t fixed[] = {
        0x00, 0x0a, 0x00, 0x04, 0x00, 0x02, 0x00, 0x1d,                         /* groups: x25519 */
        /* signature_algorithms: only what x509.c verifies. ECDSA P-256/P-384 and
         * RSA-PSS for CertificateVerify; the PKCS#1 entries are for signatures
         * inside certificates only (RFC 8446 4.2.3). No Ed25519. */
        0x00, 0x0d, 0x00, 0x12, 0x00, 0x10, 0x04, 0x03, 0x05, 0x03, 0x08, 0x04, 0x08, 0x05,
        0x08, 0x06, 0x04, 0x01, 0x05, 0x01, 0x06, 0x01,
        0x00, 0x2b, 0x00, 0x03, 0x02, 0x03, 0x04,                               /* supported_versions: 1.3 */
        0x00, 0x10, 0x00, 0x0b, 0x00, 0x09, 0x08, 'h', 't', 't', 'p', '/', '1', '.', '1', /* ALPN */
        0x00, 0x2d, 0x00, 0x02, 0x01, 0x01,                                     /* psk_key_exchange_modes */
    };
    memcpy(m + o, fixed, sizeof(fixed)); o += sizeof(fixed);
    wr16(m + o, 0x0033); wr16(m + o + 2, 38); wr16(m + o + 4, 36);              /* key_share */
    wr16(m + o + 6, 0x001d); wr16(m + o + 8, 32); memcpy(m + o + 10, pub, 32);
    o += 42;
    wr16(m + ext_len_at, (uint16_t)(o - ext_len_at - 2));
    m[0] = HS_CLIENT_HELLO;
    wr24(m + 1, (uint32_t)(o - 4));
    send_handshake(c, m, o, false);
    flush(c);
}

/* ------------------------------------------------------------------ */
/* Handshake messages                                                  */
/* ------------------------------------------------------------------ */

static void handle_server_hello(TlsConn *c, const uint8_t *b, size_t len)
{
    if (len < 38) { fail(c, "malformed ServerHello"); return; }
    if (memcmp(b + 2, hrr_random, 32) == 0) {
        fail(c, "the server wants a key exchange ojjyOS does not support yet (HelloRetryRequest)");
        return;
    }
    size_t o = 34;
    size_t sid = b[o++];
    if (o + sid + 3 > len) { fail(c, "malformed ServerHello"); return; }
    o += sid;
    c->suite = rd16(b + o); o += 2;
    if (c->suite != SUITE_AES128_GCM && c->suite != SUITE_CHACHA20) { fail(c, "the server chose an unsupported cipher"); return; }
    if (b[o++] != 0) { fail(c, "malformed ServerHello"); return; }
    bool tls13 = false;
    const uint8_t *share = NULL;
    if (o + 2 <= len) {
        size_t ext_end = o + 2 + rd16(b + o);
        o += 2;
        if (ext_end > len) { fail(c, "malformed ServerHello extensions"); return; }
        while (o + 4 <= ext_end) {
            uint16_t type = rd16(b + o), el = rd16(b + o + 2);
            const uint8_t *e = b + o + 4;
            if (o + 4 + el > ext_end) { fail(c, "malformed ServerHello extension"); return; }
            if (type == 0x002b && el == 2 && rd16(e) == 0x0304) tls13 = true;
            if (type == 0x0033 && el >= 4 && rd16(e) == 0x001d && rd16(e + 2) == 32 && el >= 36) share = e + 4;
            o += 4 + el;
        }
    }
    if (!tls13) { fail(c, "the server only supports TLS 1.2 or older (ojjyOS speaks TLS 1.3)"); return; }
    if (!share) { fail(c, "the server did not send an x25519 key share"); return; }

    uint8_t shared[32], zero[32] = {0}, empty_hash[32], derived[32], early[32], th[32];
    x25519(shared, c->priv, share);
    crypto_wipe(c->priv, sizeof(c->priv));
    if (crypto_equal(shared, zero, 32)) { fail(c, "invalid server key share"); return; }
    sha256("", 0, empty_hash);
    hkdf_extract(NULL, 0, zero, 32, early);
    hkdf_expand_label(early, "derived", empty_hash, 32, derived, 32);
    hkdf_extract(derived, 32, shared, 32, c->hs_secret);
    crypto_wipe(shared, sizeof(shared));
    transcript_hash(c, th);
    hkdf_expand_label(c->hs_secret, "c hs traffic", th, 32, c->c_hs, 32);
    hkdf_expand_label(c->hs_secret, "s hs traffic", th, 32, c->s_hs, 32);
    set_keys(c, &c->rd, c->s_hs);
    set_keys(c, &c->wr, c->c_hs);
    c->rd_encrypted = true;
    c->stage = 1;
}

static void handle_encrypted_extensions(TlsConn *c, const uint8_t *b, size_t len)
{
    if (len < 2 || 2 + (size_t)rd16(b) > len) { fail(c, "malformed EncryptedExtensions"); return; }
    size_t end = 2 + rd16(b);
    for (size_t o = 2; o + 4 <= end;) {
        uint16_t type = rd16(b + o), el = rd16(b + o + 2);
        if (o + 4 + el > end) break;
        if (type == 0x0010 && el >= 4) {
            size_t n = b[o + 6];
            if (n && n < sizeof(c->alpn) && 7 + n <= (size_t)el + 4) { memcpy(c->alpn, b + o + 7, n); c->alpn[n] = '\0'; }
        }
        o += 4 + el;
    }
    c->stage = 2;
}

static uint8_t alert_for(X509Status st)
{
    switch (st) {
        case X509_E_EXPIRED: case X509_E_NOT_YET_VALID: return ALERT_CERTIFICATE_EXPIRED;
        case X509_E_UNKNOWN_ISSUER: case X509_E_SELF_SIGNED: return ALERT_UNKNOWN_CA;
        default: return ALERT_BAD_CERTIFICATE;
    }
}

static void copy_text(char *dst, size_t size, const char *src)
{
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static void handle_certificate(TlsConn *c, const uint8_t *b, size_t len)
{
    /* context<0..255>, certificate_list<0..2^24-1> of (cert_data<1..2^24-1>, extensions<0..2^16-1>) */
    if (len < 4 || b[0] != 0) { fail_alert(c, ALERT_ILLEGAL_PARAMETER, "malformed Certificate"); return; }
    size_t o = 1;
    size_t end = o + 3 + rd24(b + o);
    o += 3;
    if (end != len) { fail_alert(c, ALERT_ILLEGAL_PARAMETER, "malformed Certificate"); return; }
    X509Span certs[TLS_MAX_CERTS];
    size_t count = 0;
    while (o < end) {
        if (end - o < 3) { fail_alert(c, ALERT_ILLEGAL_PARAMETER, "malformed Certificate"); return; }
        size_t cl = rd24(b + o);
        o += 3;
        if (cl == 0 || cl > end - o || end - o - cl < 2) { fail_alert(c, ALERT_ILLEGAL_PARAMETER, "malformed Certificate"); return; }
        if (count < TLS_MAX_CERTS) { certs[count].p = b + o; certs[count].len = cl; count++; }
        o += cl;
        size_t el = rd16(b + o);                    /* per-certificate extensions: skipped */
        o += 2;
        if (el > end - o) { fail_alert(c, ALERT_ILLEGAL_PARAMETER, "malformed Certificate"); return; }
        o += el;
    }
    if (!count) { fail_alert(c, ALERT_BAD_CERTIFICATE, "the server sent no certificate"); return; }

    /* The leaf's key is needed for CertificateVerify after this message is gone. */
    static X509Cert leaf;                           /* static: kernel stacks are small */
    if (!x509_parse(certs[0].p, certs[0].len, &leaf)) {
        fail_alert(c, ALERT_BAD_CERTIFICATE, "malformed certificate"); return;
    }
    const X509Span *key = leaf.key.type == X509_KEY_RSA ? &leaf.key.n : &leaf.key.point;
    if (leaf.key.type == X509_KEY_NONE || key->len > sizeof(c->leaf_key)) {
        fail_alert(c, ALERT_BAD_CERTIFICATE, "unsupported server key type"); return;
    }
    c->leaf_key_type = leaf.key.type;
    c->leaf_e = leaf.key.e;
    c->leaf_key_len = (uint16_t)key->len;
    memcpy(c->leaf_key, key->p, key->len);

    uint64_t started = timer_get_ticks();
    X509Policy policy = {x509_builtin_roots, x509_builtin_root_count, clock_now_unix(),
                         clock_ntp_synced() ? 0 : CLOCK_SLACK_UNSYNCED};
    static X509Result result;
    X509Status st = x509_verify_chain(certs, count, c->host, &policy, &result);
    c->verify_ms = (uint32_t)(timer_get_ticks() - started);
    c->cert_status = (int)st;
    copy_text(c->cert_subject, sizeof(c->cert_subject), result.subject);
    copy_text(c->cert_issuer, sizeof(c->cert_issuer), result.issuer);
    copy_text(c->cert_root, sizeof(c->cert_root), result.root);
    if (st == X509_OK) {
        c->verified = true;
    } else {
        copy_text(c->cert_error, sizeof(c->cert_error), result.reason);
        if (!(c->flags & TLS_ALLOW_UNTRUSTED)) {
            c->cert_failed = true;
            fail_alert(c, alert_for(st), result.reason);
            return;
        }
    }
    c->stage = 3;
}

/* CertificateVerify (RFC 8446 4.4.3): the server signs the transcript up to
 * Certificate with the leaf key, proving it holds that key. */
static void handle_cert_verify(TlsConn *c, const uint8_t *b, size_t len, const uint8_t th[32])
{
    if (len < 4 || 4 + (size_t)rd16(b + 2) != len) { fail_alert(c, ALERT_DECRYPT_ERROR, "malformed CertificateVerify"); return; }
    uint16_t scheme = rd16(b);
    int alg = 0;
    if (scheme == 0x0403 && c->leaf_key_type == X509_KEY_EC_P256) alg = X509_SIG_ECDSA_SHA256;
    else if (scheme == 0x0503 && c->leaf_key_type == X509_KEY_EC_P384) alg = X509_SIG_ECDSA_SHA384;
    else if (scheme == 0x0804 && c->leaf_key_type == X509_KEY_RSA) alg = X509_SIG_RSA_PSS_SHA256;
    else if (scheme == 0x0805 && c->leaf_key_type == X509_KEY_RSA) alg = X509_SIG_RSA_PSS_SHA384;
    else if (scheme == 0x0806 && c->leaf_key_type == X509_KEY_RSA) alg = X509_SIG_RSA_PSS_SHA512;
    if (!alg) {
        fail_alert(c, ALERT_ILLEGAL_PARAMETER, (scheme >> 8) <= 6 && (scheme & 0xff) == 1
                   ? "the server signed the handshake with PKCS#1 v1.5 (not allowed in TLS 1.3)"
                   : "the server signed the handshake with an unsupported scheme");
        return;
    }
    static const char context[] = "TLS 1.3, server CertificateVerify";
    uint8_t content[64 + sizeof(context) + 32];   /* sizeof includes the 0 separator */
    memset(content, 0x20, 64);
    memcpy(content + 64, context, sizeof(context));
    memcpy(content + 64 + sizeof(context), th, 32);
    X509Key key;
    memset(&key, 0, sizeof(key));
    key.type = c->leaf_key_type;
    key.e = c->leaf_e;
    key.n.p = key.point.p = c->leaf_key;
    key.n.len = key.point.len = c->leaf_key_len;
    uint64_t started = timer_get_ticks();
    bool ok = x509_verify_message(alg, -1, &key, content, sizeof(content), b + 4, len - 4);
    c->verify_ms += (uint32_t)(timer_get_ticks() - started);
    if (!ok) { fail_alert(c, ALERT_DECRYPT_ERROR, "bad signature: the server does not hold its certificate's key"); return; }
    serial_printf("[TLS] verify %u ms %s%s%s\n", (unsigned)c->verify_ms, c->host,
                  c->verified ? " verified by " : " NOT verified: ", c->verified ? c->cert_root : c->cert_error);
    c->stage = 4;
}

static void send_client_finished(TlsConn *c)
{
    static const uint8_t ccs = 1;
    put_plain(c, CT_CCS, &ccs, 1);                              /* middlebox compatibility */
    if (c->cert_requested) {                                    /* no client certificate */
        static const uint8_t empty_cert[] = {HS_CERTIFICATE, 0, 0, 4, 0, 0, 0, 0};
        send_handshake(c, empty_cert, sizeof(empty_cert), true);
    }
    uint8_t fk[32], th[32], msg[36];
    hkdf_expand_label(c->c_hs, "finished", NULL, 0, fk, 32);
    transcript_hash(c, th);
    msg[0] = HS_FINISHED;
    wr24(msg + 1, 32);
    hmac_sha256(fk, 32, th, 32, msg + 4);
    send_handshake(c, msg, sizeof(msg), true);
    crypto_wipe(fk, sizeof(fk));
}

static void handle_finished(TlsConn *c, const uint8_t *msg, size_t msg_len)
{
    const uint8_t *b = msg + 4;
    size_t len = msg_len - 4;
    uint8_t fk[32], th[32], expect[32];
    hkdf_expand_label(c->s_hs, "finished", NULL, 0, fk, 32);
    transcript_hash(c, th);
    hmac_sha256(fk, 32, th, 32, expect);
    if (len != 32 || !crypto_equal(expect, b, 32)) { fail(c, "the server's handshake did not verify (bad Finished)"); return; }
    sha256_update(&c->transcript, msg, msg_len);

    /* Application traffic secrets use the transcript through server Finished. */
    uint8_t empty_hash[32], derived[32], zero[32] = {0}, master[32];
    sha256("", 0, empty_hash);
    hkdf_expand_label(c->hs_secret, "derived", empty_hash, 32, derived, 32);
    hkdf_extract(derived, 32, zero, 32, master);
    transcript_hash(c, th);
    hkdf_expand_label(master, "c ap traffic", th, 32, c->c_ap, 32);
    hkdf_expand_label(master, "s ap traffic", th, 32, c->s_ap, 32);
    crypto_wipe(master, sizeof(master));

    send_client_finished(c);
    set_keys(c, &c->wr, c->c_ap);
    set_keys(c, &c->rd, c->s_ap);
    crypto_wipe(c->hs_secret, sizeof(c->hs_secret));
    c->stage = 5;
    c->state = TLS_OPEN;
}

static void handle_key_update(TlsConn *c, const uint8_t *b, size_t len)
{
    if (len != 1) { fail(c, "malformed KeyUpdate"); return; }
    hkdf_expand_label(c->s_ap, "traffic upd", NULL, 0, c->s_ap, 32);
    set_keys(c, &c->rd, c->s_ap);
    if (b[0] == 1) {                                            /* update_requested */
        static const uint8_t ku[] = {HS_KEY_UPDATE, 0, 0, 1, 0};
        put_encrypted(c, CT_HANDSHAKE, ku, sizeof(ku));
        hkdf_expand_label(c->c_ap, "traffic upd", NULL, 0, c->c_ap, 32);
        set_keys(c, &c->wr, c->c_ap);
    }
}

/* Process complete handshake messages in c->hs. */
static void process_handshake(TlsConn *c)
{
    size_t o = 0;
    while (c->state != TLS_FAILED && c->hs_len - o >= 4) {
        uint8_t type = c->hs[o];
        size_t len = rd24(c->hs + o + 1);
        if (len > TLS_HS_MAX - 4) { fail(c, "handshake message too large"); return; }
        if (c->hs_len - o < 4 + len) break;
        const uint8_t *msg = c->hs + o, *body = msg + 4;
        if (type == HS_FINISHED && c->stage == 4 && c->state == TLS_HANDSHAKE) {
            handle_finished(c, msg, 4 + len);           /* hashes itself after verifying */
        } else if (type == HS_CERT_VERIFY && c->stage == 3 && c->state == TLS_HANDSHAKE) {
            uint8_t th[32];
            transcript_hash(c, th);                     /* signed: everything before this message */
            sha256_update(&c->transcript, msg, 4 + len);
            handle_cert_verify(c, body, len, th);
        } else if (c->state == TLS_OPEN) {
            if (type == HS_KEY_UPDATE) handle_key_update(c, body, len);
            /* NewSessionTicket and others: ignored (no resumption) */
        } else {
            sha256_update(&c->transcript, msg, 4 + len);
            if (type == HS_SERVER_HELLO && c->stage == 0) handle_server_hello(c, body, len);
            else if (type == HS_ENCRYPTED_EXT && c->stage == 1) handle_encrypted_extensions(c, body, len);
            else if (type == HS_CERT_REQUEST && c->stage == 2) c->cert_requested = true;
            else if (type == HS_CERTIFICATE && c->stage == 2) handle_certificate(c, body, len);
            else { fail(c, "unexpected handshake message from the server"); return; }
        }
        o += 4 + len;
    }
    memmove(c->hs, c->hs + o, c->hs_len - o);
    c->hs_len -= o;
}

static void add_handshake_bytes(TlsConn *c, const uint8_t *data, size_t len)
{
    if (c->hs_len + len > TLS_HS_MAX) { fail(c, "handshake too large"); return; }
    memcpy(c->hs + c->hs_len, data, len);
    c->hs_len += len;
    process_handshake(c);
}

/* ------------------------------------------------------------------ */
/* Records                                                             */
/* ------------------------------------------------------------------ */

static void handle_alert(TlsConn *c, const uint8_t *a, size_t len)
{
    if (len < 2) { fail(c, "malformed alert"); return; }
    if (a[1] == 0) { c->peer_closed = true; if (c->state == TLS_HANDSHAKE) fail(c, "the server closed the connection"); return; }
    fail(c, alert_text(a[1]));
}

/* Returns false when processing must pause (application buffer full). */
static bool process_record(TlsConn *c)
{
    uint8_t type = c->in[0];
    size_t len = rd16(c->in + 3);
    uint8_t *body = c->in + 5;
    if (type == CT_CCS) return true;
    if (type == CT_ALERT && !c->rd_encrypted) { handle_alert(c, body, len); return true; }
    if (type == CT_HANDSHAKE && !c->rd_encrypted) { add_handshake_bytes(c, body, len); return true; }
    if (type != CT_APP || !c->rd_encrypted) { fail(c, "unexpected TLS record"); return true; }
    if (c->app_len) return false;                   /* wait until the reader drains */
    if (len < 17) { fail(c, "malformed encrypted record"); return true; }

    uint8_t nonce[12];
    nonce_for(&c->rd, nonce);
    size_t clen = len - 16;
    bool ok = c->suite == SUITE_AES128_GCM
        ? aes128_gcm_open(&c->rd.gcm, nonce, c->in, 5, body, clen, body + clen, c->app)
        : chacha20_poly1305_open(c->rd.key, nonce, c->in, 5, body, clen, body + clen, c->app);
    if (!ok) { fail(c, "a secure record failed to decrypt (bad MAC)"); return true; }
    c->rd.seq++;
    size_t n = clen;
    while (n && c->app[n - 1] == 0) n--;            /* strip padding */
    if (!n) { fail(c, "malformed inner record"); return true; }
    uint8_t inner = c->app[--n];
    if (inner == CT_APP) {
        if (c->state != TLS_OPEN) { fail(c, "application data before the handshake finished"); return true; }
        c->app_off = 0;
        c->app_len = n;
    } else if (inner == CT_HANDSHAKE) {
        add_handshake_bytes(c, c->app, n);          /* copied into c->hs */
    } else if (inner == CT_ALERT) {
        handle_alert(c, c->app, n);
    } else {
        fail(c, "unexpected inner record type");
    }
    return true;
}

TlsState tls_poll(TlsConn *c)
{
    if (c->state == TLS_FAILED || c->state == TLS_CLOSED) return c->state;
    flush(c);
    for (int rounds = 0; rounds < 64 && c->state != TLS_FAILED; rounds++) {
        /* Parse complete records already buffered. */
        if (c->in_len >= 5) {
            size_t len = rd16(c->in + 3);
            if (len > TLS_RECORD_MAX) { fail(c, "oversized TLS record"); break; }
            if (c->in_len >= 5 + len) {
                if (!process_record(c)) break;
                memmove(c->in, c->in + 5 + len, c->in_len - 5 - len);
                c->in_len -= 5 + len;
                flush(c);
                continue;
            }
        }
        if (c->peer_closed) break;
        int n = c->io.recv(c->io.ctx, c->in + c->in_len, sizeof(c->in) - c->in_len);
        if (n < 0) {
            c->peer_closed = true;
            if (c->state == TLS_HANDSHAKE) fail(c, "the server closed the connection during the secure handshake");
            break;
        }
        if (n == 0) break;
        c->in_len += (size_t)n;
    }
    if (c->state == TLS_OPEN && c->peer_closed && !c->app_len && c->in_len < 5) c->state = TLS_CLOSED;
    return c->state;
}

int tls_send(TlsConn *c, const void *data, size_t len)
{
    if (c->state != TLS_OPEN) return -1;
    flush(c);
    size_t sent = 0;
    const uint8_t *p = (const uint8_t *)data;
    while (sent < len) {
        size_t n = len - sent > 16384 ? 16384 : len - sent;
        if (!out_room(c, n + 1 + 16 + 5)) break;
        put_encrypted(c, CT_APP, p + sent, n);
        sent += n;
    }
    flush(c);
    return (int)sent;
}

int tls_recv(TlsConn *c, void *buf, size_t len)
{
    if (!c->app_len) tls_poll(c);
    size_t n = len < c->app_len ? len : c->app_len;
    memcpy(buf, c->app + c->app_off, n);
    c->app_off += n;
    c->app_len -= n;
    return (int)n;
}

bool tls_eof(TlsConn *c)
{
    return (c->state == TLS_CLOSED || (c->peer_closed && c->state == TLS_OPEN && c->in_len < 5)) && !c->app_len;
}

void tls_close(TlsConn *c)
{
    if (c->state == TLS_OPEN) {
        static const uint8_t close_notify[2] = {1, 0};
        put_encrypted(c, CT_ALERT, close_notify, 2);
        flush(c);
        c->state = TLS_CLOSED;
    }
}
