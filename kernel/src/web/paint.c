/*
 * ojjyOS v3 - Web engine: painting (see paint.h)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "web/paint.h"
#include "webfont.h"
#include "string.h"

#define CLIP_DEPTH 32

static int sc(const GfxSurface *s) { return s->scale > 1 ? s->scale : 1; }

static void set_clip(GfxSurface *s, int x1, int y1, int x2, int y2)
{
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > s->width) x2 = s->width;
    if (y2 > s->height) y2 = s->height;
    if (x2 < x1) x2 = x1;
    if (y2 < y1) y2 = y1;
    s->clip_x1 = x1; s->clip_y1 = y1; s->clip_x2 = x2; s->clip_y2 = y2;
}

void web_paint_image(GfxSurface *s, const uint32_t *px, int iw, int ih, int x, int y, int w, int h)
{
    if (!px || iw <= 0 || ih <= 0 || w <= 0 || h <= 0) return;
    int k = sc(s);
    int X = x * k, Y = y * k, W = w * k, H = h * k;
    int x1 = X < s->clip_x1 ? s->clip_x1 : X, x2 = X + W > s->clip_x2 ? s->clip_x2 : X + W;
    int y1 = Y < s->clip_y1 ? s->clip_y1 : Y, y2 = Y + H > s->clip_y2 ? s->clip_y2 : Y + H;
    if (x1 >= x2 || y1 >= y2) return;
    /* 16.16 fixed-point source steps; bilinear when upscaling or mild
     * scaling, box-ish (nearest of the pixel centre) when shrinking a lot */
    uint32_t sx_step = (uint32_t)(((uint64_t)iw << 16) / (uint32_t)W);
    uint32_t sy_step = (uint32_t)(((uint64_t)ih << 16) / (uint32_t)H);
    bool bilinear = sx_step < (2u << 16) && sy_step < (2u << 16);
    for (int py = y1; py < y2; py++) {
        uint32_t *dst = s->pixels + (size_t)py * (size_t)s->pitch;
        int64_t fy = (int64_t)(py - Y) * sy_step + (sy_step >> 1) - (1 << 15);
        if (fy < 0) fy = 0;
        int sy0 = (int)(fy >> 16);
        if (sy0 >= ih) sy0 = ih - 1;
        int sy1 = sy0 + 1 < ih ? sy0 + 1 : sy0;
        uint32_t wy = (uint32_t)(fy >> 8) & 0xFF;
        const uint32_t *r0 = px + (size_t)sy0 * (size_t)iw, *r1 = px + (size_t)sy1 * (size_t)iw;
        for (int pxl = x1; pxl < x2; pxl++) {
            int64_t fx = (int64_t)(pxl - X) * sx_step + (sx_step >> 1) - (1 << 15);
            if (fx < 0) fx = 0;
            int sx0 = (int)(fx >> 16);
            if (sx0 >= iw) sx0 = iw - 1;
            uint32_t c;
            if (bilinear) {
                int sx1 = sx0 + 1 < iw ? sx0 + 1 : sx0;
                uint32_t wx = (uint32_t)(fx >> 8) & 0xFF;
                uint32_t p00 = r0[sx0], p01 = r0[sx1], p10 = r1[sx0], p11 = r1[sx1];
                uint32_t out = 0;
                for (int sh = 0; sh < 32; sh += 8) {
                    uint32_t a = (p00 >> sh) & 0xFF, b = (p01 >> sh) & 0xFF, cc = (p10 >> sh) & 0xFF, d = (p11 >> sh) & 0xFF;
                    uint32_t top = a * (256 - wx) + b * wx, bot = cc * (256 - wx) + d * wx;
                    uint32_t v = (top * (256 - wy) + bot * wy) >> 16;
                    out |= (v > 255 ? 255 : v) << sh;
                }
                c = out;
            } else {
                c = r0[sx0];
            }
            uint32_t a = c >> 24;
            if (a == 255) dst[pxl] = c;
            else if (a) dst[pxl] = gfx_blend(dst[pxl], c & 0xFFFFFF, a);
        }
    }
}

