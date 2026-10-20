/*
 * ojjyOS v3 - WebP decoder: RIFF container (simple and extended/VP8X),
 * lossy VP8 key frames (RFC 6386), lossless VP8L, and ALPH alpha (raw or
 * VP8L-coded, with its spatial filters). Animations show their first frame.
 *
 * Written for ojjyOS from the specifications; the VP8 probability tables
 * come from RFC 6386 (webp_tables.h). Untrusted input: every read is
 * bounded, a corrupt stream returns false, and all working memory comes
 * from the caller's scratch buffer.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "image/image.h"
#include "image/webp_tables.h"
#include "string.h"

/* ------------------------------------------------------------------ */
/* Scratch allocation                                                  */
/* ------------------------------------------------------------------ */

typedef struct { uint8_t *base; size_t size, used; } Pool;

static void *palloc(Pool *p, size_t n)
{
    size_t start = (p->used + 15) & ~(size_t)15;
    if (n > p->size || start > p->size - n) return NULL;
    p->used = start + n;
    memset(p->base + start, 0, n);
    return p->base + start;
}

static uint32_t le24(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16); }
static uint32_t le32(const uint8_t *p) { return le24(p) | ((uint32_t)p[3] << 24); }

/* ------------------------------------------------------------------ */
/* VP8 boolean decoder                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    const uint8_t *p, *end;
    uint32_t value, range;
    int bit_count;
} Bool;

static uint8_t next_byte(Bool *b) { return b->p < b->end ? *b->p++ : 0; }

static void bool_init(Bool *b, const uint8_t *p, size_t len)
{
    b->p = p;
    b->end = p + len;
    b->value = ((uint32_t)next_byte(b) << 8) | next_byte(b);
    b->range = 255;
    b->bit_count = 0;
}

static int get_bit(Bool *b, int prob)
{
    uint32_t split = 1 + (((b->range - 1) * (uint32_t)prob) >> 8);
    uint32_t big = split << 8;
    int r;
    if (b->value >= big) { r = 1; b->range -= split; b->value -= big; }
    else { r = 0; b->range = split; }
    while (b->range < 128) {
        b->value <<= 1;
        b->range <<= 1;
        if (++b->bit_count == 8) { b->bit_count = 0; b->value |= next_byte(b); }
    }
    return r;
}

static int get_value(Bool *b, int n)
{
    int v = 0;
    while (n--) v = (v << 1) | get_bit(b, 128);
    return v;
}

static int get_signed_value(Bool *b, int n)
{
    int v = get_value(b, n);
    return get_bit(b, 128) ? -v : v;
}

/* ------------------------------------------------------------------ */
/* VP8 lossy                                                           */
/* ------------------------------------------------------------------ */

enum { B_DC = 0, B_TM, B_VE, B_HE, B_RD, B_VR, B_LD, B_VL, B_HD, B_HU };
#define BPS 32

static const uint8_t kZigzag[16] = {0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15};
static const uint8_t kBands[17] = {0, 1, 2, 3, 6, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7, 0};
static const uint8_t kCat3[] = {173, 148, 140, 0};
static const uint8_t kCat4[] = {176, 155, 140, 135, 0};
static const uint8_t kCat5[] = {180, 157, 141, 134, 130, 0};
static const uint8_t kCat6[] = {254, 254, 243, 230, 196, 177, 153, 140, 133, 130, 129, 0};
static const uint8_t *const kCat3456[] = {kCat3, kCat4, kCat5, kCat6};

typedef struct {
    int16_t y1[2], y2[2], uv[2];
} Quant;

typedef struct {
    uint8_t limit, ilevel, hev, inner;
} FInfo;

typedef struct {
    int width, height, mbw, mbh;
    uint8_t proba[4][8][3][11];
    uint8_t seg_proba[3];
    bool use_segment, update_map, absolute_delta;
    int8_t seg_quant[4], seg_filter[4];
    int filter_type, filter_level, sharpness;
    bool use_lf_delta;
    int ref_lf_delta[4], mode_lf_delta[4];
    bool use_skip;
    int skip_p;
    Quant dq[4];
    FInfo fstr[4][2];
    Bool br;                        /* first partition */
    Bool parts[8];
    int nparts;
    /* frame planes (unfiltered while decoding) */
    uint8_t *y, *u, *v;
    int ystride, uvstride;
    /* per-MB state */
    uint8_t *intra_t;               /* 4 per MB column */
    uint8_t intra_l[4];
    uint8_t *nz_t, *nzdc_t;         /* top non-zero contexts per MB column */
    uint8_t nz_l, nzdc_l;
    FInfo *finfo;                   /* per MB, whole frame */
    uint8_t *top_y, *top_u, *top_v; /* unfiltered bottom rows of the previous MB row */
} Vp8;

static int clip_q(int v, int max) { return v < 0 ? 0 : v > max ? max : v; }

static bool vp8_headers(Vp8 *d, const uint8_t *data, size_t len)
{
    if (len < 10) return false;
    uint32_t bits = le24(data);
    bool key = !(bits & 1);
    int profile = (bits >> 1) & 7;
    bool show = (bits >> 4) & 1;
    uint32_t part0 = bits >> 5;
    if (!key || profile > 3 || !show) return false;
    if (data[3] != 0x9d || data[4] != 0x01 || data[5] != 0x2a) return false;
    d->width = (data[6] | (data[7] << 8)) & 0x3fff;
    d->height = (data[8] | (data[9] << 8)) & 0x3fff;
    if (!d->width || !d->height) return false;
    d->mbw = (d->width + 15) >> 4;
    d->mbh = (d->height + 15) >> 4;
    const uint8_t *buf = data + 10;
    size_t size = len - 10;
    if (part0 > size) return false;
    bool_init(&d->br, buf, part0);
    buf += part0;
    size -= part0;
    Bool *br = &d->br;
    get_value(br, 1);                       /* colour space */
    get_value(br, 1);                       /* clamping type */
    /* segments */
    d->use_segment = get_value(br, 1);
    memset(d->seg_proba, 255, sizeof(d->seg_proba));
    memset(d->seg_quant, 0, sizeof(d->seg_quant));
    memset(d->seg_filter, 0, sizeof(d->seg_filter));
    d->update_map = false;
    if (d->use_segment) {
        d->update_map = get_value(br, 1);
        if (get_value(br, 1)) {
            d->absolute_delta = get_value(br, 1);
            for (int s = 0; s < 4; s++) d->seg_quant[s] = (int8_t)(get_value(br, 1) ? get_signed_value(br, 7) : 0);
            for (int s = 0; s < 4; s++) d->seg_filter[s] = (int8_t)(get_value(br, 1) ? get_signed_value(br, 6) : 0);
        }
        if (d->update_map)
            for (int s = 0; s < 3; s++) d->seg_proba[s] = (uint8_t)(get_value(br, 1) ? get_value(br, 8) : 255);
    }
    /* filter */
    int simple = get_value(br, 1);
    d->filter_level = get_value(br, 6);
    d->sharpness = get_value(br, 3);
    d->use_lf_delta = get_value(br, 1);
    memset(d->ref_lf_delta, 0, sizeof(d->ref_lf_delta));
    memset(d->mode_lf_delta, 0, sizeof(d->mode_lf_delta));
    if (d->use_lf_delta && get_value(br, 1)) {
        for (int i = 0; i < 4; i++) if (get_value(br, 1)) d->ref_lf_delta[i] = get_signed_value(br, 6);
        for (int i = 0; i < 4; i++) if (get_value(br, 1)) d->mode_lf_delta[i] = get_signed_value(br, 6);
    }
    d->filter_type = d->filter_level == 0 ? 0 : simple ? 1 : 2;
    /* partitions */
    int last = (1 << get_value(br, 2)) - 1;
    d->nparts = last + 1;
    if (size < (size_t)(3 * last)) return false;
    const uint8_t *sz = buf, *pstart = buf + 3 * last;
    size_t left = size - 3 * (size_t)last;
    for (int p = 0; p < last; p++) {
        size_t ps = le24(sz);
        if (ps > left) ps = left;
        bool_init(&d->parts[p], pstart, ps);
        pstart += ps;
        left -= ps;
        sz += 3;
    }
    bool_init(&d->parts[last], pstart, left);
    /* quantisers */
    int base_q = get_value(br, 7);
    int dqy1_dc = get_value(br, 1) ? get_signed_value(br, 4) : 0;
    int dqy2_dc = get_value(br, 1) ? get_signed_value(br, 4) : 0;
    int dqy2_ac = get_value(br, 1) ? get_signed_value(br, 4) : 0;
    int dquv_dc = get_value(br, 1) ? get_signed_value(br, 4) : 0;
    int dquv_ac = get_value(br, 1) ? get_signed_value(br, 4) : 0;
    for (int i = 0; i < 4; i++) {
        int q = base_q;
        if (d->use_segment) { q = d->seg_quant[i]; if (!d->absolute_delta) q += base_q; }
        else if (i > 0) { d->dq[i] = d->dq[0]; continue; }
        Quant *m = &d->dq[i];
        m->y1[0] = vp8_dc_table[clip_q(q + dqy1_dc, 127)];
        m->y1[1] = (int16_t)vp8_ac_table[clip_q(q, 127)];
        m->y2[0] = (int16_t)(vp8_dc_table[clip_q(q + dqy2_dc, 127)] * 2);
        m->y2[1] = (int16_t)((vp8_ac_table[clip_q(q + dqy2_ac, 127)] * 101581) >> 16);
        if (m->y2[1] < 8) m->y2[1] = 8;
        m->uv[0] = vp8_dc_table[clip_q(q + dquv_dc, 117)];
        m->uv[1] = (int16_t)vp8_ac_table[clip_q(q + dquv_ac, 127)];
    }
    get_value(br, 1);                       /* refresh entropy probs: ignored */
    /* token probabilities */
    int k = 0;
    for (int t = 0; t < 4; t++)
        for (int b = 0; b < 8; b++)
            for (int c = 0; c < 3; c++)
                for (int p = 0; p < 11; p++, k++)
                    d->proba[t][b][c][p] = (uint8_t)(get_bit(br, vp8_coeffs_update[k]) ? get_value(br, 8) : vp8_coeffs0[k]);
    d->use_skip = get_value(br, 1);
    d->skip_p = d->use_skip ? get_value(br, 8) : 0;
    /* filter strengths per segment and 4x4-ness */
    for (int s = 0; s < 4; s++) {
        int base = d->filter_level;
        if (d->use_segment) { base = d->seg_filter[s]; if (!d->absolute_delta) base += d->filter_level; }
        for (int i4 = 0; i4 <= 1; i4++) {
            FInfo *f = &d->fstr[s][i4];
            int level = base;
            if (d->use_lf_delta) { level += d->ref_lf_delta[0]; if (i4) level += d->mode_lf_delta[0]; }
            level = level < 0 ? 0 : level > 63 ? 63 : level;
            if (level > 0) {
                int il = level;
                if (d->sharpness > 0) {
                    il >>= d->sharpness > 4 ? 2 : 1;
                    if (il > 9 - d->sharpness) il = 9 - d->sharpness;
                }
                if (il < 1) il = 1;
                f->ilevel = (uint8_t)il;
                f->limit = (uint8_t)(2 * level + il);
                f->hev = (uint8_t)(level >= 40 ? 2 : level >= 15 ? 1 : 0);
            } else {
                f->limit = 0;
            }
            f->inner = (uint8_t)i4;
        }
    }
    return true;
}

