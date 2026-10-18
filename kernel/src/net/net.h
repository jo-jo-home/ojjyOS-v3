/*
 * ojjyOS v3 - Network stack (arch-neutral)
 *
 * Ethernet II, ARP, IPv4, ICMP echo, UDP, DHCP client and a DNS resolver.
 * TCP lives in tcp.h. Everything is a polled state machine driven from the
 * kernel main loop: drivers call net_input() for each received frame and
 * the loop calls net_poll(now_ms) for timers. Nothing here blocks.
 *
 * Time is always passed in (net_poll) so host tests control it.
 *
 * ojjyOS - Created by Jonas Lee
 */

#ifndef _OJJY_NET_H
#define _OJJY_NET_H

#include "types.h"

typedef uint32_t ip4_t;                 /* host byte order */
#define IP4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                         ((uint32_t)(c) << 8) | (uint32_t)(d))

#define ETH_HDR_LEN     14
#define ETH_FRAME_MAX   1514
#define IP_HDR_LEN      20
#define NET_MTU         1500

#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

/* Big-endian field access on byte pointers (no alignment assumptions). */
static inline uint16_t net_rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t net_rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline void net_wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void net_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

typedef int (*net_tx_fn)(const uint8_t *frame, size_t len);

typedef struct {
    bool present;           /* a NIC driver is attached */
    bool enabled;           /* user switch (Control Center Wi-Fi tile) */
    bool configured;        /* holds a DHCP lease */
    const char *driver;
    uint8_t mac[6];
    ip4_t ip, netmask, gateway, dns, dhcp_server;
    uint32_t lease_seconds;
    uint64_t rx_packets, tx_packets, rx_bytes, tx_bytes;
    uint64_t rx_dropped, tx_dropped, arp_misses;
} NetInfo;

void net_init(void);
void net_attach_device(const char *driver, const uint8_t mac[6], net_tx_fn tx);
void net_input(const uint8_t *frame, size_t len);
void net_poll(uint64_t now_ms);
uint64_t net_now(void);
const NetInfo *net_info(void);
void net_set_enabled(bool enabled);     /* down: drops lease and all sockets */
void net_renew(void);                   /* restart DHCP */
bool net_ready(void);                   /* present, enabled and configured */

/* Formatting / parsing helpers */
void net_format_ip(ip4_t ip, char *out, size_t size);      /* "10.0.2.15" */
void net_format_mac(const uint8_t mac[6], char *out, size_t size);
bool net_parse_ip(const char *s, ip4_t *out);              /* dotted quad only */

/* Internet checksum over data, continuing from `sum` (pass 0 to start). */
uint32_t net_checksum_add(uint32_t sum, const uint8_t *data, size_t len);
uint16_t net_checksum_finish(uint32_t sum);

/* IPv4 send: payload is copied. Returns 0 if sent or queued for ARP. */
int net_ip_send(ip4_t dst, uint8_t proto, const uint8_t *payload, size_t len);

/* UDP */
typedef void (*udp_handler_fn)(ip4_t src, uint16_t src_port, uint16_t dst_port,
                               const uint8_t *data, size_t len);
int net_udp_bind(uint16_t port, udp_handler_fn handler);
void net_udp_unbind(uint16_t port);
int net_udp_send(ip4_t dst, uint16_t src_port, uint16_t dst_port, const uint8_t *data, size_t len);
uint16_t net_ephemeral_port(void);

/* DNS: non-blocking. Call repeatedly until it stops returning PENDING. */
typedef enum { DNS_PENDING = 0, DNS_DONE = 1, DNS_FAIL = -1 } DnsStatus;
DnsStatus dns_resolve(const char *name, ip4_t *out);

/* ICMP echo ("ping"): start, then poll for the reply. */
int net_ping_send(ip4_t dst, uint16_t seq);
/* Returns the round-trip in ms for `seq`, or -1 if no reply yet. */
int net_ping_result(uint16_t seq);

/* Randomness for xids, ports, TCP ISNs and (later) TLS: see random.h. */

/* Host tests: inspect the DHCP state machine and reset everything. */
#ifdef OJJY_HOST_TEST
void net_test_reset(void);
#endif

#endif /* _OJJY_NET_H */
