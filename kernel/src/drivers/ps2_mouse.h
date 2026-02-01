/*
 * ojjyOS v3 Kernel - PS/2 Mouse Driver
 *
 * Standard PS/2 mouse driver with scroll wheel support.
 */

#ifndef _OJJY_PS2_MOUSE_H
#define _OJJY_PS2_MOUSE_H

#include "../types.h"
#include "driver.h"

/* Get the PS/2 mouse driver */
Driver *ps2_mouse_get_driver(void);

/* Initialize PS/2 mouse */
void ps2_mouse_init(void);

#endif /* _OJJY_PS2_MOUSE_H */
