/*
 * ojjyOS v3 - Calculator arithmetic (see calcmath.h)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "ui/calcmath.h"
#include "string.h"

bool calc_parse(const char *s, int64_t *out)
{
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    int64_t whole = 0, frac = 0, scale = CALC_SCALE;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
        whole = whole * 10 + (*s++ - '0');
        if (++digits > 11) return false;
    }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            if (scale > 1) { scale /= 10; frac += (*s - '0') * scale; }
            s++;
        }
    }
    if (*s) return false;
    int64_t v = whole * CALC_SCALE + frac;
    if (v >= CALC_LIMIT) return false;
    *out = neg ? -v : v;
    return true;
}

void calc_format(int64_t v, char *out, size_t size)
{
    if (size < 4) { if (size) out[0] = '\0'; return; }
    char buf[40];
    size_t n = 0;
    uint64_t u = v < 0 ? (uint64_t)(-v) : (uint64_t)v;
    uint64_t whole = u / CALC_SCALE, frac = u % CALC_SCALE;
    if (v < 0 && u) buf[n++] = '-';
    char w[24];
    utoa(whole, w, 10);
    for (char *c = w; *c; c++) buf[n++] = *c;
    if (frac) {
        char f[8];
        for (int i = 5; i >= 0; i--) { f[i] = (char)('0' + frac % 10); frac /= 10; }
        int last = 5;
        while (last > 0 && f[last] == '0') last--;
        buf[n++] = '.';
        for (int i = 0; i <= last; i++) buf[n++] = f[i];
    }
    buf[n] = '\0';
    strncpy(out, buf, size - 1);
    out[size - 1] = '\0';
}

static CalcStatus checked(int64_t v, int64_t *out)
{
    if (v >= CALC_LIMIT || v <= -CALC_LIMIT) return CALC_OVERFLOW;   /* 12 digits won't fit */
    *out = v;
    return CALC_OK;
}

CalcStatus calc_apply(char op, int64_t a, int64_t b, int64_t *out)
{
    switch (op) {
        case '+': return checked(a + b, out);          /* |a|,|b| <= 1e17: no wrap */
        case '-': return checked(a - b, out);
        case '*': {
            bool neg = (a < 0) != (b < 0);
            uint64_t x = a < 0 ? (uint64_t)-a : (uint64_t)a, y = b < 0 ? (uint64_t)-b : (uint64_t)b;
            uint64_t xi = x / CALC_SCALE, xf = x % CALC_SCALE, yi = y / CALC_SCALE, yf = y % CALC_SCALE;
            /* x*y/S = xi*yi*S + xi*yf + xf*yi + xf*yf/S, each checked */
            uint64_t t1;
            if (__builtin_mul_overflow(xi, yi, &t1) || __builtin_mul_overflow(t1, (uint64_t)CALC_SCALE, &t1))
                return CALC_OVERFLOW;
            uint64_t r = t1 + xi * yf + xf * yi + (xf * yf) / CALC_SCALE;   /* xi,yi <= 1e11, yf,xf < 1e6 */
            if (r >= (uint64_t)CALC_LIMIT) return CALC_OVERFLOW;
            *out = neg ? -(int64_t)r : (int64_t)r;
            return CALC_OK;
        }
        case '/': {
            if (b == 0) return CALC_DIV_ZERO;
            bool neg = (a < 0) != (b < 0);
            uint64_t x = a < 0 ? (uint64_t)-a : (uint64_t)a, y = b < 0 ? (uint64_t)-b : (uint64_t)b;
            uint64_t q = x / y, r = x % y;
            if (q > (uint64_t)CALC_LIMIT / CALC_SCALE) return CALC_OVERFLOW;
            q *= CALC_SCALE;
            uint64_t place = CALC_SCALE / 10;
            for (int i = 0; i < 6; i++) {           /* long division: r < y <= 1e17, r*10 fits */
                r *= 10;
                q += (r / y) * place;
                r %= y;
                place /= 10;
            }
            if (q >= (uint64_t)CALC_LIMIT) return CALC_OVERFLOW;
            *out = neg ? -(int64_t)q : (int64_t)q;
            return CALC_OK;
        }
        default:
            *out = b;
            return CALC_OK;
    }
}
