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
#define UART(off) ((uintptr_t)UART0_BASE + (off))

void serial_init(uint16_t port)
{
    (void)port;     /* Fixed UART base on this platform */

    /* Mask UART interrupts; the debug channel is polled */
    mmio_write32(UART(UART_IMSC), 0);
    mmio_write32(UART(UART_ICR), 0x7FF);
}

void serial_putc(char c)
{
    /* Wait until the transmit FIFO has room */
    while (mmio_read32(UART(UART_FR)) & FR_TXFF) {
        /* Busy wait */
    }

    mmio_write32(UART(UART_DR), (uint32_t)(uint8_t)c);

    /* Mirror the x86 driver's LF -> CRLF behavior */
    if (c == '\n') {
        serial_putc('\r');
    }
}
