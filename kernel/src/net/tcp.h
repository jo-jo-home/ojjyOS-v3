/*
 * ojjyOS v3 - TCP client (RFC 793/9293 subset)
 *
 * Active open only (the OS makes outgoing connections, it runs no servers).
 * In-order receive into a per-connection ring (advertised window = free
 * space), go-back-N retransmission with exponential backoff, graceful FIN
 * and RST handling. No SACK, window scaling, or TIME_WAIT (client ports are
 * never reused immediately). Handles are small integers; always release.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_TCP_H
#define _OJJY_TCP_H

#include "net/net.h"

#define TCP_MAX_CONN   4
#define TCP_RX_BUF     (64 * 1024)
#define TCP_TX_BUF     (16 * 1024)

typedef enum {
    TCP_S_FREE = 0,
    TCP_S_SYN_SENT,
    TCP_S_ESTABLISHED,
    TCP_S_FIN_WAIT1,
    TCP_S_FIN_WAIT2,
    TCP_S_CLOSE_WAIT,
    TCP_S_LAST_ACK,
    TCP_S_CLOSED,       /* finished cleanly; data may remain to be read */
    TCP_S_ERROR
} TcpState;

typedef enum {
    TCP_ERR_NONE = 0,
    TCP_ERR_REFUSED,    /* RST in reply to our SYN */
    TCP_ERR_TIMEOUT,    /* retransmissions exhausted */
    TCP_ERR_RESET,      /* peer reset an open connection */
    TCP_ERR_NONET       /* no configured interface / network turned off */
} TcpError;

int tcp_connect(ip4_t ip, uint16_t port);           /* handle, or -1 */
int tcp_send(int h, const void *data, size_t len);  /* bytes queued, -1 on error */
int tcp_recv(int h, void *buf, size_t len);         /* bytes read (0 = none yet) */
size_t tcp_available(int h);
bool tcp_peer_closed(int h);        /* peer sent FIN and all data was read */
TcpState tcp_state(int h);
TcpError tcp_error(int h);
void tcp_close(int h);              /* graceful: FIN after queued data */
void tcp_release(int h);            /* free the handle (RST if still open) */
const char *tcp_error_string(TcpError err);

/* Stack internals (called by net.c) */
void tcp_input(ip4_t src, ip4_t dst, const uint8_t *seg, size_t len);
void tcp_poll(uint64_t now_ms);
void tcp_reset_all(void);

#endif /* _OJJY_TCP_H */
