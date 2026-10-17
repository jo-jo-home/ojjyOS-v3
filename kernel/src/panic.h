/*
 * ojjyOS v3 Kernel - Panic Handler
 *
 * Displays kernel panic screen and halts.
 */

#ifndef _OJJY_PANIC_H
#define _OJJY_PANIC_H

#include "types.h"
#ifdef __x86_64__
#include "idt.h"
#endif

/* Kernel panic - displays error and halts */
void panic(const char *message);

#ifdef __x86_64__
/* Panic with register dump from interrupt frame */
void panic_with_frame(const char *message, InterruptFrame *frame);
#endif

/* Assert macro */
#define ASSERT(cond) do { \
    if (!(cond)) { \
        panic("Assertion failed: " #cond); \
    } \
} while (0)

#endif /* _OJJY_PANIC_H */
