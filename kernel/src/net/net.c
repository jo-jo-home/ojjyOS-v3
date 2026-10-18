/*
 * ojjyOS v3 - Network stack core: Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS
 *
 * Every received byte is untrusted: all parsers bounds-check against the
 * frame length before reading. Nothing blocks; timers run in net_poll().
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "net/net.h"
#include "net/tcp.h"
#include "random.h"
#include "string.h"
#include "serial.h"

#define ETHERTYPE_IP    0x0800
#define ETHERTYPE_ARP   0x0806

static NetInfo info;
static net_tx_fn device_tx;
static uint64_t now_ms;
static uint16_t ip_ident;
static uint8_t tx_frame[ETH_FRAME_MAX];
static const uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

uint64_t net_now(void) { return now_ms; }
const NetInfo *net_info(void) { return &info; }
bool net_ready(void) { return info.present && info.enabled && info.configured; }

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

uint32_t net_checksum_add(uint32_t sum, const uint8_t *data, size_t len)
{
    size_t i = 0;
    for (; i + 1 < len; i += 2) sum += (uint32_t)((data[i] << 8) | data[i + 1]);
    if (i < len) sum += (uint32_t)(data[i] << 8);
    return sum;
}

uint16_t net_checksum_finish(uint32_t sum)
{
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void append_uint(char *out, size_t size, size_t *pos, unsigned value)
{
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + value % 10); value /= 10; } while (value && n < 11);
    while (n > 0 && *pos + 1 < size) out[(*pos)++] = tmp[--n];
}

void net_format_ip(ip4_t ip, char *out, size_t size)
{
    if (!out || size == 0) return;
    size_t pos = 0;
    for (int i = 3; i >= 0; i--) {
        append_uint(out, size, &pos, (ip >> (i * 8)) & 0xFF);
        if (i && pos + 1 < size) out[pos++] = '.';
    }
    out[pos] = '\0';
}

void net_format_mac(const uint8_t mac[6], char *out, size_t size)
{
    static const char hex[] = "0123456789abcdef";
    size_t pos = 0;
    for (int i = 0; i < 6 && pos + 3 < size; i++) {
        out[pos++] = hex[mac[i] >> 4];
        out[pos++] = hex[mac[i] & 15];
        if (i < 5) out[pos++] = ':';
    }
    if (size) out[pos < size ? pos : size - 1] = '\0';
}

bool net_parse_ip(const char *s, ip4_t *out)
{
    uint32_t ip = 0;
    for (int part = 0; part < 4; part++) {
        if (*s < '0' || *s > '9') return false;
        uint32_t v = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (uint32_t)(*s++ - '0');
            if (++digits > 3 || v > 255) return false;
        }
        ip = (ip << 8) | v;
        if (part < 3) { if (*s != '.') return false; s++; }
    }
    if (*s) return false;
    *out = ip;
    return true;
}

uint16_t net_ephemeral_port(void)
{
    static uint16_t next;
    if (!next) next = (uint16_t)(49152 + (random_u32() % 8192));
    next = (uint16_t)(next + 1);
    if (next < 49152) next = 49152;
    return next;
}

/* ------------------------------------------------------------------ */
/* Ethernet + ARP                                                      */
/* ------------------------------------------------------------------ */

#define ARP_CACHE_SIZE 16
#define ARP_PENDING    4

typedef struct { ip4_t ip; uint8_t mac[6]; bool valid; uint64_t seen_ms; } ArpEntry;
typedef struct { bool used; ip4_t hop; uint64_t since_ms, asked_ms; size_t len; uint8_t frame[ETH_FRAME_MAX]; } ArpPending;

static ArpEntry arp_cache[ARP_CACHE_SIZE];
static ArpPending arp_pending[ARP_PENDING];

