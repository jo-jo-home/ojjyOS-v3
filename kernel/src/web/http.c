/*
 * ojjyOS v3 - HTTP/1.1 client (see http.h)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "web/http.h"
#include "net/tcp.h"
#include "net/tls.h"
#include "memory.h"
#include "string.h"

#define HTTP_IDLE_TIMEOUT_MS 20000
#define HTTP_MAX_REDIRECTS   5

/* ------------------------------------------------------------------ */
/* TLS connection pool (each TlsConn holds ~115 KB of record buffers)  */
/* ------------------------------------------------------------------ */

#define TLS_POOL 2
typedef struct TlsSlot { bool used; int tcp; TlsConn conn; } TlsSlot;
static TlsSlot *tls_pool[TLS_POOL];

static int tls_tcp_send(void *ctx, const uint8_t *data, size_t len)
{
    return tcp_send((int)(intptr_t)ctx, data, len);
}

static int tls_tcp_recv(void *ctx, uint8_t *data, size_t len)
{
    int h = (int)(intptr_t)ctx;
    int n = tcp_recv(h, data, len);
    if (n > 0) return n;
    if (tcp_state(h) == TCP_S_ERROR || tcp_peer_closed(h)) return -1;
    return 0;
}

static TlsSlot *tls_acquire(void)
{
    for (int i = 0; i < TLS_POOL; i++) {
        if (!tls_pool[i]) tls_pool[i] = (TlsSlot *)pmm_alloc_bytes(sizeof(TlsSlot));
        if (tls_pool[i] && !tls_pool[i]->used) { tls_pool[i]->used = true; return tls_pool[i]; }
    }
    return NULL;
}

static void close_transport(HttpFetch *f)
{
    if (f->tls) { tls_close(&f->tls->conn); f->tls->used = false; f->tls = NULL; }
    if (f->conn >= 0) { tcp_release(f->conn); f->conn = -1; }
}

static int stream_send(HttpFetch *f, const void *data, size_t len)
{
    return f->tls ? tls_send(&f->tls->conn, data, len) : tcp_send(f->conn, data, len);
}

static int stream_recv(HttpFetch *f, void *buf, size_t len)
{
    return f->tls ? tls_recv(&f->tls->conn, buf, len) : tcp_recv(f->conn, buf, len);
}