static int large_value(Bool *br, const uint8_t *p)
{
    int v;
    if (!get_bit(br, p[3])) {
        v = !get_bit(br, p[4]) ? 2 : 3 + get_bit(br, p[5]);
    } else if (!get_bit(br, p[6])) {
        if (!get_bit(br, p[7])) v = 5 + get_bit(br, 159);
        else { v = 7 + 2 * get_bit(br, 165); v += get_bit(br, 145); }
    } else {
        int bit1 = get_bit(br, p[8]);
        int bit0 = get_bit(br, p[9 + bit1]);
        int cat = 2 * bit1 + bit0;
        v = 0;
        for (const uint8_t *tab = kCat3456[cat]; *tab; tab++) v += v + get_bit(br, *tab);
        v += 3 + (8 << cat);
    }
    return v;
}

/* Returns the index of the last non-zero coefficient plus one. */
static int get_coeffs(Bool *br, uint8_t (*bands)[3][11], int ctx, const int16_t *dq, int n, int16_t *out)
{
    const uint8_t *p = bands[kBands[n]][ctx];
    for (; n < 16; n++) {
        if (!get_bit(br, p[0])) return n;
        while (!get_bit(br, p[1])) {
            if (++n == 16) return 16;
            p = bands[kBands[n]][0];
        }
        int v;
        if (!get_bit(br, p[2])) { v = 1; p = bands[kBands[n + 1]][1]; }
        else { v = large_value(br, p); p = bands[kBands[n + 1]][2]; }
        if (get_bit(br, 128)) v = -v;
        out[kZigzag[n]] = (int16_t)(v * dq[n > 0]);
    }
    return 16;
}

static void wht(const int16_t *in, int16_t *out)
{
    int tmp[16];
    for (int i = 0; i < 4; i++) {
        int a0 = in[0 + i] + in[12 + i], a1 = in[4 + i] + in[8 + i];
        int a2 = in[4 + i] - in[8 + i], a3 = in[0 + i] - in[12 + i];
        tmp[0 + i] = a0 + a1;
        tmp[8 + i] = a0 - a1;
        tmp[4 + i] = a3 + a2;
        tmp[12 + i] = a3 - a2;
    }
    for (int i = 0; i < 4; i++) {
        int dc = tmp[0 + i * 4] + 3;
        int a0 = dc + tmp[3 + i * 4], a1 = tmp[1 + i * 4] + tmp[2 + i * 4];
        int a2 = tmp[1 + i * 4] - tmp[2 + i * 4], a3 = dc - tmp[3 + i * 4];
        out[0] = (int16_t)((a0 + a1) >> 3);
        out[16] = (int16_t)((a3 + a2) >> 3);
        out[32] = (int16_t)((a0 - a1) >> 3);
        out[48] = (int16_t)((a3 - a2) >> 3);
        out += 64;
    }
}

static uint8_t clip8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
#define MUL1(a) ((((a) * 20091) >> 16) + (a))
#define MUL2(a) (((a) * 35468) >> 16)

static void idct_add(const int16_t *in, uint8_t *dst)
{
    int C[16], *tmp = C;
    for (int i = 0; i < 4; i++) {
        int a = in[0] + in[8], b = in[0] - in[8];
        int c = MUL2(in[4]) - MUL1(in[12]), d = MUL1(in[4]) + MUL2(in[12]);
        tmp[0] = a + d; tmp[1] = b + c; tmp[2] = b - c; tmp[3] = a - d;
        tmp += 4;
        in++;
    }
    tmp = C;
    for (int i = 0; i < 4; i++) {
        int dc = tmp[0] + 4;
        int a = dc + tmp[8], b = dc - tmp[8];
        int c = MUL2(tmp[4]) - MUL1(tmp[12]), d = MUL1(tmp[4]) + MUL2(tmp[12]);
        dst[0] = clip8(dst[0] + ((a + d) >> 3));
        dst[1] = clip8(dst[1] + ((b + c) >> 3));
        dst[2] = clip8(dst[2] + ((b - c) >> 3));
        dst[3] = clip8(dst[3] + ((a - d) >> 3));
        tmp++;
        dst += BPS;
    }
}

static bool any_nz(const int16_t *c)
{
    for (int i = 0; i < 16; i++) if (c[i]) return true;
    return false;
}

#define AVG3(a, b, c) ((uint8_t)(((a) + 2 * (b) + (c) + 2) >> 2))
#define AVG2(a, b) ((uint8_t)(((a) + (b) + 1) >> 1))
#define DST(x, y) dst[(x) + (y) * BPS]

static void pred_tm(uint8_t *dst, int size)
{
    const uint8_t *top = dst - BPS;
    int tl = top[-1];
    for (int y = 0; y < size; y++) {
        int l = dst[-1];
        for (int x = 0; x < size; x++) dst[x] = clip8(l + top[x] - tl);
        dst += BPS;
    }
}

