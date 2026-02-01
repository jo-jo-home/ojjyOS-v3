/*
 * ojjyOS v3 Kernel - PS/2 Keyboard Driver (Refactored)
 *
 * Integrated with driver model and input event system.
 */

#ifndef _OJJY_PS2_KEYBOARD_H
#define _OJJY_PS2_KEYBOARD_H

#include "../types.h"
#include "driver.h"

/* Get the PS/2 keyboard driver */
Driver *ps2_keyboard_get_driver(void);

/* Initialize PS/2 keyboard (registers driver) */
void ps2_keyboard_init(void);

#endif /* _OJJY_PS2_KEYBOARD_H */
