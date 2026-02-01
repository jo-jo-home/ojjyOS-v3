/*
 * ojjyOS v3 Kernel - Diagnostics Screen
 *
 * In-OS diagnostics display showing hardware status.
 */

#ifndef _OJJY_DIAGNOSTICS_H
#define _OJJY_DIAGNOSTICS_H

#include "../types.h"

/* Show full diagnostics screen */
void diagnostics_show(void);

/* Update live diagnostics (cursor rendering, etc.) */
void diagnostics_update(void);

/* Reset cursor state (call after screen clear) */
void diagnostics_reset_cursor(void);

#endif /* _OJJY_DIAGNOSTICS_H */