static void pred16(uint8_t *dst, int mode, int mbx, int mby)
{
    if (mode == B_DC) {
        int dc = 0;
        if (mbx > 0 && mby > 0) { for (int j = 0; j < 16; j++) dc += dst[-1 + j * BPS] + dst[j - BPS]; dc = (dc + 16) >> 5; }
        else if (mby > 0) { for (int j = 0; j < 16; j++) dc += dst[j - BPS]; dc = (dc + 8) >> 4; }
        else if (mbx > 0) { for (int j = 0; j < 16; j++) dc += dst[-1 + j * BPS]; dc = (dc + 8) >> 4; }
        else dc = 0x80;
        for (int j = 0; j < 16; j++) memset(dst + j * BPS, dc, 16);
    } else if (mode == B_TM) pred_tm(dst, 16);
    else if (mode == B_VE) { for (int j = 0; j < 16; j++) memcpy(dst + j * BPS, dst - BPS, 16); }
    else { for (int j = 0; j < 16; j++) memset(dst + j * BPS, dst[-1 + j * BPS], 16); }
}

static void pred8(uint8_t *dst, int mode, int mbx, int mby)
{
    if (mode == B_DC) {
        int dc = 0;
        if (mbx > 0 && mby > 0) { for (int j = 0; j < 8; j++) dc += dst[-1 + j * BPS] + dst[j - BPS]; dc = (dc + 8) >> 4; }
        else if (mby > 0) { for (int j = 0; j < 8; j++) dc += dst[j - BPS]; dc = (dc + 4) >> 3; }
        else if (mbx > 0) { for (int j = 0; j < 8; j++) dc += dst[-1 + j * BPS]; dc = (dc + 4) >> 3; }
        else dc = 0x80;
        for (int j = 0; j < 8; j++) memset(dst + j * BPS, dc, 8);
    } else if (mode == B_TM) pred_tm(dst, 8);
    else if (mode == B_VE) { for (int j = 0; j < 8; j++) memcpy(dst + j * BPS, dst - BPS, 8); }
    else { for (int j = 0; j < 8; j++) memset(dst + j * BPS, dst[-1 + j * BPS], 8); }
}

static void pred4(uint8_t *dst, int mode)
{
    const uint8_t *top = dst - BPS;
    int I = dst[-1], J = dst[-1 + BPS], K = dst[-1 + 2 * BPS], L = dst[-1 + 3 * BPS], X = top[-1];
    int A = top[0], B = top[1], C = top[2], D = top[3], E = top[4], F = top[5], G = top[6], H = top[7];
    switch (mode) {
    case B_DC: {
        int dc = 4;
        for (int i = 0; i < 4; i++) dc += top[i] + dst[-1 + i * BPS];
        dc >>= 3;
        for (int i = 0; i < 4; i++) memset(dst + i * BPS, dc, 4);
        break;
    }
    case B_TM: pred_tm(dst, 4); break;
    case B_VE: {
        uint8_t v[4] = {AVG3(X, A, B), AVG3(A, B, C), AVG3(B, C, D), AVG3(C, D, E)};
        for (int i = 0; i < 4; i++) memcpy(dst + i * BPS, v, 4);
        break;
    }
    case B_HE: {
        int Xp = X;
        memset(dst + 0 * BPS, AVG3(Xp, I, J), 4);
        memset(dst + 1 * BPS, AVG3(I, J, K), 4);
        memset(dst + 2 * BPS, AVG3(J, K, L), 4);
        memset(dst + 3 * BPS, AVG3(K, L, L), 4);
        break;
    }
    case B_RD:
        DST(0, 3) = AVG3(J, K, L);
        DST(1, 3) = DST(0, 2) = AVG3(I, J, K);
        DST(2, 3) = DST(1, 2) = DST(0, 1) = AVG3(X, I, J);
        DST(3, 3) = DST(2, 2) = DST(1, 1) = DST(0, 0) = AVG3(A, X, I);
        DST(3, 2) = DST(2, 1) = DST(1, 0) = AVG3(B, A, X);
        DST(3, 1) = DST(2, 0) = AVG3(C, B, A);
        DST(3, 0) = AVG3(D, C, B);
        break;
    case B_LD:
        DST(0, 0) = AVG3(A, B, C);
        DST(1, 0) = DST(0, 1) = AVG3(B, C, D);
        DST(2, 0) = DST(1, 1) = DST(0, 2) = AVG3(C, D, E);
        DST(3, 0) = DST(2, 1) = DST(1, 2) = DST(0, 3) = AVG3(D, E, F);
        DST(3, 1) = DST(2, 2) = DST(1, 3) = AVG3(E, F, G);
        DST(3, 2) = DST(2, 3) = AVG3(F, G, H);
        DST(3, 3) = AVG3(G, H, H);
        break;
    case B_VR:
        DST(0, 0) = DST(1, 2) = AVG2(X, A);
        DST(1, 0) = DST(2, 2) = AVG2(A, B);
        DST(2, 0) = DST(3, 2) = AVG2(B, C);
        DST(3, 0) = AVG2(C, D);
        DST(0, 3) = AVG3(K, J, I);
        DST(0, 2) = AVG3(J, I, X);
        DST(0, 1) = DST(1, 3) = AVG3(I, X, A);
        DST(1, 1) = DST(2, 3) = AVG3(X, A, B);
        DST(2, 1) = DST(3, 3) = AVG3(A, B, C);
        DST(3, 1) = AVG3(B, C, D);
        break;
    case B_VL:
        DST(0, 0) = AVG2(A, B);
        DST(1, 0) = DST(0, 2) = AVG2(B, C);
        DST(2, 0) = DST(1, 2) = AVG2(C, D);
        DST(3, 0) = DST(2, 2) = AVG2(D, E);
        DST(0, 1) = AVG3(A, B, C);
        DST(1, 1) = DST(0, 3) = AVG3(B, C, D);
        DST(2, 1) = DST(1, 3) = AVG3(C, D, E);
        DST(3, 1) = DST(2, 3) = AVG3(D, E, F);
        DST(3, 2) = AVG3(E, F, G);
        DST(3, 3) = AVG3(F, G, H);
        break;
    case B_HU:
        DST(0, 0) = AVG2(I, J);
        DST(2, 0) = DST(0, 1) = AVG2(J, K);
        DST(2, 1) = DST(0, 2) = AVG2(K, L);
        DST(1, 0) = AVG3(I, J, K);
        DST(3, 0) = DST(1, 1) = AVG3(J, K, L);
        DST(3, 1) = DST(1, 2) = AVG3(K, L, L);
        DST(3, 2) = DST(2, 2) = DST(0, 3) = DST(1, 3) = DST(2, 3) = DST(3, 3) = (uint8_t)L;
        break;
    default:            /* B_HD */
        DST(0, 0) = DST(2, 1) = AVG2(I, X);
        DST(0, 1) = DST(2, 2) = AVG2(J, I);
        DST(0, 2) = DST(2, 3) = AVG2(K, J);
        DST(0, 3) = AVG2(L, K);
        DST(3, 0) = AVG3(A, B, C);
        DST(2, 0) = AVG3(X, A, B);
        DST(1, 0) = DST(3, 1) = AVG3(I, X, A);
        DST(1, 1) = DST(3, 2) = AVG3(J, I, X);
        DST(1, 2) = DST(3, 3) = AVG3(K, J, I);
        DST(1, 3) = AVG3(L, K, J);
        break;
    }
}

/* ---- Loop filter ---- */

static int abs_i(int v) { return v < 0 ? -v : v; }
static int sclip1(int v) { return v < -128 ? -128 : v > 127 ? 127 : v; }
static int sclip2(int v) { return v < -16 ? -16 : v > 15 ? 15 : v; }

static void filter2(uint8_t *p, int step)
{
    int p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step];
    int a = 3 * (q0 - p0) + sclip1(p1 - q1);
    int a1 = sclip2((a + 4) >> 3), a2 = sclip2((a + 3) >> 3);
    p[-step] = clip8(p0 + a2);
    p[0] = clip8(q0 - a1);
}

static void filter4(uint8_t *p, int step)
{
    int p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step];
    int a = 3 * (q0 - p0);
    int a1 = sclip2((a + 4) >> 3), a2 = sclip2((a + 3) >> 3), a3 = (a1 + 1) >> 1;
    p[-2 * step] = clip8(p1 + a3);
    p[-step] = clip8(p0 + a2);
    p[0] = clip8(q0 - a1);
    p[step] = clip8(q1 - a3);
}

