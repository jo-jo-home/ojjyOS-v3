/*
 * ojjyOS v3 Kernel - aarch64 Architecture Support
 *
 * Implements the platform layer for 64-bit ARM ("Apple Silicon"
 * class machines, run under QEMU -M virt on M-series Macs):
 *
 *   - Entry from UEFI at EL2/EL1, exception vectors
 *   - GICv2 interrupt controller (QEMU -M virt,gic-version=2)
 *   - ARM generic timer (1 kHz tick)
 *   - PL011 UART serial
 *   - PL031 real-time clock
 *   - virtio-mmio transport + virtio-input (keyboard/tablet)
 *     and virtio-blk (disk)
 *   - PSCI power management (system off / reset)
 *
 * The kernel core, filesystem, desktop and apps are architecture
 * independent and shared with the x86_64 build.
 *
 * ojjyOS - Created by Jonas Lee
 */

#ifndef _OJJY_ARCH_AARCH64_H
#define _OJJY_ARCH_AARCH64_H

#include "../types.h"
#include "../boot_info.h"

/*
 * Initialize the platform: GIC interrupt controller, virtio devices.
 * Called after serial/framebuffer are up.
 */
void a64_init(BootInfo *boot_info);

/*
 * IRQ entry (called from the exception vector table).
 */
void a64_irq_handler(void);

/*
 * Exception (sync error) entry: prints diagnostics and halts.
 */
void a64_exception_handler(uint64_t esr, uint64_t elr, uint64_t spsr);

/*
 * Harvest pending input events from virtio devices.
 * Called from the kernel main loop (input is polled; the 1 kHz
 * timer interrupt wakes the loop).
 */
void a64_poll_input(void);

#endif /* _OJJY_ARCH_AARCH64_H */
