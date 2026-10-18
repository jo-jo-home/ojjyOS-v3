/*
 * ojjyOS v3 Kernel - PCI Configuration Space (aarch64)
 *
 * ECAM (MMCONFIG) access from the MCFG ACPI table. Apple's Virtualization
 * framework exposes every device on PCIe bus 0 with firmware-assigned BARs;
 * QEMU virt has no PCI (platform.ecam == 0) and never calls into this file.
 *
 * Every config access is a single LDR/STR through mmio.h: hypervisors
 * (HVF/VZ) cannot emulate compound accesses without a syndrome.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_ARCH_PCI_H
#define _OJJY_ARCH_PCI_H

#include "types.h"
#include "platform.h"

typedef struct PciDev {
    uint8_t bus, dev, fn;
    uint16_t vendor, device;
    uint8_t class_code, subclass, prog_if;
    uint16_t command;
    uint64_t bars[6];       /* Decoded base addresses (0 = unmapped) */
    uint8_t bar_64[6];      /* BAR i starts a 64-bit address */
} PciDev;

/* Enumerate bus 0, print what was found and enable memory + bus master. */
void pci_init(void);

/* Scan bus 0 for vendor:device. Works before pci_init(). Returns 0 and fills
 * *out (BARs decoded) when found, -1 otherwise. */
int pci_find(uint16_t vendor, uint16_t device, PciDev *out);

/* Enable Memory Space + Bus Master (the devices DMA our RAM). */
void pci_enable(const PciDev *d);

/* Raw config access (offset must be naturally aligned). */
uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off);
void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off, uint32_t value);

#endif /* _OJJY_ARCH_PCI_H */
