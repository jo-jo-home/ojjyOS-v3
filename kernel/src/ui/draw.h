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

#endif /* _OJJY_UI_DRAW_H */
