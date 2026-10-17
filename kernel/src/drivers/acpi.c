/*
 * ojjyOS v3 Kernel - ACPI Power Management (x86_64)
 *
 * Minimal ACPI support, just enough for a clean power-off:
 * RSDP (found by the bootloader) -> RSDT/XSDT -> FADT -> PM1a control
 * block, plus a scan of the DSDT for the S5 sleep object.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "acpi.h"
#include "arch_power.h"
#include "serial.h"
#include "string.h"
#include "boot_info.h"
#include "idt.h"
#include "types.h"

extern BootInfo *g_boot_info;

/* RSDP signature: "RSD PTR " (note the trailing space) */
#define RSDP_SIG    "RSD PTR "

/*
 * Read a little-endian value from a possibly-unaligned address
 */
static uint32_t read32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

/*
 * 8-bit checksum validation over a table region
 */
static bool checksum_ok(const void *table, uint32_t length)
{
    const uint8_t *b = (const uint8_t *)table;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) {
        sum = (uint8_t)(sum + b[i]);
    }
    return sum == 0;
}

/*
 * Find the FADT (signature "FACP") via the RSDT or XSDT.
 */
static const void *find_fadt(void)
{
    uint64_t rsdp_addr = g_boot_info ? g_boot_info->rsdp_addr : 0;
    if (!rsdp_addr) return NULL;

    const uint8_t *rsdp = (const uint8_t *)rsdp_addr;
    if (memcmp(rsdp, RSDP_SIG, 8) != 0) {
        serial_printf("[ACPI] Bad RSDP signature\n");
        return NULL;
    }

    uint8_t revision = rsdp[15];

    if (revision >= 2) {
        /* ACPI 2.0+: XSDT (64-bit entries) */
        uint64_t xsdt_addr = 0;
        const uint8_t *p = rsdp + 24;
        for (int i = 0; i < 8; i++) {
            xsdt_addr |= (uint64_t)p[i] << (8 * i);
        }
        if (!xsdt_addr) return NULL;

        const uint8_t *xsdt = (const uint8_t *)xsdt_addr;
        uint32_t len = read32(xsdt + 4);
        if (!checksum_ok(xsdt, len)) return NULL;

        for (uint32_t off = 36; off + 8 <= len; off += 8) {
            uint64_t entry = 0;
            for (int i = 0; i < 8; i++) {
                entry |= (uint64_t)xsdt[off + i] << (8 * i);
            }
            const uint8_t *tbl = (const uint8_t *)entry;
            if (memcmp(tbl, "FACP", 4) == 0) {
                return tbl;
            }
        }
    } else {
        /* ACPI 1.0: RSDT (32-bit entries) */
        uint32_t rsdt_addr = read32(rsdp + 16);
        if (!rsdt_addr) return NULL;

        const uint8_t *rsdt = (const uint8_t *)(uintptr_t)rsdt_addr;
        uint32_t len = read32(rsdt + 4);
        if (!checksum_ok(rsdt, len)) return NULL;

        for (uint32_t off = 36; off + 4 <= len; off += 4) {
            uint32_t entry = read32(rsdt + off);
            const uint8_t *tbl = (const uint8_t *)(uint64_t)entry;
            if (memcmp(tbl, "FACP", 4) == 0) {
                return tbl;
            }
        }
    }

    return NULL;
}

/*
 * Scan the DSDT AML for the S5 package (_S5_) to learn the SLP_TYP
 * value required for the PM1a control register.
 * Returns true and fills slp_typ on success.
 */
