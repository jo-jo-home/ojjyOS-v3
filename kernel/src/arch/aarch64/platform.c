/*
 * ojjyOS v3 Kernel - ARM64 machine description (see platform.h)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "platform.h"
#include "string.h"

Platform platform;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static void qemu_defaults(void)
{
    platform.qemu = true;
    platform.name = "QEMU virt";
    platform.gicd = 0x08000000UL;
    platform.gicc = 0x08010000UL;
    platform.gicr = 0x080A0000UL;
    platform.gic_version = 0;           /* GICv2 under TCG, v3 under HVF */
    platform.uart = 0x09000000UL;
    platform.rtc = 0x09010000UL;
    platform.vtimer_ppi = 27;
}

static void parse_madt(const uint8_t *t, uint32_t len)
{
    for (uint32_t q = 44; q + 2 <= len && t[q + 1]; q += t[q + 1]) {
        const uint8_t *e = t + q;
        switch (e[0]) {
        case 0x0B:                                  /* GICC (per CPU) */
            if (!platform.gicc && rd64(e + 32)) platform.gicc = rd64(e + 32);
            if (!platform.gicr && e[1] >= 68 && rd64(e + 60)) platform.gicr = rd64(e + 60);
            break;
        case 0x0C:                                  /* GICD */
            platform.gicd = rd64(e + 8);
            if (e[20]) platform.gic_version = e[20];
            break;
        case 0x0E:                                  /* GICR range */
            platform.gicr = rd64(e + 4);
            break;
        default:
            break;
        }
    }
}

void platform_init(const BootInfo *info)
{
    memset(&platform, 0, sizeof(platform));
    platform.vtimer_ppi = 27;
    const uint8_t *rsdp = info ? (const uint8_t *)(uintptr_t)info->rsdp_addr : NULL;
    if (!rsdp || memcmp(rsdp, "RSD PTR ", 8) != 0 || rsdp[15] < 2) { qemu_defaults(); return; }

    /* QEMU's tables carry OEM id "BOCHS "; anything else is described
     * only by its tables (Apple VZ). */
    if (memcmp(rsdp + 9, "BOCHS", 5) == 0) qemu_defaults();
    else platform.name = "Apple Virtualization";
    platform.acpi = true;

    const uint8_t *xsdt = (const uint8_t *)(uintptr_t)rd64(rsdp + 24);
    uint32_t xlen = rd32(xsdt + 4);
    for (uint32_t o = 36; o + 8 <= xlen; o += 8) {
        const uint8_t *t = (const uint8_t *)(uintptr_t)rd64(xsdt + o);
        uint32_t len = rd32(t + 4);
        if (memcmp(t, "APIC", 4) == 0) {
            parse_madt(t, len);
        } else if (memcmp(t, "MCFG", 4) == 0 && len >= 60) {
            platform.ecam = rd64(t + 44);
            platform.bus_start = t[54];
            platform.bus_end = t[55];
        } else if (memcmp(t, "GTDT", 4) == 0 && len >= 68) {
            if (rd32(t + 64)) platform.vtimer_ppi = rd32(t + 64);
        } else if (memcmp(t, "SPCR", 4) == 0 && len >= 52 && t[36] == 3) {
            platform.uart = rd64(t + 44);           /* ARM PL011 */
        }
    }
    /* Owned tables currently cover 0-8 GiB. QEMU highmem=off places
     * ECAM/BARs below 4 GiB and lets our PCI path be tested with a UART;
     * the default high ECAM window is not mapped yet. */
    if (platform.ecam >= (8ULL << 30)) platform.ecam = 0;
}
