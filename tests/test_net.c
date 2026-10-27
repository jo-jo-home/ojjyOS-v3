/*
 * Network stack tests against a simulated LAN: a fake gateway answers ARP,
 * DHCP and DNS, and a scriptable TCP peer exercises the real TCP code.
 * Created by Jonas Lee.
 */
#include "net/net.h"
#include "net/tcp.h"
#include "random.h"
#include "test_support.h"

/* net.c drives the NTP client (kernel/src/clock.c) after DHCP; these
 * tests do not link that file, so provide quiet stubs — otherwise the
 * clock would inject real NTP frames into the captured traffic. */
void clock_ntp_start(uint64_t now_ms) { (void)now_ms; }
void clock_ntp_poll(uint64_t now_ms) { (void)now_ms; }

static const uint8_t guest_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static const uint8_t gw_mac[6] = {0x52, 0x55, 0x0a, 0x00, 0x02, 0x02};
#define GW   IP4(10, 0, 2, 2)
#define DNS  IP4(10, 0, 2, 3)
#define ME   IP4(10, 0, 2, 15)
#define WEB  IP4(93, 184, 215, 14)

/* ---- captured guest transmissions ---- */
#define CAP_MAX 256
static uint8_t cap[CAP_MAX][1600];
static size_t cap_len[CAP_MAX];
static int cap_count;
static bool drop_next_tx;

static int fake_tx(const uint8_t *frame, size_t len)
{
    if (drop_next_tx) { drop_next_tx = false; return 0; }
    if (cap_count < CAP_MAX && len <= 1600) { memcpy(cap[cap_count], frame, len); cap_len[cap_count++] = len; }
    return 0;
}

static uint64_t now;
static uint8_t rx[1600];

/* Like slirp: every on-link address (gateway, DNS) answers ARP. */
static void serve_arp(void)
{
    for (int i = 0; i < cap_count; i++) {
        uint8_t *f = cap[i];
        if (net_rd16(f + 12) != 0x0806 || net_rd16(f + 20) != 1) continue;
        f[12] = 0;                                  /* served */
        static uint8_t r[60];
        memcpy(r, guest_mac, 6); memcpy(r + 6, gw_mac, 6); net_wr16(r + 12, 0x0806);
        uint8_t *a = r + 14;
        net_wr16(a, 1); net_wr16(a + 2, 0x0800); a[4] = 6; a[5] = 4; net_wr16(a + 6, 2);
        memcpy(a + 8, gw_mac, 6); memcpy(a + 14, f + 38, 4);
        memcpy(a + 18, guest_mac, 6); memcpy(a + 24, f + 28, 4);
        net_input(r, 42);
    }
}
static void tick(uint64_t ms) { now += ms; net_poll(now); serve_arp(); }

/* ---- frame builders (from the gateway) ---- */

static size_t build_ip(uint8_t *f, ip4_t src, ip4_t dst, uint8_t proto, const uint8_t *payload, size_t plen)
{
    memcpy(f, guest_mac, 6); memcpy(f + 6, gw_mac, 6); net_wr16(f + 12, 0x0800);
    uint8_t *ip = f + 14;
    memset(ip, 0, 20);
    ip[0] = 0x45; net_wr16(ip + 2, (uint16_t)(20 + plen)); ip[8] = 64; ip[9] = proto;
    net_wr32(ip + 12, src); net_wr32(ip + 16, dst);
    net_wr16(ip + 10, net_checksum_finish(net_checksum_add(0, ip, 20)));
    memcpy(ip + 20, payload, plen);
    return 14 + 20 + plen;
}

static uint32_t pseudo(ip4_t s, ip4_t d, uint8_t proto, size_t len)
{
    uint8_t ph[12];
    net_wr32(ph, s); net_wr32(ph + 4, d); ph[8] = 0; ph[9] = proto; net_wr16(ph + 10, (uint16_t)len);
    return net_checksum_add(0, ph, 12);
}

static void deliver_udp(ip4_t src, uint16_t sport, ip4_t dst, uint16_t dport, const uint8_t *data, size_t len)
{
    uint8_t u[1500];
    net_wr16(u, sport); net_wr16(u + 2, dport); net_wr16(u + 4, (uint16_t)(8 + len)); net_wr16(u + 6, 0);
    memcpy(u + 8, data, len);
    net_wr16(u + 6, net_checksum_finish(net_checksum_add(pseudo(src, dst, 17, 8 + len), u, 8 + len)));
    net_input(rx, build_ip(rx, src, dst, 17, u, 8 + len));
}

