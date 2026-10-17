/*
 * ojjyOS v3 Kernel - virtio Device Drivers (aarch64)
 *
 * Declarations shared between the transport (virtio.c) and the
 * device drivers (virtio_input.c, virtio_blk.c).
 *
 * ojjyOS - Created by Jonas Lee
 */

#ifndef _OJJY_ARCH_VIRTIO_DEVICES_H
#define _OJJY_ARCH_VIRTIO_DEVICES_H

#include "../types.h"

/* Device attach hooks called by the transport during bus scan.
 * Return 0 on success. */
int virtio_blk_attach(uint32_t mmio_base);
int virtio_input_attach(uint32_t mmio_base);

/* Finalize device bring-up (set DRIVER_OK). */
void virtio_dev_ready(uint32_t base);

/* Poll all input devices for pending events (called each main-loop
 * iteration by a64_poll_input). */
void virtio_input_poll(void);

#endif /* _OJJY_ARCH_VIRTIO_DEVICES_H */
