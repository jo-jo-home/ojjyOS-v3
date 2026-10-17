/*
 * ojjyOS v3 Kernel - aarch64 Platform Glue
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "arch64.h"
#include "gicv2.h"
#include "virtio.h"
#include "virtio_devices.h"
#include "serial.h"
#include "framebuffer.h"
#include "console.h"
#include "timer.h"
#include "drivers/rtc.h"
#include "types.h"

/* Implemented in timer_a64.c: re-arms the EL1 physical timer and
 * advances the millisecond tick counter. */
void a64_timer_isr(void);

void a64_init(BootInfo *boot_info)
{
    (void)boot_info;

    /* Exception vectors were installed by entry.S (VBAR_EL1). */
    serial_printf("[A64] Exception level 1, vectors installed\n");

    /* Interrupt controller: only the generic timer uses it. */
    gicv2_init();

    /* Discover virtio-mmio devices (block + input). */
    virtio_mmio_init();

    /* Real-time clock */
    rtc_init();
}

/*
 * IRQ entry from the vector table.
 */
void a64_irq_handler(void)
{
    uint32_t intid = gicv2_ack();

    if (intid == 30) {
        /* EL1 physical timer PPI: advance the tick and re-arm */
        a64_timer_isr();
    } else if (intid != 1023 /* spurious */) {
        serial_printf("[A64] Unhandled interrupt %d\n", (int)intid);
    }

    gicv2_eoi(intid);
}

/*
 * Synchronous exception entry: print what happened and halt.
 * (A fuller fault handler comes with real task management.)
 */
void a64_exception_handler(uint64_t esr, uint64_t elr, uint64_t spsr)
{
    cli();
    serial_printf("\n[PANIC] Exception at EL1\n");
    serial_printf("  ESR_EL1 = 0x%x  (EC=0x%x, ISS=0x%x)\n",
        (unsigned int)esr,
        (unsigned int)((esr >> 26) & 0x3F),
        (unsigned int)(esr & 0x1FFFFFF));
    serial_printf("  ELR_EL1 = 0x%x\n", (unsigned int)elr);
    serial_printf("  SPSR_EL1 = 0x%x\n", (unsigned int)spsr);
    serial_printf("System halted.\n");

    if (fb_get_width() > 0 && fb_get_height() > 0) {
        fb_clear(RGB(140, 20, 20));
        console_set_colors(COLOR_WHITE, RGB(140, 20, 20));
        console_clear();
        console_printf("\n  ========================================\n");
        console_printf("       ojjyOS Kernel Panic (aarch64)\n");
        console_printf("  ========================================\n\n");
        console_printf("  Synchronous Exception at EL1\n");
        console_printf("  ESR_EL1:  0x%x (EC=0x%x, ISS=0x%x)\n",
            (unsigned int)esr,
            (unsigned int)((esr >> 26) & 0x3F),
            (unsigned int)(esr & 0x1FFFFFF));
        console_printf("  ELR_EL1:  0x%x\n", (unsigned int)elr);
        console_printf("  SPSR_EL1: 0x%x\n\n", (unsigned int)spsr);
        console_printf("  System halted.\n");
    }

    while (1) {
        __asm__ volatile("wfi");
    }
}

/*
 * Polled device harvest: called from the kernel main loop.
 * The 1 kHz timer interrupt keeps the loop alive (wfi wakes on IRQ),
 * so input latency is at most one millisecond.
 */
void a64_poll_input(void)
{
    virtio_input_poll();
}
