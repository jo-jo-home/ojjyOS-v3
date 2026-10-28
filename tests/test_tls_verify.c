/* TLS server-identity tests: replay recorded server flights into net/tls.c
 * (tests/gen_tls_flight.py), then re-encrypt edited flights with the
 * handshake secret the deterministic client derived, and check that every
 * forgery is refused: no Certificate, no CertificateVerify, a bad
 * signature, PKCS#1 or a scheme that does not fit the key - also with
 * TLS_ALLOW_UNTRUSTED, which may only skip certificate *policy* problems.
 * Plus a fuzz loop over the decrypted handshake messages. Created by Jonas Lee. */
#include "net/tls.h"
#include "crypto/x509.h"
#include "crypto/crypto.h"
#include "tls_flight.h"
#include "test_support.h"

/* The recorder's client randomness (tests/tls_probe.c, TLS_PROBE_FIXED_RANDOM). */
void random_add_entropy(const void *data, size_t len) { (void)data; (void)len; }
void random_bytes(void *out, size_t len)
{
    uint8_t *p = (uint8_t *)out;
    for (size_t i = 0; i < len; i++) p[i] = (uint8_t)(0x5a + 7 * i);
}
static int64_t fake_now = FLIGHT_NOW;
int64_t clock_now_unix(void) { return fake_now; }
bool clock_ntp_synced(void) { return true; }
uint64_t net_now(void) { return 0; }

static TlsConn conn;
static uint8_t client_out[8192];
static size_t client_len;
static const uint8_t *feed;
static size_t feed_len, feed_off;

static int io_send(void *ctx, const uint8_t *d, size_t n)
{
    (void)ctx;
    if (client_len + n <= sizeof(client_out)) { memcpy(client_out + client_len, d, n); client_len += n; }
    return (int)n;
}

static int io_recv(void *ctx, uint8_t *d, size_t n)
{
    (void)ctx;
    if (feed_off >= feed_len) return 0;
    size_t k = feed_len - feed_off;
    if (k > n) k = n;
    if (k > 777) k = 777;                         /* records arrive in pieces */
    memcpy(d, feed + feed_off, k);
    feed_off += k;
    return (int)k;
}

static TlsState replay(const char *host, const uint8_t *flight, size_t len, unsigned flags)
{
    client_len = 0;
    feed = flight;
    feed_len = len;
    feed_off = 0;
    TlsIo io = {io_send, io_recv, 0};
    tls_start_ex(&conn, host, io, flags);
    for (int i = 0; i < 400 && conn.state == TLS_HANDSHAKE; i++) tls_poll(&conn);
    return conn.state;
}

static bool has(const char *s, const char *part)
{
    size_t n = strlen(part);
    for (; *s; s++) if (strncmp(s, part, n) == 0) return true;
    return false;
}

/* ------------------------------------------------------------------ */
/* Flight surgery                                                       */
/* ------------------------------------------------------------------ */

typedef struct { uint8_t data[8192]; size_t len; } Msg;

static uint8_t server_hello_record[1024];
static size_t server_hello_len;
static uint8_t client_hello[2048];
static size_t client_hello_len;
static uint8_t s_hs[32];
static uint16_t suite;
static Msg msgs[6];                               /* EE, Certificate, CertificateVerify */
static int msg_count;

static void traffic_keys(uint8_t key[32], uint8_t iv[12])
{
    hkdf_expand_label(s_hs, "key", NULL, 0, key, suite == 0x1301 ? 16 : 32);
    hkdf_expand_label(s_hs, "iv", NULL, 0, iv, 12);
}

static void nonce_for(const uint8_t iv[12], uint64_t seq, uint8_t nonce[12])
{
    memcpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++) nonce[11 - i] ^= (uint8_t)(seq >> (8 * i));
}

/* Learn the secrets from a clean run, then split the recorded flight
 * into ServerHello + the decrypted messages before Finished. */
