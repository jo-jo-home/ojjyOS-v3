#ifndef OJJY_A64_USER_ARCH_H
#define OJJY_A64_USER_ARCH_H
#include "user.h"
typedef struct {
    uint64_t x[31];
    uint64_t sp, pc, pstate, esr, far;
} A64UserFrame;
_Static_assert(sizeof(A64UserFrame) == 288, "user assembly frame size");
_Static_assert(__builtin_offsetof(A64UserFrame, sp) == 248, "user assembly SP offset");
_Static_assert(__builtin_offsetof(A64UserFrame, pc) == 256, "user assembly PC offset");
int a64_user_sync(A64UserFrame *frame);
int a64_user_irq(A64UserFrame *frame);
#endif
