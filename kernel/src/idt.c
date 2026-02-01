/*
 * ojjyOS v3 Kernel - Interrupt Descriptor Table Implementation
 *
 * Sets up IDT, remaps PIC, and provides basic interrupt handling.
 */

#include "idt.h"
#include "gdt.h"
#include "serial.h"
#include "panic.h"

/* IDT entry structure */
typedef struct {
    uint16_t offset_low;    /* Offset bits 0-15 */
    uint16_t selector;      /* Code segment selector */
    uint8_t  ist;           /* Interrupt Stack Table offset */
    uint8_t  type_attr;     /* Type and attributes */
    uint16_t offset_mid;    /* Offset bits 16-31 */
    uint32_t offset_high;   /* Offset bits 32-63 */
    uint32_t reserved;
} PACKED IdtEntry;

/* IDT pointer structure */
typedef struct {
    uint16_t limit;
    uint64_t base;
} PACKED IdtPointer;

/* Number of IDT entries */
#define IDT_ENTRIES 256

/* The IDT itself */
static IdtEntry idt[IDT_ENTRIES];
static IdtPointer idt_ptr;

/* Handler table */
static InterruptHandler handlers[IDT_ENTRIES];

/* PIC ports */
#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1

/* Exception names for debugging */
static const char *exception_names[] = {
    "Division Error",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
};

/* Forward declarations for ISR stubs (defined in assembly) */
extern void isr_stub_0(void);
extern void isr_stub_1(void);
extern void isr_stub_2(void);
extern void isr_stub_3(void);
extern void isr_stub_4(void);
extern void isr_stub_5(void);
extern void isr_stub_6(void);
extern void isr_stub_7(void);
extern void isr_stub_8(void);
extern void isr_stub_9(void);
extern void isr_stub_10(void);
extern void isr_stub_11(void);
extern void isr_stub_12(void);
extern void isr_stub_13(void);
extern void isr_stub_14(void);
extern void isr_stub_15(void);
extern void isr_stub_16(void);
extern void isr_stub_17(void);
extern void isr_stub_18(void);
extern void isr_stub_19(void);
extern void isr_stub_20(void);
extern void isr_stub_21(void);
extern void isr_stub_22(void);
extern void isr_stub_23(void);
extern void isr_stub_24(void);
extern void isr_stub_25(void);
extern void isr_stub_26(void);
extern void isr_stub_27(void);
extern void isr_stub_28(void);
extern void isr_stub_29(void);
extern void isr_stub_30(void);
extern void isr_stub_31(void);

/* IRQ stubs (32-47) */
extern void isr_stub_32(void);
extern void isr_stub_33(void);
extern void isr_stub_34(void);
extern void isr_stub_35(void);
extern void isr_stub_36(void);
extern void isr_stub_37(void);
extern void isr_stub_38(void);
extern void isr_stub_39(void);
extern void isr_stub_40(void);
extern void isr_stub_41(void);
extern void isr_stub_42(void);
extern void isr_stub_43(void);
extern void isr_stub_44(void);
extern void isr_stub_45(void);
extern void isr_stub_46(void);
extern void isr_stub_47(void);

/* Stub table */
static void (*isr_stubs[48])(void) = {
    isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3,
    isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7,
    isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11,
    isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15,
    isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19,
    isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23,
    isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27,
    isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31,
    isr_stub_32, isr_stub_33, isr_stub_34, isr_stub_35,
    isr_stub_36, isr_stub_37, isr_stub_38, isr_stub_39,
    isr_stub_40, isr_stub_41, isr_stub_42, isr_stub_43,
    isr_stub_44, isr_stub_45, isr_stub_46, isr_stub_47,
};

/*
 * Set an IDT entry
 */
static void idt_set_entry(int index, uint64_t handler, uint8_t type_attr)
{
    idt[index].offset_low = handler & 0xFFFF;
    idt[index].selector = GDT_KERNEL_CODE;
    idt[index].ist = 0;
    idt[index].type_attr = type_attr;
    idt[index].offset_mid = (handler >> 16) & 0xFFFF;
    idt[index].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[index].reserved = 0;
}

