/*
 * ojjyOS v3 Kernel - Architecture Power Interface
 *
 * Each architecture implements:
 *   x86_64:   ACPI S5 power-off + 8042 keyboard-controller reset
 *   aarch64:  PSCI SYSTEM_OFF / SYSTEM_RESET
 *
 * ojjyOS - Created by Jonas Lee
 */

#ifndef _OJJY_ARCH_POWER_H
#define _OJJY_ARCH_POWER_H

/*
 * Attempt a clean power-off.
 * Returns 0 on success (never returns in practice),
 * -1 if power-off is not supported on this machine.
 */
int arch_power_off(void);

/*
 * Reset the machine. Never returns.
 */
void arch_reboot(void);

#endif /* _OJJY_ARCH_POWER_H */
