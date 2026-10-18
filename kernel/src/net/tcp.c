/*
 * ojjyOS v3 - TCP client implementation (see tcp.h)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "net/tcp.h"
#include "random.h"
#include "string.h"

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define TCP_MSS_DEFAULT 536
#define TCP_MSS_LOCAL   1460
#define TCP_RTO_INITIAL 1000
#define TCP_RTO_MAX     8000
#define TCP_SYN_RETRIES 4
#define TCP_DATA_RETRIES 7

typedef struct {
    TcpState state;
    TcpError err;
    ip4_t rip;
    uint16_t rport, lport;
    uint32_t iss, snd_una, snd_nxt, snd_wnd;
    uint16_t mss;
    uint32_t rcv_nxt;
    uint8_t rx[TCP_RX_BUF];
    size_t rx_head, rx_len;
    uint8_t tx[TCP_TX_BUF];
    size_t tx_len;              /* bytes from snd_una not yet acknowledged */
    bool fin_queued, fin_sent, fin_acked, peer_fin;
    uint32_t fin_seq;
    uint64_t rto_ms, rto_deadline;
    bool timer_on, window_update;
    unsigned retries;
} TcpConn;

static TcpConn conns[TCP_MAX_CONN];
static uint64_t tcp_now;

static inline bool seq_lt(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }
static inline bool seq_le(uint32_t a, uint32_t b) { return (int32_t)(a - b) <= 0; }

static TcpConn *get(int h)
{
    if (h < 0 || h >= TCP_MAX_CONN || conns[h].state == TCP_S_FREE) return NULL;
    return &conns[h];
}

static size_t rx_free(const TcpConn *c) { return TCP_RX_BUF - c->rx_len; }

static uint16_t adv_window(const TcpConn *c)
{
    size_t w = rx_free(c);
    return (uint16_t)(w > 65535 ? 65535 : w);
}

static void emit(TcpConn *c, uint32_t seq, uint8_t flags, const uint8_t *data, size_t len)
{
    static uint8_t seg[NET_MTU];
    size_t hdr = (flags & TCP_SYN) ? 24 : 20;
    if (hdr + len > NET_MTU - IP_HDR_LEN) return;
    net_wr16(seg, c->lport);
    net_wr16(seg + 2, c->rport);
    net_wr32(seg + 4, seq);
    net_wr32(seg + 8, (flags & TCP_ACK) ? c->rcv_nxt : 0);
    seg[12] = (uint8_t)((hdr / 4) << 4);
    seg[13] = flags;
    net_wr16(seg + 14, adv_window(c));
    net_wr16(seg + 16, 0);
    net_wr16(seg + 18, 0);
    if (flags & TCP_SYN) { seg[20] = 2; seg[21] = 4; net_wr16(seg + 22, TCP_MSS_LOCAL); }
    if (len) memcpy(seg + hdr, data, len);

    uint8_t ph[12];
    net_wr32(ph, net_info()->ip); net_wr32(ph + 4, c->rip);
    ph[8] = 0; ph[9] = IP_PROTO_TCP; net_wr16(ph + 10, (uint16_t)(hdr + len));
    uint32_t sum = net_checksum_add(net_checksum_add(0, ph, 12), seg, hdr + len);
    net_wr16(seg + 16, net_checksum_finish(sum));
    net_ip_send(c->rip, IP_PROTO_TCP, seg, hdr + len);
}

static void send_ack(TcpConn *c) { emit(c, c->snd_nxt, TCP_ACK, NULL, 0); }

static void arm_timer(TcpConn *c)
{
    c->timer_on = true;
    c->rto_deadline = tcp_now + c->rto_ms;
}

static void fail(TcpConn *c, TcpError err)
{
    c->state = TCP_S_ERROR;
    c->err = err;
    c->timer_on = false;
}

