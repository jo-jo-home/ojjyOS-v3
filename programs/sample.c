/* Tiny real EL0 programs, including deliberate fault probes. Jonas Lee. */
#include "user_abi.h"
typedef unsigned long U;
static long call(U n, U a, U b, U c)
{
    register U x0 __asm__("x0") = a, x1 __asm__("x1") = b, x2 __asm__("x2") = c;
    register U x8 __asm__("x8") = n;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return (long)x0;
}
static void print(const char *s, U n) { call(USER_SYS_WRITE, 1, (U)s, n); }
static __attribute__((noreturn)) void finish(long code)
{
    call(USER_SYS_EXIT, (U)code, 0, 0);
    for (;;) __asm__ volatile("b .");
}
static volatile U bss[128];
static volatile U data = 123;
void _start(void)
{
#if CASE == 0
    for (U i = 0; i < 128; i++) if (bss[i]) finish(1);
    bss[0] = data;
    if (bss[0] != 123 || call(USER_SYS_GETPID, 0, 0, 0) <= 4 ||
        call(USER_SYS_GETUID, 0, 0, 0) < 501 || call(USER_SYS_UPTIME, 0, 0, 0) < 0) finish(2);
    print("Hello from protected EL0!\n", 26);
    finish(0);
#elif CASE == 1
    __asm__ volatile("ldr x0, [%0]" : : "r"(0x41000000UL) : "x0", "memory");
#elif CASE == 2
    __asm__ volatile("str xzr, [%0]" : : "r"(0x41000000UL) : "memory");
#elif CASE == 3
    __asm__ volatile("str xzr, [%0]" : : "r"((U)_start) : "memory");
#elif CASE == 4
    U insn = 0xd65f03c0;
    __asm__ volatile("br %0" : : "r"(&insn) : "memory");
#elif CASE == 5
    __asm__ volatile("mov x0, xzr; ldr x0, [x0]" : : : "x0", "memory");
#elif CASE == 6
    __asm__ volatile("msr daifset, #0xf");
#elif CASE == 7
    __asm__ volatile(".inst 0x9e6703e0"); /* fmov d0,xzr: deliberate FP trap */
#elif CASE == 8
    for (;;) __asm__ volatile("b .");
#elif CASE == 9
    if (call(USER_SYS_WRITE, 1, 0x41000000, 16) != -14 ||
        call(USER_SYS_WRITE, 1, ~0UL - 4, 16) != -14 ||
        call(USER_SYS_WRITE, 1, 0x1001fefffUL, 2) != -14 ||
        call(USER_SYS_WRITE, 3, (U)&data, 1) != -9 ||
        call(USER_SYS_WRITE, 1, (U)&data, 513) != -22 ||
        call(0xffff, 0, 0, 0) != -38 || call(9, 0x41000000, 0, 0) != -38 ||
        call(USER_SYS_EXIT, 0x100000000UL, 0, 0) != -22 ||
        call(USER_SYS_WRITE, 1, ~0UL, 0) != 0) finish(3);
    print("Invalid pointers and calls rejected\n", 36);
    finish(0);
#elif CASE == 10
    for (U i = 0; i < 300; i++) call(USER_SYS_GETPID, 0, 0, 0);
#elif CASE == 11
    for (U i = 0; i < 9; i++) {
        long rc = call(USER_SYS_WRITE, 1, (U)bss, 512);
        if (rc != (i < 7 ? 512 : -28)) finish(4);
    }
    finish(0);
#elif CASE == 12
    __asm__ volatile("svc #1");
#elif CASE == 13
    __asm__ volatile("ldr x0, [%0]" : : "r"(0x1001fa000UL) : "x0", "memory");
#elif CASE == 14
    __asm__ volatile("wfi");
#elif CASE == 15
    __asm__ volatile("ldr x0, [%0]" : : "r"(0x09000000UL) : "x0", "memory");
#elif CASE == 16
    __asm__ volatile("br %0" : : "r"(0x41000000UL) : "memory");
#elif CASE == 17
    __asm__ volatile("mov sp, %0; str xzr, [sp]" : : "r"(0x41000000UL) : "memory");
#endif
    finish(99);
}
