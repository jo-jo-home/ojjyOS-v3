/* Calculator input, history and shared hit geometry. Created by Jonas Lee. */
#ifndef OJJY_CALCULATOR_H
#define OJJY_CALCULATOR_H
#include "types.h"

typedef struct {
    char display[24];
    int64_t acc;
    char op;
    bool clear_next, has_error;
    char history[72];
    int pressed_key;
    uint64_t pressed_until;
} CalculatorState;

typedef struct { int lcd_x, lcd_y, lcd_w, lcd_h, grid_y, key_w, key_h; } CalculatorLayout;
void calculator_reset(CalculatorState *state);
bool calculator_press(CalculatorState *state, int key, uint64_t now_ms);
bool calculator_expire(CalculatorState *state, uint64_t now_ms);
int calculator_key_for_char(char key);
bool calculator_layout(int width, int height, CalculatorLayout *out);
int calculator_hit(const CalculatorLayout *layout, int x, int y);
#endif
