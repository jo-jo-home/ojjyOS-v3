/*
 * ojjyOS v3 Kernel - PL011 UART Serial (aarch64)
 *
 * Implements the arch serial primitive (serial_init / serial_putc)
 * on the PL011 UART at the QEMU virt machine's fixed address.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "serial.h"
#include "types.h"

#define UART0_BASE      0x09000000UL

/* PL011 register offsets */
#define UART_DR         0x00    /* Data register */
#define UART_FR         0x18    /* Flag register */
#define UART_IBRD       0x24    /* Integer baud rate divisor */
#define UART_FBRD       0x28    /* Fractional baud rate divisor */
#define UART_LCR_H      0x2C    /* Line control */
#define UART_CR         0x30    /* Control */
#define UART_IMSC       0x38    /* Interrupt mask */
#define UART_ICR        0x44    /* Interrupt clear */

/* Flag register bits */
#define FR_BUSY         (1u << 3)
#define FR_TXFF         (1u << 5)    /* Transmit FIFO full */

#include "mmio.h"
#include "platform.h"
#define UART(off) ((uintptr_t)platform.uart + (off))

/* Every log byte is kept in a ring, so a console that appears later (the
 * virtio-console on Apple's Virtualization framework, which has no UART)
 * still receives the whole boot log. */
#define LOG_RING_SIZE 65536
static char log_ring[LOG_RING_SIZE];
static uint32_t log_pos;
static bool log_wrapped;
static void (*log_sink)(char c);

void serial_init(uint16_t port)
{
    (void)port;
    if (!platform.uart) return;
    /* Mask UART interrupts; the debug channel is polled */
    mmio_write32(UART(UART_IMSC), 0);
    mmio_write32(UART(UART_ICR), 0x7FF);
}

static void uart_putc(char c)
{
    /* Wait until the transmit FIFO has room */
    while (mmio_read32(UART(UART_FR)) & FR_TXFF) {
        /* Busy wait */
    }
    mmio_write32(UART(UART_DR), (uint32_t)(uint8_t)c);
}

void serial_putc(char c)
{
    log_ring[log_pos++] = c;
    if (log_pos == LOG_RING_SIZE) { log_pos = 0; log_wrapped = true; }
    if (platform.uart) {
        uart_putc(c);
        if (c == '\n') uart_putc('\r');   /* Mirror the x86 driver's LF -> CRLF */
    }
    if (log_sink) log_sink(c);
}

void serial_set_sink(void (*sink)(char c))
{
    if (sink) {
        if (log_wrapped) for (uint32_t i = log_pos; i < LOG_RING_SIZE; i++) sink(log_ring[i]);
        for (uint32_t i = 0; i < log_pos; i++) sink(log_ring[i]);
    }
    log_sink = sink;
}
