/*
 * ojjyOS v3 - HTTP/1.1 client (polled)
 *
 * http_start() begins a GET; call http_poll() from the main loop until it
 * returns HTTP_DONE or HTTP_ERROR. The response (headers + body) lands in
 * the caller's buffer; the body is decoded in place (chunked supported).
 * Redirects (301/302/303/307/308) are followed up to 5 times. Responses
 * larger than the buffer are truncated and flagged.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_HTTP_H
#define _OJJY_HTTP_H

#include "net/net.h"
#include "web/url.h"

typedef enum {
    HTTP_IDLE = 0,
    HTTP_RESOLVING,
    HTTP_CONNECTING,
    HTTP_SECURING,              /* TLS handshake (https) */
    HTTP_SENDING,
    HTTP_RECEIVING,
    HTTP_DONE,
    HTTP_ERROR
} HttpPhase;

typedef struct {
    int status;
    char content_type[64];
    char location[URL_TEXT_MAX];
    long content_length;        /* -1 when absent */
    bool chunked;
    bool gzip;                  /* Content-Encoding: gzip / deflate */
    bool deflate;
    bool attachment;            /* Content-Disposition: attachment */
    char filename[96];          /* Content-Disposition filename */
    size_t header_len;          /* bytes up to and including the blank line */
} HttpResponseHead;

typedef struct {
    HttpPhase phase;
    Url url;                    /* current (after redirects) */
    char error[96];
    int redirects;
    uint8_t *buf;
    size_t cap, len;            /* raw bytes received */
    const uint8_t *body;        /* valid when DONE */
    size_t body_len;
    size_t wire_len;            /* body bytes as sent (before Content-Encoding) */
    bool truncated;
    bool secure;                /* https: encrypted with TLS 1.3 */
    char tls_suite[40];         /* negotiated cipher suite */
    HttpResponseHead head;
    /* internals */
    int conn;
    struct TlsSlot *tls;        /* pooled TLS connection while https is open */
    ip4_t ip;
    size_t sent, chunk_scan;
    char request[8192];
    size_t request_len;
    bool post;
    const char *post_body;      /* POST body (caller-owned, valid until done) */
    size_t post_len;
    char body_type[80];
    char referer[256];
    uint64_t phase_ms, activity_ms;
} HttpFetch;

void http_start(HttpFetch *f, const char *url_text, uint8_t *buf, size_t cap);
/* Full request: method "GET"/"POST", optional body + content type, Referer. */
void http_start_request(HttpFetch *f, const Url *url, bool post, const char *body, size_t body_len,
                        const char *body_type, const char *referer, uint8_t *buf, size_t cap);
/* Cookie jar hooks (the browser owns the jar): get() writes a Cookie header
 * value for a request URL ("" for none); set() receives each Set-Cookie value. */
void http_set_cookie_hooks(void (*get)(const Url *url, char *out, size_t size),
                           void (*set)(const Url *url, const char *value));
/* Enables Accept-Encoding: gzip, deflate; bodies are decoded into buf and
 * copied back in place (at most min(cap, room after the headers)). */
void http_set_inflate_buffer(uint8_t *buf, size_t cap);
#define HTTP_USER_AGENT "Mozilla/5.0 (ojjyOS 3.1) ojjyOSWeb/3.1 (custom kernel by Jonas Lee)"
void http_start_url(HttpFetch *f, const Url *url, uint8_t *buf, size_t cap);
HttpPhase http_poll(HttpFetch *f);
void http_cancel(HttpFetch *f);
const char *http_phase_text(HttpPhase phase);

/* Pure helpers (host-tested) */
/* Parse status line + headers. Returns 1 complete, 0 need more, -1 bad. */
int http_parse_head(const uint8_t *data, size_t len, HttpResponseHead *head);
/* Decode a chunked body in place. Returns decoded length, or -1 if the
 * terminating 0-chunk has not arrived (*complete=false) / malformed (-2). */
long http_dechunk(uint8_t *data, size_t len, bool *complete);
/* Undo Content-Encoding in place (room = space available at body). Returns
 * the new length (sets *truncated when cut short), or -1 if undecodable. */
long http_decode_body(const HttpResponseHead *h, uint8_t *body, size_t len, size_t room,
                      uint8_t *scratch, size_t scratch_cap, bool *truncated);

#endif /* _OJJY_HTTP_H */
