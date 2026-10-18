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

#include "types.h"
#include "boot_info.h"

/* Device attach hooks called by the transport during bus scan.
 * Return 0 on success. */
int virtio_blk_attach(uint32_t mmio_base, bool supports_flush);
int virtio_input_attach(uint32_t mmio_base);
void virtio_input_handle_key(uint16_t code, int32_t value);

/* Finalize device bring-up (set DRIVER_OK). */
void virtio_dev_ready(uint32_t base);
/* Reset + feature negotiation (up to FEATURES_OK) for early drivers. */
int virtio_dev_begin(uint32_t base, uint32_t features);

/* virtio-gpu display: probed before the rest of the bus, right after the
 * page allocator exists. Replaces info's framebuffer on success. */
bool virtio_gpu_display_init(BootInfo *info);
bool virtio_gpu_owns(uint32_t base);
void virtio_gpu_poll(void);

/* Poll all input devices for pending events (called each main-loop
 * iteration by a64_poll_input). */
void virtio_input_poll(void);

/* virtio-net (device 1): attach, then poll from the main loop. */
int virtio_net_attach(uint32_t base, bool has_mac);
void virtio_net_poll(void);

/* virtio-rng (device 4): fills the kernel entropy pool. */
int virtio_rng_attach(uint32_t base);
void virtio_rng_poll(void);
void virtio_rng_request(void);
#ifdef OJJY_HOST_TEST
void virtio_input_test_pointer_init(int32_t min_x, int32_t max_x, int32_t min_y, int32_t max_y);
void virtio_input_test_pointer_event(uint16_t type, uint16_t code, int32_t value);
#endif

#endif /* _OJJY_ARCH_VIRTIO_DEVICES_H */