/* ---- captured frame accessors ---- */
static const uint8_t *ipv4_of(int i) { return i >= 0 && net_rd16(cap[i] + 12) == 0x0800 ? cap[i] + 14 : NULL; }
static int find_udp(uint16_t dport)
{
    for (int i = cap_count - 1; i >= 0; i--) {
        const uint8_t *ip = ipv4_of(i);
        if (ip && ip[9] == 17 && net_rd16(ip + 22) == dport) return i;
    }
    return -1;
}

/* ---- DHCP server ---- */
static void dhcp_reply(int req, uint8_t type)
{
    const uint8_t *ip = ipv4_of(req), *b = ip + 28;
    uint8_t p[300];
    memset(p, 0, sizeof(p));
    p[0] = 2; p[1] = 1; p[2] = 6;
    memcpy(p + 4, b + 4, 4);                    /* xid */
    net_wr32(p + 16, ME);
    memcpy(p + 28, guest_mac, 6);
    net_wr32(p + 236, 0x63825363);
    size_t o = 240;
    p[o++] = 53; p[o++] = 1; p[o++] = type;
    p[o++] = 54; p[o++] = 4; net_wr32(p + o, GW); o += 4;
    p[o++] = 1; p[o++] = 4; net_wr32(p + o, IP4(255, 255, 255, 0)); o += 4;
    p[o++] = 3; p[o++] = 4; net_wr32(p + o, GW); o += 4;
    p[o++] = 6; p[o++] = 4; net_wr32(p + o, DNS); o += 4;
    p[o++] = 51; p[o++] = 4; net_wr32(p + o, 86400); o += 4;
    p[o++] = 255;
    deliver_udp(GW, 67, 0xFFFFFFFFu, 68, p, o);
}

static uint8_t dhcp_option(int req, uint8_t code, uint8_t *val)
{
    const uint8_t *b = ipv4_of(req) + 28;
    size_t len = net_rd16(ipv4_of(req) + 24) - 8;
    for (size_t o = 240; o + 1 < len;) {
        if (b[o] == 255) break;
        if (b[o] == 0) { o++; continue; }
        if (b[o] == code) { memcpy(val, b + o + 2, b[o + 1]); return b[o + 1]; }
        o += 2 + b[o + 1];
    }
    return 0;
}

static void bring_up(void)
{
    now = 0; cap_count = 0;
    net_test_reset();
    net_attach_device("test", guest_mac, fake_tx);
    tick(1);
    int d = find_udp(67);
    dhcp_reply(d, 2);
    tick(1);
    int r = find_udp(67);
    dhcp_reply(r, 5);
    /* gateway MAC learned */
    uint8_t arp[28];
    net_wr16(arp, 1); net_wr16(arp + 2, 0x0800); arp[4] = 6; arp[5] = 4; net_wr16(arp + 6, 2);
    memcpy(arp + 8, gw_mac, 6); net_wr32(arp + 14, GW); memcpy(arp + 18, guest_mac, 6); net_wr32(arp + 24, ME);
    memcpy(rx, guest_mac, 6); memcpy(rx + 6, gw_mac, 6); net_wr16(rx + 12, 0x0806); memcpy(rx + 14, arp, 28);
    net_input(rx, 42);
    cap_count = 0;
}

/* ---- TCP peer ---- */
typedef struct { uint32_t seq, ack; uint8_t flags; uint16_t win, sport, dport; const uint8_t *data; size_t len; } Seg;
static bool tcp_of(int i, Seg *s)
{
    const uint8_t *ip = ipv4_of(i);
    if (!ip || ip[9] != 6) return false;
    const uint8_t *t = ip + 20;
    size_t total = net_rd16(ip + 2) - 20, off = (size_t)(t[12] >> 4) * 4;
    s->sport = net_rd16(t); s->dport = net_rd16(t + 2);
    s->seq = net_rd32(t + 4); s->ack = net_rd32(t + 8); s->flags = t[13]; s->win = net_rd16(t + 14);
    s->data = t + off; s->len = total - off;
    return true;
}

