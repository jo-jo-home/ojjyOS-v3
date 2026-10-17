/* Small freestanding-friendly host test helpers. Created by Jonas Lee. */
#include "string.h"
extern long write(int fd, const void *buf, unsigned long count);
static int checks, failures;
static void emit(const char *s)
{
    write(1, s, (unsigned long)strlen(s));
    write(1, "\n", 1);
}
#define CHECK(condition, label) do { \
    checks++; \
    if (!(condition)) { failures++; emit("FAIL: " label); } \
} while (0)
static int finish(void)
{
    char number[24];
    utoa((uint64_t)checks, number, 10);
    emit(number);
    emit(failures ? "TESTS FAILED" : "ALL TESTS PASSED");
    return failures != 0;
}
void serial_printf(const char *fmt, ...) { (void)fmt; }
void serial_puts(const char *s) { (void)s; }
