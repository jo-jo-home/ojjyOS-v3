/*
 * ojjyOS v3 Kernel - virtio-rng driver (aarch64, virtio-mmio)
 *
 * Requests 64-byte blocks of host entropy and mixes them into the kernel
 * pool (random.c). A bounded number of refills runs at boot; later code may
 * call virtio_rng_request() for fresh material (e.g. before TLS handshakes).
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "virtio.h"
#include "virtio_devices.h"
#include "random.h"
#include "serial.h"

static VirtQueueMem rng_mem;
static VirtQueue rngq;
static uint8_t rng_buf[64] __attribute__((aligned(16)));
static bool attached, in_flight;
static unsigned refills_left;

void virtio_rng_request(void)
{
    if (!attached || in_flight) return;
    rngq.free_head = 0;
    uint16_t head = virtio_desc_add(&rngq, (uint64_t)(uintptr_t)rng_buf, sizeof(rng_buf), VRING_DESC_F_WRITE);
    if (head == 0xFFFF) return;
    in_flight = true;
    virtio_submit(&rngq, head);
}

int virtio_rng_attach(VirtioDev *dev)
{
    if (attached) return -1;
    if (virtio_dev_queue_init(dev, &rngq, &rng_mem, 0) != 0) return -1;
    virtio_dev_ready(dev);
    attached = true;
    refills_left = 16;          /* 1 KiB of host entropy at boot */
    virtio_rng_request();
    serial_printf("[VIRTIO-RNG] Entropy source attached\n");
    return 0;
}

void virtio_rng_poll(void)
{
    if (!attached || !in_flight) return;
    uint32_t len = 0;
    if (!virtio_poll_used(&rngq, &len)) return;
    in_flight = false;
    if (len > sizeof(rng_buf)) len = sizeof(rng_buf);
    if (len) {
        random_add_entropy(rng_buf, len);
        random_mark_hw_entropy();
    }
    if (refills_left) { refills_left--; virtio_rng_request(); }
}
