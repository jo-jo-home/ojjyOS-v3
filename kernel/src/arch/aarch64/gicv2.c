/*
 * ojjyOS v3 Kernel - GICv2 Interrupt Controller (aarch64)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "gicv2.h"
#include "serial.h"

static inline volatile uint32_t *gicd_reg(uint32_t off)
{
    return (volatile uint32_t *)(GICD_BASE + off);
}

static inline volatile uint32_t *gicc_reg(uint32_t off)
{
    return (volatile uint32_t *)(GICC_BASE + off);
}

void gicv2_enable_intid(uint32_t intid)
{
    if (intid < 32) {
        /* SGI/PPI bank 0 (this CPU) */
        gicd_reg(GICD_ISENABLER)[0] = (1u << intid);
    } else {
        gicd_reg(GICD_ISENABLER)[intid / 32] = (1u << (intid % 32));
    }
}

void gicv2_disable_intid(uint32_t intid)
{
    if (intid < 32) {
        gicd_reg(GICD_ICENABLER)[0] = (1u << intid);
    } else {
        gicd_reg(GICD_ICENABLER)[intid / 32] = (1u << (intid % 32));
    }
}

void gicv2_init(void)
{
    /* Disable the distributor while configuring */
    *gicd_reg(GICD_CTLR) = 0;

    /* Number of interrupt lines: (TYPER.ITLinesNumber + 1) * 32 */
    uint32_t typer = *gicd_reg(GICD_TYPER);
    uint32_t lines = ((typer & 0x1F) + 1) * 32;

    /* Mask (disable) all SPIs; leave SGI/PPI bank alone for now */
    for (uint32_t i = 1; i < lines / 32; i++) {
        gicd_reg(GICD_ICENABLER)[i] = 0xFFFFFFFFu;
    }

    /* Set all SPI priorities to the lowest (0xFF) */
    for (uint32_t i = 8; i < lines; i++) {
        /* IPRIORITYR is byte-per-intid */
        volatile uint8_t *prio =
            (volatile uint8_t *)(GICD_BASE + GICD_IPRIORITYR + i);
        *prio = 0xFF;
    }

    /* CPU interface: unmask all priorities, enable signaling */
    *gicc_reg(GICC_PMR) = 0xFF;
    *gicc_reg(GICC_CTLR) = 0x1;      /* Enable group 0 */

    /* Enable the distributor */
    *gicd_reg(GICD_CTLR) = 0x1;

    /* Keep DAIF masked until the kernel is ready (main.c calls sti) */
    serial_printf("[GIC] GICv2 ready (%d lines)\n", (int)lines);
}

uint32_t gicv2_ack(void)
{
    return *gicc_reg(GICC_IAR);
}

void gicv2_eoi(uint32_t intid)
{
    *gicc_reg(GICC_EOIR) = intid;
}
