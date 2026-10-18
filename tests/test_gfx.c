/* 2D graphics primitive tests (anti-aliasing, clipping, shadows, images).
 * Created by Jonas Lee. */
#include "gfx.h"
#include "test_support.h"

#define W 64
#define H 48
static uint32_t px[(H + 2) * (W + 2)];      /* 1-pixel guard frame around the surface */
static GfxSurface surf;

static void reset(uint32_t color)
{
    for (int i = 0; i < (H + 2) * (W + 2); i++) px[i] = 0xDEADBE;
    surf.pixels = px + (W + 2) + 1;
    surf.width = W; surf.height = H; surf.pitch = W + 2;
    surf.clip_x1 = surf.clip_y1 = 0; surf.clip_x2 = W; surf.clip_y2 = H;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) surf.pixels[y * surf.pitch + x] = color;
}
static uint32_t at(int x, int y) { return surf.pixels[y * surf.pitch + x] & 0xFFFFFF; }
static bool guard_intact(void)
{
    for (int x = 0; x < W + 2; x++) if (px[x] != 0xDEADBE || px[(H + 1) * (W + 2) + x] != 0xDEADBE) return false;
    for (int y = 0; y < H + 2; y++) if (px[y * (W + 2)] != 0xDEADBE || px[y * (W + 2) + W + 1] != 0xDEADBE) return false;
    return true;
}
static int red(uint32_t c) { return (int)((c >> 16) & 0xFF); }

