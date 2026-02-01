/*
 * ojjyOS v3 Kernel - Text Console
 *
 * Provides printf-style output to framebuffer.
 */

#ifndef _OJJY_CONSOLE_H
#define _OJJY_CONSOLE_H

#include "types.h"
#include "framebuffer.h"

/* Initialize console */
void console_init(void);

/* Set console colors */
void console_set_colors(Color fg, Color bg);

/* Clear console */
void console_clear(void);

/* Print a character */
void console_putc(char c);

/* Print a string */
void console_puts(const char *s);

/* Formatted print */
void console_printf(const char *fmt, ...);

/* Move cursor */
void console_set_cursor(int x, int y);
void console_get_cursor(int *x, int *y);

#endif /* _OJJY_CONSOLE_H */