static void filter6(uint8_t *p, int step)
{
    int p2 = p[-3 * step], p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step], q2 = p[2 * step];
    int a = sclip1(3 * (q0 - p0) + sclip1(p1 - q1));
    int a1 = (27 * a + 63) >> 7, a2 = (18 * a + 63) >> 7, a3 = (9 * a + 63) >> 7;
    p[-3 * step] = clip8(p2 + a3);
    p[-2 * step] = clip8(p1 + a2);
    p[-step] = clip8(p0 + a1);
    p[0] = clip8(q0 - a1);
    p[step] = clip8(q1 - a2);
    p[2 * step] = clip8(q2 - a3);
}

static bool hev(const uint8_t *p, int step, int t)
{
    return abs_i(p[-2 * step] - p[-step]) > t || abs_i(p[step] - p[0]) > t;
}

static bool needs(const uint8_t *p, int step, int t)
{
    return 4 * abs_i(p[-step] - p[0]) + abs_i(p[-2 * step] - p[step]) <= t;
}

static bool needs2(const uint8_t *p, int step, int t, int it)
{
    int p3 = p[-4 * step], p2 = p[-3 * step], p1 = p[-2 * step], p0 = p[-step];
    int q0 = p[0], q1 = p[step], q2 = p[2 * step], q3 = p[3 * step];
    if (4 * abs_i(p0 - q0) + abs_i(p1 - q1) > t) return false;
    return abs_i(p3 - p2) <= it && abs_i(p2 - p1) <= it && abs_i(p1 - p0) <= it &&
           abs_i(q3 - q2) <= it && abs_i(q2 - q1) <= it && abs_i(q1 - q0) <= it;
}

static void simple_filter(uint8_t *p, int hstride, int vstride, int n, int thresh)
{
    int t2 = 2 * thresh + 1;
    for (int i = 0; i < n; i++) { if (needs(p, hstride, t2)) filter2(p, hstride); p += vstride; }
}

static void loop26(uint8_t *p, int hstride, int vstride, int n, int thresh, int ithresh, int hev_t)
{
    int t2 = 2 * thresh + 1;
    for (int i = 0; i < n; i++) {
        if (needs2(p, hstride, t2, ithresh)) { if (hev(p, hstride, hev_t)) filter2(p, hstride); else filter6(p, hstride); }
        p += vstride;
    }
}

static void loop24(uint8_t *p, int hstride, int vstride, int n, int thresh, int ithresh, int hev_t)
{
    int t2 = 2 * thresh + 1;
    for (int i = 0; i < n; i++) {
        if (needs2(p, hstride, t2, ithresh)) { if (hev(p, hstride, hev_t)) filter2(p, hstride); else filter4(p, hstride); }
        p += vstride;
    }
}

static void filter_mb(Vp8 *d, int mbx, int mby)
{
    const FInfo *f = &d->finfo[mby * d->mbw + mbx];
    int limit = f->limit;
    if (!limit) return;
    int ys = d->ystride, us = d->uvstride;
    uint8_t *y = d->y + (size_t)mby * 16 * ys + mbx * 16;
    if (d->filter_type == 1) {
        if (mbx > 0) simple_filter(y, 1, ys, 16, limit + 4);
        if (f->inner) for (int k = 1; k < 4; k++) simple_filter(y + 4 * k, 1, ys, 16, limit);
        if (mby > 0) simple_filter(y, ys, 1, 16, limit + 4);
        if (f->inner) for (int k = 1; k < 4; k++) simple_filter(y + 4 * k * ys, ys, 1, 16, limit);
        return;
    }
    uint8_t *u = d->u + (size_t)mby * 8 * us + mbx * 8, *v = d->v + (size_t)mby * 8 * us + mbx * 8;
    int il = f->ilevel, ht = f->hev;
    if (mbx > 0) {
        loop26(y, 1, ys, 16, limit + 4, il, ht);
        loop26(u, 1, us, 8, limit + 4, il, ht);
        loop26(v, 1, us, 8, limit + 4, il, ht);
    }
    if (f->inner) {
        for (int k = 1; k < 4; k++) loop24(y + 4 * k, 1, ys, 16, limit, il, ht);
        loop24(u + 4, 1, us, 8, limit, il, ht);
        loop24(v + 4, 1, us, 8, limit, il, ht);
    }
    if (mby > 0) {
        loop26(y, ys, 1, 16, limit + 4, il, ht);
        loop26(u, us, 1, 8, limit + 4, il, ht);
        loop26(v, us, 1, 8, limit + 4, il, ht);
    }
    if (f->inner) {
        for (int k = 1; k < 4; k++) loop24(y + 4 * k * ys, ys, 1, 16, limit, il, ht);
        loop24(u + 4 * us, us, 1, 8, limit, il, ht);
        loop24(v + 4 * us, us, 1, 8, limit, il, ht);
    }
}

/* YUV -> RGB (BT.601, the conversion libwebp uses) */
static int mult_hi(int v, int c) { return (v * c) >> 8; }
static uint8_t yuv_clip(int v) { return (uint8_t)((v & ~16383) == 0 ? v >> 6 : v < 0 ? 0 : 255); }

