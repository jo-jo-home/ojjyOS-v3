/* Calculator state model. Fixed point only. Created by Jonas Lee. */
#include "ui/calculator.h"
#include "ui/calcmath.h"
#include "string.h"

void calculator_reset(CalculatorState *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    strcpy(s->display, "0");
    s->clear_next = true;
    s->pressed_key = -1;
}

static int64_t value(const CalculatorState *s)
{
    int64_t n = 0;
    calc_parse(s->display, &n);
    return n;
}

static void history_operand(CalculatorState *s)
{
    calc_format(s->acc, s->history, sizeof(s->history));
    size_t n = strlen(s->history);
    s->history[n++] = ' '; s->history[n++] = s->op; s->history[n] = '\0';
}

static void evaluate(CalculatorState *s)
{
    int64_t result;
    history_operand(s);
    size_t n = strlen(s->history);
    s->history[n++] = ' ';
    /* Each operand is at most 23 bytes; the fixed 72-byte history fits
     * both, the operator and equals without an unbounded append. */
    strncpy(s->history + n, s->display, sizeof(s->history) - n - 3);
    s->history[sizeof(s->history) - 3] = '\0';
    n = strlen(s->history);
    s->history[n++] = ' '; s->history[n++] = '='; s->history[n] = '\0';
    CalcStatus status = calc_apply(s->op, s->acc, value(s), &result);
    if (status != CALC_OK) {
        strcpy(s->display, status == CALC_DIV_ZERO ? "Can't divide by 0" : "Too large");
        s->has_error = true;
        s->op = 0;
    } else {
        s->acc = result;
        calc_format(result, s->display, sizeof(s->display));
    }
}

int calculator_key_for_char(char c)
{
    if (c == 'C') c = 'c';
    if (c == '\n' || c == '\r') c = '=';
    if (c == '\b') return 18;
    static const char keys[] = "cn%/789*456-123+0.b=";
    for (int i = 0; i < 20; i++) if (c == keys[i]) return i;
    return -1;
}

bool calculator_press(CalculatorState *s, int key, uint64_t now_ms)
{
    if (!s || key < 0 || key >= 20) return false;
    static const char keys[] = "cn%/789*456-123+0.b=";
    char c = keys[key];
    if (c == 'c') calculator_reset(s);
    else if ((c >= '0' && c <= '9') || c == '.') {
        if (s->has_error) calculator_reset(s);
        if (s->clear_next || strcmp(s->display, "0") == 0) {
            if (c == '.') strcpy(s->display, "0.");
            else { s->display[0] = c; s->display[1] = '\0'; }
            s->clear_next = false;
        } else {
            size_t n = strlen(s->display);
            bool point = false;
            for (size_t i = 0; i < n; i++) if (s->display[i] == '.') point = true;
            if (n < 16 && !(c == '.' && point)) {
                s->display[n] = c; s->display[n + 1] = '\0';
                int64_t probe;
                if (c != '.' && !calc_parse(s->display, &probe)) s->display[n] = '\0';
            }
        }
    } else if (c == '+' || c == '-' || c == '*' || c == '/') {
        if (s->has_error) calculator_reset(s);
        if (s->op && !s->clear_next) evaluate(s);
        else if (!s->op) s->acc = value(s);
        if (!s->has_error) { s->op = c; s->clear_next = true; history_operand(s); }
    } else if (c == '=') {
        if (s->op && !s->has_error) { evaluate(s); s->op = 0; s->clear_next = true; }
    } else if (c == 'b') {
        size_t n = strlen(s->display);
        if (s->has_error) calculator_reset(s);
        else if (n > 1 && !s->clear_next) s->display[n - 1] = '\0';
        else { strcpy(s->display, "0"); s->clear_next = true; }
    } else if (!s->has_error) {
        int64_t n = value(s);
        if (c == 'n') calc_format(-n, s->display, sizeof(s->display));
        if (c == '%') {
            calc_apply('/', n, 100 * CALC_SCALE, &n);
            calc_format(n, s->display, sizeof(s->display)); s->clear_next = true;
        }
    }
    s->pressed_key = key;
    s->pressed_until = now_ms + 180;
    return true;
}

bool calculator_expire(CalculatorState *s, uint64_t now_ms)
{
    if (!s || s->pressed_key < 0 || now_ms < s->pressed_until) return false;
    s->pressed_key = -1;
    return true;
}

bool calculator_layout(int w, int h, CalculatorLayout *l)
{
    if (!l || w < 140 || h < 240) return false;
    l->lcd_x = 12; l->lcd_y = 10; l->lcd_w = w - 24; l->lcd_h = 72;
    l->grid_y = 92;
    l->key_w = (w - 42) / 4;
    l->key_h = (h - l->grid_y - 34) / 5;
    return true;
}

int calculator_hit(const CalculatorLayout *l, int x, int y)
{
    if (!l) return -1;
    for (int r = 0; r < 5; r++) for (int c = 0; c < 4; c++) {
        int bx = l->lcd_x + c * (l->key_w + 6), by = l->grid_y + r * (l->key_h + 6);
        if (x >= bx && x < bx + l->key_w && y >= by && y < by + l->key_h) return r * 4 + c;
    }
    return -1;
}
