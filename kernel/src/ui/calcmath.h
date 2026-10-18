/*
 * ojjyOS v3 - Calculator arithmetic (fixed point, no floating point)
 *
 * Values are int64 scaled by 10^6 (six decimal places) and kept strictly
 * inside +/- 10^11 so every intermediate product/remainder fits in 64 bits.
 * Results that would leave that range are reported as overflow.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_CALCMATH_H
#define _OJJY_CALCMATH_H

#include "types.h"

#define CALC_SCALE  1000000LL
#define CALC_LIMIT  (100000000000LL * CALC_SCALE)     /* 1e11, scaled */

typedef enum { CALC_OK = 0, CALC_DIV_ZERO, CALC_OVERFLOW } CalcStatus;

bool calc_parse(const char *text, int64_t *out);            /* "-12.5" -> scaled */
void calc_format(int64_t value, char *out, size_t size);    /* trims trailing zeros */
CalcStatus calc_apply(char op, int64_t a, int64_t b, int64_t *out);   /* + - * / */

#endif /* _OJJY_CALCMATH_H */