/* Send whatever the peer window allows; then the FIN if requested. */
static void send_pending(TcpConn *c)
{
    if (c->state != TCP_S_ESTABLISHED && c->state != TCP_S_CLOSE_WAIT &&
        c->state != TCP_S_FIN_WAIT1 && c->state != TCP_S_LAST_ACK) return;
    size_t data_sent = (size_t)(c->snd_nxt - c->snd_una) - ((c->fin_sent && !c->fin_acked) ? 1 : 0);
    size_t wnd = c->snd_wnd;
    while (!c->fin_sent && data_sent < c->tx_len && data_sent < wnd) {
        size_t n = c->tx_len - data_sent;
        if (n > c->mss) n = c->mss;
        if (n > wnd - data_sent) n = wnd - data_sent;
        bool idle = c->snd_nxt == c->snd_una;
        emit(c, c->snd_nxt, TCP_ACK | TCP_PSH, c->tx + data_sent, n);
        c->snd_nxt += (uint32_t)n;
        data_sent += n;
        if (idle || !c->timer_on) arm_timer(c);
    }
    if (c->fin_queued && !c->fin_sent && data_sent == c->tx_len) {
        c->fin_seq = c->snd_nxt;
        emit(c, c->snd_nxt, TCP_FIN | TCP_ACK, NULL, 0);
        c->snd_nxt += 1;
        c->fin_sent = true;
        if (!c->timer_on) arm_timer(c);
        if (c->state == TCP_S_ESTABLISHED) c->state = TCP_S_FIN_WAIT1;
        else if (c->state == TCP_S_CLOSE_WAIT) c->state = TCP_S_LAST_ACK;
    }
}

int tcp_connect(ip4_t ip, uint16_t port)
{
    if (!net_ready()) return -1;
    int h = -1;
    for (int i = 0; i < TCP_MAX_CONN; i++) if (conns[i].state == TCP_S_FREE) { h = i; break; }
    if (h < 0) return -1;
    TcpConn *c = &conns[h];
    memset(c, 0, sizeof(*c));
    c->state = TCP_S_SYN_SENT;
    c->rip = ip;
    c->rport = port;
    c->lport = net_ephemeral_port();
    c->iss = random_u32();
    c->snd_una = c->iss;
    c->snd_nxt = c->iss + 1;
    c->mss = TCP_MSS_DEFAULT;
    c->rto_ms = TCP_RTO_INITIAL;
    emit(c, c->iss, TCP_SYN, NULL, 0);
    arm_timer(c);
    return h;
}

int tcp_send(int h, const void *data, size_t len)
{
    TcpConn *c = get(h);
    if (!c || c->state == TCP_S_ERROR || c->fin_queued) return -1;
    size_t space = TCP_TX_BUF - c->tx_len;
    if (len > space) len = space;
    memcpy(c->tx + c->tx_len, data, len);
    c->tx_len += len;
    send_pending(c);
    return (int)len;
}

int tcp_recv(int h, void *buf, size_t len)
{
    TcpConn *c = get(h);
    if (!c) return -1;
    size_t before_free = rx_free(c);
    size_t n = len < c->rx_len ? len : c->rx_len;
    uint8_t *out = (uint8_t *)buf;
    for (size_t i = 0; i < n; i++) out[i] = c->rx[(c->rx_head + i) % TCP_RX_BUF];
    c->rx_head = (c->rx_head + n) % TCP_RX_BUF;
    c->rx_len -= n;
    /* Re-open a nearly closed window so the sender can continue. */
    if (n && before_free < (size_t)c->mss * 2 && rx_free(c) >= (size_t)c->mss * 2) c->window_update = true;
    return (int)n;
}

size_t tcp_available(int h) { TcpConn *c = get(h); return c ? c->rx_len : 0; }
bool tcp_peer_closed(int h) { TcpConn *c = get(h); return !c || (c->peer_fin && c->rx_len == 0); }
TcpState tcp_state(int h) { TcpConn *c = get(h); return c ? c->state : TCP_S_FREE; }
TcpError tcp_error(int h) { TcpConn *c = get(h); return c ? c->err : TCP_ERR_NONE; }

void tcp_close(int h)
{
    TcpConn *c = get(h);
    if (!c || c->fin_queued || c->state == TCP_S_ERROR || c->state == TCP_S_CLOSED) return;
    if (c->state == TCP_S_SYN_SENT) { fail(c, TCP_ERR_RESET); return; }
    c->fin_queued = true;
    send_pending(c);
}

