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