static uint32_t yuv_to_argb(int y, int u, int v)
{
    int r = yuv_clip(mult_hi(y, 19077) + mult_hi(v, 26149) - 14234);
    int g = yuv_clip(mult_hi(y, 19077) - mult_hi(u, 6419) - mult_hi(v, 13320) + 8708);
    int b = yuv_clip(mult_hi(y, 19077) + mult_hi(u, 33050) - 17685);
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static bool vp8_decode(const uint8_t *data, size_t len, uint32_t *argb, size_t pixels, int *ow, int *oh, Pool *pool)
{
    Vp8 *d = (Vp8 *)palloc(pool, sizeof(Vp8));
    if (!d || !vp8_headers(d, data, len)) return false;
    if (ow) *ow = d->width;
    if (oh) *oh = d->height;
    if (!argb) return true;
    if ((size_t)d->width * (size_t)d->height > pixels) return false;
    d->ystride = d->mbw * 16;
    d->uvstride = d->mbw * 8;
    d->y = (uint8_t *)palloc(pool, (size_t)d->ystride * d->mbh * 16);
    d->u = (uint8_t *)palloc(pool, (size_t)d->uvstride * d->mbh * 8);
    d->v = (uint8_t *)palloc(pool, (size_t)d->uvstride * d->mbh * 8);
    d->intra_t = (uint8_t *)palloc(pool, (size_t)d->mbw * 4);
    d->nz_t = (uint8_t *)palloc(pool, (size_t)d->mbw);
    d->nzdc_t = (uint8_t *)palloc(pool, (size_t)d->mbw);
    d->finfo = (FInfo *)palloc(pool, sizeof(FInfo) * (size_t)d->mbw * d->mbh);
    d->top_y = (uint8_t *)palloc(pool, (size_t)d->mbw * 16 + 4);
    d->top_u = (uint8_t *)palloc(pool, (size_t)d->mbw * 8);
    d->top_v = (uint8_t *)palloc(pool, (size_t)d->mbw * 8);
    if (!d->y || !d->u || !d->v || !d->intra_t || !d->nz_t || !d->nzdc_t || !d->finfo || !d->top_y || !d->top_u || !d->top_v)
        return false;
    /* work buffers with borders: 1 row above, 1..4 columns left, 4 right */
    static uint8_t ybuf[BPS * 17], ubuf[BPS * 9], vbuf[BPS * 9];
    uint8_t *yd = ybuf + BPS + 8, *ud = ubuf + BPS + 8, *vd = vbuf + BPS + 8;
    static int16_t coeffs[384];
    memset(d->intra_t, B_DC, (size_t)d->mbw * 4);
    for (int mby = 0; mby < d->mbh; mby++) {
        Bool *tok = &d->parts[mby & (d->nparts - 1)];
        memset(d->intra_l, B_DC, 4);
        d->nz_l = d->nzdc_l = 0;
        for (int j = 0; j < 16; j++) yd[j * BPS - 1] = 129;
        for (int j = 0; j < 8; j++) { ud[j * BPS - 1] = 129; vd[j * BPS - 1] = 129; }
        if (mby > 0) { yd[-1 - BPS] = ud[-1 - BPS] = vd[-1 - BPS] = 129; }
        else { memset(yd - BPS - 1, 127, 16 + 4 + 1); memset(ud - BPS - 1, 127, 9); memset(vd - BPS - 1, 127, 9); }
        for (int mbx = 0; mbx < d->mbw; mbx++) {
            /* ---- modes (first partition) ---- */
            Bool *br = &d->br;
            int segment = 0;
            if (d->update_map) segment = !get_bit(br, d->seg_proba[0]) ? get_bit(br, d->seg_proba[1]) : get_bit(br, d->seg_proba[2]) + 2;
            int skip = d->use_skip ? get_bit(br, d->skip_p) : 0;
            bool i4x4 = !get_bit(br, 145);
            uint8_t modes[16];
            uint8_t *top = d->intra_t + 4 * mbx;
            if (!i4x4) {
                int ym = get_bit(br, 156) ? (get_bit(br, 128) ? B_TM : B_HE) : (get_bit(br, 163) ? B_VE : B_DC);
                modes[0] = (uint8_t)ym;
                memset(top, ym, 4);
                memset(d->intra_l, ym, 4);
            } else {
                for (int y = 0; y < 4; y++) {
                    int ym = d->intra_l[y];
                    for (int x = 0; x < 4; x++) {
                        const uint8_t *prob = &vp8_bmodes_proba[(top[x] * 10 + ym) * 9];
                        ym = !get_bit(br, prob[0]) ? B_DC : !get_bit(br, prob[1]) ? B_TM : !get_bit(br, prob[2]) ? B_VE :
                             !get_bit(br, prob[3]) ? (!get_bit(br, prob[4]) ? B_HE : (!get_bit(br, prob[5]) ? B_RD : B_VR)) :
                             (!get_bit(br, prob[6]) ? B_LD : (!get_bit(br, prob[7]) ? B_VL : (!get_bit(br, prob[8]) ? B_HD : B_HU)));
                        top[x] = (uint8_t)ym;
                        modes[y * 4 + x] = (uint8_t)ym;
                    }
                    d->intra_l[y] = (uint8_t)ym;
                }
            }
            int uvmode = !get_bit(br, 142) ? B_DC : !get_bit(br, 114) ? B_VE : get_bit(br, 183) ? B_TM : B_HE;
            /* ---- residuals ---- */
            memset(coeffs, 0, sizeof(coeffs));
            const Quant *q = &d->dq[segment];
            bool nonzero = false;
            if (!skip) {
                int first;
                uint8_t (*ac)[3][11];
                if (!i4x4) {
                    int16_t dc[16];
                    memset(dc, 0, sizeof(dc));
                    int ctx = d->nzdc_t[mbx] + d->nzdc_l;
                    int nz = get_coeffs(tok, d->proba[1], ctx, q->y2, 0, dc);
                    d->nzdc_t[mbx] = d->nzdc_l = nz > 0;
                    wht(dc, coeffs);
                    first = 1;
                    ac = d->proba[0];
                } else {
                    first = 0;
                    ac = d->proba[3];
                }
                uint8_t tnz = d->nz_t[mbx] & 0x0f, lnz = d->nz_l & 0x0f;
                int16_t *dst = coeffs;
                for (int y = 0; y < 4; y++) {
                    int l = lnz & 1;
                    for (int x = 0; x < 4; x++) {
                        int ctx = l + (tnz & 1);
                        int nz = get_coeffs(tok, ac, ctx, q->y1, first, dst);
                        l = nz > first;
                        tnz = (uint8_t)((tnz >> 1) | (l << 7));
                        dst += 16;
                    }
                    tnz >>= 4;
                    lnz = (uint8_t)((lnz >> 1) | (l << 7));
                }
                uint32_t out_t = tnz, out_l = lnz >> 4;
                for (int ch = 0; ch < 4; ch += 2) {
                    tnz = (uint8_t)(d->nz_t[mbx] >> (4 + ch));
                    lnz = (uint8_t)(d->nz_l >> (4 + ch));
                    for (int y = 0; y < 2; y++) {
                        int l = lnz & 1;
                        for (int x = 0; x < 2; x++) {
                            int ctx = l + (tnz & 1);
                            int nz = get_coeffs(tok, d->proba[2], ctx, q->uv, 0, dst);
                            l = nz > 0;
                            tnz = (uint8_t)((tnz >> 1) | (l << 3));
                            dst += 16;
                        }
                        tnz >>= 2;
                        lnz = (uint8_t)((lnz >> 1) | (l << 5));
                    }
                    out_t |= (uint32_t)(tnz << 4) << ch;
                    out_l |= (uint32_t)(lnz & 0xf0) << ch;
                }
                d->nz_t[mbx] = (uint8_t)out_t;
                d->nz_l = (uint8_t)out_l;
                for (int i = 0; i < 384; i++) if (coeffs[i]) { nonzero = true; break; }
            } else {
                d->nz_t[mbx] = d->nz_l = 0;
                if (!i4x4) d->nzdc_t[mbx] = d->nzdc_l = 0;
            }
            if (d->filter_type) {
                FInfo *f = &d->finfo[mby * d->mbw + mbx];
                *f = d->fstr[segment][i4x4];
                f->inner |= (uint8_t)nonzero;
            }
            /* ---- reconstruction in the work buffer ---- */
            if (mbx > 0) {
                for (int j = -1; j < 16; j++) memcpy(&yd[j * BPS - 4], &yd[j * BPS + 12], 4);
                for (int j = -1; j < 8; j++) { memcpy(&ud[j * BPS - 4], &ud[j * BPS + 4], 4); memcpy(&vd[j * BPS - 4], &vd[j * BPS + 4], 4); }
            }
            if (mby > 0) {
                memcpy(yd - BPS, d->top_y + mbx * 16, 16);
                memcpy(ud - BPS, d->top_u + mbx * 8, 8);
                memcpy(vd - BPS, d->top_v + mbx * 8, 8);
            }
            if (i4x4) {
                uint8_t *tr = yd - BPS + 16;
                if (mby > 0) {
                    if (mbx >= d->mbw - 1) memset(tr, d->top_y[mbx * 16 + 15], 4);
                    else memcpy(tr, d->top_y + (mbx + 1) * 16, 4);
                }
                memcpy(tr + 4 * BPS, tr, 4);
                memcpy(tr + 8 * BPS, tr, 4);
                memcpy(tr + 12 * BPS, tr, 4);
                for (int n = 0; n < 16; n++) {
                    uint8_t *dst = yd + (n & 3) * 4 + (n >> 2) * 4 * BPS;
                    pred4(dst, modes[n]);
                    if (any_nz(coeffs + n * 16)) idct_add(coeffs + n * 16, dst);
                }
            } else {
                pred16(yd, modes[0], mbx, mby);
                for (int n = 0; n < 16; n++)
                    if (any_nz(coeffs + n * 16)) idct_add(coeffs + n * 16, yd + (n & 3) * 4 + (n >> 2) * 4 * BPS);
            }
            pred8(ud, uvmode, mbx, mby);
            pred8(vd, uvmode, mbx, mby);
            for (int n = 0; n < 4; n++) {
                if (any_nz(coeffs + 256 + n * 16)) idct_add(coeffs + 256 + n * 16, ud + (n & 1) * 4 + (n >> 1) * 4 * BPS);
                if (any_nz(coeffs + 320 + n * 16)) idct_add(coeffs + 320 + n * 16, vd + (n & 1) * 4 + (n >> 1) * 4 * BPS);
            }
            /* keep the unfiltered bottom row for the next MB row; store the MB */
            memcpy(d->top_y + mbx * 16, yd + 15 * BPS, 16);
            memcpy(d->top_u + mbx * 8, ud + 7 * BPS, 8);
            memcpy(d->top_v + mbx * 8, vd + 7 * BPS, 8);
            for (int j = 0; j < 16; j++) memcpy(d->y + (size_t)(mby * 16 + j) * d->ystride + mbx * 16, yd + j * BPS, 16);
            for (int j = 0; j < 8; j++) {
                memcpy(d->u + (size_t)(mby * 8 + j) * d->uvstride + mbx * 8, ud + j * BPS, 8);
                memcpy(d->v + (size_t)(mby * 8 + j) * d->uvstride + mbx * 8, vd + j * BPS, 8);
            }
        }
    }
    /* loop filter over the whole frame, in decoding order */
    if (d->filter_type)
        for (int mby = 0; mby < d->mbh; mby++)
            for (int mbx = 0; mbx < d->mbw; mbx++) filter_mb(d, mbx, mby);
    /* colour conversion with "fancy" (bilinear) chroma upsampling */
    int cw = (d->width + 1) / 2, ch = (d->height + 1) / 2;
    for (int y = 0; y < d->height; y++) {
        int cy = y >> 1, cy2 = (y & 1) ? cy + 1 : cy - 1;
        if (cy2 < 0) cy2 = 0;
        if (cy2 >= ch) cy2 = ch - 1;
        const uint8_t *yr = d->y + (size_t)y * d->ystride;
        const uint8_t *u0 = d->u + (size_t)cy * d->uvstride, *u1 = d->u + (size_t)cy2 * d->uvstride;
        const uint8_t *v0 = d->v + (size_t)cy * d->uvstride, *v1 = d->v + (size_t)cy2 * d->uvstride;
        uint32_t *out = argb + (size_t)y * d->width;
        for (int x = 0; x < d->width; x++) {
            int cx = x >> 1, cx2 = (x & 1) ? cx + 1 : cx - 1;
            if (cx2 < 0) cx2 = 0;
            if (cx2 >= cw) cx2 = cw - 1;
            int u = (9 * u0[cx] + 3 * u0[cx2] + 3 * u1[cx] + u1[cx2] + 8) >> 4;
            int v = (9 * v0[cx] + 3 * v0[cx2] + 3 * v1[cx] + v1[cx2] + 8) >> 4;
            out[x] = yuv_to_argb(yr[x], u, v);
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* VP8L lossless                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    const uint8_t *p;
    size_t len, pos;            /* bit position */
    bool eos;
} LBits;

static uint32_t lbits(LBits *b, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        size_t byte = b->pos >> 3;
        if (byte >= b->len) { b->eos = true; return v; }
        v |= (uint32_t)((b->p[byte] >> (b->pos & 7)) & 1) << i;
        b->pos++;
    }
    return v;
}

/* Canonical Huffman decoded one bit at a time (deflate bit order). */
#define HMAX 2328
typedef struct {
    uint16_t count[16];
    uint16_t symbol[HMAX];
    int nsym;
    int single;                 /* >= 0: the only symbol (zero-length code) */
} LHuff;

static bool lhuff_build(LHuff *h, const uint8_t *lengths, int n)
{
    memset(h->count, 0, sizeof(h->count));
    h->nsym = n;
    h->single = -1;
    int used = 0, last = -1;
    for (int i = 0; i < n; i++) if (lengths[i]) { if (lengths[i] > 15) return false; h->count[lengths[i]]++; used++; last = i; }
    if (used == 0) return false;
    if (used == 1) { h->single = last; return true; }
    int left = 1;
    for (int l = 1; l < 16; l++) { left <<= 1; left -= h->count[l]; if (left < 0) return false; }
    if (left > 0) return false;                     /* incomplete codes are invalid in VP8L */
    uint16_t offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = (uint16_t)(offs[l] + h->count[l]);
    for (int i = 0; i < n; i++) if (lengths[i]) h->symbol[offs[lengths[i]]++] = (uint16_t)i;
    return true;
}

static int lhuff_read(LHuff *h, LBits *b)
{
    if (h->single >= 0) return h->single;
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        code |= (int)lbits(b, 1);
        int count = h->count[l];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (b->eos) return -1;
    }
    return -1;
}

static const uint8_t kCodeLengthOrder[19] = {17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static const uint8_t kCodeToPlane[120] = {
    0x18, 0x07, 0x17, 0x19, 0x28, 0x06, 0x27, 0x29, 0x16, 0x1a, 0x26, 0x2a, 0x38, 0x05, 0x37, 0x39, 0x15, 0x1b, 0x36, 0x3a,
    0x25, 0x2b, 0x48, 0x04, 0x47, 0x49, 0x14, 0x1c, 0x35, 0x3b, 0x46, 0x4a, 0x24, 0x2c, 0x58, 0x45, 0x4b, 0x34, 0x3c, 0x03,
    0x57, 0x59, 0x13, 0x1d, 0x56, 0x5a, 0x23, 0x2d, 0x44, 0x4c, 0x55, 0x5b, 0x33, 0x3d, 0x68, 0x02, 0x67, 0x69, 0x12, 0x1e,
    0x66, 0x6a, 0x22, 0x2e, 0x54, 0x5c, 0x43, 0x4d, 0x65, 0x6b, 0x32, 0x3e, 0x78, 0x01, 0x77, 0x79, 0x53, 0x5d, 0x11, 0x1f,
    0x64, 0x6c, 0x42, 0x4e, 0x76, 0x7a, 0x21, 0x2f, 0x75, 0x7b, 0x31, 0x3f, 0x63, 0x6d, 0x52, 0x5e, 0x00, 0x74, 0x7c, 0x41,
    0x4f, 0x10, 0x20, 0x62, 0x6e, 0x30, 0x73, 0x7d, 0x51, 0x5f, 0x40, 0x72, 0x7e, 0x61, 0x6f, 0x50, 0x71, 0x7f, 0x60, 0x70};

static bool read_code(LBits *b, LHuff *h, int alphabet, Pool *pool)
{
    uint8_t *lengths = (uint8_t *)palloc(pool, (size_t)alphabet);
    if (!lengths) return false;
    if (lbits(b, 1)) {                              /* simple code */
        int nsym = (int)lbits(b, 1) + 1;
        int first8 = (int)lbits(b, 1);
        int s0 = (int)lbits(b, first8 ? 8 : 1);
        if (s0 >= alphabet) return false;
        lengths[s0] = 1;
        if (nsym == 2) {
            int s1 = (int)lbits(b, 8);
            if (s1 >= alphabet) return false;
            lengths[s1] = 1;
        }
        return lhuff_build(h, lengths, alphabet) && !b->eos;
    }
    uint8_t cl[19];
    memset(cl, 0, sizeof(cl));
    int ncl = 4 + (int)lbits(b, 4);
    if (ncl > 19) return false;
    for (int i = 0; i < ncl; i++) cl[kCodeLengthOrder[i]] = (uint8_t)lbits(b, 3);
    LHuff *clh = (LHuff *)palloc(pool, sizeof(LHuff));
    if (!clh || !lhuff_build(clh, cl, 19)) return false;
    int max_symbol = alphabet;
    if (lbits(b, 1)) {
        int nbits = 2 + 2 * (int)lbits(b, 3);
        max_symbol = 2 + (int)lbits(b, nbits);
        if (max_symbol > alphabet) return false;
    }
    int prev = 8;
    int sym = 0;
    while (sym < alphabet) {
        if (max_symbol-- == 0) break;
        int c = lhuff_read(clh, b);
        if (c < 0) return false;
        if (c < 16) {
            lengths[sym++] = (uint8_t)c;
            if (c) prev = c;
        } else {
            int extra = c == 16 ? 2 : c == 17 ? 3 : 7;
            int offset = c == 16 ? 3 : c == 17 ? 3 : 11;
            int rep = offset + (int)lbits(b, extra);
            if (sym + rep > alphabet) return false;
            int val = c == 16 ? prev : 0;
            for (int i = 0; i < rep; i++) lengths[sym++] = (uint8_t)val;
        }
    }
    return !b->eos && lhuff_build(h, lengths, alphabet);
}

typedef struct { LHuff h[5]; } HGroup;

static int prefix_value(int prefix, LBits *b)
{
    if (prefix < 4) return prefix + 1;
    int extra = (prefix - 2) >> 1;
    int offset = (2 + (prefix & 1)) << extra;
    return offset + (int)lbits(b, extra) + 1;
}

static bool decode_image(LBits *b, int w, int h, bool level0, uint32_t *out, Pool *pool);

/* Entropy-coded pixels (after transforms and colour cache setup). */
static bool decode_pixels(LBits *b, int w, int h, bool level0, uint32_t *out, Pool *pool)
{
    int cache_bits = 0;
    if (lbits(b, 1)) {
        cache_bits = (int)lbits(b, 4);
        if (cache_bits < 1 || cache_bits > 11) return false;
    }
    int hbits = 0, hw = 0, ngroups = 1;
    uint32_t *himg = NULL;
    if (level0 && lbits(b, 1)) {
        hbits = (int)lbits(b, 3) + 2;
        hw = (w + (1 << hbits) - 1) >> hbits;
        int hh = (h + (1 << hbits) - 1) >> hbits;
        himg = (uint32_t *)palloc(pool, sizeof(uint32_t) * (size_t)hw * hh);
        if (!himg || !decode_image(b, hw, hh, false, himg, pool)) return false;
        for (int i = 0; i < hw * hh; i++) {
            int g = (int)((himg[i] >> 8) & 0xffff);
            if (g + 1 > ngroups) ngroups = g + 1;
        }
        if (ngroups > 4096) return false;
    }
    int cache_size = cache_bits ? 1 << cache_bits : 0;
    HGroup *groups = (HGroup *)palloc(pool, sizeof(HGroup) * (size_t)ngroups);
    if (!groups) return false;
    for (int g = 0; g < ngroups; g++) {
        static const int base[5] = {256 + 24, 256, 256, 256, 40};
        for (int k = 0; k < 5; k++) {
            size_t mark = pool->used;
            if (!read_code(b, &groups[g].h[k], base[k] + (k == 0 ? cache_size : 0), pool)) return false;
            pool->used = mark;          /* code lengths were temporary */
        }
    }
    uint32_t *cache = cache_size ? (uint32_t *)palloc(pool, sizeof(uint32_t) * (size_t)cache_size) : NULL;
    if (cache_size && !cache) return false;
    size_t total = (size_t)w * h, pos = 0, last_cached = 0;
    while (pos < total) {
        int x = (int)(pos % (size_t)w), y = (int)(pos / (size_t)w);
        HGroup *g = &groups[himg ? (himg[(y >> hbits) * hw + (x >> hbits)] >> 8) & 0xffff : 0];
        int s = lhuff_read(&g->h[0], b);
        if (s < 0 || b->eos) return false;
        if (s < 256) {
            int r = lhuff_read(&g->h[1], b), bl = lhuff_read(&g->h[2], b), a = lhuff_read(&g->h[3], b);
            if (r < 0 || bl < 0 || a < 0) return false;
            out[pos++] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)s << 8) | (uint32_t)bl;
        } else if (s < 256 + 24) {
            int length = prefix_value(s - 256, b);
            int ds = lhuff_read(&g->h[4], b);
            if (ds < 0) return false;
            int dcode = prefix_value(ds, b);
            int dist;
            if (dcode > 120) dist = dcode - 120;
            else {
                int pc = kCodeToPlane[dcode - 1];
                dist = (pc >> 4) * w + (8 - (pc & 0xf));
                if (dist < 1) dist = 1;
            }
            if ((size_t)dist > pos || pos + (size_t)length > total) return false;
            for (int i = 0; i < length; i++, pos++) out[pos] = out[pos - (size_t)dist];
        } else {
            int idx = s - 280;
            if (!cache || idx >= cache_size) return false;
            /* the cache holds everything decoded so far */
            for (; last_cached < pos; last_cached++) cache[(0x1e35a7bdu * out[last_cached]) >> (32 - cache_bits)] = out[last_cached];
            out[pos++] = cache[idx];
        }
        if (cache) for (; last_cached < pos; last_cached++) cache[(0x1e35a7bdu * out[last_cached]) >> (32 - cache_bits)] = out[last_cached];
    }
    return !b->eos || pos == total;
}

