/*
 * ojjyOS v3 Kernel - Tahoe UI Theme Tokens
 */

#include "theme.h"

static ThemeTokens theme_light_tokens = {
    .name = "Tahoe Light",

    /* Palette derived from lighttahoe.png reference */
    .sky_top = RGB(15, 132, 204),       /* #0F84CC */
    .sky_mid = RGB(94, 176, 225),       /* #5EB0E1 */
    .horizon = RGB(240, 224, 202),      /* #F0E0CA */
    .glass_aqua = RGB(170, 210, 238),   /* #AAD2EE calm sky blue */
    .surf_teal = RGB(17, 165, 202),     /* #11A5CA */
    .wave_blue = RGB(15, 114, 197),     /* #0F72C5 */
    .deep_ocean = RGB(11, 63, 140),     /* #0B3F8C */
    .midnight = RGB(10, 28, 74),        /* #0A1C4A */

    .accent = RGB(48, 145, 216),        /* #3091D8 */
    .accent_soft = RGB(124, 197, 230),  /* #7CC5E6 */
    .text = RGB(20, 33, 52),            /* #142134 */
    .text_muted = RGB(82, 110, 134),    /* #526E86 */
    .dock_tint = RGB(230, 244, 252),    /* #E6F4FC */
    .shadow = RGB(6, 17, 33),           /* #061121 */

    .glass = {
        .blur_px = { 6, 12, 18 },
        .opacity = { 48, 78, 110 },
        .highlight = { 26, 42, 64 },
        .shadow_alpha = { 24, 36, 52 },
        .corner_radius = { 10, 16, 24 }
    }
};

static ThemeTokens theme_dark_tokens = {
    .name = "Tahoe Dark",

    .sky_top = RGB(8, 14, 26),          /* #080E1A */
    .sky_mid = RGB(14, 30, 58),         /* #0E1E3A */
    .horizon = RGB(38, 52, 74),         /* #26344A */
    .glass_aqua = RGB(72, 124, 172),    /* #487CAC */
    .surf_teal = RGB(28, 116, 156),     /* #1C749C */
    .wave_blue = RGB(22, 78, 140),      /* #164E8C */
    .deep_ocean = RGB(10, 38, 82),      /* #0A2652 */
    .midnight = RGB(6, 10, 22),         /* #060A16 */

    .accent = RGB(76, 164, 216),        /* #4CA4D8 */
    .accent_soft = RGB(96, 134, 170),   /* #6086AA */
    .text = RGB(228, 239, 248),         /* #E4EFF8 */
    .text_muted = RGB(162, 180, 196),   /* #A2B4C4 */
    .dock_tint = RGB(26, 34, 48),       /* #1A2230 */
    .shadow = RGB(0, 0, 0),             /* #000000 */

    .glass = {
        .blur_px = { 8, 14, 22 },
        .opacity = { 58, 88, 120 },
        .highlight = { 20, 32, 46 },
        .shadow_alpha = { 36, 52, 70 },
        .corner_radius = { 10, 16, 24 }
    }
};

const ThemeTokens *theme_light(void)
{
    return &theme_light_tokens;
}

const ThemeTokens *theme_dark(void)
{
    return &theme_dark_tokens;
}

/* Accent colours (Settings > Appearance). Blue keeps the original
 * hand-tuned light/dark pairs so the default look is unchanged; every
 * other accent derives its light and dark variants from one base. */
typedef struct {
    const char *name;
    Color base;
} AccentDef;

static const AccentDef accent_defs[] = {
    { "Blue",     RGB(28, 122, 216) },
    { "Purple",   RGB(112, 92, 226) },
    { "Pink",     RGB(240, 64, 112) },
    { "Red",      RGB(226, 62, 56) },
    { "Orange",   RGB(240, 140, 20) },
    { "Yellow",   RGB(206, 156, 12) },
    { "Green",    RGB(56, 172, 84) },
    { "Graphite", RGB(140, 144, 152) },
};

static Color color_lerp(Color a, Color b, int t)
{
    if (t < 0) t = 0;
    if (t > 255) t = 255;
    uint32_t out = 0xFF000000;
    for (int shift = 0; shift <= 16; shift += 8) {
        uint32_t ca = (a >> shift) & 0xFF, cb = (b >> shift) & 0xFF;
        out |= ((ca + (cb - ca) * (uint32_t)t / 255) & 0xFF) << shift;
    }
    return (Color)out;
}

int theme_accent_count(void)
{
    return (int)(sizeof(accent_defs) / sizeof(accent_defs[0]));
}

const char *theme_accent_name(int index)
{
    if (index < 0 || index >= theme_accent_count()) return "Blue";
    return accent_defs[index].name;
}

Color theme_accent_swatch(int index)
{
    if (index < 0 || index >= theme_accent_count()) return accent_defs[0].base;
    return accent_defs[index].base;
}

void theme_set_accent(int index)
{
    if (index <= 0 || index >= theme_accent_count()) {
        /* Tahoe Blue: the original hand-tuned pairs, pixel-unchanged. */
        theme_light_tokens.accent = RGB(48, 145, 216);
        theme_light_tokens.accent_soft = RGB(124, 197, 230);
        theme_dark_tokens.accent = RGB(76, 164, 216);
        theme_dark_tokens.accent_soft = RGB(96, 134, 170);
        return;
    }
    Color base = accent_defs[index].base;
    theme_light_tokens.accent = base;
    theme_light_tokens.accent_soft = color_lerp(base, COLOR_WHITE, 115);
    theme_dark_tokens.accent = color_lerp(base, COLOR_WHITE, 46);
    theme_dark_tokens.accent_soft = color_lerp(base, RGB(20, 24, 32), 90);
}