static int device_send(uint8_t *frame, size_t len)
{
    if (!device_tx || !info.present || !info.enabled) { info.tx_dropped++; return -1; }
    if (len < 60) { memset(frame + len, 0, 60 - len); len = 60; }   /* Ethernet minimum */
    if (device_tx(frame, len) != 0) { info.tx_dropped++; return -1; }
    info.tx_packets++;
    info.tx_bytes += len;
    return 0;
}

static void eth_header(uint8_t *frame, const uint8_t dst[6], uint16_t type)
{
    memcpy(frame, dst, 6);
    memcpy(frame + 6, info.mac, 6);
    net_wr16(frame + 12, type);
}

static const uint8_t *arp_lookup(ip4_t ip)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
        if (arp_cache[i].valid && arp_cache[i].ip == ip) return arp_cache[i].mac;
    return NULL;
}

static void arp_learn(ip4_t ip, const uint8_t mac[6])
{
    if (!ip) return;
    int slot = -1;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) if (arp_cache[i].valid && arp_cache[i].ip == ip) slot = i;
    if (slot < 0) for (int i = 0; i < ARP_CACHE_SIZE; i++) if (!arp_cache[i].valid) { slot = i; break; }
    if (slot < 0) {                     /* evict the oldest */
        slot = 0;
        for (int i = 1; i < ARP_CACHE_SIZE; i++) if (arp_cache[i].seen_ms < arp_cache[slot].seen_ms) slot = i;
    }
    arp_cache[slot].ip = ip;
    memcpy(arp_cache[slot].mac, mac, 6);
    arp_cache[slot].valid = true;
    arp_cache[slot].seen_ms = now_ms;

    /* Release frames that were waiting for this address. */
    for (int i = 0; i < ARP_PENDING; i++) {
        if (arp_pending[i].used && arp_pending[i].hop == ip) {
            memcpy(arp_pending[i].frame, mac, 6);
            device_send(arp_pending[i].frame, arp_pending[i].len);
            arp_pending[i].used = false;
        }
    }
}

static void arp_send(uint16_t op, const uint8_t dst_mac[6], ip4_t target_ip)
{
    uint8_t *f = tx_frame;
    eth_header(f, op == 1 ? broadcast_mac : dst_mac, ETHERTYPE_ARP);
    uint8_t *a = f + ETH_HDR_LEN;
    net_wr16(a, 1); net_wr16(a + 2, ETHERTYPE_IP);
    a[4] = 6; a[5] = 4; net_wr16(a + 6, op);
    memcpy(a + 8, info.mac, 6); net_wr32(a + 14, info.ip);
    if (op == 1) memset(a + 18, 0, 6); else memcpy(a + 18, dst_mac, 6);
    net_wr32(a + 24, target_ip);
    device_send(f, ETH_HDR_LEN + 28);
}

static void arp_input(const uint8_t *a, size_t len)
{
    if (len < 28 || net_rd16(a) != 1 || net_rd16(a + 2) != ETHERTYPE_IP || a[4] != 6 || a[5] != 4) return;
    uint16_t op = net_rd16(a + 6);
    ip4_t sender = net_rd32(a + 14), target = net_rd32(a + 24);
    if (sender) arp_learn(sender, a + 8);
    if (op == 1 && info.configured && target == info.ip) arp_send(2, a + 8, sender);
}

static ip4_t next_hop(ip4_t dst)
{
    if (!info.configured || !info.gateway) return dst;
    return ((dst & info.netmask) == (info.ip & info.netmask)) ? dst : info.gateway;
}

/* Send a frame whose Ethernet header (except destination) is filled. */
static int route_frame(uint8_t *frame, size_t len, ip4_t dst)
{
    if (dst == 0xFFFFFFFFu) { memcpy(frame, broadcast_mac, 6); return device_send(frame, len); }
    ip4_t hop = next_hop(dst);
    const uint8_t *mac = arp_lookup(hop);
    if (mac) { memcpy(frame, mac, 6); return device_send(frame, len); }

    info.arp_misses++;
    int slot = -1;
    for (int i = 0; i < ARP_PENDING; i++) if (!arp_pending[i].used) { slot = i; break; }
    if (slot < 0) { info.tx_dropped++; return -1; }
    arp_pending[slot].used = true;
    arp_pending[slot].hop = hop;
    arp_pending[slot].since_ms = now_ms;
    arp_pending[slot].asked_ms = now_ms;
    arp_pending[slot].len = len;
    memcpy(arp_pending[slot].frame, frame, len);
    arp_send(1, NULL, hop);
    return 0;
}

