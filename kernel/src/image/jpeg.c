/*
 * ojjyOS v3 - Baseline JPEG decoder (see jpeg.h)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "image/jpeg.h"
#include "string.h"

typedef struct {
    uint8_t bits[17];           /* number of codes of each length 1..16 */
    uint8_t vals[256];
    int32_t mincode[17], maxcode[18], valptr[17];
    bool present;
} Huff;

typedef struct {
    int id, h, v, tq;           /* component id, sampling, quant table */
    int td, ta;                 /* DC / AC Huffman table */
    int dc_pred;
} Comp;

typedef struct {
    const uint8_t *p, *end;
    uint32_t acc;               /* bit accumulator */
    int nbits;
    bool marker_hit;
    int width, height, ncomp, hmax, vmax, restart;
    uint16_t q[4][64];
    Huff dc[4], ac[4];
    Comp comp[3];
    bool baseline_ok;
} Jpeg;

static const uint8_t zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

/* cos(k*pi/16) * 4096, k = 0..8 */
static const int16_t cos16[9] = { 4096, 4017, 3784, 3406, 2896, 2276, 1567, 799, 0 };
static int32_t idct_tab[8][8];  /* [x][u] = C(u) * cos((2x+1)u pi/16) * 4096 */
static bool idct_ready;

static int32_t cosk(int k)      /* cos(k*pi/16) for any k, Q12 */
{
    k %= 32;
    if (k < 0) k += 32;
    if (k <= 8) return cos16[k];
    if (k <= 16) return -cos16[16 - k];
    if (k <= 24) return -cos16[k - 16];
    return cos16[32 - k];
}

static void idct_init(void)
{
    for (int x = 0; x < 8; x++)
        for (int u = 0; u < 8; u++) {
            int32_t c = cosk((2 * x + 1) * u);
            idct_tab[x][u] = u == 0 ? c * 2896 / 4096 : c;     /* C(0) = 1/sqrt(2) */
        }
    idct_ready = true;
}

/* 8x8 inverse DCT of dequantised coefficients (natural order) -> pixels. */
static void idct8x8(const int32_t *in, uint8_t *out)
{
    int32_t tmp[64];
    for (int u = 0; u < 8; u++) {                /* columns: v (rows of in) */
        for (int y = 0; y < 8; y++) {
            int64_t s = 0;
            for (int v = 0; v < 8; v++) {
                int32_t c = in[v * 8 + u];
                if (c) s += (int64_t)c * idct_tab[y][v];
            }
            tmp[y * 8 + u] = (int32_t)(s >> 8);  /* Q4 */
        }
    }
    for (int y = 0; y < 8; y++) {
        const int32_t *row = tmp + y * 8;
        for (int x = 0; x < 8; x++) {
            int64_t s = 0;
            for (int u = 0; u < 8; u++) if (row[u]) s += (int64_t)row[u] * idct_tab[x][u];
            /* /4 for the 2-D 1/4 factor, Q4 * Q12 = Q16 */
            int32_t val = (int32_t)((s + (1 << 17)) >> 18) + 128;
            out[y * 8 + x] = (uint8_t)(val < 0 ? 0 : val > 255 ? 255 : val);
        }
    }
}

static bool build_huff(Huff *h)
{
    int32_t code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k;
        h->mincode[l] = code;
        code += h->bits[l];
        k += h->bits[l];
        h->maxcode[l] = h->bits[l] ? code - 1 : -1;
        if (code > (1 << l)) return false;
        code <<= 1;
    }
    h->maxcode[17] = 0x7FFFFFFF;
    h->present = true;
    return k <= 256;
}

static int get_bit(Jpeg *j)
{
    if (!j->nbits) {
        if (j->marker_hit || j->p >= j->end) { j->marker_hit = true; return 0; }
        uint8_t b = *j->p++;
        if (b == 0xFF) {
            if (j->p < j->end && *j->p == 0x00) j->p++;
            else { j->p--; j->marker_hit = true; return 0; }  /* a marker: stop feeding bits */
        }
        j->acc = b;
        j->nbits = 8;
    }
    j->nbits--;
    return (int)((j->acc >> j->nbits) & 1);
}

static int get_bits(Jpeg *j, int n)
{
    int v = 0;
    while (n--) v = (v << 1) | get_bit(j);
    return v;
}