static uint16_t peer_port = 80, guest_port;
static uint32_t peer_seq, peer_ack;

static void peer_send(uint8_t flags, const uint8_t *data, size_t len, uint32_t seq_override, bool use_override)
{
    uint8_t t[1500];
    memset(t, 0, 20);
    net_wr16(t, peer_port); net_wr16(t + 2, guest_port);
    net_wr32(t + 4, use_override ? seq_override : peer_seq); net_wr32(t + 8, peer_ack);
    size_t hdr = (flags & 0x02) ? 24 : 20;
    t[12] = (uint8_t)((hdr / 4) << 4); t[13] = flags; net_wr16(t + 14, 65535);
    if (hdr == 24) { t[20] = 2; t[21] = 4; net_wr16(t + 22, 1460); }
    memcpy(t + hdr, data, len);
    net_wr16(t + 16, net_checksum_finish(net_checksum_add(pseudo(WEB, ME, 6, hdr + len), t, hdr + len)));
    net_input(rx, build_ip(rx, WEB, ME, 6, t, hdr + len));
    if (!use_override) peer_seq += (uint32_t)len + ((flags & 0x03) ? 1 : 0);
}

static int last_tcp(Seg *s)
{
    for (int i = cap_count - 1; i >= 0; i--) if (tcp_of(i, s)) return i;
    return -1;
}

static int handshake(void)
{
    cap_count = 0;
    int h = tcp_connect(WEB, 80);
    Seg s;
    if (h < 0 || last_tcp(&s) < 0 || s.flags != 0x02) return -1;
    guest_port = s.sport;
    peer_seq = 1000; peer_ack = s.seq + 1;
    peer_send(0x12, NULL, 0, 0, false);
    return h;
}

/* ---- DNS server ---- */
static void dns_answer(int q, bool nx, bool with_cname)
{
    const uint8_t *ip = ipv4_of(q), *d = ip + 28;
    size_t qlen = net_rd16(ip + 24) - 8;
    uint8_t r[512];
    memcpy(r, d, qlen);
    net_wr16(r + 2, nx ? 0x8183 : 0x8180);
    net_wr16(r + 6, nx ? 0 : (with_cname ? 2 : 1));
    size_t o = qlen;
    if (!nx) {
        if (with_cname) {
            r[o++] = 0xC0; r[o++] = 12; net_wr16(r + o, 5); net_wr16(r + o + 2, 1); net_wr32(r + o + 4, 60);
            net_wr16(r + o + 8, 6); o += 10;
            r[o++] = 3; r[o++] = 'w'; r[o++] = 'w'; r[o++] = 'w'; r[o++] = 0xC0; r[o++] = 12;
        }
        r[o++] = 0xC0; r[o++] = 12; net_wr16(r + o, 1); net_wr16(r + o + 2, 1); net_wr32(r + o + 4, 300);
        net_wr16(r + o + 8, 4); net_wr32(r + o + 10, WEB); o += 14;
    }
    deliver_udp(DNS, 53, ME, net_rd16(ip + 20), r, o);
}

static void test_dns_final_reply_window(void)
{
    bring_up();
    ip4_t addr = 0;
    CHECK(dns_resolve("fourth-reply.test", &addr) == DNS_PENDING,
          "final-attempt lookup starts pending");
    tick(1); tick(1000); tick(2000); tick(3000);
    int q = find_udp(53);
    int queries = 0;
    for (int i = 0; i < cap_count; i++) {
        const uint8_t *ip = ipv4_of(i);
        if (ip && ip[9] == 17 && net_rd16(ip + 22) == 53) queries++;
    }
    CHECK(queries == 4, "DNS sends exactly four attempts by six seconds");
    tick(1); /* a real network reply cannot arrive inline with dns_send */
    CHECK(dns_resolve("fourth-reply.test", &addr) == DNS_PENDING,
          "fourth DNS query retains its reply window after the next poll");
    dns_answer(q, false, false);
    CHECK(dns_resolve("fourth-reply.test", &addr) == DNS_DONE && addr == WEB,
          "delayed answer to fourth DNS query is accepted");

    bring_up();
    dns_resolve("all-lost.test", &addr);
    tick(1); tick(1000); tick(2000); tick(3000); tick(3998);
    CHECK(dns_resolve("all-lost.test", &addr) == DNS_PENDING,
          "no-answer lookup stays pending until original ten-second budget");
    tick(2);
    CHECK(dns_resolve("all-lost.test", &addr) == DNS_FAIL,
          "unanswered lookup still fails at ten seconds, without extra attempts");
    dns_answer(find_udp(53), false, false);
    CHECK(dns_resolve("all-lost.test", &addr) == DNS_FAIL,
          "answer arriving after the deadline cannot revive failed lookup");
}

