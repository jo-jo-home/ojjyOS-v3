/*
 * ojjyOS v3 - SVG rasterizer (see svg.h)
 *
 * Pipeline: a tolerant XML pass builds an element tree in scratch memory;
 * a recursive walk resolves inherited paint state and transforms; each
 * shape is flattened to polygons in device space (24.8 fixed point) and
 * filled by a scanline rasterizer with 4 sub-scanlines and exact horizontal
 * span coverage. Strokes become unions of quads, joins and caps filled
 * with the non-zero rule.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "image/svg.h"
#include "web/css.h"
#include "string.h"

typedef int64_t fx;                     /* 16.16 */
#define FONE ((fx)65536)

/* ------------------------------------------------------------------ */
/* Scratch pool                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *p;
    size_t cap, used;
} Pool;

static void *palloc(Pool *P, size_t n)
{
    n = (n + 7) & ~(size_t)7;
    if (P->used + n > P->cap) return NULL;
    void *r = P->p + P->used;
    P->used += n;
    return r;
}

/* ------------------------------------------------------------------ */
/* Fixed-point maths: CORDIC (Q30 radians), integer square root         */
/* ------------------------------------------------------------------ */

static const int64_t atan_tab[30] = {
    843314857, 497837829, 263043837, 133525159, 67021687, 33543516, 16775851, 8388437, 4194283, 2097149,
    1048576, 524288, 262144, 131072, 65536, 32768, 16384, 8192, 4096, 2048, 1024, 512, 256, 128, 64, 32, 16, 8, 4, 2
};
#define Q30_K      652032874LL
#define Q30_PI     3373259426LL
#define Q30_HALFPI 1686629713LL

static int64_t iabs64(int64_t v) { return v < 0 ? -v : v; }

/* cos/sin of a Q30 angle, results in Q30 */
static void sincos_q30(int64_t a, int64_t *c, int64_t *s)
{
    while (a > Q30_PI) a -= 2 * Q30_PI;
    while (a < -Q30_PI) a += 2 * Q30_PI;
    bool neg = false;
    if (a > Q30_HALFPI) { a -= Q30_PI; neg = true; }
    else if (a < -Q30_HALFPI) { a += Q30_PI; neg = true; }
    int64_t x = Q30_K, y = 0, z = a;
    for (int i = 0; i < 30; i++) {
        int64_t dx = y >> i, dy = x >> i;
        if (z >= 0) { x -= dx; y += dy; z -= atan_tab[i]; }
        else { x += dx; y -= dy; z += atan_tab[i]; }
    }
    *c = neg ? -x : x;
    *s = neg ? -y : y;
}

/* atan2 in Q30 radians, (-pi, pi] */
static int64_t atan2_q30(int64_t y, int64_t x)
{
    if (x == 0 && y == 0) return 0;
    while (iabs64(x) >= (1LL << 30) || iabs64(y) >= (1LL << 30)) { x >>= 1; y >>= 1; }
    while (iabs64(x) < (1LL << 28) && iabs64(y) < (1LL << 28)) { x *= 2; y *= 2; }
    int64_t off = 0;
    if (x < 0) { off = y >= 0 ? Q30_PI : -Q30_PI; x = -x; y = -y; }
    int64_t z = 0;
    for (int i = 0; i < 30; i++) {
        int64_t dx = y >> i, dy = x >> i;
        if (y > 0) { x += dx; y -= dy; z += atan_tab[i]; }
        else { x -= dx; y += dy; z -= atan_tab[i]; }
    }
    return z + off;
}

static uint64_t isqrt64(uint64_t v)
{
    uint64_t r = 0, bit = 1ULL << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

static fx fmul(fx a, fx b) { return (a * b) >> 16; }
static fx fdiv(fx a, fx b) { return b ? (a * 65536) / b : 0; }

/* ------------------------------------------------------------------ */
/* XML element tree                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    const char *attrs;
    const char *text;
    uint32_t nlen, alen, tlen;
    int32_t parent, first, last, next;
} El;

#define MAX_EL 4096

typedef struct {
    const char *src;
    size_t len;
    El *el;
    int nel;
} Doc;

static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static char lc(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static bool ieq_n(const char *a, size_t al, const char *b)
{
    size_t bl = strlen(b);
    if (al != bl) return false;
    for (size_t i = 0; i < al; i++) if (lc(a[i]) != lc(b[i])) return false;
    return true;
}

static bool name_is(const El *e, const char *n) { return ieq_n(e->name, e->nlen, n); }

static const char *find_str(const char *p, const char *end, const char *needle)
{
    size_t n = strlen(needle);
    for (; p + n <= end; p++) if (memcmp(p, needle, n) == 0) return p;
    return end;
}

static void parse_xml(Doc *D)
{
    const char *p = D->src, *end = D->src + D->len;
    int cur = -1;
    while (p < end) {
        if (*p != '<') {
            p++;
            continue;
        }
        if (end - p >= 4 && memcmp(p, "<!--", 4) == 0) { p = find_str(p + 4, end, "-->"); p = p + 3 <= end ? p + 3 : end; continue; }
        if (end - p >= 9 && memcmp(p, "<![CDATA[", 9) == 0) {
            const char *e = find_str(p + 9, end, "]]>");
            if (cur >= 0 && name_is(&D->el[cur], "style")) { D->el[cur].text = p + 9; D->el[cur].tlen = (uint32_t)(e - p - 9); }
            p = e + 3 <= end ? e + 3 : end;
            continue;
        }
        if (p + 1 < end && (p[1] == '!' || p[1] == '?')) {
            while (p < end && *p != '>') p++;
            p++;
            continue;
        }
        if (p + 1 < end && p[1] == '/') {
            while (p < end && *p != '>') p++;
            p++;
            if (cur >= 0) cur = D->el[cur].parent;
            continue;
        }
        /* start tag */
        const char *n = p + 1, *ne = n;
        while (ne < end && !is_ws(*ne) && *ne != '/' && *ne != '>') ne++;
        const char *a = ne, *q = ne;
        char quote = 0;
        while (q < end && (quote || *q != '>')) {
            if (quote) { if (*q == quote) quote = 0; }
            else if (*q == '"' || *q == '\'') quote = *q;
            q++;
        }
        bool self = q > a && q[-1] == '/';
        if (D->nel < MAX_EL && ne > n) {
            El *e = &D->el[D->nel];
            memset(e, 0, sizeof(*e));
            e->name = n;
            e->nlen = (uint32_t)(ne - n);
            e->attrs = a;
            e->alen = (uint32_t)((self ? q - 1 : q) - a);
            e->parent = cur;
            e->first = e->last = e->next = -1;
            e->text = q < end ? q + 1 : end;
            const char *t = e->text;
            while (t < end && *t != '<') t++;
            e->tlen = (uint32_t)(t - e->text);
            if (cur >= 0) {
                El *pe = &D->el[cur];
                if (pe->last >= 0) D->el[pe->last].next = D->nel;
                else pe->first = D->nel;
                pe->last = D->nel;
            }
            if (!self) cur = D->nel;
            D->nel++;
        }
        p = q < end ? q + 1 : end;
    }
}

/* Attribute value (raw, quotes removed). Names match case-insensitively. */
static bool attr(const El *e, const char *name, const char **v, size_t *vl)
{
    const char *p = e->attrs, *end = e->attrs + e->alen;
    while (p < end) {
        while (p < end && (is_ws(*p) || *p == '/')) p++;
        const char *n = p;
        while (p < end && !is_ws(*p) && *p != '=') p++;
        size_t nl = (size_t)(p - n);
        while (p < end && is_ws(*p)) p++;
        const char *val = p;
        size_t len = 0;
        if (p < end && *p == '=') {
            p++;
            while (p < end && is_ws(*p)) p++;
            if (p < end && (*p == '"' || *p == '\'')) {
                char q = *p++;
                val = p;
                while (p < end && *p != q) p++;
                len = (size_t)(p - val);
                if (p < end) p++;
            } else {
                val = p;
                while (p < end && !is_ws(*p)) p++;
                len = (size_t)(p - val);
            }
        }
        if (nl && ieq_n(n, nl, name)) { *v = val; *vl = len; return true; }
        if (!nl) p++;
    }
    return false;
}

