/*
 * ojjyOS v3 Kernel - ARM64 machine description (QEMU virt or Apple VZ)
 *
 * Filled from ACPI before anything else runs (no output yet). QEMU's
 * fixed virt layout is used when there is no ACPI or the tables come from
 * QEMU itself; Apple's Virtualization framework is described entirely by
 * MADT (GIC), MCFG (PCIe ECAM) and GTDT (timer interrupts).
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_ARCH_PLATFORM_H
#define _OJJY_ARCH_PLATFORM_H

#include "types.h"
#include "boot_info.h"

typedef struct {
    bool acpi;              /* tables were found and parsed */
    bool qemu;              /* QEMU virt: virtio-mmio slots, PL011, PL031 exist */
    const char *name;
    uint64_t gicd, gicc, gicr;
    int gic_version;        /* 2 or 3 (0 = unknown, decide at runtime) */
    uint64_t ecam;          /* PCIe configuration space (0 = no PCI) */
    uint8_t bus_start, bus_end;
    uint32_t vtimer_ppi;    /* EL1 virtual timer interrupt */
    uint64_t uart;          /* PL011 base, 0 = none (use virtio-console) */
    uint64_t rtc;           /* PL031 base, 0 = none (use boot-time clock) */
} Platform;

extern Platform platform;

void platform_init(const BootInfo *info);

/* Tables cover <= 8 GiB, so use at most 40 PA bits, never truncate a
 * supported 36-bit CPU to 32 bits (Apple VZ reports PARange=1). */
static inline uint64_t platform_table_ips(uint64_t mmfr0)
{
    uint64_t range = mmfr0 & 15;
    return range > 2 ? 2 : range;
}

#endif /* _OJJY_ARCH_PLATFORM_H */
