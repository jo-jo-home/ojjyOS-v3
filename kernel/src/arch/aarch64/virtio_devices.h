/*
 * ojjyOS v3 Kernel - virtio Device Drivers (aarch64)
 *
 * Declarations shared between the transports (virtio.c) and the device
 * drivers. Drivers are transport-independent: they receive a VirtioDev
 * handle and use the virtio.h API.
 *
 * ojjyOS - Created by Jonas Lee
 */

#ifndef _OJJY_ARCH_VIRTIO_DEVICES_H
#define _OJJY_ARCH_VIRTIO_DEVICES_H

#include "types.h"
#include "boot_info.h"
#include "virtio.h"

/* Device attach hooks called by the bus scans. Return 0 on success. */
int virtio_blk_attach(VirtioDev *dev, bool supports_flush);
int virtio_input_attach(VirtioDev *dev);
void virtio_input_handle_key(uint16_t code, int32_t value);

/* virtio-gpu display: probed before the rest of the bus, right after the
 * page allocator exists (mmio slots and PCI). Replaces info's framebuffer
 * on success. */
bool virtio_gpu_display_init(BootInfo *info);
bool virtio_gpu_owns(const VirtioDev *dev);
void virtio_gpu_poll(void);

/* Poll all input devices for pending events (called each main-loop
 * iteration by a64_poll_input). */
void virtio_input_poll(void);

/* virtio-net (device 1): attach, then poll from the main loop. */
int virtio_net_attach(VirtioDev *dev, bool has_mac);
void virtio_net_poll(void);

/* virtio-rng (device 4): fills the kernel entropy pool. */
int virtio_rng_attach(VirtioDev *dev);
void virtio_rng_poll(void);
void virtio_rng_request(void);

/* virtio-console (device 3): serial output over the PCI console device
 * (Apple VZ has no UART). Registering it replays the boot log ring. */
int virtio_console_attach(VirtioDev *dev);
void virtio_console_poll(void);

#ifdef OJJY_HOST_TEST
void virtio_input_test_pointer_init(int32_t min_x, int32_t max_x, int32_t min_y, int32_t max_y);
void virtio_input_test_pointer_event(uint16_t type, uint16_t code, int32_t value);
#endif

#endif /* _OJJY_ARCH_VIRTIO_DEVICES_H */