/* Copy an attribute into buf (entities &amp; &quot; &lt; &gt; &apos; decoded). */
static bool attr_buf(const El *e, const char *name, char *buf, size_t cap)
{
    const char *v;
    size_t vl;
    if (!attr(e, name, &v, &vl)) return false;
    size_t o = 0;
    for (size_t i = 0; i < vl && o + 1 < cap; i++) {
        if (v[i] == '&') {
            static const struct { const char *e; char c; } ents[] = {{"&amp;", '&'}, {"&quot;", '"'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&apos;", '\''}};
            bool done = false;
            for (unsigned k = 0; k < 5; k++) {
                size_t el = strlen(ents[k].e);
                if (i + el <= vl && memcmp(v + i, ents[k].e, el) == 0) { buf[o++] = ents[k].c; i += el - 1; done = true; break; }
            }
            if (done) continue;
        }
        buf[o++] = v[i];
    }
    buf[o] = '\0';
    return true;
}

static int find_id(const Doc *D, const char *id, size_t n)
{
    for (int i = 0; i < D->nel; i++) {
        const char *v;
        size_t vl;
        if (attr(&D->el[i], "id", &v, &vl) && vl == n && memcmp(v, id, n) == 0) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Numbers, lengths, transforms                                         */
/* ------------------------------------------------------------------ */

static const char *skip_sep(const char *p, const char *end)
{
    while (p < end && (is_ws(*p) || *p == ',')) p++;
    return p;
}

/* SVG number -> 16.16. Accepts "-1.5e-3", ".5", "1.5.5" (two numbers). */
static bool num(const char **pp, const char *end, fx *out)
{
    const char *p = skip_sep(*pp, end);
    bool neg = false;
    if (p < end && (*p == '-' || *p == '+')) { neg = *p == '-'; p++; }
    int64_t m = 0;
    int e10 = 0, digits = 0;
    bool any = false;
    while (p < end && *p >= '0' && *p <= '9') {
        if (digits < 12) { m = m * 10 + (*p - '0'); digits += m != 0; }
        else e10++;
        p++;
        any = true;
    }
    if (p < end && *p == '.') {
        p++;
        while (p < end && *p >= '0' && *p <= '9') {
            if (digits < 12) { m = m * 10 + (*p - '0'); digits += m != 0; e10--; }
            p++;
            any = true;
        }
    }
    if (!any) return false;
    if (p + 1 < end && (*p == 'e' || *p == 'E') && (p[1] == '-' || p[1] == '+' || (p[1] >= '0' && p[1] <= '9'))) {
        p++;
        bool en = false;
        if (*p == '-' || *p == '+') { en = *p == '-'; p++; }
        int ev = 0;
        while (p < end && *p >= '0' && *p <= '9') { if (ev < 1000) ev = ev * 10 + (*p - '0'); p++; }
        e10 += en ? -ev : ev;
    }
    int64_t v = m;
    int64_t scale = FONE;
    /* v * 65536 * 10^e10, clamped */
    while (e10 > 0) { if (v > (1LL << 40)) { v = 1LL << 40; break; } v *= 10; e10--; }
    v *= scale;
    while (e10 < 0) { v /= 10; e10++; if (!v) break; }
    if (v > (1LL << 52)) v = 1LL << 52;
    *out = neg ? -v : v;
    *pp = p;
    return true;
}

/* A length attribute: number with optional px/em/pt unit; % rejected. */
static bool length_attr(const El *e, const char *name, fx *out)
{
    char b[48];
    if (!attr_buf(e, name, b, sizeof(b))) return false;
    const char *p = b, *end = b + strlen(b);
    if (!num(&p, end, out)) return false;
    p = skip_sep(p, end);
    if (p < end && *p == '%') return false;
    if (p + 1 < end && lc(p[0]) == 'e' && lc(p[1]) == 'm') *out *= 16;
    if (p + 1 < end && lc(p[0]) == 'p' && lc(p[1]) == 't') *out = *out * 4 / 3;
    return true;
}

typedef struct { fx a, b, c, d, e, f; } Mat;

static Mat mat_mul(const Mat *p, const Mat *l)      /* p after l */
{
    Mat r;
    r.a = fmul(p->a, l->a) + fmul(p->c, l->b);
    r.b = fmul(p->b, l->a) + fmul(p->d, l->b);
    r.c = fmul(p->a, l->c) + fmul(p->c, l->d);
    r.d = fmul(p->b, l->c) + fmul(p->d, l->d);
    r.e = fmul(p->a, l->e) + fmul(p->c, l->f) + p->e;
    r.f = fmul(p->b, l->e) + fmul(p->d, l->f) + p->f;
    return r;
}

static Mat mat_translate(fx x, fx y) { Mat m = {FONE, 0, 0, FONE, x, y}; return m; }

/* degrees (16.16) -> Q30 radians */
static int64_t deg_q30(fx deg) { return (deg * (Q30_PI / 180)) >> 16; }

static void parse_transform(const char *s, Mat *m)
{
    const char *p = s, *end = s + strlen(s);
    while (p < end) {
        p = skip_sep(p, end);
        const char *n = p;
        while (p < end && *p != '(' ) p++;
        size_t nl = (size_t)(p - n);
        while (nl && is_ws(n[nl - 1])) nl--;
        if (p >= end) break;
        p++;
        fx v[6] = {0};
        int k = 0;
        while (k < 6 && num(&p, end, &v[k])) k++;
        while (p < end && *p != ')') p++;
        if (p < end) p++;
        Mat l = {FONE, 0, 0, FONE, 0, 0};
        if (ieq_n(n, nl, "matrix") && k == 6) { l.a = v[0]; l.b = v[1]; l.c = v[2]; l.d = v[3]; l.e = v[4]; l.f = v[5]; }
        else if (ieq_n(n, nl, "translate") && k >= 1) { l.e = v[0]; l.f = k > 1 ? v[1] : 0; }
        else if (ieq_n(n, nl, "scale") && k >= 1) { l.a = v[0]; l.d = k > 1 ? v[1] : v[0]; }
        else if (ieq_n(n, nl, "rotate") && k >= 1) {
            int64_t c, sn;
            sincos_q30(deg_q30(v[0]), &c, &sn);
            l.a = c >> 14; l.b = sn >> 14; l.c = -(sn >> 14); l.d = c >> 14;
            if (k >= 3) {
                Mat t1 = mat_translate(v[1], v[2]), t2 = mat_translate(-v[1], -v[2]);
                Mat r = mat_mul(&t1, &l);
                l = mat_mul(&r, &t2);
            }
        } else if ((ieq_n(n, nl, "skewx") || ieq_n(n, nl, "skewy")) && k >= 1) {
            int64_t c, sn;
            sincos_q30(deg_q30(v[0]), &c, &sn);
            fx t = c ? (fx)((sn * 65536) / c) : 0;
            if (ieq_n(n, nl, "skewx")) l.c = t; else l.b = t;
        } else {
            continue;
        }
        *m = mat_mul(m, &l);
    }
}

/* ------------------------------------------------------------------ */
/* Paint state                                                         */
/* ------------------------------------------------------------------ */

enum { PAINT_NONE, PAINT_COLOR };
enum { CAP_BUTT, CAP_ROUND, CAP_SQUARE };
enum { JOIN_MITER, JOIN_ROUND, JOIN_BEVEL };

typedef struct {
    uint8_t fill_kind, stroke_kind;
    uint32_t fill, stroke;              /* ARGB */
    fx stroke_w;
    uint8_t evenodd, cap, join;
    uint16_t opacity, fill_op, stroke_op;   /* 0..256 */
    bool hidden;
    Mat m;
} St;

typedef struct {
    const char *sel;                    /* ".cls", "tag" or "#id" */
    size_t sel_len;
    const char *decls;
    size_t decl_len;
} ClassRule;

#define MAX_RULES 256

typedef struct {
    Doc D;
    Pool P;
    uint32_t current;
    ClassRule rules[MAX_RULES];
    int nrules;
    /* output */
    uint32_t *out;
    int w, h;
    /* flattening buffers (device 24.8) */
    int32_t *px, *py;
    int npts, maxpts;
    int32_t *sub_start;
    uint8_t *sub_closed;
    int nsub, maxsub;
    /* rasterizer */
    struct Edge { int32_t x0, y0, x1, y1; int8_t dir; } *edges;
    int nedges, maxedges;
    int32_t *acc;
    struct Cross { int32_t x; int8_t dir; } *cross;
    int maxcross;
    int depth;
} Ctx;

static uint16_t op_val(const char *v)
{
    const char *p = v, *end = v + strlen(v);
    fx f;
    if (!num(&p, end, &f)) return 256;
    p = skip_sep(p, end);
    if (p < end && *p == '%') f /= 100;
    if (f < 0) f = 0;
    if (f > FONE) f = FONE;
    return (uint16_t)(f >> 8);
}

/* stop colour of a gradient (first stop; follows one href) */
static bool gradient_color(Ctx *C, int gi, uint32_t *argb, int depth)
{
    if (gi < 0 || depth > 2) return false;
    const El *g = &C->D.el[gi];
    for (int k = g->first; k >= 0; k = C->D.el[k].next) {
        const El *s = &C->D.el[k];
        if (!name_is(s, "stop")) continue;
        char b[96];
        uint32_t col = 0xFF000000u;
        bool ok = false;
        if (attr_buf(s, "stop-color", b, sizeof(b))) ok = css_parse_color(b, &col);
        uint16_t op = 256;
        if (attr_buf(s, "stop-opacity", b, sizeof(b))) op = op_val(b);
        if (attr_buf(s, "style", b, sizeof(b))) {
            const char *sc = find_str(b, b + strlen(b), "stop-color:");
            if (*sc) {
                char cb[48];
                size_t i = 0;
                sc += 11;
                while (*sc && *sc != ';' && i + 1 < sizeof(cb)) cb[i++] = *sc++;
                cb[i] = '\0';
                char *t = cb;
                while (is_ws(*t)) t++;
                ok = css_parse_color(t, &col) || ok;
            }
        }
        if (!ok) col = 0xFF000000u;
        uint32_t a = ((col >> 24) * op) >> 8;
        *argb = (a << 24) | (col & 0xFFFFFF);
        return true;
    }
    char href[128];
    if (attr_buf(g, "href", href, sizeof(href)) || attr_buf(g, "xlink:href", href, sizeof(href)))
        if (href[0] == '#') return gradient_color(C, find_id(&C->D, href + 1, strlen(href + 1)), argb, depth + 1);
    return false;
}

static bool parse_paint(Ctx *C, const char *v, uint8_t *kind, uint32_t *argb)
{
    while (is_ws(*v)) v++;
    if (ieq_n(v, 4, "none") && (v[4] == '\0' || is_ws(v[4]))) { *kind = PAINT_NONE; return true; }
    if (ieq_n(v, 12, "currentcolor")) { *kind = PAINT_COLOR; *argb = C->current; return true; }
    if (ieq_n(v, 11, "transparent")) { *kind = PAINT_NONE; return true; }
    if (ieq_n(v, 4, "url(")) {
        const char *h = v + 4;
        while (*h == ' ' || *h == '\'' || *h == '"') h++;
        const char *he = h;
        while (*he && *he != ')' && *he != '\'' && *he != '"') he++;
        if (*h == '#' && gradient_color(C, find_id(&C->D, h + 1, (size_t)(he - h - 1)), argb, 0)) { *kind = PAINT_COLOR; return true; }
        /* fallback colour after url(...) */
        const char *f = he;
        while (*f && *f != ')') f++;
        if (*f) f++;
        while (is_ws(*f)) f++;
        if (*f) return parse_paint(C, f, kind, argb);
        *kind = PAINT_NONE;
        return true;
    }
    uint32_t c;
    if (css_parse_color(v, &c)) { *kind = PAINT_COLOR; *argb = c; return true; }
    return false;
}

/* one presentation property (attribute or style declaration) */
static void apply_prop(Ctx *C, St *s, const char *n, size_t nl, const char *v)
{
    if (ieq_n(n, nl, "fill")) parse_paint(C, v, &s->fill_kind, &s->fill);
    else if (ieq_n(n, nl, "stroke")) parse_paint(C, v, &s->stroke_kind, &s->stroke);
    else if (ieq_n(n, nl, "stroke-width")) {
        const char *p = v, *end = v + strlen(v);
        fx f;
        if (num(&p, end, &f) && f >= 0) s->stroke_w = f;
    } else if (ieq_n(n, nl, "fill-rule") || ieq_n(n, nl, "clip-rule")) {
        if (ieq_n(n, nl, "fill-rule")) s->evenodd = ieq_n(v, 7, "evenodd");
    } else if (ieq_n(n, nl, "opacity")) s->opacity = (uint16_t)((s->opacity * op_val(v)) >> 8);
    else if (ieq_n(n, nl, "fill-opacity")) s->fill_op = op_val(v);
    else if (ieq_n(n, nl, "stroke-opacity")) s->stroke_op = op_val(v);
    else if (ieq_n(n, nl, "stroke-linecap")) s->cap = ieq_n(v, 5, "round") ? CAP_ROUND : ieq_n(v, 6, "square") ? CAP_SQUARE : CAP_BUTT;
    else if (ieq_n(n, nl, "stroke-linejoin")) s->join = ieq_n(v, 5, "round") ? JOIN_ROUND : ieq_n(v, 5, "bevel") ? JOIN_BEVEL : JOIN_MITER;
    else if (ieq_n(n, nl, "display")) { if (ieq_n(v, 4, "none")) s->hidden = true; }
    else if (ieq_n(n, nl, "visibility")) { if (ieq_n(v, 6, "hidden") || ieq_n(v, 8, "collapse")) s->hidden = true; }
    else if (ieq_n(n, nl, "color")) { uint32_t c; if (css_parse_color(v, &c)) C->current = c; }
}

/* "a:b; c:d" declarations */
static void apply_decls(Ctx *C, St *s, const char *p, size_t len)
{
    const char *end = p + len;
    while (p < end) {
        while (p < end && (is_ws(*p) || *p == ';')) p++;
        const char *n = p;
        while (p < end && *p != ':' && *p != ';') p++;
        if (p >= end || *p != ':') { p++; continue; }
        size_t nl = (size_t)(p - n);
        while (nl && is_ws(n[nl - 1])) nl--;
        p++;
        while (p < end && is_ws(*p)) p++;
        const char *v = p;
        while (p < end && *p != ';') p++;
        size_t vl = (size_t)(p - v);
        while (vl && is_ws(v[vl - 1])) vl--;
        char vb[160];
        if (vl >= sizeof(vb)) vl = sizeof(vb) - 1;
        memcpy(vb, v, vl);
        vb[vl] = '\0';
        char *imp = (char *)find_str(vb, vb + vl, "!important");
        if (*imp) *imp = '\0';
        apply_prop(C, s, n, nl, vb);
    }
}

static const char *const PRESENTATION[] = {
    "color", "fill", "stroke", "stroke-width", "fill-rule", "opacity", "fill-opacity", "stroke-opacity",
    "stroke-linecap", "stroke-linejoin", "display", "visibility"
};

static bool has_class(const El *e, const char *c, size_t cl)
{
    const char *v;
    size_t vl;
    if (!attr(e, "class", &v, &vl)) return false;
    for (size_t i = 0; i < vl; ) {
        while (i < vl && is_ws(v[i])) i++;
        size_t s = i;
        while (i < vl && !is_ws(v[i])) i++;
        if (i - s == cl && memcmp(v + s, c, cl) == 0) return true;
    }
    return false;
}

static void resolve_style(Ctx *C, const El *e, St *s)
{
    char b[512];
    for (unsigned i = 0; i < sizeof(PRESENTATION) / sizeof(PRESENTATION[0]); i++)
        if (attr_buf(e, PRESENTATION[i], b, sizeof(b))) apply_prop(C, s, PRESENTATION[i], strlen(PRESENTATION[i]), b);
    for (int r = 0; r < C->nrules; r++) {
        const ClassRule *R = &C->rules[r];
        bool hit = false;
        if (R->sel[0] == '.') hit = has_class(e, R->sel + 1, R->sel_len - 1);
        else if (R->sel[0] == '#') { const char *v; size_t vl; hit = attr(e, "id", &v, &vl) && vl == R->sel_len - 1 && memcmp(v, R->sel + 1, vl) == 0; }
        else hit = e->nlen == R->sel_len && memcmp(e->name, R->sel, R->sel_len) == 0;
        if (hit) apply_decls(C, s, R->decls, R->decl_len);
    }
    if (attr_buf(e, "style", b, sizeof(b))) apply_decls(C, s, b, strlen(b));
}

/* simple rules from <style>: "sel, sel { decls }" with single simple selectors */
static void collect_rules(Ctx *C)
{
    for (int i = 0; i < C->D.nel; i++) {
        const El *e = &C->D.el[i];
        if (!name_is(e, "style")) continue;
        const char *p = e->text, *end = e->text + e->tlen;
        while (p < end && C->nrules < MAX_RULES) {
            const char *sel = p;
            while (p < end && *p != '{') p++;
            if (p >= end) break;
            const char *sel_end = p++;
            const char *d = p;
            while (p < end && *p != '}') p++;
            size_t dl = (size_t)(p - d);
            if (p < end) p++;
            /* each comma-separated selector that is a single simple one */
            const char *s = sel;
            while (s < sel_end && C->nrules < MAX_RULES) {
                while (s < sel_end && (is_ws(*s) || *s == ',')) s++;
                const char *t = s;
                while (t < sel_end && *t != ',') t++;
                const char *te = t;
                while (te > s && is_ws(te[-1])) te--;
                bool simple = te > s;
                for (const char *k = s; k < te; k++) if (is_ws(*k) || *k == ':' || *k == '[' || *k == '>') simple = false;
                if (simple) {
                    ClassRule *R = &C->rules[C->nrules++];
                    R->sel = s;
                    R->sel_len = (size_t)(te - s);
                    R->decls = d;
                    R->decl_len = dl;
                }
                s = t;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Flattening into device space                                         */
/* ------------------------------------------------------------------ */

static void dev(const St *s, fx x, fx y, int32_t *ox, int32_t *oy)
{
    fx dx = fmul(s->m.a, x) + fmul(s->m.c, y) + s->m.e;
    fx dy = fmul(s->m.b, x) + fmul(s->m.d, y) + s->m.f;
    if (dx > (1LL << 38)) dx = 1LL << 38;
    if (dx < -(1LL << 38)) dx = -(1LL << 38);
    if (dy > (1LL << 38)) dy = 1LL << 38;
    if (dy < -(1LL << 38)) dy = -(1LL << 38);
    *ox = (int32_t)(dx >> 8);
    *oy = (int32_t)(dy >> 8);
}

static void begin_sub(Ctx *C)
{
    if (C->nsub > 0 && C->sub_start[C->nsub - 1] == C->npts) return;     /* empty: reuse */
    if (C->nsub >= C->maxsub) return;
    C->sub_start[C->nsub] = C->npts;
    C->sub_closed[C->nsub] = 0;
    C->nsub++;
}

static void add_dev(Ctx *C, int32_t x, int32_t y)
{
    if (C->npts >= C->maxpts || !C->nsub) return;
    int s0 = C->sub_start[C->nsub - 1];
    if (C->npts > s0 && C->px[C->npts - 1] == x && C->py[C->npts - 1] == y) return;
    C->px[C->npts] = x;
    C->py[C->npts] = y;
    C->npts++;
}

static void add_pt(Ctx *C, const St *s, fx x, fx y)
{
    int32_t dx, dy;
    dev(s, x, y, &dx, &dy);
    add_dev(C, dx, dy);
}

static void close_sub(Ctx *C)
{
    if (C->nsub) C->sub_closed[C->nsub - 1] = 1;
}

/* segment count for a curve from its device-space control polygon */
static int curve_steps(int32_t len24)
{
    int n = (int)(len24 >> 8) / 3 + 2;
    return n < 2 ? 2 : n > 64 ? 64 : n;
}

static int32_t dist24(int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
    int64_t dx = bx - ax, dy = by - ay;
    return (int32_t)isqrt64((uint64_t)(dx * dx + dy * dy));
}

static void cubic(Ctx *C, const St *s, fx x0, fx y0, fx x1, fx y1, fx x2, fx y2, fx x3, fx y3)
{
    int32_t p[8];
    dev(s, x0, y0, &p[0], &p[1]);
    dev(s, x1, y1, &p[2], &p[3]);
    dev(s, x2, y2, &p[4], &p[5]);
    dev(s, x3, y3, &p[6], &p[7]);
    int32_t len = dist24(p[0], p[1], p[2], p[3]) + dist24(p[2], p[3], p[4], p[5]) + dist24(p[4], p[5], p[6], p[7]);
    int n = curve_steps(len);
    for (int i = 1; i <= n; i++) {
        int64_t t = ((int64_t)i << 16) / n, u = FONE - t;
        int64_t uu = (u * u) >> 16, tt = (t * t) >> 16;
        int64_t b0 = (uu * u) >> 16, b1 = (3 * uu * t) >> 16, b2 = (3 * u * tt) >> 16, b3 = (tt * t) >> 16;
        int32_t x = (int32_t)((b0 * p[0] + b1 * p[2] + b2 * p[4] + b3 * p[6]) >> 16);
        int32_t y = (int32_t)((b0 * p[1] + b1 * p[3] + b2 * p[5] + b3 * p[7]) >> 16);
        add_dev(C, x, y);
    }
}

static void quad(Ctx *C, const St *s, fx x0, fx y0, fx x1, fx y1, fx x2, fx y2)
{
    /* degree elevation to a cubic */
    cubic(C, s, x0, y0, x0 + (x1 - x0) * 2 / 3, y0 + (y1 - y0) * 2 / 3, x2 + (x1 - x2) * 2 / 3, y2 + (y1 - y2) * 2 / 3, x2, y2);
}

/* SVG elliptical arc (implementation notes F.6.5), all in user space */
static void arc(Ctx *C, const St *s, fx x1, fx y1, fx rx, fx ry, fx phi_deg, bool large, bool sweep, fx x2, fx y2)
{
    if (x1 == x2 && y1 == y2) return;
    rx = rx < 0 ? -rx : rx;
    ry = ry < 0 ? -ry : ry;
    if (rx < 16 || ry < 16) { add_pt(C, s, x2, y2); return; }
    int64_t cphi, sphi;
    sincos_q30(deg_q30(phi_deg), &cphi, &sphi);
    fx cp = cphi >> 14, sp = sphi >> 14;                 /* 16.16 */
    fx hx = (x1 - x2) / 2, hy = (y1 - y2) / 2;
    fx x1p = fmul(cp, hx) + fmul(sp, hy);
    fx y1p = -fmul(sp, hx) + fmul(cp, hy);
    /* lambda = (x1'/rx)^2 + (y1'/ry)^2 in Q30: nearly closed arcs have tiny
     * ratios whose 16.16 square would round to nothing */
    int64_t nx = x1p, ny = y1p, drx = rx, dry = ry;
    while (iabs64(nx) >= (1LL << 32) || iabs64(ny) >= (1LL << 32)) { nx >>= 1; ny >>= 1; drx >>= 1; dry >>= 1; }
    if (drx < 1) drx = 1;
    if (dry < 1) dry = 1;
    int64_t rxr30 = nx * (1LL << 30) / drx, ryr30 = ny * (1LL << 30) / dry;
    if (iabs64(rxr30) > (1LL << 40)) rxr30 = rxr30 < 0 ? -(1LL << 40) : (1LL << 40);
    if (iabs64(ryr30) > (1LL << 40)) ryr30 = ryr30 < 0 ? -(1LL << 40) : (1LL << 40);
    int64_t lam30 = ((rxr30 >> 8) * (rxr30 >> 8) >> 14) + ((ryr30 >> 8) * (ryr30 >> 8) >> 14);
    fx rxr, ryr, coef = 0;
    if (lam30 > (1LL << 30)) {
        fx sq = (fx)isqrt64((uint64_t)(lam30 >> 14) << 16);    /* sqrt(lambda), 16.16 */
        rx = fmul(rx, sq);
        ry = fmul(ry, sq);
        rxr = fdiv(x1p, rx);
        ryr = fdiv(y1p, ry);
    } else {
        if (lam30 < 1) lam30 = 1;
        int64_t q30 = (((1LL << 30) - lam30) << 30) / lam30;   /* (1 - lambda) / lambda, Q30 */
        coef = 2 * (fx)isqrt64((uint64_t)q30);                  /* sqrt, 16.16 */
        if (large == sweep) coef = -coef;
        rxr = rxr30 >> 14;
        ryr = ryr30 >> 14;
    }
    fx cxp = fmul(coef, fmul(rx, ryr));
    fx cyp = -fmul(coef, fmul(ry, rxr));
    fx cx = fmul(cp, cxp) - fmul(sp, cyp) + (x1 + x2) / 2;
    fx cy = fmul(sp, cxp) + fmul(cp, cyp) + (y1 + y2) / 2;
    fx ux = fdiv(x1p - cxp, rx), uy = fdiv(y1p - cyp, ry);
    fx vx = fdiv(-x1p - cxp, rx), vy = fdiv(-y1p - cyp, ry);
    int64_t t1 = atan2_q30(uy, ux), t2 = atan2_q30(vy, vx);
    int64_t dt = t2 - t1;
    while (dt > Q30_PI) dt -= 2 * Q30_PI;
    while (dt <= -Q30_PI) dt += 2 * Q30_PI;
    if (sweep && dt < 0) dt += 2 * Q30_PI;
    if (!sweep && dt > 0) dt -= 2 * Q30_PI;
    /* steps from the device-space radius */
    int32_t ax, ay, bx, by;
    dev(s, 0, 0, &ax, &ay);
    dev(s, rx > ry ? rx : ry, 0, &bx, &by);
    int32_t r24 = dist24(ax, ay, bx, by);
    int n = (int)((iabs64(dt) >> 20) * (r24 >> 8) / 3000) + 4;      /* ~ |dt| * r / 3px */
    if (n > 96) n = 96;
    for (int i = 1; i <= n; i++) {
        int64_t a = t1 + dt * i / n, ca, sa;
        sincos_q30(a, &ca, &sa);
        fx c16 = ca >> 14, s16 = sa >> 14;
        fx ex = fmul(rx, c16), ey = fmul(ry, s16);
        fx x = cx + fmul(cp, ex) - fmul(sp, ey);
        fx y = cy + fmul(sp, ex) + fmul(cp, ey);
        if (i == n) { x = x2; y = y2; }
        add_pt(C, s, x, y);
    }
}

static bool path_flag(const char **p, const char *end, bool *f)
{
    const char *q = skip_sep(*p, end);
    if (q < end && (*q == '0' || *q == '1')) { *f = *q == '1'; *p = q + 1; return true; }
    return false;
}

static void flatten_path(Ctx *C, const St *s, const char *d, size_t len)
{
    const char *p = d, *end = d + len;
    fx cx = 0, cy = 0, sx = 0, sy = 0, lcx = 0, lcy = 0;   /* current, subpath start, last control */
    char cmd = 0, prev = 0;
    bool need_start = false;    /* after Z: the next drawing command opens a subpath at (sx, sy) */
    while (p < end) {
        p = skip_sep(p, end);
        if (p >= end) break;
        char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) { cmd = c; p++; }
        else if (!cmd) break;
        else if (cmd == 'M') cmd = 'L';
        else if (cmd == 'm') cmd = 'l';
        bool rel = cmd >= 'a';
        char uc = rel ? (char)(cmd - 32) : cmd;
        fx v[7];
        if (uc != 'M' && uc != 'Z' && need_start) {
            begin_sub(C);
            add_pt(C, s, sx, sy);
            need_start = false;
        }
        switch (uc) {
        case 'Z':
            close_sub(C);
            cx = sx; cy = sy;
            need_start = true;
            prev = 'Z';
            cmd = 0;
            continue;
        case 'M':
            if (!num(&p, end, &v[0]) || !num(&p, end, &v[1])) return;
            cx = rel ? cx + v[0] : v[0];
            cy = rel ? cy + v[1] : v[1];
            sx = cx; sy = cy;
            need_start = false;
            begin_sub(C);
            add_pt(C, s, cx, cy);
            break;
        case 'L':
            if (!num(&p, end, &v[0]) || !num(&p, end, &v[1])) return;
            cx = rel ? cx + v[0] : v[0];
            cy = rel ? cy + v[1] : v[1];
            add_pt(C, s, cx, cy);
            break;
        case 'H':
            if (!num(&p, end, &v[0])) return;
            cx = rel ? cx + v[0] : v[0];
            add_pt(C, s, cx, cy);
            break;
        case 'V':
            if (!num(&p, end, &v[0])) return;
            cy = rel ? cy + v[0] : v[0];
            add_pt(C, s, cx, cy);
            break;
        case 'C':
            for (int i = 0; i < 6; i++) if (!num(&p, end, &v[i])) return;
            if (rel) for (int i = 0; i < 6; i++) v[i] += i & 1 ? cy : cx;
            cubic(C, s, cx, cy, v[0], v[1], v[2], v[3], v[4], v[5]);
            lcx = v[2]; lcy = v[3];
            cx = v[4]; cy = v[5];
            break;
        case 'S': {
            for (int i = 0; i < 4; i++) if (!num(&p, end, &v[i])) return;
            if (rel) for (int i = 0; i < 4; i++) v[i] += i & 1 ? cy : cx;
            fx c1x = cx, c1y = cy;
            if (prev == 'C' || prev == 'S') { c1x = 2 * cx - lcx; c1y = 2 * cy - lcy; }
            cubic(C, s, cx, cy, c1x, c1y, v[0], v[1], v[2], v[3]);
            lcx = v[0]; lcy = v[1];
            cx = v[2]; cy = v[3];
            break;
        }
        case 'Q':
            for (int i = 0; i < 4; i++) if (!num(&p, end, &v[i])) return;
            if (rel) for (int i = 0; i < 4; i++) v[i] += i & 1 ? cy : cx;
            quad(C, s, cx, cy, v[0], v[1], v[2], v[3]);
            lcx = v[0]; lcy = v[1];
            cx = v[2]; cy = v[3];
            break;
        case 'T': {
            if (!num(&p, end, &v[0]) || !num(&p, end, &v[1])) return;
            if (rel) { v[0] += cx; v[1] += cy; }
            fx qx = cx, qy = cy;
            if (prev == 'Q' || prev == 'T') { qx = 2 * cx - lcx; qy = 2 * cy - lcy; }
            quad(C, s, cx, cy, qx, qy, v[0], v[1]);
            lcx = qx; lcy = qy;
            cx = v[0]; cy = v[1];
            break;
        }
        case 'A': {
            bool large, sweep;
            if (!num(&p, end, &v[0]) || !num(&p, end, &v[1]) || !num(&p, end, &v[2]) ||
                !path_flag(&p, end, &large) || !path_flag(&p, end, &sweep) || !num(&p, end, &v[3]) || !num(&p, end, &v[4]))
                return;
            fx ex = rel ? cx + v[3] : v[3], ey = rel ? cy + v[4] : v[4];
            arc(C, s, cx, cy, v[0], v[1], v[2], large, sweep, ex, ey);
            cx = ex; cy = ey;
            break;
        }
        default:
            return;
        }
        prev = uc;
    }
}

/* ellipse as four cubic arcs (kappa 0.5523) */
static void ellipse_path(Ctx *C, const St *s, fx cx, fx cy, fx rx, fx ry)
{
    const fx k = 36195;
    fx kx = fmul(rx, k), ky = fmul(ry, k);
    begin_sub(C);
    add_pt(C, s, cx + rx, cy);
    cubic(C, s, cx + rx, cy, cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
    cubic(C, s, cx, cy + ry, cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
    cubic(C, s, cx - rx, cy, cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
    cubic(C, s, cx, cy - ry, cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
    close_sub(C);
}

static void rect_path(Ctx *C, const St *s, fx x, fx y, fx w, fx h, fx rx, fx ry)
{
    if (rx > w / 2) rx = w / 2;
    if (ry > h / 2) ry = h / 2;
    begin_sub(C);
    if (rx <= 0 || ry <= 0) {
        add_pt(C, s, x, y);
        add_pt(C, s, x + w, y);
        add_pt(C, s, x + w, y + h);
        add_pt(C, s, x, y + h);
    } else {
        const fx k = 36195;
        fx kx = fmul(rx, k), ky = fmul(ry, k);
        add_pt(C, s, x + rx, y);
        add_pt(C, s, x + w - rx, y);
        cubic(C, s, x + w - rx, y, x + w - rx + kx, y, x + w, y + ry - ky, x + w, y + ry);
        add_pt(C, s, x + w, y + h - ry);
        cubic(C, s, x + w, y + h - ry, x + w, y + h - ry + ky, x + w - rx + kx, y + h, x + w - rx, y + h);
        add_pt(C, s, x + rx, y + h);
        cubic(C, s, x + rx, y + h, x + rx - kx, y + h, x, y + h - ry + ky, x, y + h - ry);
        add_pt(C, s, x, y + ry);
        cubic(C, s, x, y + ry, x, y + ry - ky, x + rx - kx, y, x + rx, y);
    }
    close_sub(C);
}

static void points_path(Ctx *C, const St *s, const char *v, bool closed)
{
    const char *p = v, *end = v + strlen(v);
    fx x, y;
    begin_sub(C);
    while (num(&p, end, &x) && num(&p, end, &y)) add_pt(C, s, x, y);
    if (closed) close_sub(C);
}

/* ------------------------------------------------------------------ */
/* Rasterizer                                                          */
/* ------------------------------------------------------------------ */

static void add_edge(Ctx *C, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (y0 == y1 || C->nedges >= C->maxedges) return;
    struct Edge *e = &C->edges[C->nedges++];
    if (y0 < y1) { e->x0 = x0; e->y0 = y0; e->x1 = x1; e->y1 = y1; e->dir = 1; }
    else { e->x0 = x1; e->y0 = y1; e->x1 = x0; e->y1 = y0; e->dir = -1; }
}

/* closed polygon of n device points (arrays) */
static void add_poly(Ctx *C, const int32_t *xs, const int32_t *ys, int n)
{
    for (int i = 0; i < n; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        add_edge(C, xs[i], ys[i], xs[j], ys[j]);
    }
}

static void blend(uint32_t *d, uint32_t argb, uint32_t sa)
{
    if (!sa) return;
    if (sa > 255) sa = 255;
    uint32_t da = *d >> 24;
    if (sa == 255 || !da) { *d = (sa << 24) | (argb & 0xFFFFFF); return; }
    uint32_t inv = 255 - sa;
    uint32_t oa = sa + da * inv / 255;
    uint32_t r = (((argb >> 16) & 0xFF) * sa + ((*d >> 16) & 0xFF) * da * inv / 255) / oa;
    uint32_t g = (((argb >> 8) & 0xFF) * sa + ((*d >> 8) & 0xFF) * da * inv / 255) / oa;
    uint32_t b = ((argb & 0xFF) * sa + (*d & 0xFF) * da * inv / 255) / oa;
    *d = (oa << 24) | (r << 16) | (g << 8) | b;
}

static void sort_cross(struct Cross *c, int n)
{
    for (int i = 1; i < n; i++) {
        struct Cross t = c[i];
        int j = i;
        while (j > 0 && c[j - 1].x > t.x) { c[j] = c[j - 1]; j--; }
        c[j] = t;
    }
}

static void fill_edges(Ctx *C, bool evenodd, uint32_t argb, uint32_t alpha256)
{
    if (!C->nedges || !alpha256) { C->nedges = 0; return; }
    int32_t ymin = 0x7fffffff, ymax = -0x7fffffff;
    for (int i = 0; i < C->nedges; i++) {
        if (C->edges[i].y0 < ymin) ymin = C->edges[i].y0;
        if (C->edges[i].y1 > ymax) ymax = C->edges[i].y1;
    }
    int py0 = ymin < 0 ? 0 : ymin >> 8, py1 = ymax >> 8;
    if (py1 >= C->h) py1 = C->h - 1;
    uint32_t ca = ((argb >> 24) * alpha256) >> 8;
    for (int py = py0; py <= py1; py++) {
        int minx = C->w, maxx = -1;
        for (int sub = 0; sub < 4; sub++) {
            int32_t sy = (py << 8) + sub * 64 + 32;
            int nc = 0;
            for (int i = 0; i < C->nedges && nc < C->maxcross; i++) {
                const struct Edge *e = &C->edges[i];
                if (sy < e->y0 || sy >= e->y1) continue;
                int32_t x = e->x0 + (int32_t)((int64_t)(e->x1 - e->x0) * (sy - e->y0) / (e->y1 - e->y0));
                C->cross[nc].x = x;
                C->cross[nc].dir = e->dir;
                nc++;
            }
            if (nc < 2) continue;
            sort_cross(C->cross, nc);
            int wind = 0;
            for (int i = 0; i + 1 < nc; i++) {
                wind += C->cross[i].dir;
                bool in = evenodd ? ((i + 1) & 1) : wind != 0;
                if (!in) continue;
                int32_t a = C->cross[i].x, b = C->cross[i + 1].x;
                if (a < 0) a = 0;
                if (b > (C->w << 8)) b = C->w << 8;
                if (b <= a) continue;
                int pa = a >> 8, pb = (b - 1) >> 8;
                if (pa < minx) minx = pa;
                if (pb > maxx) maxx = pb;
                if (pa == pb) {
                    C->acc[pa] += (b - a) >> 2;
                } else {
                    C->acc[pa] += (((pa + 1) << 8) - a) >> 2;
                    for (int x = pa + 1; x < pb; x++) C->acc[x] += 64;
                    C->acc[pb] += (b - (pb << 8)) >> 2;
                }
            }
        }
        if (maxx < 0) continue;
        uint32_t *row = C->out + (size_t)py * (size_t)C->w;
        for (int x = minx; x <= maxx; x++) {
            int32_t cov = C->acc[x];
            C->acc[x] = 0;
            if (cov <= 0) continue;
            if (cov > 256) cov = 256;
            blend(&row[x], argb, (uint32_t)((ca * (uint32_t)cov) >> 8));
        }
    }
    C->nedges = 0;
}

/* consistently oriented polygon (positive signed area) */
static void add_oriented(Ctx *C, int32_t *xs, int32_t *ys, int n)
{
    int64_t area = 0;
    for (int i = 0; i < n; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        area += (int64_t)xs[i] * ys[j] - (int64_t)xs[j] * ys[i];
    }
    if (area < 0) {
        for (int i = 0; i < n / 2; i++) {
            int32_t t = xs[i]; xs[i] = xs[n - 1 - i]; xs[n - 1 - i] = t;
            t = ys[i]; ys[i] = ys[n - 1 - i]; ys[n - 1 - i] = t;
        }
    }
    add_poly(C, xs, ys, n);
}

static void add_disc(Ctx *C, int32_t cx, int32_t cy, int32_t r)
{
    /* 16-gon: cos/sin of k*22.5 degrees in Q16 */
    static const int32_t co[16] = {65536, 60547, 46341, 25080, 0, -25080, -46341, -60547, -65536, -60547, -46341, -25080, 0, 25080, 46341, 60547};
    int32_t xs[16], ys[16];
    for (int k = 0; k < 16; k++) {
        xs[k] = cx + (int32_t)(((int64_t)r * co[k]) >> 16);
        ys[k] = cy + (int32_t)(((int64_t)r * co[(k + 12) & 15]) >> 16);
    }
    add_oriented(C, xs, ys, 16);
}

static void stroke_polyline(Ctx *C, int s0, int n, bool closed, int32_t hw, uint8_t cap, uint8_t join)
{
    if (n < 2 && !(n == 1 && cap != CAP_BUTT)) return;
    const int32_t *X = C->px + s0, *Y = C->py + s0;
    int segs = closed ? n : n - 1;
    int32_t pnx = 0, pny = 0;
    bool have_prev = false;
    for (int i = 0; i < segs; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        int64_t dx = X[j] - X[i], dy = Y[j] - Y[i];
        int64_t len = (int64_t)isqrt64((uint64_t)(dx * dx + dy * dy));
        if (!len) continue;
        int32_t nx = (int32_t)(-dy * hw / len), ny = (int32_t)(dx * hw / len);
        int32_t ax = X[i], ay = Y[i], bx = X[j], by = Y[j];
        if (cap == CAP_SQUARE && !closed) {
            int32_t ex = (int32_t)(dx * hw / len), ey = (int32_t)(dy * hw / len);
            if (i == 0) { ax -= ex; ay -= ey; }
            if (i == segs - 1) { bx += ex; by += ey; }
        }
        int32_t xs[4] = {ax + nx, bx + nx, bx - nx, ax - nx};
        int32_t ys[4] = {ay + ny, by + ny, by - ny, ay - ny};
        add_oriented(C, xs, ys, 4);
        /* join with the previous segment */
        if (have_prev) {
            if (join == JOIN_ROUND) add_disc(C, X[i], Y[i], hw);
            else {
                int32_t t1x[3] = {X[i], X[i] + pnx, X[i] + nx}, t1y[3] = {Y[i], Y[i] + pny, Y[i] + ny};
                int32_t t2x[3] = {X[i], X[i] - pnx, X[i] - nx}, t2y[3] = {Y[i], Y[i] - pny, Y[i] - ny};
                add_oriented(C, t1x, t1y, 3);
                add_oriented(C, t2x, t2y, 3);
            }
        }
        pnx = nx; pny = ny;
        have_prev = true;
    }
    if (closed && have_prev) {
        if (join == JOIN_ROUND) add_disc(C, X[0], Y[0], hw);
    } else if (cap == CAP_ROUND) {
        add_disc(C, X[0], Y[0], hw);
        add_disc(C, X[n - 1], Y[n - 1], hw);
    }
}

/* fill and stroke whatever was flattened into the point buffers */
static void paint_shape(Ctx *C, const St *s)
{
    if (s->hidden) { C->npts = C->nsub = 0; return; }
    if (s->fill_kind == PAINT_COLOR) {
        for (int k = 0; k < C->nsub; k++) {
            int a = C->sub_start[k], b = k + 1 < C->nsub ? C->sub_start[k + 1] : C->npts;
            if (b - a >= 3) add_poly(C, C->px + a, C->py + a, b - a);
        }
        fill_edges(C, s->evenodd, s->fill, (uint32_t)((s->opacity * s->fill_op) >> 8));
    }
    if (s->stroke_kind == PAINT_COLOR && s->stroke_w > 0) {
        /* device width: stroke width times sqrt(|det|) */
        int64_t det = fmul(s->m.a, s->m.d) - fmul(s->m.b, s->m.c);
        if (det < 0) det = -det;
        fx sc = (fx)isqrt64((uint64_t)det << 16);
        int32_t hw = (int32_t)(fmul(s->stroke_w, sc) >> 9);       /* half width, 24.8 */
        if (hw < 64) hw = 64;                                      /* hairlines stay visible */
        for (int k = 0; k < C->nsub; k++) {
            int a = C->sub_start[k], b = k + 1 < C->nsub ? C->sub_start[k + 1] : C->npts;
            stroke_polyline(C, a, b - a, C->sub_closed[k], hw, s->cap, s->join);
            if (C->nedges > C->maxedges - 64) fill_edges(C, false, s->stroke, (uint32_t)((s->opacity * s->stroke_op) >> 8));
        }
        fill_edges(C, false, s->stroke, (uint32_t)((s->opacity * s->stroke_op) >> 8));
    }
    C->npts = C->nsub = 0;
}

/* ------------------------------------------------------------------ */
/* Tree walk                                                           */
/* ------------------------------------------------------------------ */

static void render_el(Ctx *C, int ei, const St *parent);

static void render_children(Ctx *C, int ei, const St *s)
{
    for (int k = C->D.el[ei].first; k >= 0; k = C->D.el[k].next) render_el(C, k, s);
}

static fx attr_num(const El *e, const char *name, fx dflt)
{
    char b[48];
    if (!attr_buf(e, name, b, sizeof(b))) return dflt;
    const char *p = b;
    fx v;
    return num(&p, b + strlen(b), &v) ? v : dflt;
}

/* map a viewBox onto (x, y, w, h), xMidYMid meet unless "none" */
static bool viewbox_mat(const El *e, fx x, fx y, fx w, fx h, Mat *out)
{
    char b[96];
    if (!attr_buf(e, "viewbox", b, sizeof(b))) return false;
    const char *p = b, *end = b + strlen(b);
    fx vb[4];
    for (int i = 0; i < 4; i++) if (!num(&p, end, &vb[i])) return false;
    if (vb[2] <= 0 || vb[3] <= 0) return false;
    fx sx = fdiv(w, vb[2]), sy = fdiv(h, vb[3]);
    char par[48];
    bool none = attr_buf(e, "preserveaspectratio", par, sizeof(par)) && ieq_n(par, 4, "none");
    fx tx = x, ty = y;
    if (!none) {
        bool slice = par[0] && find_str(par, par + strlen(par), "slice")[0];
        fx sc = slice ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy);
        tx += (w - fmul(vb[2], sc)) / 2;
        ty += (h - fmul(vb[3], sc)) / 2;
        sx = sy = sc;
    }
    Mat m = {sx, 0, 0, sy, tx - fmul(vb[0], sx), ty - fmul(vb[1], sy)};
    *out = m;
    return true;
}

static void render_el(Ctx *C, int ei, const St *parent)
{
    if (C->depth > 24) return;
    const El *e = &C->D.el[ei];
    if (name_is(e, "defs") || name_is(e, "symbol") || name_is(e, "clippath") || name_is(e, "mask") ||
        name_is(e, "lineargradient") || name_is(e, "radialgradient") || name_is(e, "pattern") || name_is(e, "title") ||
        name_is(e, "desc") || name_is(e, "metadata") || name_is(e, "style") || name_is(e, "filter") ||
        name_is(e, "marker") || name_is(e, "text") || name_is(e, "image") || name_is(e, "foreignobject") ||
        name_is(e, "script"))
        return;
    St s = *parent;
    uint32_t saved_current = C->current;
    resolve_style(C, e, &s);
    if (s.hidden) { C->current = saved_current; return; }
    char b[2048];
    if (attr_buf(e, "transform", b, sizeof(b))) parse_transform(b, &s.m);
    C->depth++;
    if (name_is(e, "g") || name_is(e, "a") || name_is(e, "switch")) {
        render_children(C, ei, &s);
    } else if (name_is(e, "svg")) {
        fx x = attr_num(e, "x", 0), y = attr_num(e, "y", 0);
        fx w, h;
        Mat vm;
        if (length_attr(e, "width", &w) && length_attr(e, "height", &h) && viewbox_mat(e, x, y, w, h, &vm)) s.m = mat_mul(&s.m, &vm);
        else { Mat t = mat_translate(x, y); s.m = mat_mul(&s.m, &t); }
        render_children(C, ei, &s);
    } else if (name_is(e, "use")) {
        char href[160];
        if (attr_buf(e, "href", href, sizeof(href)) || attr_buf(e, "xlink:href", href, sizeof(href))) {
            int t = href[0] == '#' ? find_id(&C->D, href + 1, strlen(href + 1)) : -1;
            if (t >= 0 && t != ei) {
                Mat tr = mat_translate(attr_num(e, "x", 0), attr_num(e, "y", 0));
                s.m = mat_mul(&s.m, &tr);
                const El *te = &C->D.el[t];
                if (name_is(te, "symbol")) {
                    fx w, h;
                    Mat vm;
                    if (length_attr(e, "width", &w) && length_attr(e, "height", &h) && viewbox_mat(te, 0, 0, w, h, &vm))
                        s.m = mat_mul(&s.m, &vm);
                    St ss = s;
                    resolve_style(C, te, &ss);
                    if (!ss.hidden) render_children(C, t, &ss);
                } else {
                    render_el(C, t, &s);
                }
            }
        }
    } else if (name_is(e, "path")) {
        const char *d;
        size_t dl;
        if (attr(e, "d", &d, &dl)) { flatten_path(C, &s, d, dl); paint_shape(C, &s); }
    } else if (name_is(e, "rect")) {
        fx w, h;
        if (length_attr(e, "width", &w) && length_attr(e, "height", &h) && w > 0 && h > 0) {
            fx rx = attr_num(e, "rx", -1), ry = attr_num(e, "ry", -1);
            if (rx < 0) rx = ry;
            if (ry < 0) ry = rx;
            rect_path(C, &s, attr_num(e, "x", 0), attr_num(e, "y", 0), w, h, rx, ry);
            paint_shape(C, &s);
        }
    } else if (name_is(e, "circle")) {
        fx r = attr_num(e, "r", 0);
        if (r > 0) { ellipse_path(C, &s, attr_num(e, "cx", 0), attr_num(e, "cy", 0), r, r); paint_shape(C, &s); }
    } else if (name_is(e, "ellipse")) {
        fx rx = attr_num(e, "rx", 0), ry = attr_num(e, "ry", 0);
        if (rx > 0 && ry > 0) { ellipse_path(C, &s, attr_num(e, "cx", 0), attr_num(e, "cy", 0), rx, ry); paint_shape(C, &s); }
    } else if (name_is(e, "line")) {
        begin_sub(C);
        add_pt(C, &s, attr_num(e, "x1", 0), attr_num(e, "y1", 0));
        add_pt(C, &s, attr_num(e, "x2", 0), attr_num(e, "y2", 0));
        s.fill_kind = PAINT_NONE;
        paint_shape(C, &s);
    } else if (name_is(e, "polyline") || name_is(e, "polygon")) {
        if (attr_buf(e, "points", b, sizeof(b))) { points_path(C, &s, b, name_is(e, "polygon")); paint_shape(C, &s); }
    }
    C->depth--;
    C->current = saved_current;
}

/* ------------------------------------------------------------------ */
/* Entry points                                                        */
/* ------------------------------------------------------------------ */

bool svg_detect(const uint8_t *data, size_t len)
{
    size_t n = len < 1024 ? len : 1024;
    const char *s = (const char *)data;
    for (size_t i = 0; i + 4 <= n; i++)
        if (s[i] == '<' && lc(s[i + 1]) == 's' && lc(s[i + 2]) == 'v' && lc(s[i + 3]) == 'g') return true;
    return false;
}

/* the root <svg> element's start tag, parsed on its own */
static bool root_el(const uint8_t *data, size_t len, El *e)
{
    const char *s = (const char *)data, *end = s + len;
    const char *p = s;
    for (; p + 4 <= end; p++)
        if (p[0] == '<' && lc(p[1]) == 's' && lc(p[2]) == 'v' && lc(p[3]) == 'g' && (p + 4 == end || is_ws(p[4]) || p[4] == '>' || p[4] == '/')) break;
    if (p + 4 > end) return false;
    memset(e, 0, sizeof(*e));
    e->name = p + 1;
    e->nlen = 3;
    e->attrs = p + 4;
    const char *q = p + 4;
    char quote = 0;
    while (q < end && (quote || *q != '>')) {
        if (quote) { if (*q == quote) quote = 0; }
        else if (*q == '"' || *q == '\'') quote = *q;
        q++;
    }
    e->alen = (uint32_t)(q - e->attrs);
    return true;
}

bool svg_info(const uint8_t *data, size_t len, int *w, int *h)
{
    El e;
    if (!data || !root_el(data, len, &e)) return false;
    fx fw = 0, fh = 0;
    bool hw = length_attr(&e, "width", &fw), hh = length_attr(&e, "height", &fh);
    char b[96];
    fx vb[4] = {0, 0, 0, 0};
    bool hv = false;
    if (attr_buf(&e, "viewbox", b, sizeof(b))) {
        const char *p = b, *end = b + strlen(b);
        hv = true;
        for (int i = 0; i < 4; i++) if (!num(&p, end, &vb[i])) hv = false;
        if (vb[2] <= 0 || vb[3] <= 0) hv = false;
    }
    if (!hw && !hh) {
        if (hv) { fw = vb[2]; fh = vb[3]; }
        else { fw = 300 * FONE; fh = 150 * FONE; }
    } else if (!hw) {
        fw = hv ? fmul(fh, fdiv(vb[2], vb[3])) : fh;
    } else if (!hh) {
        fh = hv ? fmul(fw, fdiv(vb[3], vb[2])) : fw;
    }
    int iw = (int)((fw + FONE / 2) >> 16), ih = (int)((fh + FONE / 2) >> 16);
    if (iw < 1) iw = 1;
    if (ih < 1) ih = 1;
    if (iw > 4096) iw = 4096;
    if (ih > 4096) ih = 4096;
    *w = iw;
    *h = ih;
    return true;
}

bool svg_render(const uint8_t *data, size_t len, uint32_t *argb, int w, int h, uint32_t current_color,
                uint8_t *scratch, size_t scratch_size)
{
    if (!data || !argb || w <= 0 || h <= 0 || w > 8192 || h > 8192 || !scratch) return false;
    memset(argb, 0, (size_t)w * (size_t)h * 4);
    static Ctx C;
    memset(&C, 0, sizeof(C));
    C.P.p = scratch;
    C.P.cap = scratch_size;
    C.D.src = (const char *)data;
    C.D.len = len;
    C.D.el = (El *)palloc(&C.P, sizeof(El) * MAX_EL);
    C.maxpts = 16384;
    C.maxsub = 2048;
    C.maxedges = 16384;
    C.maxcross = 2048;
    C.px = (int32_t *)palloc(&C.P, sizeof(int32_t) * (size_t)C.maxpts);
    C.py = (int32_t *)palloc(&C.P, sizeof(int32_t) * (size_t)C.maxpts);
    C.sub_start = (int32_t *)palloc(&C.P, sizeof(int32_t) * (size_t)C.maxsub);
    C.sub_closed = (uint8_t *)palloc(&C.P, (size_t)C.maxsub);
    C.edges = (struct Edge *)palloc(&C.P, sizeof(struct Edge) * (size_t)C.maxedges);
    C.cross = (struct Cross *)palloc(&C.P, sizeof(struct Cross) * (size_t)C.maxcross);
    C.acc = (int32_t *)palloc(&C.P, sizeof(int32_t) * (size_t)(w + 2));
    if (!C.D.el || !C.px || !C.py || !C.sub_start || !C.sub_closed || !C.edges || !C.cross || !C.acc) return false;
    memset(C.acc, 0, sizeof(int32_t) * (size_t)(w + 2));
    C.out = argb;
    C.w = w;
    C.h = h;
    C.current = current_color | (current_color >> 24 ? 0 : 0xFF000000u);
    parse_xml(&C.D);
    int root = -1;
    for (int i = 0; i < C.D.nel; i++) if (name_is(&C.D.el[i], "svg")) { root = i; break; }
    if (root < 0) return false;
    collect_rules(&C);
    St s;
    memset(&s, 0, sizeof(s));
    s.fill_kind = PAINT_COLOR;
    s.fill = 0xFF000000u;
    s.stroke_kind = PAINT_NONE;
    s.stroke_w = FONE;
    s.opacity = s.fill_op = s.stroke_op = 256;
    /* root viewport: viewBox onto the output, else intrinsic size scaled */
    const El *re = &C.D.el[root];
    Mat vm;
    if (!viewbox_mat(re, 0, 0, (fx)w << 16, (fx)h << 16, &vm)) {
        int iw, ih;
        svg_info(data, len, &iw, &ih);
        Mat m = {((fx)w << 16) / iw, 0, 0, ((fx)h << 16) / ih, 0, 0};
        vm = m;
    }
    s.m = vm;
    /* the root's own presentation attributes and style (not its x/y/size) */
    resolve_style(&C, re, &s);
    if (s.hidden) return true;
    C.depth = 1;
    render_children(&C, root, &s);
    return true;
}
