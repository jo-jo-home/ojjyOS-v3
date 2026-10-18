/*
 * ojjyOS v3 - TLS 1.3 client (RFC 8446 subset)
 *
 * x25519 key exchange; TLS_CHACHA20_POLY1305_SHA256 and TLS_AES_128_GCM_SHA256;
 * SNI; ALPN http/1.1; middlebox-compatibility mode. The server's Finished
 * MAC is verified (the handshake was not altered relative to the key
 * exchange), but the certificate chain is NOT verified yet: the connection
 * is encrypted, the server's identity is unauthenticated. Callers must say
 * so in the UI. Transport-agnostic: bytes move through TlsIo callbacks, so
 * the same code runs over kernel TCP and host sockets (tests/tls_probe.c).
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
    size_t cert_len;            /* leaf certificate (DER), kept for later checks */
    uint8_t cert[4096];
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

/* Begin the handshake (sends ClientHello). host is used for SNI. */
void tls_start(TlsConn *c, const char *host, TlsIo io);
/* Move bytes: flush output, read records, run the handshake. */
TlsState tls_poll(TlsConn *c);
/* Application data. send returns bytes accepted (whole records only). */
int tls_send(TlsConn *c, const void *data, size_t len);
int tls_recv(TlsConn *c, void *buf, size_t len);
bool tls_eof(TlsConn *c);           /* close_notify / transport EOF and drained */
void tls_close(TlsConn *c);         /* send close_notify (best effort) */
const char *tls_suite_name(uint16_t suite);

#endif /* _OJJY_TLS_H */
