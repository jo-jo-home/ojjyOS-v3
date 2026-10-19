/*
 * ojjyOS v3 - PNG decoder: all colour types (grey, RGB, palette, grey+alpha,
 * RGBA), bit depths 1-16, tRNS transparency, Adam7 interlacing, all five
 * row filters. Untrusted input: every chunk and row access is bounded.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "image/image.h"
#include "image/inflate.h"
#include "string.h"

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static const uint8_t SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};

typedef struct {
    uint32_t w, h;
    uint8_t depth, color, interlace;
    uint8_t channels;
    uint32_t palette[256];
    int palette_n;
    bool trns_grey, trns_rgb;
    uint16_t trns_g, trns_r, trns_gr, trns_b;
} Png;

static bool header(const uint8_t *d, size_t len, Png *p)
{
    if (len < 33 || memcmp(d, SIG, 8) != 0 || be32(d + 8) != 13 || memcmp(d + 12, "IHDR", 4) != 0) return false;
    p->w = be32(d + 16);
    p->h = be32(d + 20);
    p->depth = d[24];
    p->color = d[25];
    p->interlace = d[28];
    if (!p->w || !p->h || p->w > 16384 || p->h > 16384 || d[26] != 0 || d[27] != 0 || p->interlace > 1) return false;
    switch (p->color) {
        case 0: p->channels = 1; if (p->depth != 1 && p->depth != 2 && p->depth != 4 && p->depth != 8 && p->depth != 16) return false; break;
        case 2: p->channels = 3; if (p->depth != 8 && p->depth != 16) return false; break;
        case 3: p->channels = 1; if (p->depth != 1 && p->depth != 2 && p->depth != 4 && p->depth != 8) return false; break;
        case 4: p->channels = 2; if (p->depth != 8 && p->depth != 16) return false; break;
        case 6: p->channels = 4; if (p->depth != 8 && p->depth != 16) return false; break;
        default: return false;
    }
    return true;
}

bool png_info(const uint8_t *data, size_t len, int *w, int *h)
{
    Png p;
    if (!header(data, len, &p)) return false;
    *w = (int)p.w;
    *h = (int)p.h;
    return true;
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

/* Undo filters for one pass in place. raw: rows of (1 + stride) bytes. */
static bool unfilter(uint8_t *raw, uint32_t rows, size_t stride, size_t bpp)
{
    uint8_t *prev = NULL;
    for (uint32_t y = 0; y < rows; y++) {
        uint8_t *row = raw + (size_t)y * (stride + 1);
        uint8_t f = row[0];
        uint8_t *cur = row + 1;
        switch (f) {
        case 0: break;
        case 1: for (size_t i = bpp; i < stride; i++) cur[i] = (uint8_t)(cur[i] + cur[i - bpp]); break;
        case 2: if (prev) for (size_t i = 0; i < stride; i++) cur[i] = (uint8_t)(cur[i] + prev[i]); break;
        case 3:
            for (size_t i = 0; i < stride; i++) {
                int a = i >= bpp ? cur[i - bpp] : 0, b = prev ? prev[i] : 0;
                cur[i] = (uint8_t)(cur[i] + ((a + b) >> 1));
            }
            break;
        case 4:
            for (size_t i = 0; i < stride; i++) {
                int a = i >= bpp ? cur[i - bpp] : 0, b = prev ? prev[i] : 0, c = (prev && i >= bpp) ? prev[i - bpp] : 0;
                cur[i] = (uint8_t)(cur[i] + paeth(a, b, c));
            }
            break;
        default: return false;
        }
        prev = cur;
    }
    return true;
}

static uint32_t sample(const Png *p, const uint8_t *row, uint32_t x)
{
    uint32_t r, g, b, a = 255;
    if (p->depth < 8) {
        uint32_t bit = x * p->depth;
        uint32_t v = (row[bit >> 3] >> (8 - p->depth - (bit & 7))) & ((1u << p->depth) - 1);
        if (p->color == 3) return (int)v < p->palette_n ? p->palette[v] : 0xFF000000u;
        if (p->trns_grey && v == p->trns_g) a = 0;
        uint32_t gv = v * 255 / ((1u << p->depth) - 1);
        return (a << 24) | (gv << 16) | (gv << 8) | gv;
    }
    size_t bytes = p->depth / 8;
    const uint8_t *s = row + (size_t)x * p->channels * bytes;
    #define C(i) (bytes == 2 ? s[(i) * 2] : s[i])
    #define C16(i) (bytes == 2 ? (uint16_t)((s[(i) * 2] << 8) | s[(i) * 2 + 1]) : s[i])
    switch (p->color) {
    case 0:
        r = g = b = C(0);
        if (p->trns_grey && C16(0) == p->trns_g) a = 0;
        break;
    case 2:
        r = C(0); g = C(1); b = C(2);
        if (p->trns_rgb && C16(0) == p->trns_r && C16(1) == p->trns_gr && C16(2) == p->trns_b) a = 0;
        break;
    case 3:
        return s[0] < p->palette_n ? p->palette[s[0]] : 0xFF000000u;
    case 4:
        r = g = b = C(0); a = C(1);
        break;
    default:
        r = C(0); g = C(1); b = C(2); a = C(3);
        break;
    }
    #undef C
    #undef C16
    return (a << 24) | (r << 16) | (g << 8) | b;
}

