/*
 * ojjyOS v3 - Network shell commands (see netcmd.h)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "ui/netcmd.h"
#include "net/net.h"
#include "net/tcp.h"
#include "web/http.h"
#include "string.h"
#include "serial.h"
#include "memory.h"

#define LINE 78

typedef enum { JOB_NONE, JOB_PING, JOB_DNS, JOB_CURL } JobKind;

static struct {
    JobKind kind;
    char host[URL_HOST_MAX];
    ip4_t ip;
    bool resolved;
    int count, sent, replies;
    uint16_t seq_base;
    uint64_t next_ms, started_ms, last_sent_ms;
    int rtt_min, rtt_max, rtt_sum;
    bool headers_only;
} job;

static HttpFetch curl_fetch;
#define CURL_BUF (256 * 1024)
static uint8_t *curl_buf;           /* page-allocated on first curl */

static void cat(char *dst, const char *src)
{
    size_t a = strlen(dst), b = strlen(src);
    if (a + b > LINE) b = LINE - a;
    memcpy(dst + a, src, b);
    dst[a + b] = '\0';
}

static void cat_num(char *dst, uint64_t v)
{
    char n[24];
    utoa(v, n, 10);
    cat(dst, n);
}

static void cat_ip(char *dst, ip4_t ip)
{
    char n[16];
    net_format_ip(ip, n, sizeof(n));
    cat(dst, n);
}

static const char *skip(const char *s) { while (*s == ' ') s++; return s; }

static bool network_usable(netcmd_print_fn print)
{
    const NetInfo *ni = net_info();
    if (!ni->present) { print("No network hardware found (QEMU needs -device virtio-net-device)."); return false; }
    if (!ni->enabled) {
        print("Network is off. Turn it on with 'net on' or Control Center.");
        serial_printf("[NET] command refused: network is off\n");
        return false;
    }
    return true;
}

static void ifconfig(netcmd_print_fn print)
{
    const NetInfo *ni = net_info();
    char l[LINE + 1], mac[20];
    if (!ni->present) { print("net0: no network hardware"); return; }
    l[0] = 0; cat(l, "net0 (");  cat(l, ni->driver ? ni->driver : "nic"); cat(l, "): ");
    cat(l, !ni->enabled ? "off" : ni->configured ? "connected" : "waiting for DHCP");
    print(l);
    net_format_mac(ni->mac, mac, sizeof(mac));
    l[0] = 0; cat(l, "  ether   "); cat(l, mac); print(l);
    if (ni->configured) {
        l[0] = 0; cat(l, "  inet    "); cat_ip(l, ni->ip); cat(l, "  netmask "); cat_ip(l, ni->netmask); print(l);
        l[0] = 0; cat(l, "  gateway "); cat_ip(l, ni->gateway); cat(l, "  dns "); cat_ip(l, ni->dns); print(l);
        l[0] = 0; cat(l, "  lease   "); cat_num(l, ni->lease_seconds); cat(l, " s"); print(l);
    }
    l[0] = 0; cat(l, "  RX "); cat_num(l, ni->rx_packets); cat(l, " pkts "); cat_num(l, ni->rx_bytes / 1024);
    cat(l, " KB  TX "); cat_num(l, ni->tx_packets); cat(l, " pkts "); cat_num(l, ni->tx_bytes / 1024); cat(l, " KB");
    print(l);
    l[0] = 0; cat(l, "  dropped rx "); cat_num(l, ni->rx_dropped); cat(l, " tx "); cat_num(l, ni->tx_dropped);
    print(l);
}

bool netcmd_busy(void) { return job.kind != JOB_NONE; }

void netcmd_cancel(netcmd_print_fn print)
{
    if (job.kind == JOB_CURL) http_cancel(&curl_fetch);
    if (job.kind != JOB_NONE) print("^C");
    job.kind = JOB_NONE;
}

