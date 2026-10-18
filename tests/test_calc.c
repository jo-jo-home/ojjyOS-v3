/* Calculator fixed-point arithmetic tests. Created by Jonas Lee. */
#include "ui/calcmath.h"
#include "test_support.h"

static bool eval(const char *a, char op, const char *b, const char *expect)
{
    int64_t x, y, r;
    char out[40];
    if (!calc_parse(a, &x) || !calc_parse(b, &y)) return false;
    if (calc_apply(op, x, y, &r) != CALC_OK) return false;
    calc_format(r, out, sizeof(out));
    return strcmp(out, expect) == 0;
}

static CalcStatus status(const char *a, char op, const char *b)
{
    int64_t x, y, r;
    calc_parse(a, &x); calc_parse(b, &y);
    return calc_apply(op, x, y, &r);
}

int main(void)
{
    int64_t v;
    char out[40];
    CHECK(calc_parse("12.5", &v) && v == 12500000, "parse decimal");
    CHECK(calc_parse("-0.000001", &v) && v == -1, "parse smallest unit");
    CHECK(calc_parse("3.1415926535", &v) && v == 3141592, "extra digits truncated");
    CHECK(!calc_parse("1.2.3", &v) && !calc_parse("abc", &v) && !calc_parse("123456789012", &v), "reject bad input");
    calc_format(-2500000, out, sizeof(out));
    CHECK(strcmp(out, "-2.5") == 0, "format trims zeros");
    calc_format(7 * CALC_SCALE, out, sizeof(out));
    CHECK(strcmp(out, "7") == 0, "format integer");
    calc_format(0, out, sizeof(out));
    CHECK(strcmp(out, "0") == 0, "format zero");
    CHECK(eval("0.1", '+', "0.2", "0.3"), "0.1 + 0.2 = 0.3 exactly");
    CHECK(eval("10", '/', "4", "2.5"), "10 / 4");
    CHECK(eval("1", '/', "3", "0.333333"), "1 / 3 to six places");
    CHECK(eval("-7.5", '*', "2", "-15"), "negative multiply");
    CHECK(eval("1.5", '*', "1.5", "2.25"), "fractional multiply");
    CHECK(eval("99999999999", '*', "1", "99999999999"), "largest value");
    CHECK(eval("5", '-', "8", "-3"), "subtract below zero");
    CHECK(eval("-9", '/', "3", "-3"), "negative divide");
    CHECK(status("1", '/', "0") == CALC_DIV_ZERO, "divide by zero");
    CHECK(status("99999999999", '*', "99999999999") == CALC_OVERFLOW, "multiply overflow reported");
    CHECK(status("99999999999", '+', "1") == CALC_OVERFLOW, "add overflow reported");
    CHECK(status("99999999999", '/', "0.000001") == CALC_OVERFLOW, "divide overflow reported");
    return finish();
}
