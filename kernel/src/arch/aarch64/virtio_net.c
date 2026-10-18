/*
 * ojjyOS v3 Kernel - virtio-net driver (aarch64, virtio-mmio)
 *
 * Queue 0 = receive (a pool of device-writable buffers kept posted),
 * queue 1 = transmit (one descriptor per frame, reclaimed from the used
 * ring). With VIRTIO_F_VERSION_1 every buffer starts with a 12-byte
 * virtio_net_hdr; no offloads are negotiated, so it is all zero on TX and
 * ignored on RX. Frames are handed to the arch-neutral stack (net/net.c).
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "virtio.h"
#include "mmio.h"
#include "virtio_devices.h"
#include "net/net.h"
#include "random.h"
#include "serial.h"
#include "string.h"

#define NET_HDR_LEN   12
#define NET_BUFS      32
#define NET_BUF_SIZE  1536

static VirtQueueMem rx_mem, tx_mem;
static VirtQueue rxq, txq;
static uint8_t rx_bufs[NET_BUFS][NET_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t tx_bufs[NET_BUFS][NET_BUF_SIZE] __attribute__((aligned(16)));
static bool tx_busy[NET_BUFS];
static unsigned tx_next;
static bool attached;

static void post_rx(uint16_t id)
{
    uint16_t slot = rxq.avail_idx & (rxq.size - 1);
    rxq.mem->avail.ring[slot] = id;
    virtio_memory_barrier();
    rxq.avail_idx = (uint16_t)(rxq.avail_idx + 1);
    rxq.mem->avail.idx = rxq.avail_idx;
}

static void reclaim_tx(void)
{
    while (txq.used_idx != *(volatile uint16_t *)&txq.mem->used.idx) {
        virtio_memory_barrier();
        uint32_t id = txq.mem->used.elems[txq.used_idx & (txq.size - 1)].id;
        if (id < NET_BUFS) tx_busy[id] = false;
        txq.used_idx = (uint16_t)(txq.used_idx + 1);
    }
}

static int virtio_net_tx(const uint8_t *frame, size_t len)
{
    if (!attached || len > NET_BUF_SIZE - NET_HDR_LEN) return -1;
    reclaim_tx();
    unsigned limit = NET_BUFS < txq.size ? NET_BUFS : txq.size;
    int id = -1;
    for (unsigned i = 0; i < limit; i++) {
        unsigned c = (tx_next + i) % limit;
        if (!tx_busy[c]) { id = (int)c; break; }
    }
    if (id < 0) return -1;                  /* ring full: caller counts a drop */
    tx_next = (unsigned)id + 1;

    uint8_t *buf = tx_bufs[id];
    memset(buf, 0, NET_HDR_LEN);
    memcpy(buf + NET_HDR_LEN, frame, len);
    VringDesc *d = &txq.mem->desc[id];
    d->addr = (uint64_t)(uintptr_t)buf;
    d->len = (uint32_t)(NET_HDR_LEN + len);
    d->flags = 0;
    d->next = 0;
    tx_busy[id] = true;

    uint16_t slot = txq.avail_idx & (txq.size - 1);
    txq.mem->avail.ring[slot] = (uint16_t)id;
    virtio_memory_barrier();
    txq.avail_idx = (uint16_t)(txq.avail_idx + 1);
    txq.mem->avail.idx = txq.avail_idx;
    virtio_notify(&txq);
    return 0;
}

int virtio_net_attach(VirtioDev *dev, bool has_mac)
{
    if (attached) return -1;
    if (virtio_dev_queue_init(dev, &rxq, &rx_mem, 0) != 0 ||
        virtio_dev_queue_init(dev, &txq, &tx_mem, 1) != 0) {
        serial_printf("[VIRTIO-NET] queue setup failed\n");
        return -1;
    }

    uint8_t mac[6];
    if (has_mac) {
        for (int i = 0; i < 6; i++) mac[i] = virtio_dev_cfg8(dev, (uint32_t)i);
    } else {
        random_bytes(mac, 6);
        mac[0] = (uint8_t)((mac[0] & 0xFE) | 0x02);     /* local, unicast */
    }

    virtio_dev_ready(dev);

    unsigned count = NET_BUFS < rxq.size ? NET_BUFS : rxq.size;
    for (unsigned i = 0; i < count; i++) {
        VringDesc *d = &rxq.mem->desc[i];
        d->addr = (uint64_t)(uintptr_t)rx_bufs[i];
        d->len = NET_BUF_SIZE;
        d->flags = VRING_DESC_F_WRITE;
        d->next = 0;
        post_rx((uint16_t)i);
    }
    virtio_notify(&rxq);

    attached = true;
    net_attach_device("virtio-net", mac, virtio_net_tx);
    return 0;
}

void virtio_net_poll(void)
{
    if (!attached) return;
    bool recycled = false;
    for (unsigned budget = 0; budget < rxq.size &&
         rxq.used_idx != *(volatile uint16_t *)&rxq.mem->used.idx; budget++) {
        virtio_memory_barrier();
        VringUsedElem *e = &rxq.mem->used.elems[rxq.used_idx & (rxq.size - 1)];
        uint32_t id = e->id, len = e->len;
        rxq.used_idx = (uint16_t)(rxq.used_idx + 1);
        if (id >= NET_BUFS) continue;
        if (len > NET_HDR_LEN && len <= NET_BUF_SIZE) net_input(rx_bufs[id] + NET_HDR_LEN, len - NET_HDR_LEN);
        post_rx((uint16_t)id);
        recycled = true;
    }
    if (recycled) virtio_notify(&rxq);
    reclaim_tx();
}
