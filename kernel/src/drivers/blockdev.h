/*
 * ojjyOS v3 Kernel - Block Device Abstraction
 *
 * A tiny sector-based block layer so the rest of the kernel (persistence,
 * future filesystems) works on any architecture:
 *   - x86_64:  ATA/IDE PIO driver (VirtualBox, QEMU -M pc)
 *   - aarch64: virtio-blk over virtio-mmio (QEMU -M virt, Apple Silicon VMs)
 *
 * ojjyOS - Created by Jonas Lee
 */

#ifndef _OJJY_BLOCKDEV_H
#define _OJJY_BLOCKDEV_H

#include "../types.h"

/*
 * Read `count` sectors (512 bytes each) starting at `lba` into `buf`.
 * Returns 0 on success, -1 on failure (no disk / I/O error).
 * `buf` must be at least count*512 bytes.
 */
int block_read_sectors(uint64_t lba, void *buf, uint32_t count);

/*
 * Write `count` sectors starting at `lba` from `buf`.
 * Returns 0 on success, -1 on failure.
 */
int block_write_sectors(uint64_t lba, const void *buf, uint32_t count);

/*
 * True when a usable disk is attached.
 */
bool block_available(void);

#endif /* _OJJY_BLOCKDEV_H */
