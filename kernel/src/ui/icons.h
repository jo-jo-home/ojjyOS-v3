/*
 * ojjyOS v3 - Built-in icon set (tools/mkicons.py), 128x128 straight-alpha
 * RGBA. Drawn with gfx_draw_rgba_scaled(), which filters them down to the
 * size needed (sharp at 1x and 2x).
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_UI_ICONS_H
#define _OJJY_UI_ICONS_H

#include "../types.h"

#define ICON_PX 128

typedef enum {
    ICON_FINDER, ICON_TERMINAL, ICON_TEXTEDIT, ICON_NOTES, ICON_PREVIEW,
    ICON_SETTINGS, ICON_ABOUT, ICON_CALENDAR, ICON_CALCULATOR, ICON_MONITOR,
    ICON_WEB, ICON_FOLDER, ICON_FILE, ICON_DISK, ICON_TRASH,
    ICON_COUNT
} IconId;

extern const uint8_t *const icon_pixels[ICON_COUNT];

#endif /* _OJJY_UI_ICONS_H */
