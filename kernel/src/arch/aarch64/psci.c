/*
 * ojjyOS v3 Kernel - PSCI Power Management (aarch64)
 *
 * Power off and reset through the ARM Power State Coordination
 * Interface, which QEMU's virt machine implements. The conduit is
 * HVC on QEMU (no EL3); SMC is tried as a fallback.
 *
 * Function IDs (PSCI 0.2, SMC32 calling convention):
 *   SYSTEM_OFF   = 0x84000008
 *   SYSTEM_RESET = 0x84000009
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "arch_power.h"
#include "types.h"
#include "serial.h"

#define PSCI_SYSTEM_OFF     0x84000008UL
#define PSCI_SYSTEM_RESET   0x84000009UL
#define PSCI_NOT_SUPPORTED  ((int64_t)-1)

/*
 * Invoke a PSCI function. `conduit` selects the instruction:
 * 0 = HVC, 1 = SMC. Returns the x0 result code.
 */
static int64_t psci_call(uint64_t fn, int conduit)
{
    register uint64_t x0 __asm__("x0") = fn;
    if (conduit == 0) {
        __asm__ volatile("hvc #0"
                         : "+r"(x0)
                         :
                         : "x1", "x2", "x3", "x4", "x5", "x6", "x7", "memory");
    } else {
        __asm__ volatile("smc #0"
                         : "+r"(x0)
                         :
                         : "x1", "x2", "x3", "x4", "x5", "x6", "x7", "memory");
    }
    return (int64_t)x0;
}

/*
 * Try SYSTEM_OFF via HVC, then SMC. Never returns on success.
 * Returns -1 if the platform does not implement PSCI.
 */
int arch_power_off(void)
{
    serial_printf("[PSCI] SYSTEM_OFF (hvc)\n");
    int64_t ret = psci_call(PSCI_SYSTEM_OFF, 0);
    if (ret == PSCI_NOT_SUPPORTED) {
        serial_printf("[PSCI] hvc not supported, trying smc\n");
        ret = psci_call(PSCI_SYSTEM_OFF, 1);
    }
    if (ret == PSCI_NOT_SUPPORTED) {
        return -1;
    }
    /* Success should never return; if it does, halt forever */
    while (1) {
        __asm__ volatile("wfi");
    }
    return -1;
}

/*
 * SYSTEM_RESET. Never returns on success.
 */
void arch_reboot(void)
{
    serial_printf("[PSCI] SYSTEM_RESET (hvc)\n");
    int64_t ret = psci_call(PSCI_SYSTEM_RESET, 0);
    if (ret == PSCI_NOT_SUPPORTED) {
        ret = psci_call(PSCI_SYSTEM_RESET, 1);
    }

    if (ret == PSCI_NOT_SUPPORTED) {
        serial_printf("[PSCI] Reset not supported - halting\n");
    }

    while (1) {
        __asm__ volatile("wfi");
    }
}