bool png_decode(const uint8_t *d, size_t len, uint32_t *argb, size_t pixels, uint8_t *scratch, size_t scratch_size)
{
    Png p;
    memset(&p, 0, sizeof(p));
    if (!header(d, len, &p) || (size_t)p.w * p.h > pixels) return false;
    /* gather IDAT payloads at the start of scratch */
    size_t zlen = 0;
    size_t pos = 8;
    bool end = false;
    while (pos + 12 <= len && !end) {
        uint32_t clen = be32(d + pos);
        const uint8_t *type = d + pos + 4;
        if (clen > len - pos - 12) return false;
        const uint8_t *body = d + pos + 8;
        if (memcmp(type, "PLTE", 4) == 0) {
            int n = (int)(clen / 3);
            if (n > 256) n = 256;
            for (int i = 0; i < n; i++) p.palette[i] = 0xFF000000u | ((uint32_t)body[i * 3] << 16) | ((uint32_t)body[i * 3 + 1] << 8) | body[i * 3 + 2];
            p.palette_n = n;
        } else if (memcmp(type, "tRNS", 4) == 0) {
            if (p.color == 3) {
                for (uint32_t i = 0; i < clen && (int)i < p.palette_n; i++) p.palette[i] = (p.palette[i] & 0xFFFFFF) | ((uint32_t)body[i] << 24);
            } else if (p.color == 0 && clen >= 2) {
                p.trns_grey = true;
                p.trns_g = (uint16_t)((body[0] << 8) | body[1]);
                if (p.depth < 16) p.trns_g &= (uint16_t)((1u << p.depth) - 1);
            } else if (p.color == 2 && clen >= 6) {
                p.trns_rgb = true;
                p.trns_r = (uint16_t)((body[0] << 8) | body[1]);
                p.trns_gr = (uint16_t)((body[2] << 8) | body[3]);
                p.trns_b = (uint16_t)((body[4] << 8) | body[5]);
                if (p.depth == 8) { p.trns_r &= 0xFF; p.trns_gr &= 0xFF; p.trns_b &= 0xFF; }
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (zlen + clen > scratch_size / 2) return false;
            memcpy(scratch + zlen, body, clen);
            zlen += clen;
        } else if (memcmp(type, "IEND", 4) == 0) {
            end = true;
        }
        pos += 12 + clen;
    }
    if (!zlen) return false;
    if (p.color == 3 && !p.palette_n) return false;
    size_t bits_pp = (size_t)p.depth * p.channels;
    size_t bpp = bits_pp >= 8 ? bits_pp / 8 : 1;
    /* passes: Adam7 or one full pass */
    static const uint8_t sx[7] = {0, 4, 0, 2, 0, 1, 0}, sy[7] = {0, 0, 4, 0, 2, 0, 1};
    static const uint8_t dx[7] = {8, 8, 4, 4, 2, 2, 1}, dy[7] = {8, 8, 8, 4, 4, 2, 2};
    int passes = p.interlace ? 7 : 1;
    size_t raw_size = 0;
    for (int k = 0; k < passes; k++) {
        uint32_t pw = p.interlace ? (p.w > sx[k] ? (p.w - sx[k] + dx[k] - 1) / dx[k] : 0) : p.w;
        uint32_t ph = p.interlace ? (p.h > sy[k] ? (p.h - sy[k] + dy[k] - 1) / dy[k] : 0) : p.h;
        if (!pw || !ph) continue;
        raw_size += (size_t)ph * (1 + (pw * bits_pp + 7) / 8);
    }
    uint8_t *raw = scratch + ((zlen + 15) & ~(size_t)15);
    size_t room = scratch_size - (size_t)(raw - scratch);
    if (raw_size > room) return false;
    long got = zlib_inflate(scratch, zlen, raw, raw_size);
    if (got < 0 || (size_t)got < raw_size) return false;
    uint8_t *cur = raw;
    for (int k = 0; k < passes; k++) {
        uint32_t pw = p.interlace ? (p.w > sx[k] ? (p.w - sx[k] + dx[k] - 1) / dx[k] : 0) : p.w;
        uint32_t ph = p.interlace ? (p.h > sy[k] ? (p.h - sy[k] + dy[k] - 1) / dy[k] : 0) : p.h;
        if (!pw || !ph) continue;
        size_t stride = (pw * bits_pp + 7) / 8;
        if (!unfilter(cur, ph, stride, bpp)) return false;
        for (uint32_t y = 0; y < ph; y++) {
            const uint8_t *row = cur + (size_t)y * (stride + 1) + 1;
            uint32_t oy = p.interlace ? sy[k] + y * dy[k] : y;
            for (uint32_t x = 0; x < pw; x++) {
                uint32_t ox = p.interlace ? sx[k] + x * dx[k] : x;
                argb[(size_t)oy * p.w + ox] = sample(&p, row, x);
            }
        }
        cur += (size_t)ph * (stride + 1);
    }
    return true;
}