static void arp_poll(void)
{
    for (int i = 0; i < ARP_PENDING; i++) {
        ArpPending *p = &arp_pending[i];
        if (!p->used) continue;
        if (now_ms - p->since_ms > 3000) { p->used = false; info.tx_dropped++; continue; }
        if (now_ms - p->asked_ms >= 1000) { p->asked_ms = now_ms; arp_send(1, NULL, p->hop); }
    }
}

/* ------------------------------------------------------------------ */
/* IPv4 + ICMP                                                         */
/* ------------------------------------------------------------------ */

int net_ip_send(ip4_t dst, uint8_t proto, const uint8_t *payload, size_t len)
{
    if (len > NET_MTU - IP_HDR_LEN) return -1;
    uint8_t *f = tx_frame;
    eth_header(f, broadcast_mac, ETHERTYPE_IP);
    uint8_t *ip = f + ETH_HDR_LEN;
    ip[0] = 0x45; ip[1] = 0;
    net_wr16(ip + 2, (uint16_t)(IP_HDR_LEN + len));
    net_wr16(ip + 4, ++ip_ident);
    net_wr16(ip + 6, 0x4000);               /* DF: we never fragment */
    ip[8] = 64; ip[9] = proto;
    net_wr16(ip + 10, 0);
    net_wr32(ip + 12, info.ip);
    net_wr32(ip + 16, dst);
    net_wr16(ip + 10, net_checksum_finish(net_checksum_add(0, ip, IP_HDR_LEN)));
    memcpy(ip + IP_HDR_LEN, payload, len);
    return route_frame(f, ETH_HDR_LEN + IP_HDR_LEN + len, dst);
}

#define PING_SLOTS 64
static uint16_t ping_id;
static struct { uint16_t seq; uint64_t sent_ms; int rtt; bool used; } ping_slots[PING_SLOTS];

int net_ping_send(ip4_t dst, uint16_t seq)
{
    if (!net_ready()) return -1;
    if (!ping_id) ping_id = (uint16_t)(random_u32() | 1);
    uint8_t pkt[8 + 32];
    pkt[0] = 8; pkt[1] = 0; net_wr16(pkt + 2, 0);
    net_wr16(pkt + 4, ping_id); net_wr16(pkt + 6, seq);
    for (int i = 0; i < 32; i++) pkt[8 + i] = (uint8_t)('a' + i % 26);
    net_wr16(pkt + 2, net_checksum_finish(net_checksum_add(0, pkt, sizeof(pkt))));
    int s = seq % PING_SLOTS;
    ping_slots[s].seq = seq; ping_slots[s].sent_ms = now_ms; ping_slots[s].rtt = -1; ping_slots[s].used = true;
    return net_ip_send(dst, IP_PROTO_ICMP, pkt, sizeof(pkt));
}

int net_ping_result(uint16_t seq)
{
    int s = seq % PING_SLOTS;
    return (ping_slots[s].used && ping_slots[s].seq == seq) ? ping_slots[s].rtt : -1;
}

