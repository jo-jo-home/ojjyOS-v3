/*
 * ojjyOS v3 Kernel - PCI Configuration Space (see pci.h)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "pci.h"
#include "mmio.h"
#include "serial.h"
#include "string.h"

#define PCI_CONFIG_OFF(bus, dev, fn, off) \
    ((uintptr_t)platform.ecam + ((uintptr_t)(bus) << 20) + \
     ((uintptr_t)(dev) << 15) + ((uintptr_t)(fn) << 12) + (uintptr_t)(off))

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off)
{
    return mmio_read32(PCI_CONFIG_OFF(bus, dev, fn, off));
}

void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off, uint32_t value)
{
    mmio_write32(PCI_CONFIG_OFF(bus, dev, fn, off), value);
}

static uint16_t config_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off)
{
    /* 16-bit fields (vendor/device/class) are aligned within the dword. */
    uint32_t v = pci_config_read32(bus, dev, fn, (uint16_t)(off & ~3u));
    return (uint16_t)(v >> ((off & 3) * 8));
}

static void config_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off, uint16_t value)
{
    uint32_t v = pci_config_read32(bus, dev, fn, (uint16_t)(off & ~3u));
    uint32_t shift = (off & 3) * 8;
    v = (v & ~(0xFFFFu << shift)) | ((uint32_t)value << shift);
    pci_config_write32(bus, dev, fn, (uint16_t)(off & ~3u), v);
}

/* Decode the six base-address registers. The firmware has already assigned
 * every BAR, so only read them - never reassign. */
static void pci_read_bars(PciDev *d)
{
    for (int i = 0; i < 6; i++) {
        uint32_t lo = pci_config_read32(d->bus, d->dev, d->fn, (uint16_t)(0x10 + i * 4));
        if (lo & 1) {                 /* I/O space BAR: not used by this kernel */
            d->bars[i] = lo & ~0x3u;
            continue;
        }
        if ((lo & 6) == 4) {           /* 64-bit memory BAR */
            uint32_t hi = pci_config_read32(d->bus, d->dev, d->fn, (uint16_t)(0x14 + i * 4));
            d->bars[i] = ((uint64_t)hi << 32) | (lo & ~0xFu);
            d->bar_64[i] = 1;
            i++;                       /* the next BAR is the high half */
        } else {
            d->bars[i] = lo & ~0xFu;
        }
    }
}

int pci_find(uint16_t vendor, uint16_t device, PciDev *out)
{
    if (!out || !platform.ecam) return -1;
    for (uint8_t dev = 0; dev < 32; dev++) {
        if (config_read16(0, dev, 0, 0) != vendor) continue;
        if (config_read16(0, dev, 0, 2) != device) continue;
        memset(out, 0, sizeof(*out));
        out->bus = 0;
        out->dev = dev;
        out->fn = 0;
        out->vendor = vendor;
        out->device = device;
        uint32_t rc = pci_config_read32(0, dev, 0, 8);
        out->class_code = (uint8_t)(rc >> 24);
        out->subclass = (uint8_t)(rc >> 16);
        out->prog_if = (uint8_t)(rc >> 8);
        out->command = config_read16(0, dev, 0, 4);
        pci_read_bars(out);
        return 0;
    }
    return -1;
}

void pci_init(void)
{
    if (!platform.ecam) return;

    int found = 0;
    for (uint8_t dev = 0; dev < 32; dev++) {
        for (uint8_t fn = 0; fn < 8; fn++) {
            uint16_t vendor = config_read16(0, dev, fn, 0);
            if (vendor == 0xFFFF) {
                if (fn == 0) break;    /* no device, no functions either */
                continue;
            }
            uint16_t device = config_read16(0, dev, fn, 2);
            uint32_t rc = pci_config_read32(0, dev, fn, 8);
            uint16_t command = config_read16(0, dev, fn, 4);

            /* Enable memory space + bus master (the devices DMA our RAM). */
            config_write16(0, dev, fn, 4, (uint16_t)(command | 0x6));

            serial_printf("[PCI] %d:%d.%d %x:%x class %x%x%x cmd %x\n",
                0, (int)dev, (int)fn,
                (unsigned int)vendor, (unsigned int)device,
                (unsigned int)(rc >> 24) & 0xFF,
                (unsigned int)(rc >> 16) & 0xFF,
                (unsigned int)(rc >> 8) & 0xFF,
                (unsigned int)command);
            found++;

            PciDev d;
            memset(&d, 0, sizeof(d));
            d.bus = 0; d.dev = dev; d.fn = fn;
            pci_read_bars(&d);
            for (int i = 0; i < 6; i++) {
                if (d.bars[i]) {
                    serial_printf("[PCI]   bar%d 0x%llx%s\n", i,
                        (unsigned long long)d.bars[i],
                        d.bar_64[i] ? " (64-bit)" : "");
                }
            }

            if (fn == 0 && !(rc & 0x00800000)) break; /* single-function */
        }
    }
    serial_printf("[PCI] %d function(s) on bus 0\n", found);
}
