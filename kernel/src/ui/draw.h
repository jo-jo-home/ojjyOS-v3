/*
 * ojjyOS v3 - Shared drawing helpers exported by the compositor for app
 * modules that live in their own files (e.g. the browser).
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_UI_DRAW_H
#define _OJJY_UI_DRAW_H

#include "../framebuffer.h"
#include "theme.h"

const ThemeTokens *ui_theme(void);
void ui_rounded_rect(int x, int y, int w, int h, int r, Color color, uint8_t alpha);
Color ui_blend(Color bg, Color fg, uint8_t alpha);
void ui_mark_dirty(void);
/* Liquid Glass controls (LG4): a neutral glass capsule/rect (translucent
 * fill + specular rim + hairline; `raised` adds a soft shadow), and a
 * capsule push button (primary = accent). Logical coordinates. */
void ui_glass_control(int x, int y, int w, int h, int r, bool raised);
void ui_push_button(int x, int y, int w, int h, const char *label, bool primary);

#endif /* _OJJY_UI_DRAW_H */
