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
#include "platform.h"
#include "pci.h"
#include "xhci.h"
#include "arch_power.h"

/* Implemented in timer_a64.c: re-arms the EL1 virtual timer and
 * counts delivered IRQs independently of the counter-based uptime. */
void a64_timer_isr(void);
void a64_timer_report_health(void);

void a64_init(BootInfo *boot_info)
{
    (void)boot_info;

    /* Exception vectors were installed by entry.S (VBAR_EL1). */
    serial_printf("[A64] Exception level 1, vectors installed\n");

    /* Interrupt controller: only the generic timer uses it. */
    gicv2_init();

    /* QEMU virt: fixed virtio-mmio slots. Apple VZ: PCIe devices. */
    if (platform.qemu) virtio_mmio_init();
    if (platform.ecam) {
        pci_init();
        virtio_pci_init();
        xhci_init();
    }

    /* Real-time clock (PL031 on QEMU; boot-time clock elsewhere) */
    rtc_init();
}

/*
 * IRQ entry from the vector table.
 */
void a64_irq_handler(void)
{
    uint32_t intid = gicv2_ack();

    if (intid == platform.vtimer_ppi) {
        /* EL1 virtual timer PPI: record delivery and re-arm */
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
void a64_exception_handler(uint64_t esr, uint64_t elr, uint64_t spsr, uint64_t caller)
{
    uint64_t far;
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));
    cli();
    fb_use_backbuffer(false);
    serial_printf("\n[PANIC] Exception at EL1\n");
    serial_printf("  ESR_EL1 = 0x%x  (EC=0x%x, ISS=0x%x)\n",
        (unsigned int)esr,
        (unsigned int)((esr >> 26) & 0x3F),
        (unsigned int)(esr & 0x1FFFFFF));
    serial_printf("  ELR_EL1 = 0x%x\n", (unsigned int)elr);
    serial_printf("  FAR_EL1 = 0x%llx\n", (unsigned long long)far);
    serial_printf("  Caller  = 0x%llx\n", (unsigned long long)caller);
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
        fb_flush_all();
    }

    while (1) {
        __asm__ volatile("wfi");
    }
}

/*
 * Polled device harvest: called from the kernel main loop.
 * The 1 kHz timer interrupt keeps the loop alive (wfi wakes on IRQ),
     * so idle input is checked each tick. Rendering and blocking I/O can
     * still delay a harvest; do not claim a hard one-millisecond bound.
 */
void a64_poll_input(void)
{
    virtio_input_poll();
    virtio_net_poll();
    virtio_rng_poll();
    virtio_gpu_poll();
    virtio_console_poll();
    xhci_poll();
    a64_timer_report_health();
}