static bool learn(const char *host, const uint8_t *flight, size_t len)
{
    if (replay(host, flight, len, 0) != TLS_OPEN) return false;
    memcpy(s_hs, conn.s_hs, 32);
    suite = conn.suite;
    if (client_len < 9 || client_out[0] != 22) return false;
    client_hello_len = ((size_t)client_out[3] << 8) | client_out[4];
    memcpy(client_hello, client_out + 5, client_hello_len);

    size_t o = 0;
    server_hello_len = 5 + (((size_t)flight[3] << 8) | flight[4]);
    if (flight[0] != 22 || server_hello_len > sizeof(server_hello_record)) return false;
    memcpy(server_hello_record, flight, server_hello_len);
    o = server_hello_len;
    static uint8_t plain[32768];
    size_t plain_len = 0;
    uint8_t key[32], iv[12], nonce[12];
    traffic_keys(key, iv);
    Aes128Gcm gcm;
    if (suite == 0x1301) aes128_gcm_init(&gcm, key);
    uint64_t seq = 0;
    msg_count = 0;
    while (o + 5 <= len) {
        size_t rl = ((size_t)flight[o + 3] << 8) | flight[o + 4];
        const uint8_t *body = flight + o + 5;
        if (o + 5 + rl > len) break;
        if (flight[o] == 23) {
            uint8_t out[16640];
            nonce_for(iv, seq++, nonce);
            bool ok = suite == 0x1301
                ? aes128_gcm_open(&gcm, nonce, flight + o, 5, body, rl - 16, body + rl - 16, out)
                : chacha20_poly1305_open(key, nonce, flight + o, 5, body, rl - 16, body + rl - 16, out);
            if (!ok) return false;
            size_t n = rl - 16;
            while (n && out[n - 1] == 0) n--;
            if (!n || out[n - 1] != 22) return false;
            memcpy(plain + plain_len, out, n - 1);
            plain_len += n - 1;
        }
        o += 5 + rl;
        /* stop at the server's Finished */
        size_t p = 0;
        while (p + 4 <= plain_len) {
            size_t ml = ((size_t)plain[p + 1] << 16) | ((size_t)plain[p + 2] << 8) | plain[p + 3];
            if (p + 4 + ml > plain_len) break;
            if (plain[p] == 20) {
                msg_count = 0;
                for (size_t q = 0; q < p;) {
                    size_t l = 4 + (((size_t)plain[q + 1] << 16) | ((size_t)plain[q + 2] << 8) | plain[q + 3]);
                    memcpy(msgs[msg_count].data, plain + q, l);
                    msgs[msg_count++].len = l;
                    q += l;
                }
                return msg_count == 3;
            }
            p += 4 + ml;
        }
    }
    return false;
}

/* ServerHello + CCS + one record with the given messages and a correct
 * server Finished over (ClientHello, ServerHello, messages). */
static size_t build(uint8_t *out, const Msg *list, int n)
{
    size_t o = 0;
    memcpy(out, server_hello_record, server_hello_len);
    o += server_hello_len;
    static const uint8_t ccs[] = {20, 3, 3, 0, 1, 1};
    memcpy(out + o, ccs, sizeof(ccs));
    o += sizeof(ccs);

    static uint8_t inner[20000];
    size_t il = 0;
    Sha256 th;
    sha256_init(&th);
    sha256_update(&th, client_hello, client_hello_len);
    sha256_update(&th, server_hello_record + 5, server_hello_len - 5);
    for (int i = 0; i < n; i++) {
        memcpy(inner + il, list[i].data, list[i].len);
        il += list[i].len;
        sha256_update(&th, list[i].data, list[i].len);
    }
    uint8_t hash[32], fk[32];
    sha256_final(&th, hash);
    hkdf_expand_label(s_hs, "finished", NULL, 0, fk, 32);
    inner[il++] = 20; inner[il++] = 0; inner[il++] = 0; inner[il++] = 32;
    hmac_sha256(fk, 32, hash, 32, inner + il);
    il += 32;
    inner[il++] = 22;                             /* inner content type */

    uint8_t *rec = out + o;
    size_t total = il + 16;
    rec[0] = 23; rec[1] = 3; rec[2] = 3; rec[3] = (uint8_t)(total >> 8); rec[4] = (uint8_t)total;
    uint8_t key[32], iv[12], nonce[12];
    traffic_keys(key, iv);
    nonce_for(iv, 0, nonce);
    if (suite == 0x1301) {
        Aes128Gcm gcm;
        aes128_gcm_init(&gcm, key);
        aes128_gcm_seal(&gcm, nonce, rec, 5, inner, il, rec + 5, rec + 5 + il);
    } else {
        chacha20_poly1305_seal(key, nonce, rec, 5, inner, il, rec + 5, rec + 5 + il);
    }
    return o + 5 + total;
}

static uint32_t rng_state = 0x2026a;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static uint8_t flight[40000];