static uint32_t add_px(uint32_t a, uint32_t b)
{
    uint32_t ag = (a & 0xff00ff00u) + (b & 0xff00ff00u), rb = (a & 0x00ff00ffu) + (b & 0x00ff00ffu);
    return (ag & 0xff00ff00u) | (rb & 0x00ff00ffu);
}

static uint32_t avg2(uint32_t a, uint32_t b) { return (((a ^ b) & 0xfefefefeu) >> 1) + (a & b); }
static int clip255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static uint32_t predict(int mode, uint32_t L, uint32_t T, uint32_t TR, uint32_t TL)
{
    switch (mode) {
    case 0: return 0xff000000u;
    case 1: return L;
    case 2: return T;
    case 3: return TR;
    case 4: return TL;
    case 5: return avg2(avg2(L, TR), T);
    case 6: return avg2(L, TL);
    case 7: return avg2(L, T);
    case 8: return avg2(TL, T);
    case 9: return avg2(T, TR);
    case 10: return avg2(avg2(L, TL), avg2(T, TR));
    case 11: {
        int d = 0;
        for (int sh = 0; sh < 32; sh += 8) {
            int t = (int)((T >> sh) & 255), l = (int)((L >> sh) & 255), tl = (int)((TL >> sh) & 255);
            d += abs_i(l - tl) - abs_i(t - tl);
        }
        return d <= 0 ? T : L;
    }
    case 12: {
        uint32_t r = 0;
        for (int sh = 0; sh < 32; sh += 8)
            r |= (uint32_t)clip255((int)((L >> sh) & 255) + (int)((T >> sh) & 255) - (int)((TL >> sh) & 255)) << sh;
        return r;
    }
    default: {
        uint32_t ave = avg2(L, T), r = 0;
        for (int sh = 0; sh < 32; sh += 8) {
            int a = (int)((ave >> sh) & 255), c = (int)((TL >> sh) & 255);
            r |= (uint32_t)clip255(a + (a - c) / 2) << sh;
        }
        return r;
    }
    }
}

