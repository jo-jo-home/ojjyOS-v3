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
    bool truncated;
    HttpResponseHead head;
    /* internals */
    int conn;
    ip4_t ip;
    size_t sent, chunk_scan;
    char request[URL_PATH_MAX + URL_HOST_MAX + 256];
    size_t request_len;
    uint64_t phase_ms, activity_ms;
} HttpFetch;

void http_start(HttpFetch *f, const char *url_text, uint8_t *buf, size_t cap);
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

#endif /* _OJJY_HTTP_H */