int main(void)
{
    test_dns_final_reply_window();
    /* RFC 1071 example */
    const uint8_t sample[] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7};
    CHECK(net_checksum_finish(net_checksum_add(0, sample, sizeof(sample))) == 0x220d, "RFC 1071 checksum");
    ip4_t ipv; char text[20];
    CHECK(net_parse_ip("10.0.2.15", &ipv) && ipv == ME, "parse dotted quad");
    CHECK(!net_parse_ip("256.1.1.1", &ipv) && !net_parse_ip("1.2.3", &ipv) && !net_parse_ip("1.2.3.4x", &ipv),
          "reject bad dotted quads");
    net_format_ip(ME, text, sizeof(text));
    CHECK(strcmp(text, "10.0.2.15") == 0, "format ip");

    /* DHCP */
    net_test_reset();
    now = 0; cap_count = 0;
    net_attach_device("test", guest_mac, fake_tx);
    CHECK(!net_ready(), "not ready before DHCP");
    tick(1);
    int d = find_udp(67);
    uint8_t opt[16];
    CHECK(d >= 0 && memcmp(cap[d], "\xff\xff\xff\xff\xff\xff", 6) == 0 && dhcp_option(d, 53, opt) && opt[0] == 1,
          "DHCPDISCOVER broadcast");
    CHECK(memcmp(ipv4_of(d) + 28 + 28, guest_mac, 6) == 0, "DHCP chaddr is our MAC");
    dhcp_reply(d, 2);
    tick(1);
    int r = find_udp(67);
    CHECK(r > d && dhcp_option(r, 53, opt) && opt[0] == 3, "DHCPREQUEST after OFFER");
    CHECK(dhcp_option(r, 50, opt) == 4 && net_rd32(opt) == ME, "REQUEST asks for offered address");
    dhcp_reply(r, 5);
    const NetInfo *ni = net_info();
    CHECK(net_ready() && ni->ip == ME && ni->gateway == GW && ni->dns == DNS && ni->lease_seconds == 86400,
          "DHCPACK configures ip/gateway/dns/lease");

    /* ARP: who-has for us is answered; traffic to the gateway waits for ARP */
    cap_count = 0;
    uint8_t arp[28];
    net_wr16(arp, 1); net_wr16(arp + 2, 0x0800); arp[4] = 6; arp[5] = 4; net_wr16(arp + 6, 1);
    memcpy(arp + 8, gw_mac, 6); net_wr32(arp + 14, GW); memset(arp + 18, 0, 6); net_wr32(arp + 24, ME);
    memcpy(rx, "\xff\xff\xff\xff\xff\xff", 6); memcpy(rx + 6, gw_mac, 6); net_wr16(rx + 12, 0x0806);
    memcpy(rx + 14, arp, 28);
    net_input(rx, 42);
    CHECK(cap_count == 1 && net_rd16(cap[0] + 12) == 0x0806 && net_rd16(cap[0] + 20) == 2 &&
          memcmp(cap[0], gw_mac, 6) == 0, "ARP request for our IP answered");

    net_test_reset(); bring_up();
    CHECK(net_ready(), "bring_up helper configures");

    /* Pending ARP: a fresh stack queues the first packet until the reply */
    net_test_reset();
    now = 0; cap_count = 0;
    net_attach_device("test", guest_mac, fake_tx);
    tick(1); dhcp_reply(find_udp(67), 2); tick(1); dhcp_reply(find_udp(67), 5);
    cap_count = 0;
    CHECK(net_ping_send(WEB, 1) == 0 && cap_count == 1 && net_rd16(cap[0] + 12) == 0x0806,
          "unknown next hop triggers ARP request, packet queued");
    net_wr16(arp + 6, 2); memcpy(rx, guest_mac, 6); memcpy(rx + 6, gw_mac, 6);
    net_wr16(rx + 12, 0x0806); memcpy(rx + 14, arp, 28);
    net_input(rx, 42);
    CHECK(cap_count == 2 && ipv4_of(1) && ipv4_of(1)[9] == 1 && memcmp(cap[1], gw_mac, 6) == 0,
          "queued packet released to the gateway MAC after ARP reply");

    /* Ping reply measures RTT */
    {
        const uint8_t *ip = ipv4_of(1);
        uint8_t icmp[40];
        memcpy(icmp, ip + 20, 40);
        icmp[0] = 0; net_wr16(icmp + 2, 0);
        net_wr16(icmp + 2, net_checksum_finish(net_checksum_add(0, icmp, 40)));
        tick(12);
        net_input(rx, build_ip(rx, WEB, ME, 1, icmp, 40));
        CHECK(net_ping_result(1) == 12, "ping reply round-trip measured");
    }

    /* DNS */
    bring_up();
    ip4_t addr = 0;
    CHECK(dns_resolve("93.184.215.14", &addr) == DNS_DONE && addr == WEB, "literal IP resolves immediately");
    CHECK(dns_resolve("Example.COM", &addr) == DNS_PENDING, "lookup pending");
    tick(1);
    int q = find_udp(53);
    CHECK(q >= 0 && net_rd32(ipv4_of(q) + 16) == DNS, "query sent to DHCP DNS server");
    dns_answer(q, false, true);
    CHECK(dns_resolve("example.com", &addr) == DNS_DONE && addr == WEB, "CNAME + compressed A answer parsed");
    CHECK(dns_resolve("nope.invalid", &addr) == DNS_PENDING, "second lookup pending");
    tick(1);
    dns_answer(find_udp(53), true, false);
    CHECK(dns_resolve("nope.invalid", &addr) == DNS_FAIL, "NXDOMAIN fails");
    CHECK(dns_resolve("slow.test", &addr) == DNS_PENDING, "timeout lookup pending");
    for (int i = 0; i < 12; i++) tick(1000);
    CHECK(dns_resolve("slow.test", &addr) == DNS_FAIL, "unanswered lookup fails after retries");
    CHECK(dns_resolve("a..b", &addr) != DNS_DONE, "empty label rejected");

    /* TCP: handshake, request, large ordered download, close */
    bring_up();
    int h = handshake();
    Seg s;
    CHECK(h >= 0 && tcp_state(h) == TCP_S_ESTABLISHED, "three-way handshake");
    CHECK(last_tcp(&s) >= 0 && s.flags == 0x10 && s.ack == peer_seq, "handshake ACK");
    const char *req = "GET / HTTP/1.1\r\n\r\n";
    cap_count = 0;
    CHECK(tcp_send(h, req, strlen(req)) == (int)strlen(req), "request queued");
    CHECK(last_tcp(&s) >= 0 && s.len == strlen(req) && memcmp(s.data, req, s.len) == 0, "request segment on wire");
    peer_ack += (uint32_t)s.len;
    static uint8_t body[200000], got[200000];
    for (size_t i = 0; i < sizeof(body); i++) body[i] = (uint8_t)(i * 7 + (i >> 9));
    size_t sent = 0, received = 0;
    bool window_ok = true;
    while (sent < sizeof(body)) {
        size_t n = sizeof(body) - sent < 1460 ? sizeof(body) - sent : 1460;
        cap_count = 0;
        peer_send(0x18, body + sent, n, 0, false);
        if (last_tcp(&s) < 0 || s.ack != peer_seq) window_ok = false;
        sent += n;
        if (sent % (1460 * 20) == 0 || sent == sizeof(body)) {
            int got_now;
            while ((got_now = tcp_recv(h, got + received, 4096)) > 0) received += (size_t)got_now;
            tick(1);
        }
    }
    CHECK(window_ok, "every in-order data segment acknowledged");
    CHECK(received == sizeof(body) && memcmp(got, body, sizeof(body)) == 0, "200 KB download intact through 64 KB ring");

    /* Out-of-order segment is dropped with a duplicate ACK; retransmission fills it */
    cap_count = 0;
    uint8_t chunk[100]; memset(chunk, 'x', sizeof(chunk));
    peer_send(0x18, chunk, 100, peer_seq + 100, true);
    CHECK(last_tcp(&s) >= 0 && s.ack == peer_seq && tcp_available(h) == 0, "out-of-order data -> dup ACK, not stored");
    peer_send(0x18, chunk, 100, 0, false);
    CHECK(tcp_available(h) == 100, "in-order retransmission accepted");
    peer_send(0x18, chunk, 100, peer_seq - 100, true);
    CHECK(tcp_available(h) == 100, "duplicate segment ignored");
    tcp_recv(h, got, sizeof(got));

    /* Lost guest data is retransmitted after RTO */
    cap_count = 0;
    drop_next_tx = true;
    tcp_send(h, "more", 4);
    CHECK(cap_count == 0, "first transmission lost");
    tick(1100);
    CHECK(last_tcp(&s) >= 0 && s.len == 4 && memcmp(s.data, "more", 4) == 0, "retransmitted after RTO");
    peer_ack += 4;
    peer_send(0x10, NULL, 0, 0, false);

    /* Peer closes: FIN -> CLOSE_WAIT, our close -> LAST_ACK -> CLOSED */
    peer_send(0x11, NULL, 0, 0, false);
    CHECK(tcp_state(h) == TCP_S_CLOSE_WAIT && tcp_peer_closed(h), "peer FIN -> CLOSE_WAIT");
    cap_count = 0;
    tcp_close(h);
    CHECK(last_tcp(&s) >= 0 && (s.flags & 0x01) && tcp_state(h) == TCP_S_LAST_ACK, "our FIN sent");
    peer_ack += 1;
    peer_send(0x10, NULL, 0, 0, false);
    CHECK(tcp_state(h) == TCP_S_CLOSED, "FIN acknowledged -> CLOSED");
    tcp_release(h);
    CHECK(tcp_state(h) == TCP_S_FREE, "handle released");

    /* Refused connection */
    bring_up();
    h = tcp_connect(WEB, 80);
    last_tcp(&s);
    guest_port = s.sport; peer_seq = 0; peer_ack = s.seq + 1;
    peer_send(0x14, NULL, 0, 0, false);
    CHECK(tcp_state(h) == TCP_S_ERROR && tcp_error(h) == TCP_ERR_REFUSED, "RST|ACK to SYN -> refused");
    tcp_release(h);

    /* SYN timeout */
    h = tcp_connect(WEB, 81);
    int syns = 0;
    for (int i = 0; i < 40 && tcp_state(h) == TCP_S_SYN_SENT; i++) { cap_count = 0; tick(1000); if (last_tcp(&s) >= 0 && s.flags == 0x02) syns++; }
    CHECK(tcp_state(h) == TCP_S_ERROR && tcp_error(h) == TCP_ERR_TIMEOUT && syns == 4, "SYN retries then timeout");
    tcp_release(h);

    /* Turning the network off fails open connections and drops the lease */
    h = handshake();
    net_set_enabled(false);
    tick(1);
    CHECK(!net_ready() && tcp_state(h) == TCP_S_ERROR && tcp_error(h) == TCP_ERR_NONET, "network off fails sockets");
    CHECK(tcp_connect(WEB, 80) < 0, "no connections while off");
    tcp_release(h);
    net_set_enabled(true);
    tick(1);
    CHECK(find_udp(67) >= 0, "turning on restarts DHCP");

    /* Fuzz: random and mutated frames never crash (run under ASan/UBSan) */
    bring_up();
    h = handshake();
    random_add_entropy("fuzz", 4);
    for (int i = 0; i < 30000; i++) {
        size_t len = 14 + random_u32() % 600;
        if (i & 1) {
            build_ip(rx, WEB, ME, (uint8_t)((i >> 1) % 3 == 0 ? 6 : (i >> 1) % 3 == 1 ? 17 : 1), rx + 34, 0);
            for (size_t k = 14 + (random_u32() % 40); k < len; k++) rx[k] = (uint8_t)random_u32();
        } else {
            random_bytes(rx, len);
            memcpy(rx, guest_mac, 6);
            net_wr16(rx + 12, (i & 2) ? 0x0800 : 0x0806);
        }
        net_input(rx, len);
        if (i % 1000 == 0) tick(50);
    }
    CHECK(true, "30000 malformed frames survived");
    return finish();
}