void tcp_release(int h)
{
    TcpConn *c = get(h);
    if (!c) return;
    bool open = c->state == TCP_S_ESTABLISHED || c->state == TCP_S_SYN_SENT ||
                c->state == TCP_S_FIN_WAIT1 || c->state == TCP_S_FIN_WAIT2 ||
                c->state == TCP_S_CLOSE_WAIT || c->state == TCP_S_LAST_ACK;
    /* Unread data or an unfinished exchange: tell the peer we are gone. */
    if (open && net_ready() && !(c->state == TCP_S_LAST_ACK && c->fin_sent))
        emit(c, c->snd_nxt, TCP_RST | TCP_ACK, NULL, 0);
    c->state = TCP_S_FREE;
}

const char *tcp_error_string(TcpError err)
{
    switch (err) {
        case TCP_ERR_REFUSED: return "connection refused";
        case TCP_ERR_TIMEOUT: return "connection timed out";
        case TCP_ERR_RESET:   return "connection reset";
        case TCP_ERR_NONET:   return "network is offline";
        default:              return "no error";
    }
}

static void process_ack(TcpConn *c, uint32_t ack, uint16_t win)
{
    if (seq_le(ack, c->snd_una) || seq_lt(c->snd_nxt, ack)) {
        if (ack == c->snd_una) c->snd_wnd = win;   /* window update / dup ack */
        return;
    }
    uint32_t acked = ack - c->snd_una;
    if (c->fin_sent && seq_lt(c->fin_seq, ack)) { c->fin_acked = true; acked -= 1; }
    if (acked > c->tx_len) acked = (uint32_t)c->tx_len;
    memmove(c->tx, c->tx + acked, c->tx_len - acked);
    c->tx_len -= acked;
    c->snd_una = ack;
    c->snd_wnd = win;
    c->retries = 0;
    c->rto_ms = TCP_RTO_INITIAL;
    if (c->snd_una == c->snd_nxt) c->timer_on = false; else arm_timer(c);

    if (c->fin_acked) {
        if (c->state == TCP_S_FIN_WAIT1) c->state = c->peer_fin ? TCP_S_CLOSED : TCP_S_FIN_WAIT2;
        else if (c->state == TCP_S_LAST_ACK) c->state = TCP_S_CLOSED;
    }
}

void tcp_input(ip4_t src, ip4_t dst, const uint8_t *seg, size_t len)
{
    if (len < 20) return;
    size_t off = (size_t)(seg[12] >> 4) * 4;
    if (off < 20 || off > len) return;
    uint8_t ph[12];
    net_wr32(ph, src); net_wr32(ph + 4, dst);
    ph[8] = 0; ph[9] = IP_PROTO_TCP; net_wr16(ph + 10, (uint16_t)len);
    if (net_checksum_finish(net_checksum_add(net_checksum_add(0, ph, 12), seg, len)) != 0) return;

    uint16_t sport = net_rd16(seg), dport = net_rd16(seg + 2);
    TcpConn *c = NULL;
    for (int i = 0; i < TCP_MAX_CONN; i++)
        if (conns[i].state != TCP_S_FREE && conns[i].rip == src &&
            conns[i].rport == sport && conns[i].lport == dport) c = &conns[i];
    if (!c || c->state == TCP_S_ERROR) return;

    uint32_t seq = net_rd32(seg + 4), ack = net_rd32(seg + 8);
    uint8_t flags = seg[13];
    uint16_t win = net_rd16(seg + 14);
    const uint8_t *data = seg + off;
    size_t dlen = len - off;

    if (flags & TCP_RST) {
        if (c->state == TCP_S_SYN_SENT) {
            if ((flags & TCP_ACK) && ack == c->snd_nxt) fail(c, TCP_ERR_REFUSED);
        } else if (seq_le(c->rcv_nxt, seq) && seq_lt(seq, c->rcv_nxt + TCP_RX_BUF)) {
            fail(c, TCP_ERR_RESET);
        }
        return;
    }

    if (c->state == TCP_S_SYN_SENT) {
        if ((flags & (TCP_SYN | TCP_ACK)) != (TCP_SYN | TCP_ACK) || ack != c->snd_nxt) return;
        for (size_t o = 20; o + 1 < off;) {             /* options: find MSS */
            uint8_t kind = seg[o];
            if (kind == 0) break;
            if (kind == 1) { o++; continue; }
            uint8_t l = seg[o + 1];
            if (l < 2 || o + l > off) break;
            if (kind == 2 && l == 4) {
                uint16_t mss = net_rd16(seg + o + 2);
                c->mss = mss < 64 ? 64 : (mss > TCP_MSS_LOCAL ? TCP_MSS_LOCAL : mss);
            }
            o += l;
        }
        c->rcv_nxt = seq + 1;
        c->snd_una = ack;
        c->snd_wnd = win;
        c->state = TCP_S_ESTABLISHED;
        c->timer_on = false;
        c->retries = 0;
        c->rto_ms = TCP_RTO_INITIAL;
        send_ack(c);
        send_pending(c);
        return;
    }

    if (flags & TCP_ACK) process_ack(c, ack, win);

    bool need_ack = false;
    if (dlen || (flags & TCP_FIN)) {
        /* Trim bytes we already have (retransmissions overlapping rcv_nxt). */
        if (seq_lt(seq, c->rcv_nxt)) {
            uint32_t dup = c->rcv_nxt - seq;
            if (dup >= dlen) {
                if (dup > dlen || !(flags & TCP_FIN)) { send_ack(c); return; }
                dlen = 0;
            } else {
                data += dup;
                dlen -= dup;
            }
            seq = c->rcv_nxt;
        }
        need_ack = true;
        if (seq == c->rcv_nxt && !c->peer_fin) {
            size_t take = dlen < rx_free(c) ? dlen : rx_free(c);
            for (size_t i = 0; i < take; i++) c->rx[(c->rx_head + c->rx_len + i) % TCP_RX_BUF] = data[i];
            c->rx_len += take;
            c->rcv_nxt += (uint32_t)take;
            if ((flags & TCP_FIN) && take == dlen) {
                c->rcv_nxt += 1;
                c->peer_fin = true;
                if (c->state == TCP_S_ESTABLISHED) c->state = TCP_S_CLOSE_WAIT;
                else if (c->state == TCP_S_FIN_WAIT2) c->state = TCP_S_CLOSED;
                else if (c->state == TCP_S_FIN_WAIT1) c->state = c->fin_acked ? TCP_S_CLOSED : TCP_S_LAST_ACK;
            }
        }
        /* Out-of-order segments are dropped; the duplicate ACK asks again. */
    }
    if (need_ack) send_ack(c);
    send_pending(c);
}