/* CSS background image inside the item's box: size, position, repeat. */
static void paint_bg(GfxSurface *s, const WebDoc *d, const WebItem *it, int x, int y)
{
    if (it->ref < 0 || it->ref >= d->image_count) return;
    const WebImage *im = &d->images[it->ref];
    if (im->state != IMG_READY || !im->pixels || im->w <= 0 || im->h <= 0 || it->w <= 0 || it->h <= 0) return;
    int zoom = d->zoom > 0 ? d->zoom : 100;
    int64_t iw = (int64_t)im->w * zoom / 100, ih = (int64_t)im->h * zoom / 100;
    if (iw < 1) iw = 1;
    if (ih < 1) ih = 1;
    int64_t W = it->w, H = it->h, dw = iw, dh = ih;
    switch (it->bg_size) {
    case BG_COVER:
    case BG_CONTAIN: {
        /* compare W/iw with H/ih without floats */
        bool wider = W * ih > H * iw;
        bool by_w = it->bg_size == BG_COVER ? wider : !wider;
        if (by_w) { dw = W; dh = ih * W / iw; } else { dh = H; dw = iw * H / ih; }
        break;
    }
    case BG_LEN: {
        int64_t bw = it->bg_w >= 0 ? it->bg_w : it->bg_w <= -2 ? W * (-it->bg_w - 2) / 100 : -1;
        int64_t bh = it->bg_h >= 0 ? it->bg_h : it->bg_h <= -2 ? H * (-it->bg_h - 2) / 100 : -1;
        if (bw > 0 && bh > 0) { dw = bw; dh = bh; }
        else if (bw > 0) { dw = bw; dh = ih * bw / iw; }
        else if (bh > 0) { dh = bh; dw = iw * bh / ih; }
        break;
    }
    default: break;
    }
    if (dw < 1 || dh < 1 || dw > 20000 || dh > 20000) return;
    int64_t ox = it->bg_px[0] + (W - dw) * it->bg_pct[0] / 10000;
    int64_t oy = it->bg_px[1] + (H - dh) * it->bg_pct[1] / 10000;
    /* clip to the box */
    int k = sc(s);
    int saved[4] = {s->clip_x1, s->clip_y1, s->clip_x2, s->clip_y2};
    int bx1 = x * k, by1 = y * k, bx2 = (x + it->w) * k, by2 = (y + it->h) * k;
    if (bx1 > s->clip_x1) s->clip_x1 = bx1;
    if (by1 > s->clip_y1) s->clip_y1 = by1;
    if (bx2 < s->clip_x2) s->clip_x2 = bx2;
    if (by2 < s->clip_y2) s->clip_y2 = by2;
    if (s->clip_x1 < s->clip_x2 && s->clip_y1 < s->clip_y2) {
        bool rx = it->bg_repeat == BG_REPEAT || it->bg_repeat == BG_REPEAT_X;
        bool ry = it->bg_repeat == BG_REPEAT || it->bg_repeat == BG_REPEAT_Y;
        int64_t x0 = ox, y0 = oy;
        if (rx) { x0 = ox % dw; if (x0 > 0) x0 -= dw; }
        if (ry) { y0 = oy % dh; if (y0 > 0) y0 -= dh; }
        int tiles = 0;
        for (int64_t ty = y0; ty < H && tiles < 600; ty += dh) {
            for (int64_t tx = x0; tx < W && tiles < 600; tx += dw) {
                web_paint_image(s, im->pixels, im->w, im->h, x + (int)tx, y + (int)ty, (int)dw, (int)dh);
                tiles++;
                if (!rx) break;
            }
            if (!ry) break;
        }
    }
    s->clip_x1 = saved[0]; s->clip_y1 = saved[1]; s->clip_x2 = saved[2]; s->clip_y2 = saved[3];
}

