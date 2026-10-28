/*
 * ojjyOS v3 - TLS 1.3 client (RFC 8446 subset)
 *
 * x25519 key exchange; TLS_CHACHA20_POLY1305_SHA256 and TLS_AES_128_GCM_SHA256;
 * SNI; ALPN http/1.1; middlebox-compatibility mode. The server's identity
 * is verified (NET-02): the certificate chain is checked against the
 * built-in roots, the host name and the clock (crypto/x509.h), and the
 * CertificateVerify signature proves the server holds the leaf's key
 * (RSA-PSS or ECDSA; PKCS#1 v1.5 is refused there as RFC 8446 requires).
 * The Finished MAC is only accepted after that. A certificate problem
 * fails the handshake with the reason in error[] and cert_failed set,
 * unless TLS_ALLOW_UNTRUSTED was passed ("Visit anyway", curl -k): then
 * the handshake (including CertificateVerify) completes, verified stays
 * false and cert_error[] keeps the reason. Transport-agnostic: bytes move
 * through TlsIo callbacks, so the same code runs over kernel TCP and host
 * sockets (tests/tls_probe.c, tests/test_tls_local.py).
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_TLS_H
#define _OJJY_TLS_H

#include "types.h"
#include "crypto/crypto.h"

#define TLS_RECORD_MAX  (16384 + 256)
#define TLS_HS_MAX      (64 * 1024)

typedef struct {
    /* bytes accepted (0..n), or -1 on a dead transport */
    int (*send)(void *ctx, const uint8_t *data, size_t len);
    /* bytes read (>0), 0 when nothing is available yet, -1 at EOF/error */
    int (*recv)(void *ctx, uint8_t *data, size_t len);
    void *ctx;
} TlsIo;

typedef enum { TLS_HANDSHAKE = 0, TLS_OPEN, TLS_CLOSED, TLS_FAILED } TlsState;

#define TLS_ALLOW_UNTRUSTED 1u      /* finish despite a certificate problem */
#define TLS_MAX_CERTS       8       /* certificates looked at per handshake */

typedef struct {
    uint8_t key[32];
    uint8_t iv[12];
    uint64_t seq;
    Aes128Gcm gcm;
} TlsKeys;

typedef struct {
    TlsState state;
    char error[96];
    TlsIo io;
    char host[128];
    uint16_t suite;
    char alpn[16];
    bool cert_requested;
    int stage;
    uint8_t priv[32];
    Sha256 transcript;
    uint8_t hs_secret[32], c_hs[32], s_hs[32], c_ap[32], s_ap[32];
    TlsKeys rd, wr;
    bool rd_encrypted;
    unsigned flags;             /* TLS_ALLOW_UNTRUSTED */
    bool verified;              /* chain, host name and dates checked */
    bool cert_failed;           /* the handshake failed on the certificate */
    int cert_status;            /* X509Status of the chain check */
    char cert_error[96];        /* why it is not verified ("" when it is) */
    char cert_subject[64];      /* leaf CN, issuer O/CN, trust anchor */
    char cert_issuer[64];
    char cert_root[64];
    uint32_t verify_ms;         /* chain + CertificateVerify time */
    int leaf_key_type;          /* X509_KEY_* of the leaf, for CertificateVerify */
    uint32_t leaf_e;
    uint16_t leaf_key_len;
    uint8_t leaf_key[512];      /* RSA modulus (<= 4096 bits) or EC point */
    bool peer_closed;
    uint8_t in[TLS_RECORD_MAX + 5];
    size_t in_len;
    uint8_t hs[TLS_HS_MAX];
    size_t hs_len;
    uint8_t app[TLS_RECORD_MAX];
    size_t app_off, app_len;
    uint8_t out[TLS_RECORD_MAX + 64];
    size_t out_off, out_len;
} TlsConn;

/* Begin the handshake (sends ClientHello). host is used for SNI and is
 * the name the certificate must match. tls_start = tls_start_ex(.., 0). */
void tls_start(TlsConn *c, const char *host, TlsIo io);
void tls_start_ex(TlsConn *c, const char *host, TlsIo io, unsigned flags);
/* Move bytes: flush output, read records, run the handshake. */
TlsState tls_poll(TlsConn *c);
/* Application data. send returns bytes accepted (whole records only). */
int tls_send(TlsConn *c, const void *data, size_t len);
int tls_recv(TlsConn *c, void *buf, size_t len);
bool tls_eof(TlsConn *c);           /* close_notify / transport EOF and drained */
void tls_close(TlsConn *c);         /* send close_notify (best effort) */
const char *tls_suite_name(uint16_t suite);

#endif /* _OJJY_TLS_H */