static bool find_s5_slp_typ(const uint8_t *dsdt, uint32_t dsdt_len,
                            uint8_t *slp_typ)
{
    if (!dsdt || dsdt_len < 8) return false;

    /* NameOp (0x08) + "_S5_" */
    const uint8_t pattern[5] = { 0x08, '_', 'S', '5', '_' };

    for (uint32_t i = 0; i + 16 < dsdt_len; i++) {
        if (memcmp(dsdt + i, pattern, 5) != 0) continue;

        /* Expect PackageOp (0x12) shortly after */
        uint32_t j = i + 5;
        while (j < dsdt_len && dsdt[j] != 0x12) {
            j++;
        }
        if (j >= dsdt_len) continue;

        /* Skip: PackageOp(1) + pkg length(1, assume < 63) + count(1)
         * then expect BytePrefix (0x0A) + SLP_TYP */
        j += 3;
        if (j < dsdt_len && dsdt[j] == 0x0A && j + 1 < dsdt_len) {
            *slp_typ = dsdt[j + 1];
            return true;
        }
    }

    return false;
}

int acpi_power_off(void)
{
    const uint8_t *fadt = (const uint8_t *)find_fadt();
    if (!fadt) {
        serial_printf("[ACPI] FADT not found - cannot power off\n");
        return -1;
    }

    /* FADT layout (offsets from the ACPI spec):
     *   0x24 firmware_ctrl, 0x28 DSDT, 0x30 SMI_CMD,
     *   0x34 ACPI_ENABLE, 0x38 PM1a_EVT, 0x40 PM1a_CNT */
    uint32_t dsdt_addr = read32(fadt + 0x28);
    uint32_t smi_cmd = read32(fadt + 0x30);
    uint8_t acpi_enable = fadt[0x34];
    uint32_t pm1a_cnt = read32(fadt + 0x40);

    if (!pm1a_cnt) {
        serial_printf("[ACPI] No PM1a control block\n");
        return -1;
    }

    /* Enable ACPI mode first if the platform requires it */
    if (smi_cmd && acpi_enable) {
        outb((uint16_t)smi_cmd, acpi_enable);
    }

    /* Find the S5 sleep type in the DSDT */
    uint8_t slp_typ = 0;
    if (dsdt_addr) {
        uint32_t dsdt_len = read32((const void *)(uint64_t)dsdt_addr + 4);
        if (!find_s5_slp_typ((const uint8_t *)(uint64_t)dsdt_addr, dsdt_len, &slp_typ)) {
            serial_printf("[ACPI] S5 object not found - cannot power off\n");
            return -1;
        }
    } else {
        serial_printf("[ACPI] No DSDT - cannot power off\n");
        return -1;
    }

    serial_printf("[ACPI] Powering off (SLP_TYP=0x%x)\n", slp_typ);

    /* SLP_EN (bit 13) + SLP_TYP (bits 10-12) */
    interrupts_disable();
    uint16_t val = (uint16_t)((slp_typ << 10) | (1 << 13));
    outw((uint16_t)pm1a_cnt, val);

    /* Some firmware needs the write repeated */
    for (int i = 0; i < 1000; i++) {
        outw((uint16_t)pm1a_cnt, val);
        for (volatile int d = 0; d < 10000; d++) {}
    }

    /* Still here: power-off not honored */
    serial_printf("[ACPI] Power-off write ignored\n");
    return -1;
}

/*
 * arch_power_off: ACPI S5
 */
int arch_power_off(void)
{
    return acpi_power_off();
}

/*
 * arch_reboot (x86_64): 8042 keyboard-controller reset pulse, with a
 * triple-fault as the fallback. Never returns.
 */
void arch_reboot(void)
{
    interrupts_disable();

    /* Pulse the CPU reset line through the keyboard controller */
    int timeout = 100000;
    while ((inb(0x64) & 0x02) && timeout-- > 0) {
        /* Wait for input buffer empty */
    }
    outb(0x64, 0xFE);

    /* Give the controller a moment */
    for (volatile int i = 0; i < 10000000; i++) {}

    /* Fallback: triple fault via a null IDT */
    static const struct {
        uint16_t limit;
        uint64_t base;
    } PACKED null_idtr = { 0, 0 };

    __asm__ volatile("lidt %0" : : "m"(null_idtr) : "memory");
    __asm__ volatile("int3");

    /* Absolute last resort */
    while (1) {
        __asm__ volatile("hlt");
    }
}