static void icmp_input(ip4_t src, const uint8_t *p, size_t len)
{
    if (len < 8 || net_checksum_finish(net_checksum_add(0, p, len)) != 0) { info.rx_dropped++; return; }
    if (p[0] == 8 && p[1] == 0 && info.configured) {          /* echo request -> reply */
        static uint8_t reply[NET_MTU];
        if (len > sizeof(reply) - IP_HDR_LEN) return;
        memcpy(reply, p, len);
        reply[0] = 0; net_wr16(reply + 2, 0);
        net_wr16(reply + 2, net_checksum_finish(net_checksum_add(0, reply, len)));
        net_ip_send(src, IP_PROTO_ICMP, reply, len);
    } else if (p[0] == 0 && net_rd16(p + 4) == ping_id) {      /* our echo reply */
        uint16_t seq = net_rd16(p + 6);
        int s = seq % PING_SLOTS;
        if (ping_slots[s].used && ping_slots[s].seq == seq && ping_slots[s].rtt < 0)
            ping_slots[s].rtt = (int)(now_ms - ping_slots[s].sent_ms);
    }
}

/* ------------------------------------------------------------------ */
/* UDP                                                                 */
/* ------------------------------------------------------------------ */

#define UDP_BINDINGS 8
static struct { uint16_t port; udp_handler_fn fn; } udp_binds[UDP_BINDINGS];

int net_udp_bind(uint16_t port, udp_handler_fn handler)
{
    for (int i = 0; i < UDP_BINDINGS; i++) if (udp_binds[i].port == port) { udp_binds[i].fn = handler; return 0; }
    for (int i = 0; i < UDP_BINDINGS; i++)
        if (!udp_binds[i].port) { udp_binds[i].port = port; udp_binds[i].fn = handler; return 0; }
    return -1;
}

void net_udp_unbind(uint16_t port)
{
    for (int i = 0; i < UDP_BINDINGS; i++) if (udp_binds[i].port == port) udp_binds[i].port = 0;
}

static uint32_t pseudo_sum(ip4_t src, ip4_t dst, uint8_t proto, size_t len)
{
    uint8_t ph[12];
    net_wr32(ph, src); net_wr32(ph + 4, dst);
    ph[8] = 0; ph[9] = proto; net_wr16(ph + 10, (uint16_t)len);
    return net_checksum_add(0, ph, sizeof(ph));
}

int net_udp_send(ip4_t dst, uint16_t src_port, uint16_t dst_port, const uint8_t *data, size_t len)
{
    static uint8_t pkt[NET_MTU];
    if (len > NET_MTU - IP_HDR_LEN - 8) return -1;
    net_wr16(pkt, src_port); net_wr16(pkt + 2, dst_port);
    net_wr16(pkt + 4, (uint16_t)(8 + len)); net_wr16(pkt + 6, 0);
    memcpy(pkt + 8, data, len);
    uint16_t sum = net_checksum_finish(net_checksum_add(pseudo_sum(info.ip, dst, IP_PROTO_UDP, 8 + len), pkt, 8 + len));
    net_wr16(pkt + 6, sum ? sum : 0xFFFF);
    return net_ip_send(dst, IP_PROTO_UDP, pkt, 8 + len);
}

static void udp_input(ip4_t src, ip4_t dst, const uint8_t *p, size_t len)
{
    if (len < 8) { info.rx_dropped++; return; }
    size_t ulen = net_rd16(p + 4);
    if (ulen < 8 || ulen > len) { info.rx_dropped++; return; }
    if (net_rd16(p + 6) &&
        net_checksum_finish(net_checksum_add(pseudo_sum(src, dst, IP_PROTO_UDP, ulen), p, ulen)) != 0) {
        info.rx_dropped++;
        return;
    }
    uint16_t dport = net_rd16(p + 2);
    for (int i = 0; i < UDP_BINDINGS; i++)
        if (udp_binds[i].port == dport && udp_binds[i].fn) {
            udp_binds[i].fn(src, net_rd16(p), dport, p + 8, ulen - 8);
            return;
        }
}

