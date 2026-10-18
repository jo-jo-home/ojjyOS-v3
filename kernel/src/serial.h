/*
 * ojjyOS v3 Kernel - Serial Output
 *
 * Arch-neutral API. The primitive (init + putc) is implemented per
 * architecture:
 *   x86_64:   16550 UART on COM1
 *   aarch64:  PL011 UART (QEMU virt machine)
 *
 * Formatting helpers are shared (serial_core.c).
 */

#ifndef _OJJY_SERIAL_H
#define _OJJY_SERIAL_H

#include "types.h"

/* Port selector (ignored on architectures with a fixed UART base) */
#define COM1_PORT   0x3F8
#define COM2_PORT   0x2F8
#define COM3_PORT   0x3E8
#define COM4_PORT   0x2E8

/* Initialize serial output */
void serial_init(uint16_t port);

/* Write a character (arch primitive) */
void serial_putc(char c);

/* aarch64 only: replays the boot-log ring into `sink` and forwards every
 * later byte (used by virtio-console on platforms without a UART). */
void serial_set_sink(void (*sink)(char c));

/* Write a string (shared) */
void serial_puts(const char *s);

/* Write a hex number (shared) */
void serial_puthex(uint64_t value);

/* Formatted print (shared; simple subset) */
void serial_printf(const char *fmt, ...);

#endif /* _OJJY_SERIAL_H */
