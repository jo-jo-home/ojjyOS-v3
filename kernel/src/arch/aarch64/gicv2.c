/*
 * ojjyOS v3 Kernel - GICv2 / GICv3 Interrupt Controller (aarch64)
 *
 * Auto-detects GICv3 (used by QEMU virt with Apple Silicon HVF) vs
 * GICv2 (used by QEMU virt with TCG/gic-version=2).
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "gicv2.h"
#include "serial.h"

/* QEMU virt GICv3 Redistributor base (CPU 0) */
#define GICR_BASE       0x080A0000UL
#define GICR_SGI_BASE   (GICR_BASE + 0x10000UL)
#define GICR_WAKER      0x0014
#define GICR_IGROUPR0   0x0080
#define GICR_ISENABLER0 0x0100
#define GICR_ICENABLER0 0x0180
#define GICR_IPRIORITYR 0x0400
#define GICR_IGRPMODR0  0x0D00

static int gic_version = 2;
static uint32_t saved_ppi_mask = (1u << 30); /* Keep timer PPI 30 enabled */

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
        saved_ppi_mask |= (1u << intid);
        if (gic_version >= 3) {
            *(volatile uint32_t *)(GICR_SGI_BASE + GICR_ISENABLER0) = (1u << intid);
        } else {
            gicd_reg(GICD_ISENABLER)[0] = (1u << intid);
        }
    } else {
        gicd_reg(GICD_ISENABLER)[intid / 32] = (1u << (intid % 32));
    }
}

void gicv2_disable_intid(uint32_t intid)
{
    if (intid < 32) {
        saved_ppi_mask &= ~(1u << intid);
        if (gic_version >= 3) {
            *(volatile uint32_t *)(GICR_SGI_BASE + GICR_ICENABLER0) = (1u << intid);
        } else {
            gicd_reg(GICD_ICENABLER)[0] = (1u << intid);
        }
    } else {
        gicd_reg(GICD_ICENABLER)[intid / 32] = (1u << (intid % 32));
    }
}

void gicv2_init(void)
{
    /* Detect GICv3 system register support from ID_AA64PFR0_EL1 bits [27:24] */
    uint64_t pfr0 = 0;
    __asm__ volatile("mrs %0, id_aa64pfr0_el1" : "=r"(pfr0));
    if (((pfr0 >> 24) & 0xF) != 0) {
        gic_version = 3;
    } else {
        gic_version = 2;
    }

    /* Disable the distributor while configuring */
    *gicd_reg(GICD_CTLR) = 0;

    /* Number of interrupt lines: (TYPER.ITLinesNumber + 1) * 32 */
    uint32_t typer = *gicd_reg(GICD_TYPER);
    uint32_t lines = ((typer & 0x1F) + 1) * 32;

    /* Mask (disable) all SPIs; leave SGI/PPI bank alone for now */
    for (uint32_t i = 1; i < lines / 32; i++) {
        gicd_reg(GICD_ICENABLER)[i] = 0xFFFFFFFFu;
    }

    /* Set all SPI priorities to 0x80 */
    for (uint32_t i = 32; i < lines; i++) {
        volatile uint8_t *prio =
            (volatile uint8_t *)(GICD_BASE + GICD_IPRIORITYR + i);
        *prio = 0x80;
    }

    if (gic_version >= 3) {
        /* Enable Distributor with Affinity Routing + Group 1 NS */
        *gicd_reg(GICD_CTLR) = 0x37;

        /* Wake Redistributor for CPU 0 */
        volatile uint32_t *waker = (volatile uint32_t *)(GICR_BASE + GICR_WAKER);
        *waker &= ~(1u << 1);
        for (int spin = 0; spin < 100000 && (*waker & (1u << 2)); spin++) {
            /* Wait for ChildrenAsleep to clear */
        }

        /* Place SGI/PPIs in Non-Secure Group 1 and set priorities */
        *(volatile uint32_t *)(GICR_SGI_BASE + GICR_IGROUPR0) = 0xFFFFFFFFu;
        *(volatile uint32_t *)(GICR_SGI_BASE + GICR_IGRPMODR0) = 0x00000000u;
        for (uint32_t i = 0; i < 32; i++) {
            *(volatile uint8_t *)(GICR_SGI_BASE + GICR_IPRIORITYR + i) = 0x80;
        }
        *(volatile uint32_t *)(GICR_SGI_BASE + GICR_ISENABLER0) = saved_ppi_mask;

        /* Enable GICv3 system register interface (ICC_SRE_EL1, ICC_PMR_EL1, ICC_IGRPEN1_EL1) */
        uint64_t sre = 0;
        __asm__ volatile("mrs %0, S3_0_C12_C12_5" : "=r"(sre));
        sre |= 1;
        __asm__ volatile("msr S3_0_C12_C12_5, %0\nisb" : : "r"(sre));
        uint64_t pmr = 0xFF;
        __asm__ volatile("msr S3_0_C4_C6_0, %0" : : "r"(pmr));
        uint64_t igrpen1 = 1;
        __asm__ volatile("msr S3_0_C12_C12_7, %0\nisb" : : "r"(igrpen1));

        serial_printf("[GIC] GICv3 ready (%d lines, HVF/Redistributor active)\n", (int)lines);
    } else {
        for (uint32_t i = 0; i < 32; i++) {
            volatile uint8_t *prio =
                (volatile uint8_t *)(GICD_BASE + GICD_IPRIORITYR + i);
            *prio = 0x80;
        }
        gicd_reg(GICD_ISENABLER)[0] = saved_ppi_mask;

        /* CPU interface: unmask all priorities, enable signaling */
        *gicc_reg(GICC_PMR) = 0xFF;
        *gicc_reg(GICC_CTLR) = 0x1;

        /* Enable the distributor */
        *gicd_reg(GICD_CTLR) = 0x1;

        serial_printf("[GIC] GICv2 ready (%d lines)\n", (int)lines);
    }
}

uint32_t gicv2_ack(void)
{
    if (gic_version >= 3) {
        uint64_t iar = 1023;
        __asm__ volatile("mrs %0, S3_0_C12_C12_0" : "=r"(iar));
        return (uint32_t)iar;
    }
    return *gicc_reg(GICC_IAR);
}

void gicv2_eoi(uint32_t intid)
{
    if (gic_version >= 3) {
        uint64_t v = intid;
        __asm__ volatile("msr S3_0_C12_C12_1, %0\nisb" : : "r"(v));
        return;
    }
    *gicc_reg(GICC_EOIR) = intid;
}
