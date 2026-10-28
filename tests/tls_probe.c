/*
 * Hosted TLS probe: runs the kernel's TLS 1.3 client (net/tls.c) over a
 * macOS TCP socket so the handshake can be checked against openssl
 * s_server, Python's TLS server and real sites before any QEMU run.
 * Link it with kernel/src/crypto/roots_data.c (real sites) or a test root
 * file (tests/test_tls_local.py). Created by Jonas Lee.
 *
 * usage: tls_probe [-k] [-r file] <ipv4> <port> <sni-host> [path]
 * -k connects despite a certificate problem (like curl -k).
 * -r writes the server's bytes until the handshake ends to file; built with
 *    -DTLS_PROBE_FIXED_RANDOM (and without random.c) the client is
 *    deterministic, so tests/test_tls_verify.c can replay the flight.
 * Prints "PROBE ok suite=... verified=0|1 ... status=<line> bytes=<n>" or
 * "PROBE fail handshake: <reason> cert_failed=0|1".
 */
#include "net/tls.h"
#include "random.h"
#include "string.h"

int printf(const char *fmt, ...);
int socket(int, int, int);
int connect(int, const void *, unsigned);
long send(int, const void *, unsigned long, int);
long recv(int, void *, unsigned long, int);
int close(int);
int open(const char *, int, ...);
long read(int, void *, unsigned long);
int setsockopt(int, int, int, const void *, unsigned);
int *__error(void);
unsigned long time(unsigned long *);
void serial_printf(const char *fmt, ...) { (void)fmt; }
/* kernel services tls.c uses: the host clock (always "synced") and a ms timer */
int64_t clock_now_unix(void) { return (int64_t)time(0); }
bool clock_ntp_synced(void) { return true; }
struct timespec_mac { long sec, nsec; };
int clock_gettime(int, struct timespec_mac *);
uint64_t net_now(void) { struct timespec_mac t; clock_gettime(6 /* CLOCK_UPTIME_RAW */, &t); return (uint64_t)t.sec * 1000 + (uint64_t)t.nsec / 1000000; }

struct sockaddr_in_mac { uint8_t len, family; uint16_t port; uint32_t addr; char zero[8]; };
struct timeval_mac { long sec; int usec; };

#ifdef TLS_PROBE_FIXED_RANDOM
void random_add_entropy(const void *data, size_t len) { (void)data; (void)len; }
void random_bytes(void *out, size_t len)
{
    uint8_t *p = (uint8_t *)out;
    for (size_t i = 0; i < len; i++) p[i] = (uint8_t)(0x5a + 7 * i);
}
#endif

long write(int, const void *, unsigned long);
static int fd, record_fd = -1;
static int io_send(void *ctx, const uint8_t *d, size_t n) { (void)ctx; long r = send(fd, d, n, 0); return r < 0 ? -1 : (int)r; }
static int io_recv(void *ctx, uint8_t *d, size_t n)
{
    (void)ctx;
    long r = recv(fd, d, n, 0);
    if (r > 0 && record_fd >= 0) write(record_fd, d, (unsigned long)r);
    if (r > 0) return (int)r;
    if (r == 0) return -1;
    return (*__error() == 35 /* EAGAIN */) ? 0 : -1;
}

static uint32_t parse_ip(const char *s)
{
    uint32_t ip = 0, part = 0;
    for (; *s; s++) {
        if (*s == '.') { ip = (ip << 8) | part; part = 0; }
        else part = part * 10 + (uint32_t)(*s - '0');
    }
    return (ip << 8) | part;
}

static TlsConn conn;
static char response[1 << 20];

int main(int argc, char **argv)
{
    unsigned flags = 0;
    if (argc > 1 && strcmp(argv[1], "-k") == 0) { flags = TLS_ALLOW_UNTRUSTED; argv++; argc--; }
    if (argc > 2 && strcmp(argv[1], "-r") == 0) {
        record_fd = open(argv[2], 0x0601 /* O_WRONLY|O_CREAT|O_TRUNC */, 0644);
        argv += 2; argc -= 2;
    }
    if (argc < 4) { printf("usage: tls_probe [-k] <ipv4> <port> <sni> [path]\n"); return 2; }
    uint8_t seed[64];
    int rfd = open("/dev/urandom", 0);
    read(rfd, seed, sizeof(seed));
    close(rfd);
    random_add_entropy(seed, sizeof(seed));

    uint32_t ip = parse_ip(argv[1]);
    uint16_t port = 0;
    for (const char *p = argv[2]; *p; p++) port = (uint16_t)(port * 10 + (*p - '0'));
    struct sockaddr_in_mac sa = {sizeof(sa), 2, (uint16_t)((port >> 8) | (port << 8)),
                                 ((ip >> 24) & 0xff) | ((ip >> 8) & 0xff00) | ((ip << 8) & 0xff0000) | (ip << 24), {0}};
    fd = socket(2, 1, 0);
    if (connect(fd, &sa, sizeof(sa)) != 0) { printf("PROBE fail connect\n"); return 1; }
    struct timeval_mac tv = {0, 20000};
    setsockopt(fd, 0xffff, 0x1006, &tv, sizeof(tv));

    TlsIo io = {io_send, io_recv, 0};
    tls_start_ex(&conn, argv[3], io, flags);
    unsigned long started = time(0);
    while (tls_poll(&conn) == TLS_HANDSHAKE && time(0) - started < 15) {}
    if (record_fd >= 0) { close(record_fd); record_fd = -1; }
    if (conn.state != TLS_OPEN) {
        printf("PROBE fail handshake: %s cert_failed=%d\n", conn.state == TLS_FAILED ? conn.error : "timeout", (int)conn.cert_failed);
        return 1;
    }

    char req[512];
    const char *path = argc > 4 ? argv[4] : "/";
    req[0] = 0;
    const char *parts[] = {"GET ", path, " HTTP/1.1\r\nHost: ", argv[3],
                           "\r\nUser-Agent: ojjyOS-Web/3.1\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n"};
    for (int i = 0; i < 5; i++) {
        size_t a = strlen(req), b = strlen(parts[i]);
        memcpy(req + a, parts[i], b + 1);
    }
    tls_send(&conn, req, strlen(req));
    size_t got = 0;
    started = time(0);
    while (!tls_eof(&conn) && conn.state != TLS_FAILED && time(0) - started < 20 && got < sizeof(response) - 1) {
        int n = tls_recv(&conn, response + got, sizeof(response) - 1 - got);
        if (n > 0) got += (size_t)n;
    }
    response[got] = 0;
    if (conn.state == TLS_FAILED) { printf("PROBE fail after open: %s (got %d bytes)\n", conn.error, (int)got); return 1; }
    char *eol = response;
    while (*eol && *eol != '\r' && *eol != '\n') eol++;
    *eol = 0;
    printf("PROBE ok suite=%s alpn=%s verified=%d root=%s subject=%s cert_error=%s verify_ms=%u status=%s bytes=%d\n",
           tls_suite_name(conn.suite), conn.alpn, (int)conn.verified, conn.cert_root[0] ? conn.cert_root : "-",
           conn.cert_subject, conn.cert_error[0] ? conn.cert_error : "-", (unsigned)conn.verify_ms, response, (int)got);
    close(fd);
    return 0;
}