static void paint_marker(GfxSurface *s, const WebItem *it, int x, int y)
{
    uint32_t c = it->color & 0xFFFFFF;
    uint8_t a = (uint8_t)(it->color >> 24);
    if (it->ref == LIST_SQUARE) gfx_round_rect(s, x, y, it->w, it->h, 0, c, a);
    else if (it->ref == LIST_CIRCLE) gfx_round_rect_stroke(s, x, y, it->w, it->h, it->w / 2, c, a);
    else gfx_round_rect(s, x, y, it->w, it->h, it->w / 2, c, a);
}

static void paint_text(GfxSurface *s, const WebItem *it, int x, int y, bool hover)
{
    uint32_t c = it->color & 0xFFFFFF;
    uint8_t a = (uint8_t)(it->color >> 24);
    /* nbsp placeholders draw as spaces */
    const char *t = it->text;
    int len = (int)it->len;
    bool nb = false;
    for (int i = 0; i < len; i++) if (t[i] == '\x01') { nb = true; break; }
    if (nb) {
        int pen = x;
        int start = 0;
        for (int i = 0; i <= len; i++) {
            if (i == len || t[i] == '\x01') {
                pen += webfont_draw(s, pen, y, t + start, i - start, it->font, c, a);
                if (i < len) pen += webfont_width(" ", 1, it->font);
                start = i + 1;
            }
        }
    } else {
        webfont_draw(s, x, y, t, len, it->font, c, a);
    }
    int asc = webfont_ascent(it->font);
    int thick = webfont_px(it->font) >= 24 ? 2 : 1;
    if ((it->deco & DECO_UNDERLINE) || hover)
        gfx_round_rect(s, x, y + asc + 2, it->w, thick, 0, c, a);
    if (it->deco & DECO_LINE_THROUGH)
        gfx_round_rect(s, x, y + asc - asc * 3 / 10, it->w, thick, 0, c, a);
    if (it->deco & DECO_OVERLINE)
        gfx_round_rect(s, x, y, it->w, thick, 0, c, a);
}