/*
 * Remap PIC to avoid conflict with CPU exceptions
 */
static void pic_remap(void)
{
    /* Save masks */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    /* Start initialization sequence */
    outb(PIC1_CMD, 0x11);
    io_wait();
    outb(PIC2_CMD, 0x11);
    io_wait();

    /* Set vector offsets */
    outb(PIC1_DATA, IRQ_BASE);      /* IRQ 0-7 -> INT 32-39 */
    io_wait();
    outb(PIC2_DATA, IRQ_BASE + 8);  /* IRQ 8-15 -> INT 40-47 */
    io_wait();

    /* Tell PICs about each other */
    outb(PIC1_DATA, 0x04);  /* Master has slave at IRQ2 */
    io_wait();
    outb(PIC2_DATA, 0x02);  /* Slave cascade identity */
    io_wait();

    /* 8086 mode */
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /* Restore masks */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

/*
 * Send End Of Interrupt to PIC
 */
static void pic_eoi(uint8_t irq)
{
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);
    }
    outb(PIC1_CMD, 0x20);
}

/*
 * Enable a specific IRQ
 */
void pic_enable_irq(uint8_t irq)
{
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

/*
 * Disable a specific IRQ
 */
void pic_disable_irq(uint8_t irq)
{
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    value = inb(port) | (1 << irq);
    outb(port, value);
}

/*
 * Common interrupt handler (called from assembly stubs)
 */
void isr_handler(InterruptFrame *frame)
{
    uint64_t int_num = frame->int_num;

    /* Call registered handler if any */
    if (handlers[int_num]) {
        handlers[int_num](frame);
    } else if (int_num < 32) {
        /* Unhandled CPU exception - panic */
        const char *name = (int_num < 21) ? exception_names[int_num] : "Unknown";
        serial_printf("[PANIC] Exception %d: %s\n", int_num, name);
        serial_printf("  Error code: 0x%x\n", frame->error_code);
        serial_printf("  RIP: 0x%p\n", frame->rip);
        serial_printf("  CS:  0x%x\n", frame->cs);
        serial_printf("  RFLAGS: 0x%x\n", frame->rflags);
        serial_printf("  RSP: 0x%p\n", frame->rsp);
        serial_printf("  SS:  0x%x\n", frame->ss);

        panic_with_frame(name, frame);
    } else {
        /* Unhandled IRQ */
        serial_printf("[WARN] Unhandled IRQ %d\n", int_num - IRQ_BASE);
    }

    /* Send EOI for hardware interrupts */
    if (int_num >= IRQ_BASE && int_num < IRQ_BASE + 16) {
        pic_eoi(int_num - IRQ_BASE);
    }
}

/*
 * Initialize IDT
 */
void idt_init(void)
{
    /* Clear IDT and handlers */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_entry(i, 0, 0);
        handlers[i] = NULL;
    }

    /* Set up exception handlers (0-31) and IRQ handlers (32-47) */
    for (int i = 0; i < 48; i++) {
        /* Type: Present (1), DPL (00), Gate type (0xE = 64-bit interrupt gate) */
        /* = 0b10001110 = 0x8E */
        idt_set_entry(i, (uint64_t)isr_stubs[i], 0x8E);
    }

    /* Remap PIC */
    pic_remap();

    /* Mask all IRQs initially */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    /* Set up IDT pointer */
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;

    /* Load IDT */
    __asm__ volatile("lidt (%0)" : : "r"(&idt_ptr));

    serial_printf("[IDT] Initialized with %d entries\n", IDT_ENTRIES);
}

/*
 * Register an interrupt handler
 */
void idt_register_handler(uint8_t vector, InterruptHandler handler)
{
    handlers[vector] = handler;
}

/*
 * Enable interrupts
 */
void interrupts_enable(void)
{
    __asm__ volatile("sti");
}

/*
 * Disable interrupts
 */
void interrupts_disable(void)
{
    __asm__ volatile("cli");
}