static void ip_input(const uint8_t *ip, size_t len)
{
    if (len < IP_HDR_LEN || (ip[0] >> 4) != 4) { info.rx_dropped++; return; }
    size_t ihl = (size_t)(ip[0] & 0xF) * 4;
    size_t total = net_rd16(ip + 2);
    if (ihl < IP_HDR_LEN || total < ihl || total > len ||
        net_checksum_finish(net_checksum_add(0, ip, ihl)) != 0) { info.rx_dropped++; return; }
    if (net_rd16(ip + 6) & 0x3FFF) { info.rx_dropped++; return; }    /* fragments unsupported */
    ip4_t src = net_rd32(ip + 12), dst = net_rd32(ip + 16);
    bool for_us = dst == 0xFFFFFFFFu || (info.configured && dst == info.ip) ||
                  (!info.configured && ip[9] == IP_PROTO_UDP);    /* DHCP before a lease */
    if (!for_us) return;
    const uint8_t *payload = ip + ihl;
    size_t plen = total - ihl;
    switch (ip[9]) {
        case IP_PROTO_ICMP: icmp_input(src, payload, plen); break;
        case IP_PROTO_UDP:  udp_input(src, dst, payload, plen); break;
        case IP_PROTO_TCP:  if (info.configured) tcp_input(src, dst, payload, plen); break;
        default: break;
    }
}

void net_input(const uint8_t *frame, size_t len)
{
    if (!info.present || !info.enabled || len < ETH_HDR_LEN) return;
    if (memcmp(frame, info.mac, 6) != 0 && memcmp(frame, broadcast_mac, 6) != 0) return;
    info.rx_packets++;
    info.rx_bytes += len;
    uint16_t type = net_rd16(frame + 12);
    if (type == ETHERTYPE_ARP) arp_input(frame + ETH_HDR_LEN, len - ETH_HDR_LEN);
    else if (type == ETHERTYPE_IP) ip_input(frame + ETH_HDR_LEN, len - ETH_HDR_LEN);
}

/* ------------------------------------------------------------------ */
/* DHCP client (RFC 2131 subset)                                       */
/* ------------------------------------------------------------------ */

typedef enum { DHCP_IDLE, DHCP_DISCOVER, DHCP_REQUEST, DHCP_BOUND } DhcpState;
static DhcpState dhcp_state;
static uint32_t dhcp_xid;
static uint64_t dhcp_next_ms, dhcp_bound_ms;
static unsigned dhcp_tries;
static ip4_t dhcp_offer_ip, dhcp_offer_server;

static void dhcp_send(uint8_t type)
{
    static uint8_t p[300];
    memset(p, 0, sizeof(p));
    p[0] = 1; p[1] = 1; p[2] = 6;
    net_wr32(p + 4, dhcp_xid);
    net_wr16(p + 10, 0x8000);                       /* ask for broadcast replies */
    if (dhcp_state == DHCP_BOUND) net_wr32(p + 12, info.ip);
    memcpy(p + 28, info.mac, 6);
    net_wr32(p + 236, 0x63825363);                  /* magic cookie */
    size_t o = 240;
    p[o++] = 53; p[o++] = 1; p[o++] = type;
    p[o++] = 12; p[o++] = 6; memcpy(p + o, "ojjyos", 6); o += 6;
    p[o++] = 55; p[o++] = 4; p[o++] = 1; p[o++] = 3; p[o++] = 6; p[o++] = 51;
    if (type == 3 && dhcp_offer_ip) {
        p[o++] = 50; p[o++] = 4; net_wr32(p + o, dhcp_offer_ip); o += 4;
        if (dhcp_offer_server) { p[o++] = 54; p[o++] = 4; net_wr32(p + o, dhcp_offer_server); o += 4; }
    }
    p[o++] = 255;
    net_udp_send(0xFFFFFFFFu, 68, 67, p, o < 300 ? 300 : o);
}

static void dhcp_start(void)
{
    dhcp_state = DHCP_DISCOVER;
    dhcp_xid = random_u32();
    dhcp_tries = 0;
    dhcp_offer_ip = dhcp_offer_server = 0;
    dhcp_next_ms = now_ms;          /* send on the next poll */
}

