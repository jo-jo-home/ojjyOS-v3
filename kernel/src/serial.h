/*
 * ojjyOS v3 Kernel - Serial Port Driver
 *
 * 16550 UART driver for debug output.
 * Uses COM1 (0x3F8) by default.
 */

#ifndef _OJJY_SERIAL_H
#define _OJJY_SERIAL_H

#include "types.h"

/* COM port addresses */
#define COM1_PORT   0x3F8
#define COM2_PORT   0x2F8
#define COM3_PORT   0x3E8
#define COM4_PORT   0x2E8

/* Initialize serial port */
void serial_init(uint16_t port);

/* Write a character */
void serial_putc(char c);

/* Write a string */
void serial_puts(const char *s);

/* Write a hex number */
void serial_puthex(uint64_t value);

/* Formatted print (simple subset) */
void serial_printf(const char *fmt, ...);

#endif /* _OJJY_SERIAL_H */