enum { T_PREDICT = 0, T_CROSS, T_SUBGREEN, T_INDEX };
typedef struct {
    int type, bits, xsize;
    uint32_t *data;
    int data_w;
} Transform;

static bool decode_image(LBits *b, int w, int h, bool level0, uint32_t *out, Pool *pool)
{
    Transform tr[4];
    int ntr = 0;
    int xsize = w;
    if (level0) {
        while (lbits(b, 1)) {
            if (ntr >= 4) return false;
            Transform *t = &tr[ntr];
            t->type = (int)lbits(b, 2);
            for (int i = 0; i < ntr; i++) if (tr[i].type == t->type) return false;
            t->xsize = xsize;
            if (t->type == T_PREDICT || t->type == T_CROSS) {
                t->bits = (int)lbits(b, 3) + 2;
                t->data_w = (xsize + (1 << t->bits) - 1) >> t->bits;
                int dh = (h + (1 << t->bits) - 1) >> t->bits;
                t->data = (uint32_t *)palloc(pool, sizeof(uint32_t) * (size_t)t->data_w * dh);
                if (!t->data || !decode_image(b, t->data_w, dh, false, t->data, pool)) return false;
            } else if (t->type == T_INDEX) {
                int n = (int)lbits(b, 8) + 1;
                t->bits = n > 16 ? 0 : n > 4 ? 1 : n > 2 ? 2 : 3;
                t->data = (uint32_t *)palloc(pool, sizeof(uint32_t) * 256);
                if (!t->data || !decode_image(b, n, 1, false, t->data, pool)) return false;
                for (int i = 1; i < n; i++) t->data[i] = add_px(t->data[i], t->data[i - 1]);
                t->data_w = n;
                xsize = (xsize + (1 << t->bits) - 1) >> t->bits;
            }
            ntr++;
        }
    }
    /* pixels of the (possibly packed) image */
    uint32_t *px = out;
    if (xsize != w) {
        px = (uint32_t *)palloc(pool, sizeof(uint32_t) * (size_t)xsize * h);
        if (!px) return false;
    }
    if (!decode_pixels(b, xsize, h, level0, px, pool)) return false;
    /* inverse transforms, last first */
    for (int k = ntr - 1; k >= 0; k--) {
        Transform *t = &tr[k];
        int tw = t->xsize;
        if (t->type == T_SUBGREEN) {
            for (size_t i = 0; i < (size_t)tw * h; i++) {
                uint32_t p = px[i], g = (p >> 8) & 0xff;
                uint32_t rb = ((p & 0x00ff00ffu) + ((g << 16) | g)) & 0x00ff00ffu;
                px[i] = (p & 0xff00ff00u) | rb;
            }
        } else if (t->type == T_CROSS) {
            for (int y = 0; y < h; y++)
                for (int x = 0; x < tw; x++) {
                    uint32_t m = t->data[(y >> t->bits) * t->data_w + (x >> t->bits)];
                    int8_t g2r = (int8_t)(m & 0xff), g2b = (int8_t)((m >> 8) & 0xff), r2b = (int8_t)((m >> 16) & 0xff);
                    uint32_t p = px[(size_t)y * tw + x];
                    int8_t g = (int8_t)((p >> 8) & 0xff);
                    int r = (int)((p >> 16) & 0xff), bl = (int)(p & 0xff);
                    r = (r + ((g2r * g) >> 5)) & 0xff;
                    bl = (bl + ((g2b * g) >> 5)) & 0xff;
                    bl = (bl + ((r2b * (int8_t)r) >> 5)) & 0xff;
                    px[(size_t)y * tw + x] = (p & 0xff00ff00u) | ((uint32_t)r << 16) | (uint32_t)bl;
                }
        } else if (t->type == T_PREDICT) {
            for (int y = 0; y < h; y++)
                for (int x = 0; x < tw; x++) {
                    uint32_t *p = &px[(size_t)y * tw + x];
                    uint32_t pred;
                    if (y == 0) pred = x == 0 ? 0xff000000u : p[-1];
                    else if (x == 0) pred = p[-tw];
                    else {
                        int mode = (int)((t->data[(y >> t->bits) * t->data_w + (x >> t->bits)] >> 8) & 0xf);
                        /* TR of the last column is the first pixel of this row (memory order) */
                        pred = predict(mode, p[-1], p[-tw], p[-tw + 1], p[-tw - 1]);
                    }
                    *p = add_px(*p, pred);
                }
        } else {                                    /* colour indexing */
            int bits = t->bits;
            int ppb = 1 << bits, bpp = 8 >> bits, mask = (1 << bpp) - 1;
            uint32_t *dst = k == 0 ? out : px;      /* expands in place into the full width */
            int packed_w = (tw + ppb - 1) >> bits;
            for (int y = h - 1; y >= 0; y--) {
                for (int x = tw - 1; x >= 0; x--) {
                    uint32_t packed = px[(size_t)y * packed_w + (x >> bits)];
                    int idx = (int)((packed >> 8) & 0xff);
                    if (bits) idx = (idx >> ((x & (ppb - 1)) * bpp)) & mask;
                    dst[(size_t)y * tw + x] = idx < t->data_w ? t->data[idx] : 0;
                }
            }
            px = dst;
        }
    }
    if (px != out) memcpy(out, px, sizeof(uint32_t) * (size_t)w * h);
    return true;
}

