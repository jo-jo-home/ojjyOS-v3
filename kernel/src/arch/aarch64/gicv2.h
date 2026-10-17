/*
 * ojjyOS v3 Kernel - GICv2 Interrupt Controller (aarch64)
 *
 * Minimal ARM Generic Interrupt Controller v2 driver for the
 * QEMU virt machine (gic-version=2). Only what this kernel needs:
 * one enabled interrupt source (the generic timer, PPI 30).
 *
 * ojjyOS - Created by Jonas Lee
 */

#ifndef _OJJY_ARCH_GICV2_H
#define _OJJY_ARCH_GICV2_H

#include "../types.h"

/* QEMU virt machine GICv2 bases */
#define GICD_BASE   0x08000000UL     /* Distributor */
#define GICC_BASE   0x08010000UL     /* CPU interface */

/* Distributor register offsets */
#define GICD_CTLR       0x000
#define GICD_TYPER      0x004
#define GICD_IIDR       0x008
#define GICD_ISENABLER  0x100
#define GICD_ICENABLER  0x180
#define GICD_IPRIORITYR 0x400
#define GICD_ICFGR      0xC00

/* CPU interface register offsets */
#define GICC_CTLR   0x000
#define GICC_PMR    0x004
#define GICC_IAR    0x00C
#define GICC_EOIR   0x010

/* Spurious interrupt ID */
#define GIC_SPURIOUS    1023

void gicv2_init(void);
uint32_t gicv2_ack(void);
void gicv2_eoi(uint32_t intid);
void gicv2_enable_intid(uint32_t intid);
void gicv2_disable_intid(uint32_t intid);

#endif /* _OJJY_ARCH_GICV2_H */