static void dhcp_input(ip4_t src, uint16_t sport, uint16_t dport, const uint8_t *p, size_t len)
{
    (void)src; (void)sport; (void)dport;
    if (len < 240 || p[0] != 2 || net_rd32(p + 4) != dhcp_xid ||
        memcmp(p + 28, info.mac, 6) != 0 || net_rd32(p + 236) != 0x63825363) return;
    uint8_t type = 0;
    ip4_t mask = 0, router = 0, dns = 0, server = 0;
    uint32_t lease = 0;
    for (size_t o = 240; o < len;) {
        uint8_t code = p[o++];
        if (code == 255) break;
        if (code == 0) continue;
        if (o >= len) break;
        size_t l = p[o++];
        if (o + l > len) break;
        const uint8_t *v = p + o;
        if (code == 53 && l >= 1) type = v[0];
        else if (code == 1 && l >= 4) mask = net_rd32(v);
        else if (code == 3 && l >= 4) router = net_rd32(v);
        else if (code == 6 && l >= 4) dns = net_rd32(v);
        else if (code == 54 && l >= 4) server = net_rd32(v);
        else if (code == 51 && l >= 4) lease = net_rd32(v);
        o += l;
    }
    ip4_t yiaddr = net_rd32(p + 16);
    if (type == 2 && dhcp_state == DHCP_DISCOVER && yiaddr) {          /* OFFER */
        dhcp_offer_ip = yiaddr;
        dhcp_offer_server = server;
        dhcp_state = DHCP_REQUEST;
        dhcp_tries = 0;
        dhcp_next_ms = now_ms;
    } else if (type == 5 && (dhcp_state == DHCP_REQUEST || dhcp_state == DHCP_BOUND) && yiaddr) {   /* ACK */
        bool fresh = !info.configured || info.ip != yiaddr;
        info.ip = yiaddr;
        info.netmask = mask ? mask : IP4(255, 255, 255, 0);
        info.gateway = router;
        info.dns = dns ? dns : router;
        info.dhcp_server = server;
        info.lease_seconds = lease ? lease : 3600;
        info.configured = true;
        dhcp_state = DHCP_BOUND;
        dhcp_bound_ms = now_ms;
        if (fresh) {
            char ip[16], gw[16], dn[16];
            net_format_ip(info.ip, ip, sizeof(ip));
            net_format_ip(info.gateway, gw, sizeof(gw));
            net_format_ip(info.dns, dn, sizeof(dn));
            serial_printf("[NET] DHCP lease %s gateway %s dns %s (%us)\n", ip, gw, dn, (unsigned)info.lease_seconds);
        }
    } else if (type == 6) {                                            /* NAK */
        info.configured = false;
        dhcp_start();
    }
}

static void dhcp_poll(void)
{
    if (!info.present || !info.enabled) return;
    if (dhcp_state == DHCP_IDLE) dhcp_start();
    if (dhcp_state == DHCP_BOUND) {
        /* Renew at half the lease (rebroadcast REQUEST for the same address). */
        if (now_ms - dhcp_bound_ms >= (uint64_t)info.lease_seconds * 500) {
            dhcp_state = DHCP_REQUEST;
            dhcp_offer_ip = info.ip;
            dhcp_offer_server = info.dhcp_server;
            dhcp_tries = 0;
            dhcp_next_ms = now_ms;
            dhcp_bound_ms = now_ms;
        }
        return;
    }
    if (now_ms < dhcp_next_ms) return;
    if (dhcp_state == DHCP_REQUEST && dhcp_tries >= 4 && !info.configured) { dhcp_start(); return; }
    dhcp_send(dhcp_state == DHCP_DISCOVER ? 1 : 3);
    dhcp_tries++;
    unsigned backoff = dhcp_tries < 3 ? 1000u << dhcp_tries : 8000u;
    dhcp_next_ms = now_ms + backoff;
    if (dhcp_state == DHCP_REQUEST && info.configured && dhcp_tries >= 4) dhcp_state = DHCP_BOUND;
}