static int decode_huff(Jpeg *j, const Huff *h)
{
    int32_t code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | get_bit(j);
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l]) {
            int idx = h->valptr[l] + code - h->mincode[l];
            return idx >= 0 && idx < 256 ? h->vals[idx] : -1;
        }
    }
    return -1;
}

static int extend(int v, int n) { return n && v < (1 << (n - 1)) ? v - (1 << n) + 1 : v; }

static bool decode_block(Jpeg *j, Comp *c, uint8_t *pixels)
{
    int32_t coef[64];
    memset(coef, 0, sizeof(coef));
    const uint16_t *q = j->q[c->tq];
    int t = decode_huff(j, &j->dc[c->td]);
    if (t < 0 || t > 11) return false;
    int diff = t ? extend(get_bits(j, t), t) : 0;
    c->dc_pred += diff;
    coef[0] = c->dc_pred * q[0];
    for (int k = 1; k < 64;) {
        int rs = decode_huff(j, &j->ac[c->ta]);
        if (rs < 0) return false;
        int r = rs >> 4, s = rs & 15;
        if (!s) {
            if (r != 15) break;                 /* EOB */
            k += 16;
            continue;
        }
        k += r;
        if (k > 63) return false;
        coef[zigzag[k]] = extend(get_bits(j, s), s) * q[k];
        k++;
    }
    idct8x8(coef, pixels);
    return true;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

static bool parse_headers(Jpeg *j, const uint8_t *data, size_t len, bool want_scan)
{
    memset(j, 0, sizeof(*j));
    if (len < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;
    const uint8_t *p = data + 2, *end = data + len;
    while (p + 4 <= end) {
        if (p[0] != 0xFF) return false;
        uint8_t m = p[1];
        if (m == 0xFF) { p++; continue; }
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { p += 2; continue; }
        if (m == 0xD9) return false;
        uint16_t L = be16(p + 2);
        if (L < 2 || p + 2 + L > end) return false;
        const uint8_t *s = p + 4, *se = p + 2 + L;
        switch (m) {
        case 0xC0: case 0xC1:                   /* baseline / extended sequential */
            if (L < 8 || s[0] != 8) return false;
            j->height = be16(s + 1); j->width = be16(s + 3); j->ncomp = s[5];
            if (!j->width || !j->height || j->width > 8192 || j->height > 8192) return false;
            if (j->ncomp != 1 && j->ncomp != 3) return false;
            if (L < 8 + 3 * j->ncomp) return false;
            for (int i = 0; i < j->ncomp; i++) {
                Comp *c = &j->comp[i];
                c->id = s[6 + i * 3]; c->h = s[7 + i * 3] >> 4; c->v = s[7 + i * 3] & 15; c->tq = s[8 + i * 3];
                if (c->h < 1 || c->h > 2 || c->v < 1 || c->v > 2 || c->tq > 3) return false;
                if (c->h > j->hmax) j->hmax = c->h;
                if (c->v > j->vmax) j->vmax = c->v;
            }
            j->baseline_ok = true;
            if (!want_scan) return true;
            break;
        case 0xC2: case 0xC3: case 0xC5: case 0xC6: case 0xC7: case 0xC9: case 0xCA:
        case 0xCB: case 0xCD: case 0xCE: case 0xCF:
            return false;                       /* progressive / lossless / arithmetic */
        case 0xC4:                              /* DHT */
            while (s + 17 <= se) {
                int tc = s[0] >> 4, th = s[0] & 15;
                if (tc > 1 || th > 3) return false;
                Huff *h = tc ? &j->ac[th] : &j->dc[th];
                int total = 0;
                for (int l = 1; l <= 16; l++) { h->bits[l] = s[l]; total += s[l]; }
                if (total > 256 || s + 17 + total > se) return false;
                memcpy(h->vals, s + 17, (size_t)total);
                if (!build_huff(h)) return false;
                s += 17 + total;
            }
            break;
        case 0xDB:                              /* DQT */
            while (s < se) {
                int pq = s[0] >> 4, tq = s[0] & 15;
                if (tq > 3 || pq > 1 || s + 1 + 64 * (pq + 1) > se) return false;
                for (int k = 0; k < 64; k++)
                    j->q[tq][k] = pq ? be16(s + 1 + 2 * k) : s[1 + k];
                s += 1 + 64 * (pq + 1);
            }
            break;
        case 0xDD:                              /* DRI */
            if (L < 4) return false;
            j->restart = be16(s);
            break;
        case 0xDA: {                            /* SOS */
            if (!j->baseline_ok) return false;
            int ns = s[0];
            if (ns != j->ncomp || L < 6 + 2 * ns) return false;
            for (int i = 0; i < ns; i++) {
                int cid = s[1 + 2 * i], tables = s[2 + 2 * i];
                Comp *c = NULL;
                for (int k = 0; k < j->ncomp; k++) if (j->comp[k].id == cid) c = &j->comp[k];
                if (!c) return false;
                c->td = tables >> 4; c->ta = tables & 15;
                if (c->td > 3 || c->ta > 3 || !j->dc[c->td].present || !j->ac[c->ta].present) return false;
            }
            j->p = se;
            j->end = end;
            return true;
        }
        default:
            break;                              /* APPn, COM, ... */
        }
        p = se;
    }
    return false;
}

bool jpeg_info(const uint8_t *data, size_t len, int *width, int *height)
{
    static Jpeg j;
    if (!data || !parse_headers(&j, data, len, false)) return false;
    if (width) *width = j.width;
    if (height) *height = j.height;
    return true;
}

static inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

bool jpeg_decode(const uint8_t *data, size_t len, uint32_t *out, size_t out_pixels)
{
    static Jpeg j;
    if (!data || !out || !parse_headers(&j, data, len, true)) return false;
    if ((size_t)j.width * (size_t)j.height > out_pixels) return false;
    if (!idct_ready) idct_init();

    int mcu_w = 8 * j.hmax, mcu_h = 8 * j.vmax;
    int mcus_x = (j.width + mcu_w - 1) / mcu_w, mcus_y = (j.height + mcu_h - 1) / mcu_h;
    static uint8_t blocks[3][4][64];            /* up to 2x2 blocks per component */
    int todo = j.restart;

    for (int my = 0; my < mcus_y; my++) {
        for (int mx = 0; mx < mcus_x; mx++) {
            if (j.restart && todo == 0) {
                /* Expect RSTn: realign to the byte, skip the marker, reset DC. */
                j.nbits = 0;
                while (j.p + 1 < j.end && !(j.p[0] == 0xFF && j.p[1] >= 0xD0 && j.p[1] <= 0xD7)) j.p++;
                if (j.p + 1 >= j.end) return false;
                j.p += 2;
                j.marker_hit = false;
                for (int c = 0; c < j.ncomp; c++) j.comp[c].dc_pred = 0;
                todo = j.restart;
            }
            for (int c = 0; c < j.ncomp; c++) {
                Comp *cp = &j.comp[c];
                for (int by = 0; by < cp->v; by++)
                    for (int bx = 0; bx < cp->h; bx++)
                        if (!decode_block(&j, cp, blocks[c][by * cp->h + bx])) return false;
            }
            if (j.restart) todo--;
            /* Colour convert this MCU (nearest chroma upsampling). */
            for (int py = 0; py < mcu_h; py++) {
                int y = my * mcu_h + py;
                if (y >= j.height) break;
                uint32_t *row = out + (size_t)y * (size_t)j.width;
                for (int px = 0; px < mcu_w; px++) {
                    int x = mx * mcu_w + px;
                    if (x >= j.width) break;
                    int s[3];
                    for (int c = 0; c < j.ncomp; c++) {
                        Comp *cp = &j.comp[c];
                        int sx = px * cp->h / j.hmax, sy = py * cp->v / j.vmax;
                        s[c] = blocks[c][(sy / 8) * cp->h + (sx / 8)][(sy % 8) * 8 + (sx % 8)];
                    }
                    if (j.ncomp == 1) { row[x] = (uint32_t)s[0] * 0x010101u; continue; }
                    int Y = s[0] << 10, cb = s[1] - 128, cr = s[2] - 128;
                    int r = (Y + 1436 * cr + 512) >> 10;
                    int g = (Y - 352 * cb - 731 * cr + 512) >> 10;
                    int b = (Y + 1815 * cb + 512) >> 10;
                    row[x] = ((uint32_t)clamp8(r) << 16) | ((uint32_t)clamp8(g) << 8) | clamp8(b);
                }
            }
        }
    }
    return true;
}