void web_paint(GfxSurface *s, const WebDoc *d, int ox, int oy, int scroll, int cx, int cy, int cw, int ch,
               const WebPaintOpts *opts)
{
    int k = sc(s);
    int saved[4] = {s->clip_x1, s->clip_y1, s->clip_x2, s->clip_y2};
    int base[4] = {cx * k, cy * k, (cx + cw) * k, (cy + ch) * k};
    if (base[0] < saved[0]) base[0] = saved[0];
    if (base[1] < saved[1]) base[1] = saved[1];
    if (base[2] > saved[2]) base[2] = saved[2];
    if (base[3] > saved[3]) base[3] = saved[3];
    set_clip(s, base[0], base[1], base[2], base[3]);
    gfx_round_rect(s, cx, cy, cw, ch, 0, d->bg & 0xFFFFFF, 255);
    int stack[CLIP_DEPTH][4];
    int depth = 0;
    int skipped = 0;            /* clips pushed while past the clip depth */
    int top = scroll - 64, bottom = scroll + ch + 64;
    int dy = oy - scroll;
    for (int i = 0; i < d->item_count; i++) {
        const WebItem *it = &d->items[i];
        if (it->kind == WI_CLIP) {
            if (depth >= CLIP_DEPTH) { skipped++; continue; }
            stack[depth][0] = s->clip_x1; stack[depth][1] = s->clip_y1;
            stack[depth][2] = s->clip_x2; stack[depth][3] = s->clip_y2;
            depth++;
            int x1 = (ox + it->x) * k, y1 = (dy + it->y) * k, x2 = (ox + it->x + it->w) * k, y2 = (dy + it->y + it->h) * k;
            if (x1 < s->clip_x1) x1 = s->clip_x1;
            if (y1 < s->clip_y1) y1 = s->clip_y1;
            if (x2 > s->clip_x2) x2 = s->clip_x2;
            if (y2 > s->clip_y2) y2 = s->clip_y2;
            set_clip(s, x1, y1, x2, y2);
            continue;
        }
        if (it->kind == WI_UNCLIP) {
            if (skipped) { skipped--; continue; }
            if (depth > 0) { depth--; set_clip(s, stack[depth][0], stack[depth][1], stack[depth][2], stack[depth][3]); }
            continue;
        }
        if (it->y > bottom || it->y + it->h < top) continue;
        if (s->clip_x1 >= s->clip_x2 || s->clip_y1 >= s->clip_y2) continue;
        int x = ox + it->x, y = dy + it->y;
        switch (it->kind) {
        case WI_RECT:
            if (it->color >> 24) gfx_round_rect(s, x, y, it->w, it->h, it->radius, it->color & 0xFFFFFF, (uint8_t)(it->color >> 24));
            break;
        case WI_STROKE: {
            uint32_t c = it->color & 0xFFFFFF;
            uint8_t a = (uint8_t)(it->color >> 24);
            int bw = it->ref > 0 ? it->ref : 1;
            if (it->radius == 0) {
                gfx_round_rect(s, x, y, it->w, bw, 0, c, a);
                gfx_round_rect(s, x, y + it->h - bw, it->w, bw, 0, c, a);
                gfx_round_rect(s, x, y + bw, bw, it->h - 2 * bw, 0, c, a);
                gfx_round_rect(s, x + it->w - bw, y + bw, bw, it->h - 2 * bw, 0, c, a);
            } else {
                for (int t = 0; t < bw && t < 8; t++)
                    gfx_round_rect_stroke(s, x + t, y + t, it->w - 2 * t, it->h - 2 * t, it->radius > t ? it->radius - t : 0, c, a);
            }
            break;
        }
        case WI_TEXT:
            paint_text(s, it, x, y, opts && opts->hover_link >= 0 && it->link == opts->hover_link);
            break;
        case WI_IMAGE: {
            const WebImage *im = it->ref >= 0 && it->ref < d->image_count ? &d->images[it->ref] : NULL;
            if (im && im->state == IMG_READY && im->pixels) web_paint_image(s, im->pixels, im->w, im->h, x, y, it->w, it->h);
            else gfx_round_rect(s, x, y, it->w, it->h, 0, 0xE8EAED, 255);
            break;
        }
        case WI_FIELD:
            if (opts && opts->draw_field) opts->draw_field(opts->ctx, s, it, x, y);
            break;
        case WI_MARKER:
            paint_marker(s, it, x, y);
            break;
        case WI_BGIMAGE:
            paint_bg(s, d, it, x, y);
            break;
        default:
            break;
        }
    }
    /* find-in-page highlights */
    if (opts && opts->marks) {
        set_clip(s, base[0], base[1], base[2], base[3]);
        for (int m = 0; m < opts->mark_count; m++) {
            int idx = opts->marks[m * 3], start = opts->marks[m * 3 + 1], len = opts->marks[m * 3 + 2];
            if (idx < 0 || idx >= d->item_count) continue;
            const WebItem *it = &d->items[idx];
            if (it->kind != WI_TEXT || it->y > bottom || it->y + it->h < top) continue;
            int x0 = webfont_width(it->text, start, it->font);
            int w = webfont_width(it->text + start, len, it->font);
            int x = ox + it->x + x0, y = dy + it->y;
            gfx_round_rect(s, x - 1, y, w + 2, it->h, 2, m == opts->mark_current ? 0xFF9632 : 0xFFEB3B, 255);
            WebItem t = *it;
            t.text = it->text + start;
            t.len = (uint32_t)len;
            t.w = w;
            t.color = 0xFF000000u;
            t.deco = 0;
            paint_text(s, &t, x, y, false);
        }
    }
    set_clip(s, saved[0], saved[1], saved[2], saved[3]);
}
