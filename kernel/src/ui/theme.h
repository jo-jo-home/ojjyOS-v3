/*
 * ojjyOS v3 Kernel - Tahoe UI Theme Tokens
 *
 * Centralized palette and glass material tokens for the compositor.
 */

#ifndef _OJJY_UI_THEME_H
#define _OJJY_UI_THEME_H

#include "../framebuffer.h"
#include "../types.h"

typedef struct {
    uint8_t blur_px[3];
    uint8_t opacity[3];
    uint8_t highlight[3];
    uint8_t shadow_alpha[3];
    uint8_t corner_radius[3];
} GlassTokens;

typedef struct {
    const char *name;

    /* Wallpaper-inspired palette */
    Color sky_top;
    Color sky_mid;
    Color horizon;
    Color glass_aqua;
    Color surf_teal;
    Color wave_blue;
    Color deep_ocean;
    Color midnight;

    /* UI accents */
    Color accent;
    Color accent_soft;
    Color text;
    Color text_muted;
    Color dock_tint;
    Color shadow;

    GlassTokens glass;
} ThemeTokens;

const ThemeTokens *theme_light(void);
const ThemeTokens *theme_dark(void);

/* Accent colours (Settings > Appearance). Blue is the Tahoe default.
 * set_accent mutates both token sets in place, so every `theme->accent`
 * user follows a change immediately. */
void theme_set_accent(int index);
int theme_accent_count(void);
const char *theme_accent_name(int index);
Color theme_accent_swatch(int index);

#endif /* _OJJY_UI_THEME_H */