int main(void)
{
    CHECK(gfx_isqrt(0) == 0 && gfx_isqrt(1) == 1 && gfx_isqrt(99) == 9 && gfx_isqrt(100) == 10 &&
          gfx_isqrt(4000000000u) == 63245, "integer square root");
    CHECK((gfx_blend(0x000000, 0xFFFFFF, 255) & 0xFFFFFF) == 0xFFFFFF && (gfx_blend(0x123456, 0xFFFFFF, 0) & 0xFFFFFF) == 0x123456,
          "blend endpoints exact");
    uint32_t half = gfx_blend(0x000000, 0xFF0000, 128);
    CHECK(red(half) >= 127 && red(half) <= 129, "blend midpoint");

    reset(0x000000);
    gfx_round_rect(&surf, 8, 8, 40, 30, 10, 0xFF0000, 255);
    CHECK(at(28, 22) == 0xFF0000 && at(8, 22) == 0xFF0000 && at(28, 8) == 0xFF0000, "interior and straight edges full");
    CHECK(at(8, 8) == 0 && at(7, 22) == 0 && at(48, 22) == 0, "outside untouched");
    int r_edge = red(at(10, 11));   /* centre ~9.9 px from the arc centre: partial */
    CHECK(r_edge > 0 && r_edge < 255, "corner pixel anti-aliased");
    CHECK(at(10, 11) == at(45, 11) && at(10, 11) == at(10, 34) && at(10, 11) == at(45, 34), "corners symmetric");
    CHECK(guard_intact(), "no writes outside surface");

    reset(0x000000);
    gfx_round_rect(&surf, -20, -20, 200, 200, 30, 0x00FF00, 255);   /* far larger than surface */
    gfx_round_rect(&surf, 60, 40, 30, 30, 8, 0x0000FF, 200);
    surf.clip_x1 = 10; surf.clip_x2 = 20;
    gfx_round_rect(&surf, 0, 0, W, H, 0, 0xFFFFFF, 255);
    CHECK(at(9, 5) != 0xFFFFFF && at(10, 5) == 0xFFFFFF && at(19, 5) == 0xFFFFFF && at(20, 5) != 0xFFFFFF, "clip box honoured");
    CHECK(guard_intact(), "clipped draws stay inside");

    reset(0x808080);
    gfx_round_rect(&surf, 10, 10, 20, 20, 10, 0xFFFFFF, 0);
    CHECK(at(20, 20) == 0x808080, "zero alpha is a no-op");
    gfx_round_rect(&surf, 10, 10, 20, 20, 50, 0xFFFFFF, 255);        /* radius clamps to a circle */
    CHECK(at(20, 20) == 0xFFFFFF && at(10, 10) == 0x808080, "oversized radius clamps to a circle");

    reset(0xFFFFFF);
    gfx_shadow(&surf, 16, 12, 30, 20, 6, 8, 2, 0x000000, 120);
    CHECK(at(30, 20) == 0xFFFFFF, "shadow leaves the shape interior alone");
    int near = red(at(30, 34)), far = red(at(30, 40));
    CHECK(near < far && far <= 255 && near < 255, "shadow fades with distance");
    CHECK(at(30, 45) == 0xFFFFFF, "shadow ends after spread");
    CHECK(guard_intact(), "shadow stays inside");

    reset(0x000000);
    gfx_round_rect_vgradient(&surf, 0, 0, W, H, 0, 0x000000, 0xFF0000, 255);
    CHECK(red(at(5, 0)) < 16 && red(at(5, H - 1)) > 238 && red(at(5, H / 2)) > 100 && red(at(5, H / 2)) < 160,
          "vertical gradient spans its colours");

    reset(0x000000);
    gfx_round_rect_rim(&surf, 8, 8, 40, 30, 10, 0);            /* zero strength: no-op */
    CHECK(at(28, 8) == 0x000000 && at(28, 22) == 0x000000, "rim with zero strength changes nothing");
    gfx_round_rect_rim(&surf, 8, 8, 40, 30, 10, 255);
    CHECK(at(28, 22) == 0x000000, "rim leaves the deep interior untouched");
    CHECK(at(7, 22) == 0 && at(28, 7) == 0 && at(28, 39) == 0 && at(49, 22) == 0,
          "rim never writes outside the shape");
    int rim_top = red(at(28, 8)), rim_bottom = red(at(28, 37)), rim_side = red(at(8, 22));
    CHECK(rim_top > rim_bottom && rim_bottom > rim_side && rim_side > 0,
          "rim lit from above, faint at the bottom, dimmest at the sides");
    CHECK(rim_top >= 195 && rim_top <= 235, "top rim reaches its expected edge strength");
    CHECK(red(at(28, 13)) == 0, "rim band ends a few pixels in");
    gfx_round_rect_rim(&surf, -30, -30, 100, 100, 20, 200);    /* mostly outside: clip safety */
    CHECK(guard_intact(), "rim stays inside the surface under clipping");

    reset(0x000000);
    static uint32_t src[W * H];
    for (int i = 0; i < W * H; i++) src[i] = (uint32_t)i & 0xFFFFFF;
    gfx_round_rect_image(&surf, 4, 4, 30, 20, 0, src, 255);
    CHECK(at(10, 10) == src[10 * W + 10], "image fill copies source pixels");

    static uint8_t img[16 * 16 * 4];
    for (int i = 0; i < 16 * 16; i++) { img[i * 4] = 255; img[i * 4 + 1] = 0; img[i * 4 + 2] = 0; img[i * 4 + 3] = 255; }
    reset(0x000000);
    gfx_draw_rgba_scaled(&surf, img, 16, 16, 4, 4, 40, 40, 255);    /* upscale: bilinear */
    CHECK(at(20, 20) == 0xFF0000 && at(4, 4) == 0xFF0000 && at(43, 43) == 0xFF0000, "opaque upscale keeps colour");
    reset(0x000000);
    gfx_draw_rgba_scaled(&surf, img, 16, 16, 2, 2, 8, 8, 255);      /* downscale: area average */
    CHECK(at(5, 5) == 0xFF0000 && at(1, 1) == 0, "opaque downscale keeps colour and bounds");
    for (int i = 0; i < 16 * 16; i++) img[i * 4 + 3] = (i % 2) ? 255 : 0;
    reset(0x000000);
    gfx_draw_rgba_scaled(&surf, img, 16, 16, 0, 0, 8, 8, 255);
    CHECK(red(at(3, 3)) > 110 && red(at(3, 3)) < 145, "transparent texels average as coverage, no dark fringe");
    surf.clip_x2 = W; 
    gfx_draw_rgba_scaled(&surf, img, 16, 16, 50, 40, 40, 40, 255);
    CHECK(guard_intact(), "image draw clipped");

    /* Layer blit: same size copies exactly; half size filters; mask + clip */
    static uint32_t layer[20 * 10];
    for (int i = 0; i < 20 * 10; i++) layer[i] = (i % 20) < 10 ? 0x00FF00 : 0x0000FF;
    reset(0x000000);
    gfx_blit_scaled(&surf, layer, 20, 0, 0, 20, 10, 5, 5, 20, 10, 0, 255);
    CHECK(at(5, 5) == 0x00FF00 && at(24, 14) == 0x0000FF && at(4, 5) == 0 && at(25, 5) == 0, "same-size layer blit is exact");
    reset(0x000000);
    gfx_blit_scaled(&surf, layer, 20, 0, 0, 20, 10, 0, 0, 10, 5, 0, 255);
    CHECK((at(2, 2) & 0xFFFFFF) == 0x00FF00 && (at(8, 2) & 0xFFFFFF) == 0x0000FF, "downscaled layer keeps both halves");
    reset(0x000000);
    gfx_blit_scaled(&surf, layer, 20, 0, 0, 20, 10, 10, 10, 20, 10, 5, 128);
    CHECK(at(10, 10) == 0 && (at(15, 15) >> 8 & 0xFF) > 100 && (at(15, 15) >> 8 & 0xFF) < 140, "rounded mask and alpha");
    gfx_blit_scaled(&surf, layer, 20, 0, 0, 20, 10, 50, 40, 40, 40, 0, 255);
    CHECK(guard_intact(), "layer blit clipped");

    /* Liquid Glass */
    static uint32_t scratch[(W + 4) * (H + 4) * 2];
    GfxGlass glass = { 0xFFFFFF, 64, 2, 4, 8, 200, 0, 255 };
    reset(0x808080);
    gfx_set_scratch(scratch, sizeof(scratch) / sizeof(scratch[0]));
    gfx_liquid_glass(&surf, 10, 8, 40, 30, 10, &glass);
    uint32_t expect = gfx_blend(0x808080, 0xFFFFFF, 64) & 0xFFFFFF;
    CHECK((at(30, 23) & 0xFFFFFF) == expect, "glass interior = tinted backdrop");
    CHECK(at(5, 5) == 0x808080 && at(55, 44) == 0x808080, "glass leaves outside pixels alone");
    CHECK(red(at(30, 8)) > red(at(30, 37)) && red(at(30, 37)) > red(at(30, 23)), "specular rim: top brighter than bottom, both above interior");
    CHECK(guard_intact(), "glass stays inside the surface");
    /* Refraction: a dark stripe just inside the left edge is replaced by
     * backdrop pulled from further in (light), so the rim pixel is light. */
    reset(0xFFFFFF);
    for (int y = 0; y < H; y++) for (int x = 10; x < 13; x++) surf.pixels[y * surf.pitch + x] = 0x000000;
    GfxGlass clear = { 0xFFFFFF, 0, 0, 6, 8, 0, 0, 255 };
    gfx_liquid_glass(&surf, 10, 4, 40, 40, 4, &clear);
    CHECK(red(at(10, 24)) > 200 && red(at(30, 24)) > 200, "refraction pulls inner backdrop to the rim");
    surf.clip_x1 = 0; surf.clip_x2 = 20;
    gfx_liquid_glass(&surf, -10, -10, 90, 70, 20, &glass);
    CHECK(guard_intact() && at(25, 20) == 0xFFFFFF, "glass honours the clip box");
    gfx_set_scratch(0, 0);
    reset(0x000000);
    gfx_liquid_glass(&surf, 4, 4, 20, 20, 6, &glass);
    CHECK(red(at(14, 14)) > 0 && guard_intact(), "no scratch: plain tinted fallback");

    reset(0x000000);
    gfx_round_rect_stroke(&surf, 4, 4, 40, 30, 8, 0xFFFFFF, 255);
    CHECK(at(20, 4) == 0xFFFFFF && at(20, 5) == 0 && at(4, 20) == 0xFFFFFF && at(20, 20) == 0, "hairline border");
    CHECK(guard_intact(), "stroke stays inside");
    return finish();
}
