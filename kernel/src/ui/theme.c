/*
 * ojjyOS v3 Kernel - Tahoe UI Theme Tokens
 */

#include "theme.h"

static const ThemeTokens theme_light_tokens = {
    .name = "Tahoe Light",

    /* Palette derived from lighttahoe.png reference */
    .sky_top = RGB(15, 132, 204),       /* #0F84CC */
    .sky_mid = RGB(94, 176, 225),       /* #5EB0E1 */
    .horizon = RGB(240, 224, 202),      /* #F0E0CA */
    .glass_aqua = RGB(132, 205, 200),   /* #84CDC8 */
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

static const ThemeTokens theme_dark_tokens = {
    .name = "Tahoe Dark",

    .sky_top = RGB(8, 14, 26),          /* #080E1A */
    .sky_mid = RGB(14, 30, 58),         /* #0E1E3A */
    .horizon = RGB(38, 52, 74),         /* #26344A */
    .glass_aqua = RGB(84, 148, 170),    /* #5494AA */
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