static void ping_summary(netcmd_print_fn print)
{
    char l[LINE + 1];
    l[0] = 0; cat_num(l, (uint64_t)job.sent); cat(l, " sent, "); cat_num(l, (uint64_t)job.replies);
    cat(l, " received");
    if (job.replies) {
        cat(l, ", rtt min/avg/max "); cat_num(l, (uint64_t)job.rtt_min); cat(l, "/");
        cat_num(l, (uint64_t)(job.rtt_sum / job.replies)); cat(l, "/"); cat_num(l, (uint64_t)job.rtt_max); cat(l, " ms");
    }
    print(l);
    serial_printf("[NET] ping %s\n", l);
}

bool netcmd_exec(const char *cmd, netcmd_print_fn print)
{
    cmd = skip(cmd);
    if (strcmp(cmd, "ifconfig") == 0 || strcmp(cmd, "net") == 0) { ifconfig(print); return true; }
    if (strcmp(cmd, "net on") == 0) { net_set_enabled(true); print("Network on (requesting address...)"); return true; }
    if (strcmp(cmd, "net off") == 0) { net_set_enabled(false); print("Network off"); return true; }
    if (strcmp(cmd, "net renew") == 0) { net_renew(); print("Renewing DHCP lease..."); return true; }

    bool is_ping = strncmp(cmd, "ping", 4) == 0 && (cmd[4] == ' ' || !cmd[4]);
    bool is_dns = strncmp(cmd, "nslookup", 8) == 0 && (cmd[8] == ' ' || !cmd[8]);
    bool is_curl = strncmp(cmd, "curl", 4) == 0 && (cmd[4] == ' ' || !cmd[4]);
    if (!is_ping && !is_dns && !is_curl) return false;

    if (job.kind != JOB_NONE) { print("Another network command is running (Ctrl+C to stop it)."); return true; }
    const char *arg = skip(cmd + (is_dns ? 8 : 4));
    if (!*arg) {
        print(is_ping ? "usage: ping <host> [count]" : is_dns ? "usage: nslookup <host>" : "usage: curl [-I] <url>");
        return true;
    }
    if (!network_usable(print)) return true;
    memset(&job, 0, sizeof(job));

    if (is_curl) {
        if (strncmp(arg, "-I ", 3) == 0) { job.headers_only = true; arg = skip(arg + 3); }
        if (!curl_buf) curl_buf = (uint8_t *)pmm_alloc_bytes(CURL_BUF);
        if (!curl_buf) { print("curl: not enough memory"); return true; }
        http_start(&curl_fetch, arg, curl_buf, CURL_BUF);
        if (curl_fetch.phase == HTTP_ERROR) {
            char l[LINE + 1]; l[0] = 0; cat(l, "curl: "); cat(l, curl_fetch.error); print(l);
            return true;
        }
        job.kind = JOB_CURL;
        return true;
    }

    size_t n = 0;
    while (arg[n] && arg[n] != ' ' && n + 1 < sizeof(job.host)) { job.host[n] = arg[n]; n++; }
    job.host[n] = 0;
    job.count = 4;
    if (is_ping && arg[n] == ' ') {
        int c = 0;
        for (const char *d = skip(arg + n); *d >= '0' && *d <= '9' && c < 1000; d++) c = c * 10 + (*d - '0');
        if (c >= 1 && c <= 100) job.count = c;
    }
    job.kind = is_ping ? JOB_PING : JOB_DNS;
    job.started_ms = net_now();
    job.seq_base = (uint16_t)(net_now() & 0x7FFF);
    job.rtt_min = 1 << 30;
    return true;
}

static void poll_resolve(netcmd_print_fn print)
{
    DnsStatus st = dns_resolve(job.host, &job.ip);
    char l[LINE + 1];
    if (st == DNS_PENDING) return;
    if (st == DNS_FAIL) {
        l[0] = 0; cat(l, job.kind == JOB_PING ? "ping: " : "nslookup: "); cat(l, "cannot resolve "); cat(l, job.host);
        print(l);
        job.kind = JOB_NONE;
        return;
    }
    job.resolved = true;
    if (job.kind == JOB_DNS) {
        l[0] = 0; cat(l, job.host); cat(l, " has address "); cat_ip(l, job.ip); print(l);
        serial_printf("[NET] nslookup %s\n", l);
        job.kind = JOB_NONE;
        return;
    }
    l[0] = 0; cat(l, "PING "); cat(l, job.host); cat(l, " ("); cat_ip(l, job.ip); cat(l, ")"); print(l);
    job.next_ms = net_now();
}