/* Run every forgery against one recorded flight. */
static void forgeries(const char *host, const uint8_t *rec, size_t rec_len, uint16_t scheme, uint16_t wrong_scheme,
                      const char *root)
{
    CHECK(replay(host, rec, rec_len, 0) == TLS_OPEN, "recorded flight opens");
    CHECK(conn.verified && conn.cert_error[0] == 0, "recorded flight is verified");
    CHECK(strcmp(conn.cert_root, root) == 0, "anchored at the expected test root");
    CHECK(learn(host, rec, rec_len), "flight splits into EE, Certificate, CertificateVerify");
    CHECK(msgs[0].data[0] == 8 && msgs[1].data[0] == 11 && msgs[2].data[0] == 15, "message order");
    CHECK(((msgs[2].data[4] << 8) | msgs[2].data[5]) == scheme, "CertificateVerify scheme as expected");

    size_t n = build(flight, msgs, 3);
    CHECK(replay(host, flight, n, 0) == TLS_OPEN && conn.verified, "rebuilt flight opens (harness check)");

    Msg list[3];
    list[0] = msgs[0];
    n = build(flight, list, 1);
    CHECK(replay(host, flight, n, 0) != TLS_OPEN, "no Certificate and no CertificateVerify: refused");
    CHECK(replay(host, flight, n, TLS_ALLOW_UNTRUSTED) != TLS_OPEN, "... also with ALLOW_UNTRUSTED");
    list[1] = msgs[1];
    n = build(flight, list, 2);
    CHECK(replay(host, flight, n, 0) != TLS_OPEN, "no CertificateVerify: refused");
    CHECK(replay(host, flight, n, TLS_ALLOW_UNTRUSTED) != TLS_OPEN, "... also with ALLOW_UNTRUSTED");
    list[0] = msgs[0]; list[1] = msgs[2]; list[2] = msgs[1];
    n = build(flight, list, 3);
    CHECK(replay(host, flight, n, 0) != TLS_OPEN, "CertificateVerify before Certificate: refused");

    list[0] = msgs[0]; list[1] = msgs[1]; list[2] = msgs[2];
    list[2].data[list[2].len - 5] ^= 0x01;
    n = build(flight, list, 3);
    CHECK(replay(host, flight, n, 0) == TLS_FAILED && has(conn.error, "bad signature"), "flipped signature: refused");
    CHECK(!conn.cert_failed, "bad signature is not a certificate-policy failure");
    CHECK(replay(host, flight, n, TLS_ALLOW_UNTRUSTED) == TLS_FAILED, "flipped signature refused with ALLOW_UNTRUSTED");

    list[2] = msgs[2];
    list[2].data[4] = (uint8_t)(wrong_scheme >> 8);
    list[2].data[5] = (uint8_t)wrong_scheme;
    n = build(flight, list, 3);
    CHECK(replay(host, flight, n, 0) == TLS_FAILED, "scheme that does not fit the key: refused");
    list[2] = msgs[2];
    list[2].data[4] = 0x04; list[2].data[5] = 0x01;       /* rsa_pkcs1_sha256 */
    n = build(flight, list, 3);
    CHECK(replay(host, flight, n, TLS_ALLOW_UNTRUSTED) == TLS_FAILED && has(conn.error, "PKCS#1"),
          "PKCS#1 v1.5 CertificateVerify: refused");

    /* certificate policy problems: refused, or finished unverified on request */
    n = build(flight, msgs, 3);
    fake_now = FLIGHT_NOW + 20LL * 366 * 86400;
    CHECK(replay(host, flight, n, 0) == TLS_FAILED && conn.cert_failed && has(conn.error, "certificate expired on"),
          "clock past the leaf's expiry: certificate failure");
    CHECK(replay(host, flight, n, TLS_ALLOW_UNTRUSTED) == TLS_OPEN && !conn.verified &&
          has(conn.cert_error, "certificate expired on"), "ALLOW_UNTRUSTED: opens unverified with the reason");
    fake_now = FLIGHT_NOW;

    /* another host name changes the ClientHello, so derive that handshake's secret first */
    replay("other.example.test", rec, rec_len, 0);
    memcpy(s_hs, conn.s_hs, 32);
    client_hello_len = ((size_t)client_out[3] << 8) | client_out[4];
    memcpy(client_hello, client_out + 5, client_hello_len);
    n = build(flight, msgs, 3);
    CHECK(replay("other.example.test", flight, n, 0) == TLS_FAILED && conn.cert_failed &&
          has(conn.error, "name mismatch: other.example.test vs "), "other host name: name mismatch");
    CHECK(learn(host, rec, rec_len), "secrets restored");

    /* fuzz the decrypted messages; a valid Finished keeps the flight consistent */
    int opened_unverified = 0, opened = 0;
    for (int i = 0; i < 300; i++) {
        list[0] = msgs[0]; list[1] = msgs[1]; list[2] = msgs[2];
        int which = 1 + (int)(rng() % 2);
        for (int f = 1 + (int)(rng() % 3); f > 0; f--) {
            uint32_t r = rng();
            list[which].data[4 + r % (list[which].len - 4)] ^= (uint8_t)(1u << ((r >> 8) % 8));
        }
        n = build(flight, list, 3);
        TlsState st = replay(host, flight, n, i % 3 == 0 ? TLS_ALLOW_UNTRUSTED : 0);
        opened += st == TLS_OPEN;
        if (st == TLS_OPEN && !conn.verified && i % 3 != 0) opened_unverified++;
    }
    CHECK(opened_unverified == 0, "fuzz: nothing opens unverified without ALLOW_UNTRUSTED");
    CHECK(opened < 30, "fuzz: corrupted certificates and signatures are refused");
}

int main(void)
{
    forgeries(FLIGHT_ECDSA_HOST, flight_ecdsa, sizeof(flight_ecdsa), 0x0403, 0x0503, "ojjyOS Test Root A");
    forgeries(FLIGHT_RSA_HOST, flight_rsa, sizeof(flight_rsa), 0x0804, 0x0805, "ojjyOS Test Root B");
    return finish();
}
