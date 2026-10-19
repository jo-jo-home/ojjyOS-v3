/*
 * ojjyOS v3 - GIF decoder (first frame): global/local colour tables,
 * transparency (Graphic Control Extension), interlacing, variable-width
 * LZW. Untrusted input: every read is bounded; a bad file returns false.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "image/image.h"
#include "string.h"

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool gif_info(const uint8_t *d, size_t len, int *w, int *h)
{
    if (len < 13 || memcmp(d, "GIF8", 4) != 0) return false;
    *w = le16(d + 6);
    *h = le16(d + 8);
    return *w > 0 && *h > 0;
}

bool gif_decode(const uint8_t *d, size_t len, uint32_t *argb, size_t pixels, uint8_t *scratch, size_t scratch_size)
{
    int W, H;
    if (!gif_info(d, len, &W, &H) || (size_t)W * (size_t)H > pixels) return false;
    uint32_t gpal[256], lpal[256];
    int gsize = 0;
    size_t p = 13;
    uint8_t flags = d[10];
    uint32_t bg = 0;
    if (flags & 0x80) {
        gsize = 2 << (flags & 7);
        if (p + (size_t)gsize * 3 > len) return false;
        for (int i = 0; i < gsize; i++) gpal[i] = 0xFF000000u | ((uint32_t)d[p + i * 3] << 16) | ((uint32_t)d[p + i * 3 + 1] << 8) | d[p + i * 3 + 2];
        p += (size_t)gsize * 3;
    }
    (void)bg;
    for (size_t i = 0; i < (size_t)W * (size_t)H; i++) argb[i] = 0;
    int transparent = -1;
    while (p < len) {
        uint8_t b = d[p++];
        if (b == 0x3B) break;                           /* trailer */
        if (b == 0x21) {                                /* extension */
            if (p >= len) return false;
            uint8_t label = d[p++];
            if (label == 0xF9 && p + 5 < len && d[p] == 4) {
                if (d[p + 1] & 1) transparent = d[p + 4];
            }
            while (p < len && d[p]) { p += 1 + d[p]; }
            p++;
            continue;
        }
        if (b != 0x2C) return false;                    /* image descriptor */
        if (p + 9 > len) return false;
        int ix = le16(d + p), iy = le16(d + p + 2), iw = le16(d + p + 4), ih = le16(d + p + 6);
        uint8_t lf = d[p + 8];
        p += 9;
        uint32_t *pal = gpal;
        int psize = gsize;
        if (lf & 0x80) {
            psize = 2 << (lf & 7);
            if (p + (size_t)psize * 3 > len) return false;
            for (int i = 0; i < psize; i++) lpal[i] = 0xFF000000u | ((uint32_t)d[p + i * 3] << 16) | ((uint32_t)d[p + i * 3 + 1] << 8) | d[p + i * 3 + 2];
            p += (size_t)psize * 3;
            pal = lpal;
        }
        if (!psize || p >= len) return false;
        bool interlaced = (lf & 0x40) != 0;
        int min_code = d[p++];
        if (min_code < 2 || min_code > 11) return false;
        /* LZW tables live in scratch: prefix (uint16), suffix, stack, output indices */
        size_t npix = (size_t)iw * (size_t)ih;
        if (scratch_size < 4096 * 2 + 4096 + 4097 + npix) return false;
        uint16_t *prefix = (uint16_t *)scratch;
        uint8_t *suffix = scratch + 4096 * 2;
        uint8_t *stack = suffix + 4096;
        uint8_t *out = stack + 4097;
        int clear = 1 << min_code, eoi = clear + 1;
        int code_size = min_code + 1, next = eoi + 1, old = -1;
        uint8_t first = 0;
        size_t o = 0;
        uint32_t bitbuf = 0;
        int bitcnt = 0;
        size_t block_left = 0;
        for (int i = 0; i < clear; i++) { prefix[i] = 0xFFFF; suffix[i] = (uint8_t)i; }
        bool done = false;
        while (!done && o < npix) {
            while (bitcnt < code_size) {
                if (!block_left) {
                    if (p >= len) { done = true; break; }
                    block_left = d[p++];
                    if (!block_left) { done = true; break; }
                }
                if (p >= len) { done = true; break; }
                bitbuf |= (uint32_t)d[p++] << bitcnt;
                bitcnt += 8;
                block_left--;
            }
            if (done) break;
            int code = (int)(bitbuf & ((1u << code_size) - 1));
            bitbuf >>= code_size;
            bitcnt -= code_size;
            if (code == clear) { code_size = min_code + 1; next = eoi + 1; old = -1; continue; }
            if (code == eoi) break;
            int in = code, sp = 0;
            if (old < 0) {
                if (code >= clear) return false;
                out[o++] = (uint8_t)code;
                first = (uint8_t)code;
                old = code;
                continue;
            }
            if (code >= next) {
                if (code > next) return false;
                stack[sp++] = first;
                code = old;
            }
            while (code >= clear) {
                if (sp >= 4096 || code >= 4096) return false;
                stack[sp++] = suffix[code];
                code = prefix[code];
                if (code == 0xFFFF) return false;
            }
            first = suffix[code];
            stack[sp++] = first;
            while (sp && o < npix) out[o++] = stack[--sp];
            if (next < 4096) {
                prefix[next] = (uint16_t)old;
                suffix[next] = first;
                next++;
                if (next == (1 << code_size) && code_size < 12) code_size++;
            }
            old = in;
        }
        /* skip remaining sub-blocks */
        while (block_left && p < len) { p += block_left; block_left = 0; }
        while (p < len && d[p]) p += 1 + d[p];
        /* write the frame */
        static const int istart[4] = {0, 4, 2, 1}, istep[4] = {8, 8, 4, 2};
        size_t k = 0;
        for (int pass = 0; pass < (interlaced ? 4 : 1); pass++) {
            for (int y = interlaced ? istart[pass] : 0; y < ih; y += interlaced ? istep[pass] : 1) {
                for (int x = 0; x < iw; x++, k++) {
                    if (k >= o) return true;
                    int idx = out[k];
                    int X = ix + x, Y = iy + y;
                    if (X >= W || Y >= H) continue;
                    argb[(size_t)Y * (size_t)W + (size_t)X] = idx >= psize ? 0 : idx == transparent ? (pal[idx] & 0xFFFFFF) : pal[idx];
                }
            }
        }
        return true;
    }
    return false;
}
