/*
 * ojjyOS v3 Kernel - Serial Output (arch-neutral helpers)
 *
 * serial_puts / serial_puthex / serial_printf are shared by all
 * architectures and build on the arch primitive serial_putc().
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "serial.h"
#include "string.h"

/*
 * Write a null-terminated string
 */
void serial_puts(const char *str)
{
    if (!str) return;
    while (*str) {
        serial_putc(*str++);
    }
}

/*
 * Write a hex number (with 0x prefix)
 */
void serial_puthex(uint64_t value)
{
    char buf[19];  /* "0x" + 16 hex digits + null */
    char *p = buf + 18;

    *p = '\0';

    /* Generate hex digits in reverse */
    do {
        int digit = value & 0xF;
        *--p = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        value >>= 4;
    } while (value && p > buf + 2);

    /* Pad with zeros to at least 2 digits */
    while (p > buf + 16) {
        *--p = '0';
    }

    /* Add 0x prefix */
    *--p = 'x';
    *--p = '0';

    serial_puts(p);
}

/*
 * Simple formatted print
 * Supports: %s, %d, %u, %x, %p, %c, %% (with optional 'l'/'ll' modifiers)
 */
void serial_printf(const char *fmt, ...)
{
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            serial_putc(*fmt++);
            continue;
        }

        fmt++;  /* Skip '%' */

        /* Handle 'l' length modifier */
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
            if (*fmt == 'l') {
                fmt++;  /* Consume second 'l' in 'll' */
            }
        }

        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(args, const char *);
            serial_puts(s ? s : "(null)");
            break;
        }
        case 'd': {
            int64_t val;
            if (is_long) {
                val = __builtin_va_arg(args, int64_t);
            } else {
                val = __builtin_va_arg(args, int);
            }
            char buf[32];
            itoa(val, buf, 10);
            serial_puts(buf);
            break;
        }
        case 'u': {
            uint64_t val;
            if (is_long) {
                val = __builtin_va_arg(args, uint64_t);
            } else {
                val = __builtin_va_arg(args, unsigned int);
            }
            char buf[32];
            utoa(val, buf, 10);
            serial_puts(buf);
            break;
        }
        case 'x': {
            uint64_t val;
            if (is_long) {
                val = __builtin_va_arg(args, uint64_t);
            } else {
                val = __builtin_va_arg(args, unsigned int);
            }
            char buf[32];
            utoa(val, buf, 16);
            serial_puts(buf);
            break;
        }
        case 'p': {
            uint64_t val = __builtin_va_arg(args, uint64_t);
            serial_puthex(val);
            break;
        }
        case 'c': {
            char c = (char)__builtin_va_arg(args, int);
            serial_putc(c);
            break;
        }
        case '%':
            serial_putc('%');
            break;
        default:
            serial_putc('%');
            if (*fmt) serial_putc(*fmt);
            break;
        }

        /* Format string must not end with a bare '%': stop instead of
         * walking past the terminator */
        if (*fmt == '\0') {
            break;
        }
        fmt++;
    }

    __builtin_va_end(args);
}