static bool vp8l_decode(const uint8_t *data, size_t len, uint32_t *argb, size_t pixels, int *ow, int *oh, Pool *pool)
{
    if (len < 5 || data[0] != 0x2f) return false;
    LBits b = {data + 1, len - 1, 0, false};
    int w = (int)lbits(&b, 14) + 1, h = (int)lbits(&b, 14) + 1;
    lbits(&b, 1);                                   /* alpha hint */
    if (lbits(&b, 3) != 0) return false;            /* version */
    if (ow) *ow = w;
    if (oh) *oh = h;
    if (!argb) return true;
    if ((size_t)w * (size_t)h > pixels) return false;
    return decode_image(&b, w, h, true, argb, pool);
}

/* ALPH chunk -> alpha bytes of the image */
static bool decode_alpha(const uint8_t *data, size_t len, int w, int h, uint32_t *argb, Pool *pool)
{
    if (len < 1) return false;
    int method = data[0] & 3, filter = (data[0] >> 2) & 3;
    uint8_t *a = (uint8_t *)palloc(pool, (size_t)w * h);
    if (!a) return false;
    if (method == 0) {
        if (len - 1 < (size_t)w * h) return false;
        memcpy(a, data + 1, (size_t)w * h);
    } else if (method == 1) {
        uint32_t *tmp = (uint32_t *)palloc(pool, sizeof(uint32_t) * (size_t)w * h);
        LBits b = {data + 1, len - 1, 0, false};
        if (!tmp || !decode_image(&b, w, h, true, tmp, pool)) return false;
        for (size_t i = 0; i < (size_t)w * h; i++) a[i] = (uint8_t)((tmp[i] >> 8) & 0xff);
    } else {
        return false;
    }
    /* unfilter */
    for (int y = 0; y < h; y++) {
        uint8_t *row = a + (size_t)y * w;
        const uint8_t *up = y ? row - w : NULL;
        for (int x = 0; x < w; x++) {
            int pred;
            if (filter == 0) break;
            if (y == 0) pred = x ? row[x - 1] : 0;
            else if (filter == 1) pred = x ? row[x - 1] : up[0];
            else if (filter == 2) pred = up[x];
            else pred = x ? clip255(row[x - 1] + up[x] - up[x - 1]) : up[0];
            row[x] = (uint8_t)(row[x] + pred);
        }
    }
    for (size_t i = 0; i < (size_t)w * h; i++) argb[i] = (argb[i] & 0x00ffffffu) | ((uint32_t)a[i] << 24);
    return true;
}

/* ------------------------------------------------------------------ */
/* Container                                                           */
/* ------------------------------------------------------------------ */

bool webp_info(const uint8_t *d, size_t len, int *w, int *h)
{
    if (len < 30 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WEBP", 4) != 0) return false;
    const uint8_t *c = d + 12;
    if (memcmp(c, "VP8X", 4) == 0) {
        *w = (int)le24(c + 12) + 1;
        *h = (int)le24(c + 15) + 1;
        return *w > 0 && *h > 0 && *w <= 16384 && *h <= 16384;
    }
    if (memcmp(c, "VP8 ", 4) == 0 && len >= 30) {
        *w = (c[14] | (c[15] << 8)) & 0x3fff;
        *h = (c[16] | (c[17] << 8)) & 0x3fff;
        return *w > 0 && *h > 0;
    }
    if (memcmp(c, "VP8L", 4) == 0 && len >= 25 && c[8] == 0x2f) {
        uint32_t bits = le32(c + 9);
        *w = (int)(bits & 0x3fff) + 1;
        *h = (int)((bits >> 14) & 0x3fff) + 1;
        return true;
    }
    return false;
}

bool webp_decode(const uint8_t *d, size_t len, uint32_t *argb, size_t pixels, uint8_t *scratch, size_t scratch_size)
{
    if (!argb) return false;
    if (len < 20 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WEBP", 4) != 0) return false;
    size_t riff = le32(d + 4) + 8;
    if (riff < len) len = riff;
    Pool pool = {scratch, scratch_size, 0};
    const uint8_t *alpha = NULL;
    size_t alpha_len = 0;
    size_t pos = 12;
    while (pos + 8 <= len) {
        const uint8_t *c = d + pos;
        uint32_t clen = le32(c + 4);
        if (clen > len - pos - 8) clen = (uint32_t)(len - pos - 8);
        const uint8_t *body = c + 8;
        if (memcmp(c, "ALPH", 4) == 0) { alpha = body; alpha_len = clen; }
        else if (memcmp(c, "ANMF", 4) == 0) {
            /* first animation frame: its sub-chunks follow a 16-byte header */
            if (clen < 24) return false;
            pos = (size_t)(body + 16 - d);
            continue;
        } else if (memcmp(c, "VP8 ", 4) == 0 || memcmp(c, "VP8L", 4) == 0) {
            int w = 0, h = 0;
            bool lossless = c[3] == 'L';
            bool ok = lossless ? vp8l_decode(body, clen, argb, pixels, &w, &h, &pool)
                               : vp8_decode(body, clen, argb, pixels, &w, &h, &pool);
            if (!ok) return false;
            if (!lossless && alpha) decode_alpha(alpha, alpha_len, w, h, argb, &pool);
            return true;
        }
        pos += 8 + ((size_t)clen + 1) / 2 * 2;      /* chunks are padded to even sizes */
    }
    return false;
}