void tcp_poll(uint64_t now_ms)
{
    tcp_now = now_ms;
    for (int i = 0; i < TCP_MAX_CONN; i++) {
        TcpConn *c = &conns[i];
        if (c->state == TCP_S_FREE || c->state == TCP_S_ERROR) continue;
        if (!net_ready()) { fail(c, TCP_ERR_NONET); continue; }
        if (c->window_update) { c->window_update = false; send_ack(c); }
        if (!c->timer_on || now_ms < c->rto_deadline) continue;

        c->retries++;
        unsigned limit = c->state == TCP_S_SYN_SENT ? TCP_SYN_RETRIES : TCP_DATA_RETRIES;
        if (c->retries > limit) { fail(c, TCP_ERR_TIMEOUT); continue; }
        c->rto_ms = c->rto_ms * 2 > TCP_RTO_MAX ? TCP_RTO_MAX : c->rto_ms * 2;
        if (c->state == TCP_S_SYN_SENT) {
            emit(c, c->iss, TCP_SYN, NULL, 0);
            arm_timer(c);
            continue;
        }
        /* Go-back-N from the oldest unacknowledged byte. */
        c->snd_nxt = c->snd_una;
        if (c->fin_sent && !c->fin_acked) {
            c->fin_sent = false;
            if (c->state == TCP_S_FIN_WAIT1) c->state = TCP_S_ESTABLISHED;
            else if (c->state == TCP_S_LAST_ACK) c->state = TCP_S_CLOSE_WAIT;
        }
        if (c->snd_wnd == 0) c->snd_wnd = 1;        /* zero-window probe */
        c->timer_on = false;
        send_pending(c);
        if (!c->timer_on && c->snd_una != c->snd_nxt) arm_timer(c);
    }
}

void tcp_reset_all(void)
{
    for (int i = 0; i < TCP_MAX_CONN; i++) {
        if (conns[i].state != TCP_S_FREE && conns[i].state != TCP_S_ERROR) {
            conns[i].state = TCP_S_ERROR;
            conns[i].err = TCP_ERR_NONET;
            conns[i].timer_on = false;
        }
    }
}
