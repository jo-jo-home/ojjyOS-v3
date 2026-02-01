/*
 * ojjyOS v3 Kernel - Global Descriptor Table
 *
 * Sets up GDT for long mode with kernel and user segments.
 */

#ifndef _OJJY_GDT_H
#define _OJJY_GDT_H

#include "types.h"

/* Segment selectors */
#define GDT_KERNEL_CODE     0x08
#define GDT_KERNEL_DATA     0x10
#define GDT_USER_DATA       0x18
#define GDT_USER_CODE       0x20
#define GDT_TSS             0x28

/* Initialize GDT */
void gdt_init(void);

/* Set TSS RSP0 (kernel stack for interrupts from usermode) */
void gdt_set_kernel_stack(uint64_t stack);

#endif /* _OJJY_GDT_H */