/* ------------------------------------------------------------------ */
/* DNS resolver (A records, small cache)                               */
/* ------------------------------------------------------------------ */

#define DNS_PORT_LOCAL 53535
#define DNS_CACHE 16

typedef struct {
    char name[128];
    int state;                  /* 0 free, 1 waiting, 2 done, 3 failed */
    ip4_t ip;
    uint16_t id;
    unsigned tries;
    uint64_t started_ms, next_ms, expires_ms;
} DnsEntry;
static DnsEntry dns_cache[DNS_CACHE];

static bool name_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (x != y) return false;
    }
    return *a == *b;
}

static void dns_send(DnsEntry *e)
{
    uint8_t q[300];
    net_wr16(q, e->id); net_wr16(q + 2, 0x0100); net_wr16(q + 4, 1);
    net_wr16(q + 6, 0); net_wr16(q + 8, 0); net_wr16(q + 10, 0);
    size_t o = 12;
    const char *label = e->name;
    while (*label) {
        const char *dot = label;
        while (*dot && *dot != '.') dot++;
        size_t l = (size_t)(dot - label);
        if (l == 0 || l > 63 || o + l + 6 > sizeof(q)) { e->state = 3; return; }
        q[o++] = (uint8_t)l;
        memcpy(q + o, label, l);
        o += l;
        label = *dot ? dot + 1 : dot;
    }
    q[o++] = 0;
    net_wr16(q + o, 1); net_wr16(q + o + 2, 1); o += 4;
    net_udp_send(info.dns, DNS_PORT_LOCAL, 53, q, o);
    e->tries++;
    e->next_ms = now_ms + 1000u * e->tries;
}

/* Skip a (possibly compressed) DNS name; returns new offset or 0 on error. */
static size_t dns_skip_name(const uint8_t *p, size_t len, size_t o)
{
    for (int guard = 0; guard < 128 && o < len; guard++) {
        uint8_t l = p[o];
        if (l == 0) return o + 1;
        if ((l & 0xC0) == 0xC0) return o + 2 <= len ? o + 2 : 0;
        if (l & 0xC0) return 0;
        o += 1 + l;
    }
    return 0;
}

static void dns_input(ip4_t src, uint16_t sport, uint16_t dport, const uint8_t *p, size_t len)
{
    (void)src; (void)dport;
    if (sport != 53 || len < 12) return;
    uint16_t id = net_rd16(p), flags = net_rd16(p + 2);
    DnsEntry *e = NULL;
    for (int i = 0; i < DNS_CACHE; i++) if (dns_cache[i].state == 1 && dns_cache[i].id == id) e = &dns_cache[i];
    if (!e || !(flags & 0x8000)) return;
    if (flags & 0x000F) { e->state = 3; e->expires_ms = now_ms + 10000; return; }   /* NXDOMAIN etc. */
    unsigned qd = net_rd16(p + 4), an = net_rd16(p + 6);
    size_t o = 12;
    for (unsigned i = 0; i < qd; i++) {
        o = dns_skip_name(p, len, o);
        if (!o || o + 4 > len) { e->state = 3; return; }
        o += 4;
    }
    for (unsigned i = 0; i < an; i++) {
        o = dns_skip_name(p, len, o);
        if (!o || o + 10 > len) break;
        uint16_t type = net_rd16(p + o), cls = net_rd16(p + o + 2);
        uint32_t ttl = net_rd32(p + o + 4);
        size_t rdlen = net_rd16(p + o + 8);
        o += 10;
        if (o + rdlen > len) break;
        if (type == 1 && cls == 1 && rdlen == 4) {
            e->ip = net_rd32(p + o);
            e->state = 2;
            if (ttl < 30) ttl = 30;
            if (ttl > 3600) ttl = 3600;
            e->expires_ms = now_ms + ttl * 1000ULL;
            return;
        }
        o += rdlen;
    }
    e->state = 3;
    e->expires_ms = now_ms + 10000;
}