static bool stream_closed(HttpFetch *f)
{
    if (f->tls) return tls_eof(&f->tls->conn) || f->tls->conn.state == TLS_FAILED;
    return tcp_peer_closed(f->conn) || tcp_state(f->conn) == TCP_S_CLOSED;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool header_is(const uint8_t *line, size_t len, const char *name)
{
    size_t n = strlen(name);
    if (len <= n || line[n] != ':') return false;
    for (size_t i = 0; i < n; i++) if (lower((char)line[i]) != name[i]) return false;
    return true;
}

static void header_value(const uint8_t *line, size_t len, size_t name_len, char *out, size_t size)
{
    size_t i = name_len + 1;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    size_t e = len;
    while (e > i && (line[e - 1] == ' ' || line[e - 1] == '\t')) e--;
    size_t n = e - i < size - 1 ? e - i : size - 1;
    memcpy(out, line + i, n);
    out[n] = '\0';
}

int http_parse_head(const uint8_t *d, size_t len, HttpResponseHead *h)
{
    size_t end = 0;
    for (size_t i = 0; i + 3 < len; i++)
        if (d[i] == '\r' && d[i + 1] == '\n' && d[i + 2] == '\r' && d[i + 3] == '\n') { end = i + 4; break; }
    if (!end) return len > 64 * 1024 ? -1 : 0;

    memset(h, 0, sizeof(*h));
    h->content_length = -1;
    h->header_len = end;
    if (end < 12 || memcmp(d, "HTTP/1.", 7) != 0 || d[8] != ' ') return -1;
    int status = 0;
    for (int i = 9; i < 12; i++) {
        if (d[i] < '0' || d[i] > '9') return -1;
        status = status * 10 + (d[i] - '0');
    }
    h->status = status;

    size_t line = 0;
    while (line < end && d[line] != '\n') line++;
    line++;
    while (line + 2 < end) {
        size_t e = line;
        while (e < end && d[e] != '\r') e++;
        const uint8_t *l = d + line;
        size_t ll = e - line;
        char value[32];
        if (header_is(l, ll, "content-type")) header_value(l, ll, 12, h->content_type, sizeof(h->content_type));
        else if (header_is(l, ll, "location")) header_value(l, ll, 8, h->location, sizeof(h->location));
        else if (header_is(l, ll, "content-length")) {
            header_value(l, ll, 14, value, sizeof(value));
            long v = 0;
            bool ok = value[0] != '\0';
            for (char *c = value; *c; c++) {
                if (*c < '0' || *c > '9' || v > 100000000L) { ok = false; break; }
                v = v * 10 + (*c - '0');
            }
            if (ok) h->content_length = v;
        } else if (header_is(l, ll, "transfer-encoding")) {
            header_value(l, ll, 17, value, sizeof(value));
            for (char *c = value; *c; c++) *c = lower(*c);
            if (strncmp(value, "chunked", 7) == 0) h->chunked = true;
        }
        line = e + 2;
    }
    if (h->chunked) h->content_length = -1;     /* RFC 9112: chunked wins */
    return 1;
}

static int hexval(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (uint8_t)lower((char)c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

long http_dechunk(uint8_t *d, size_t len, bool *complete)
{
    size_t in = 0, out = 0;
    *complete = false;
    for (;;) {
        size_t size = 0;
        int digits = 0;
        while (in < len && hexval(d[in]) >= 0) {
            size = size * 16 + (size_t)hexval(d[in++]);
            if (++digits > 8) return -2;
        }
        if (in >= len) return (long)out;
        if (!digits) return -2;
        while (in < len && d[in] != '\n') in++;         /* chunk extensions */
        if (in >= len) return (long)out;
        in++;
        if (size == 0) { *complete = true; return (long)out; }
        if (in + size > len) {                          /* partial chunk */
            memmove(d + out, d + in, len - in);
            return (long)(out + (len - in));
        }
        memmove(d + out, d + in, size);
        out += size;
        in += size;
        if (in + 2 > len) return (long)out;
        if (d[in] == '\r') in++;
        if (in < len && d[in] == '\n') in++;
    }
}

/* Non-destructive check whether a chunked body has fully arrived. Resumes
 * from *resume (start of the next unread chunk) so polls stay O(new data). */
static bool chunked_complete(const uint8_t *d, size_t len, size_t *resume)
{
    size_t in = *resume;
    for (int guard = 0; guard < 100000; guard++) {
        *resume = in;
        size_t size = 0;
        int digits = 0;
        while (in < len && hexval(d[in]) >= 0 && digits < 9) { size = size * 16 + (size_t)hexval(d[in++]); digits++; }
        if (!digits || in >= len) return false;
        while (in < len && d[in] != '\n') in++;
        if (in >= len) return false;
        in++;
        if (size == 0) return true;
        if (in + size + 2 > len) return false;
        in += size;
        if (d[in] == '\r') in++;
        if (in < len && d[in] == '\n') in++;
    }
    return false;
}

const char *http_phase_text(HttpPhase phase)
{
    switch (phase) {
        case HTTP_RESOLVING:  return "Looking up host";
        case HTTP_CONNECTING: return "Connecting";
        case HTTP_SECURING:   return "Securing connection";
        case HTTP_SENDING:    return "Sending request";
        case HTTP_RECEIVING:  return "Receiving";
        case HTTP_DONE:       return "Done";
        case HTTP_ERROR:      return "Failed";
        default:              return "Idle";
    }
}

static void set_error(HttpFetch *f, const char *msg)
{
    strncpy(f->error, msg, sizeof(f->error) - 1);
    f->error[sizeof(f->error) - 1] = '\0';
    f->phase = HTTP_ERROR;
    close_transport(f);
}

static void begin(HttpFetch *f)
{
    f->phase = HTTP_RESOLVING;
    f->len = f->sent = 0;
    f->chunk_scan = 0;
    f->body = NULL;
    f->body_len = 0;
    f->truncated = false;
    f->error[0] = '\0';
    f->conn = -1;
    f->phase_ms = f->activity_ms = net_now();
    memset(&f->head, 0, sizeof(f->head));
    f->secure = strcmp(f->url.scheme, "https") == 0;
    f->tls_suite[0] = '\0';
    if (!f->secure && strcmp(f->url.scheme, "http") != 0) { set_error(f, "unsupported URL scheme"); return; }

    /* GET request; identity encoding, close when done. */
    char host[URL_HOST_MAX + 8];
    strcpy(host, f->url.host);
    if (f->url.port != (f->secure ? 443 : 80)) {
        char num[8];
        utoa(f->url.port, num, 10);
        size_t l = strlen(host);
        host[l] = ':';
        strcpy(host + l + 1, num);
    }
    const char *parts[] = {
        "GET ", f->url.path, " HTTP/1.1\r\nHost: ", host,
        "\r\nUser-Agent: ojjyOS-Web/3.1 (custom kernel by Jonas Lee)\r\n"
        "Accept: text/html, text/plain;q=0.9, */*;q=0.5\r\n"
        "Accept-Encoding: identity\r\nConnection: close\r\n\r\n"
    };
    f->request_len = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(parts); i++) {
        size_t n = strlen(parts[i]);
        if (f->request_len + n >= sizeof(f->request)) { set_error(f, "request too long"); return; }
        memcpy(f->request + f->request_len, parts[i], n);
        f->request_len += n;
    }
}

void http_start_url(HttpFetch *f, const Url *url, uint8_t *buf, size_t cap)
{
    memset(f, 0, sizeof(*f));
    f->conn = -1;
    f->buf = buf;
    f->cap = cap;
    f->url = *url;
    begin(f);
}

void http_start(HttpFetch *f, const char *url_text, uint8_t *buf, size_t cap)
{
    Url u;
    memset(f, 0, sizeof(*f));
    f->conn = -1;
    if (!url_parse(url_text, &u)) {
        f->buf = buf; f->cap = cap;
        set_error(f, "not a valid address");
        return;
    }
    http_start_url(f, &u, buf, cap);
}

void http_cancel(HttpFetch *f)
{
    close_transport(f);
    if (f->phase != HTTP_DONE) { f->phase = HTTP_ERROR; strcpy(f->error, "stopped"); }
}

static void finish(HttpFetch *f)
{
    close_transport(f);
    HttpResponseHead *h = &f->head;
    int r = http_parse_head(f->buf, f->len, h);
    if (r != 1) { set_error(f, f->len ? "malformed response from server" : "server sent no data"); return; }

    if ((h->status == 301 || h->status == 302 || h->status == 303 || h->status == 307 || h->status == 308) &&
        h->location[0]) {
        if (++f->redirects > HTTP_MAX_REDIRECTS) { set_error(f, "too many redirects"); return; }
        Url next;
        if (!url_resolve(&f->url, h->location, &next)) { set_error(f, "bad redirect address"); return; }
        f->url = next;
        begin(f);
        return;
    }

    uint8_t *body = f->buf + h->header_len;
    size_t blen = f->len - h->header_len;
    if (h->chunked) {
        bool complete;
        long n = http_dechunk(body, blen, &complete);
        if (n < 0) { set_error(f, "malformed chunked response"); return; }
        if (!complete) f->truncated = true;
        blen = (size_t)n;
    } else if (h->content_length >= 0 && (size_t)h->content_length < blen) {
        blen = (size_t)h->content_length;
    } else if (h->content_length >= 0 && (size_t)h->content_length > blen) {
        f->truncated = true;
    }
    f->body = body;
    f->body_len = blen;
    f->phase = HTTP_DONE;
}

static bool response_complete(HttpFetch *f)
{
    HttpResponseHead h;
    if (http_parse_head(f->buf, f->len, &h) != 1) return false;
    if (h.status == 204 || h.status == 304 || (h.status >= 100 && h.status < 200)) return true;
    size_t body = f->len - h.header_len;
    if (h.chunked) return chunked_complete(f->buf + h.header_len, body, &f->chunk_scan);
    if (h.content_length >= 0) return body >= (size_t)h.content_length;
    return false;                       /* read until the server closes */
}

HttpPhase http_poll(HttpFetch *f)
{
    uint64_t now = net_now();
    switch (f->phase) {
        case HTTP_RESOLVING: {
            if (!net_info()->present) { set_error(f, "no network hardware"); break; }
            if (!net_info()->enabled) { set_error(f, "network is turned off"); break; }
            DnsStatus st = dns_resolve(f->url.host, &f->ip);
            if (st == DNS_FAIL) { set_error(f, "could not find the server (DNS lookup failed)"); break; }
            if (st == DNS_PENDING) break;
            f->conn = tcp_connect(f->ip, f->url.port);
            if (f->conn < 0) { set_error(f, net_ready() ? "too many open connections" : "network is not ready"); break; }
            f->phase = HTTP_CONNECTING;
            f->phase_ms = f->activity_ms = now;
            break;
        }
        case HTTP_CONNECTING: {
            TcpState st = tcp_state(f->conn);
            if (st == TCP_S_ERROR) { set_error(f, tcp_error_string(tcp_error(f->conn))); break; }
            if (st != TCP_S_ESTABLISHED && st != TCP_S_CLOSE_WAIT) break;
            f->activity_ms = now;
            if (!f->secure) { f->phase = HTTP_SENDING; goto sending; }
            f->tls = tls_acquire();
            if (!f->tls) { set_error(f, "not enough memory for a secure connection"); break; }
            f->tls->tcp = f->conn;
            TlsIo io = {tls_tcp_send, tls_tcp_recv, (void *)(intptr_t)f->conn};
            tls_start(&f->tls->conn, f->url.host, io);
            f->phase = HTTP_SECURING;
        }   /* fall through */
        case HTTP_SECURING: {
            TlsState ts = tls_poll(&f->tls->conn);
            if (ts == TLS_FAILED) {
                char msg[96] = "secure connection failed: ";
                size_t a = strlen(msg), b = strlen(f->tls->conn.error);
                if (a + b >= sizeof(msg)) b = sizeof(msg) - a - 1;
                memcpy(msg + a, f->tls->conn.error, b);
                msg[a + b] = '\0';
                set_error(f, msg);
                break;
            }
            if (ts != TLS_OPEN) {
                if (now - f->activity_ms > HTTP_IDLE_TIMEOUT_MS) set_error(f, "the secure handshake timed out");
                break;
            }
            strncpy(f->tls_suite, tls_suite_name(f->tls->conn.suite), sizeof(f->tls_suite) - 1);
            f->phase = HTTP_SENDING;
            f->activity_ms = now;
        }   /* fall through */
        sending:
        case HTTP_SENDING: {
            if (tcp_state(f->conn) == TCP_S_ERROR) { set_error(f, tcp_error_string(tcp_error(f->conn))); break; }
            int n = stream_send(f, f->request + f->sent, f->request_len - f->sent);
            if (n < 0) { set_error(f, "connection closed while sending"); break; }
            f->sent += (size_t)n;
            if (f->sent == f->request_len) { f->phase = HTTP_RECEIVING; f->activity_ms = now; }
            else break;
        }   /* fall through */
        case HTTP_RECEIVING: {
            bool got = false;
            for (int rounds = 0; rounds < 16; rounds++) {
                if (f->len >= f->cap) { f->truncated = true; break; }
                int n = stream_recv(f, f->buf + f->len, f->cap - f->len);
                if (n <= 0) break;
                f->len += (size_t)n;
                f->activity_ms = now;
                got = true;
            }
            TcpState st = tcp_state(f->conn);
            if (f->tls && f->tls->conn.state == TLS_FAILED && !f->len) {
                set_error(f, f->tls->conn.error);
            } else if (f->len >= f->cap || (got && response_complete(f)) || stream_closed(f)) {
                if (f->len >= f->cap) f->truncated = true;
                finish(f);
            } else if (st == TCP_S_ERROR) {
                if (f->len && tcp_error(f->conn) == TCP_ERR_RESET) finish(f);   /* keep what arrived */
                else set_error(f, tcp_error_string(tcp_error(f->conn)));
            } else if (now - f->activity_ms > HTTP_IDLE_TIMEOUT_MS) {
                set_error(f, "the server stopped responding");
            }
            break;
        }
        default:
            break;
    }
    return f->phase;
}