static void poll_ping(netcmd_print_fn print)
{
    uint64_t now = net_now();
    char l[LINE + 1];
    /* Report the outstanding probe once it answers or after 1 s. */
    if (job.sent > 0 && job.sent > job.replies) {
        uint16_t seq = (uint16_t)(job.seq_base + job.sent - 1);
        int rtt = net_ping_result(seq);
        static int reported_seq = -1;
        if (rtt >= 0 && reported_seq != seq) {
            reported_seq = seq;
            job.replies++;
            if (rtt < job.rtt_min) job.rtt_min = rtt;
            if (rtt > job.rtt_max) job.rtt_max = rtt;
            job.rtt_sum += rtt;
            l[0] = 0; cat(l, "reply from "); cat_ip(l, job.ip); cat(l, ": seq="); cat_num(l, (uint64_t)job.sent);
            cat(l, " time="); cat_num(l, (uint64_t)rtt); cat(l, " ms");
            print(l);
        }
    }
    if (now < job.next_ms) return;
    if (job.sent > 0 && net_ping_result((uint16_t)(job.seq_base + job.sent - 1)) < 0) {
        l[0] = 0; cat(l, "timeout: seq="); cat_num(l, (uint64_t)job.sent); print(l);
    }
    if (job.sent >= job.count) { ping_summary(print); job.kind = JOB_NONE; return; }
    net_ping_send(job.ip, (uint16_t)(job.seq_base + job.sent));
    job.sent++;
    job.next_ms = now + 1000;
}

static void poll_curl(netcmd_print_fn print)
{
    HttpPhase phase = http_poll(&curl_fetch);
    char l[LINE + 1];
    if (phase == HTTP_ERROR) {
        l[0] = 0; cat(l, "curl: "); cat(l, curl_fetch.error); print(l);
        serial_printf("[NET] curl failed: %s\n", curl_fetch.error);
        job.kind = JOB_NONE;
        return;
    }
    if (phase != HTTP_DONE) return;
    l[0] = 0; cat(l, "HTTP "); cat_num(l, (uint64_t)curl_fetch.head.status); cat(l, "  ");
    cat(l, curl_fetch.head.content_type[0] ? curl_fetch.head.content_type : "(no type)");
    cat(l, "  "); cat_num(l, curl_fetch.body_len); cat(l, " bytes");
    if (curl_fetch.truncated) cat(l, " (truncated)");
    print(l);
    serial_printf("[NET] curl done status=%d bytes=%u redirects=%d truncated=%d\n", curl_fetch.head.status,
                  (unsigned)curl_fetch.body_len, curl_fetch.redirects, (int)curl_fetch.truncated);
    if (curl_fetch.redirects) {
        char u[URL_TEXT_MAX];
        url_format(&curl_fetch.url, u, sizeof(u), false);
        l[0] = 0; cat(l, "(redirected to "); cat(l, u); cat(l, ")"); print(l);
    }
    if (!job.headers_only) {
        /* Print the start of the body as readable text lines. */
        int lines = 0;
        size_t i = 0;
        char out[LINE + 1];
        size_t o = 0;
        while (i < curl_fetch.body_len && lines < 30) {
            char c = (char)curl_fetch.body[i++];
            if (c == '\r') continue;
            if (c == '\t') c = ' ';
            if (c == '\n' || o == LINE) {
                out[o] = 0; print(out); lines++; o = 0;
                if (c == '\n') continue;
            }
            out[o++] = ((unsigned char)c >= 32 && (unsigned char)c < 127) ? c : '?';
        }
        if (o && lines < 30) { out[o] = 0; print(out); }
        if (i < curl_fetch.body_len) print("...");
    }
    job.kind = JOB_NONE;
}

void netcmd_poll(netcmd_print_fn print)
{
    switch (job.kind) {
        case JOB_PING:
        case JOB_DNS:
            if (!job.resolved) poll_resolve(print);
            else poll_ping(print);
            break;
        case JOB_CURL:
            poll_curl(print);
            break;
        default:
            break;
    }
}
