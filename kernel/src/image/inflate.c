/*
 * ojjyOS v3 - DEFLATE decompression (see inflate.h)
 *
 * Canonical Huffman decoding with count/symbol tables (the classic "puff"
 * structure): small, obviously bounded, fast enough for web pages and PNGs.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "image/inflate.h"
#include "string.h"

#define MAXBITS   15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct {
    const uint8_t *in;
    size_t inlen, incnt;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t outcap, outcnt;
    int err;                    /* 0 ok, -1 corrupt, -2 output full */
} State;

typedef struct {
    int16_t count[MAXBITS + 1];
    int16_t symbol[FIXLCODES];
} Huffman;

static int bits(State *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) { s->err = -1; return 0; }
        val |= (uint32_t)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static int stored(State *s)
{
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return -1;
    unsigned len = s->in[s->incnt] | (s->in[s->incnt + 1] << 8);
    unsigned nlen = s->in[s->incnt + 2] | (s->in[s->incnt + 3] << 8);
    s->incnt += 4;
    if (len != (~nlen & 0xffff)) return -1;
    if (s->incnt + len > s->inlen) return -1;
    if (s->outcnt + len > s->outcap) return -2;
    memcpy(s->out + s->outcnt, s->in + s->incnt, len);
    s->outcnt += len;
    s->incnt += len;
    return 0;
}

static int decode(State *s, const Huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code |= bits(s, 1);
        if (s->err) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* Build a decoding table from code lengths. Returns 0 when complete,
 * > 0 when incomplete (allowed for some distance codes), < 0 when over-subscribed. */
static int construct(Huffman *h, const int16_t *length, int n)
{
    for (int len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    int16_t offs[MAXBITS + 1];
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) offs[len + 1] = (int16_t)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++) if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (int16_t)sym;
    return left;
}

static const int16_t lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const int16_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const int16_t dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                  4097, 6145, 8193, 12289, 16385, 24577};
static const int16_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int codes(State *s, const Huffman *lencode, const Huffman *distcode)
{
    for (;;) {
        int symbol = decode(s, lencode);
        if (symbol < 0 || s->err) return -1;
        if (symbol < 256) {
            if (s->outcnt >= s->outcap) return -2;
            s->out[s->outcnt++] = (uint8_t)symbol;
        } else if (symbol == 256) {
            return 0;
        } else {
            symbol -= 257;
            if (symbol >= 29) return -1;
            int len = lbase[symbol] + bits(s, lext[symbol]);
            int ds = decode(s, distcode);
            if (ds < 0 || ds >= 30 || s->err) return -1;
            size_t dist = (size_t)(dbase[ds] + bits(s, dext[ds]));
            if (s->err || dist > s->outcnt) return -1;
            if (s->outcnt + (size_t)len > s->outcap) return -2;
            uint8_t *o = s->out + s->outcnt;
            const uint8_t *from = o - dist;
            for (int i = 0; i < len; i++) o[i] = from[i];      /* may overlap: byte by byte */
            s->outcnt += (size_t)len;
        }
    }
}

static Huffman fixed_len, fixed_dist;
static bool fixed_ready;

static int fixed(State *s)
{
    if (!fixed_ready) {
        int16_t lengths[FIXLCODES];
        int sym = 0;
        for (; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < FIXLCODES; sym++) lengths[sym] = 8;
        construct(&fixed_len, lengths, FIXLCODES);
        for (sym = 0; sym < MAXDCODES; sym++) lengths[sym] = 5;
        construct(&fixed_dist, lengths, MAXDCODES);
        fixed_ready = true;
    }
    return codes(s, &fixed_len, &fixed_dist);
}

static int dynamic(State *s)
{
    static const int16_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int16_t lengths[MAXLCODES + MAXDCODES];
    Huffman lencode, distcode;
    int nlen = bits(s, 5) + 257;
    int ndist = bits(s, 5) + 1;
    int ncode = bits(s, 4) + 4;
    if (s->err || nlen > MAXLCODES || ndist > MAXDCODES) return -1;
    int index;
    for (index = 0; index < ncode; index++) lengths[order[index]] = (int16_t)bits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (s->err) return -1;
    if (construct(&lencode, lengths, 19) != 0) return -1;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lencode);
        if (symbol < 0 || s->err) return -1;
        if (symbol < 16) {
            lengths[index++] = (int16_t)symbol;
        } else {
            int len = 0;
            if (symbol == 16) {
                if (index == 0) return -1;
                len = lengths[index - 1];
                symbol = 3 + bits(s, 2);
            } else if (symbol == 17) {
                symbol = 3 + bits(s, 3);
            } else {
                symbol = 11 + bits(s, 7);
            }
            if (s->err || index + symbol > nlen + ndist) return -1;
            while (symbol--) lengths[index++] = (int16_t)len;
        }
    }
    if (lengths[256] == 0) return -1;
    int err = construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return -1;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return -1;
    return codes(s, &lencode, &distcode);
}

long inflate_raw(const uint8_t *in, size_t len, uint8_t *out, size_t cap, size_t *consumed)
{
    State s;
    memset(&s, 0, sizeof(s));
    s.in = in;
    s.inlen = len;
    s.out = out;
    s.outcap = cap;
    int last, err;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.err) return -1;
        err = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -1;
        if (err) return err == -2 ? -2 : -1;
    } while (!last);
    if (consumed) *consumed = s.incnt;
    return (long)s.outcnt;
}

long zlib_inflate(const uint8_t *in, size_t len, uint8_t *out, size_t cap)
{
    if (len < 6) return -1;
    if ((in[0] & 0x0F) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20)) return -1;
    return inflate_raw(in + 2, len - 2, out, cap, NULL);
}

long gzip_inflate(const uint8_t *in, size_t len, uint8_t *out, size_t cap)
{
    if (len < 18 || in[0] != 0x1F || in[1] != 0x8B || in[2] != 8) return -1;
    uint8_t flags = in[3];
    size_t p = 10;
    if (flags & 4) {                        /* FEXTRA */
        if (p + 2 > len) return -1;
        size_t x = in[p] | (in[p + 1] << 8);
        p += 2 + x;
    }
    if (flags & 8) { while (p < len && in[p]) p++; p++; }     /* FNAME */
    if (flags & 16) { while (p < len && in[p]) p++; p++; }    /* FCOMMENT */
    if (flags & 2) p += 2;                  /* FHCRC */
    if (p >= len) return -1;
    return inflate_raw(in + p, len - p, out, cap, NULL);
}
