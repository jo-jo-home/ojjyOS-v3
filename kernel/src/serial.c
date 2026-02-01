/*
 * ojjyOS v3 Kernel - Serial Port Driver
 *
 * 16550 UART driver for debug output.
 */

#include "serial.h"
#include "string.h"

/* Current serial port */
static uint16_t serial_port = COM1_PORT;

/* UART register offsets */
#define UART_DATA       0   /* Data register (R/W) */
#define UART_IER        1   /* Interrupt Enable Register */
#define UART_FCR        2   /* FIFO Control Register (W) */
#define UART_LCR        3   /* Line Control Register */
#define UART_MCR        4   /* Modem Control Register */
#define UART_LSR        5   /* Line Status Register */
#define UART_MSR        6   /* Modem Status Register */

/* Line Status Register bits */
#define LSR_DR          0x01    /* Data Ready */
#define LSR_THRE        0x20    /* Transmitter Holding Register Empty */

/* Baud rate divisor for 115200 baud */
#define BAUD_DIVISOR    1

/*
 * Initialize serial port
 */
void serial_init(uint16_t port)
{
    serial_port = port;

    /* Disable interrupts */
    outb(port + UART_IER, 0x00);

    /* Enable DLAB (set baud rate divisor) */
    outb(port + UART_LCR, 0x80);

    /* Set divisor (lo byte) */
    outb(port + UART_DATA, BAUD_DIVISOR & 0xFF);

    /* Set divisor (hi byte) */
    outb(port + UART_IER, (BAUD_DIVISOR >> 8) & 0xFF);

    /* 8 bits, no parity, one stop bit */
    outb(port + UART_LCR, 0x03);

    /* Enable FIFO, clear them, with 14-byte threshold */
    outb(port + UART_FCR, 0xC7);

    /* IRQs enabled, RTS/DSR set */
    outb(port + UART_MCR, 0x0B);

    /* Test serial chip (loopback mode) */
    outb(port + UART_MCR, 0x1E);
    outb(port + UART_DATA, 0xAE);

    /* Check if serial is working */
    if (inb(port + UART_DATA) != 0xAE) {
        /* Serial port not working, but continue anyway */
    }

    /* Set normal operation mode */
    outb(port + UART_MCR, 0x0F);
}

/*
 * Check if transmitter is ready
 */
static int serial_is_transmit_empty(void)
{
    return inb(serial_port + UART_LSR) & LSR_THRE;
}

/*
 * Write a character
 */
void serial_putc(char c)
{
    /* Wait for transmitter to be ready */
    while (!serial_is_transmit_empty()) {
        /* Busy wait */
    }

    /* Send the character */
    outb(serial_port + UART_DATA, c);

    /* Also send CR if LF */
    if (c == '\n') {
        serial_putc('\r');
    }
}

/*
 * Write a string
 */
void serial_puts(const char *s)
{
    while (*s) {
        serial_putc(*s++);
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
 * Supports: %s, %d, %u, %x, %p, %c, %%
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

        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(args, const char *);
            serial_puts(s ? s : "(null)");
            break;
        }
        case 'd': {
            int64_t val = __builtin_va_arg(args, int64_t);
            char buf[32];
            itoa(val, buf, 10);
            serial_puts(buf);
            break;
        }
        case 'u': {
            uint64_t val = __builtin_va_arg(args, uint64_t);
            char buf[32];
            utoa(val, buf, 10);
            serial_puts(buf);
            break;
        }
        case 'x': {
            uint64_t val = __builtin_va_arg(args, uint64_t);
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
            serial_putc(*fmt);
            break;
        }

        fmt++;
    }

    __builtin_va_end(args);
}