DnsStatus dns_resolve(const char *name, ip4_t *out)
{
    if (!name || !name[0] || strlen(name) >= sizeof(dns_cache[0].name)) return DNS_FAIL;
    if (net_parse_ip(name, out)) return DNS_DONE;
    if (!info.present || !info.enabled) return DNS_FAIL;

    DnsEntry *e = NULL;
    for (int i = 0; i < DNS_CACHE; i++)
        if (dns_cache[i].state && name_eq(dns_cache[i].name, name)) e = &dns_cache[i];
    if (e) {
        if (e->state == 2 && now_ms < e->expires_ms) { *out = e->ip; return DNS_DONE; }
        if (e->state == 3 && now_ms < e->expires_ms) return DNS_FAIL;
        if (e->state == 1) return DNS_PENDING;
    } else {
        for (int i = 0; i < DNS_CACHE; i++) if (!dns_cache[i].state) { e = &dns_cache[i]; break; }
        if (!e) {                       /* reuse the entry that expires first */
            e = &dns_cache[0];
            for (int i = 1; i < DNS_CACHE; i++) if (dns_cache[i].expires_ms < e->expires_ms) e = &dns_cache[i];
        }
        strncpy(e->name, name, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = '\0';
    }
    e->state = 1;
    e->id = (uint16_t)random_u32();
    e->tries = 0;
    e->started_ms = now_ms;
    e->next_ms = now_ms;                /* dns_poll sends once configured */
    return DNS_PENDING;
}

static void dns_poll(void)
{
    for (int i = 0; i < DNS_CACHE; i++) {
        DnsEntry *e = &dns_cache[i];
        if (e->state != 1) continue;
        if (now_ms - e->started_ms > 10000 || e->tries >= 4) {
            e->state = 3;
            e->expires_ms = now_ms + 5000;
            continue;
        }
        if (info.configured && now_ms >= e->next_ms) dns_send(e);
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void reset_state(void)
{
    info.configured = false;
    info.ip = info.netmask = info.gateway = info.dns = info.dhcp_server = 0;
    info.lease_seconds = 0;
    dhcp_state = DHCP_IDLE;
    memset(arp_cache, 0, sizeof(arp_cache));
    memset(arp_pending, 0, sizeof(arp_pending));
    memset(dns_cache, 0, sizeof(dns_cache));
    tcp_reset_all();
}

void net_init(void)
{
    memset(&info, 0, sizeof(info));
    memset(udp_binds, 0, sizeof(udp_binds));
    device_tx = NULL;
    reset_state();
    net_udp_bind(68, dhcp_input);
    net_udp_bind(DNS_PORT_LOCAL, dns_input);
}

void net_attach_device(const char *driver, const uint8_t mac[6], net_tx_fn tx)
{
    info.present = true;
    info.enabled = true;
    info.driver = driver;
    memcpy(info.mac, mac, 6);
    device_tx = tx;
    random_add_entropy(mac, 6);
    reset_state();
    char m[20];
    net_format_mac(mac, m, sizeof(m));
    serial_printf("[NET] %s attached, MAC %s\n", driver, m);
}

void net_set_enabled(bool enabled)
{
    if (info.enabled == enabled) return;
    info.enabled = enabled;
    reset_state();
    serial_printf("[NET] Network %s\n", enabled ? "on" : "off");
}

void net_renew(void)
{
    if (!info.present || !info.enabled) return;
    reset_state();
}

void net_poll(uint64_t ms)
{
    now_ms = ms;
    if (!info.present || !info.enabled) return;
    dhcp_poll();
    arp_poll();
    dns_poll();
    tcp_poll(ms);
}

#ifdef OJJY_HOST_TEST
void net_test_reset(void)
{
    now_ms = 0;
    net_init();
}
#endif
