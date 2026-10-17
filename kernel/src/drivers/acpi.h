/*
 * ojjyOS v3 Kernel - ACPI Power Management (x86_64)
 *
 * ojjyOS - Created by Jonas Lee
 */

#ifndef _OJJY_ACPI_H
#define _OJJY_ACPI_H

#include "../types.h"

/*
 * Attempt a clean ACPI S5 power-off.
 * Returns 0 on success (never returns in practice),
 * -1 if power-off is not supported on this machine.
 */
int acpi_power_off(void);

#endif /* _OJJY_ACPI_H */
