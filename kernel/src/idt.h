/*
 * ojjyOS v3 Kernel - Interrupt Descriptor Table
 *
 * Sets up IDT for handling CPU exceptions and hardware interrupts.
 */

#ifndef _OJJY_IDT_H
#define _OJJY_IDT_H

#include "types.h"

/* Interrupt vectors */
#define INT_DIVIDE_ERROR        0
#define INT_DEBUG               1
#define INT_NMI                 2
#define INT_BREAKPOINT          3
#define INT_OVERFLOW            4
#define INT_BOUND_EXCEEDED      5
#define INT_INVALID_OPCODE      6
#define INT_DEVICE_NOT_AVAIL    7
#define INT_DOUBLE_FAULT        8
#define INT_COPROCESSOR_SEG     9
#define INT_INVALID_TSS         10
#define INT_SEGMENT_NOT_PRESENT 11
#define INT_STACK_FAULT         12
#define INT_GENERAL_PROTECTION  13
#define INT_PAGE_FAULT          14
#define INT_X87_FP_EXCEPTION    16
#define INT_ALIGNMENT_CHECK     17
#define INT_MACHINE_CHECK       18
#define INT_SIMD_FP_EXCEPTION   19
#define INT_VIRTUALIZATION      20

/* Hardware IRQs (remapped to 32-47) */
#define IRQ_BASE            32
#define IRQ_TIMER           0   /* IRQ 0 -> INT 32 */
#define IRQ_KEYBOARD        1   /* IRQ 1 -> INT 33 */
#define IRQ_CASCADE         2
#define IRQ_COM2            3
#define IRQ_COM1            4
#define IRQ_LPT2            5
#define IRQ_FLOPPY          6
#define IRQ_LPT1            7
#define IRQ_RTC             8
#define IRQ_MOUSE           12  /* IRQ 12 -> INT 44 */

/* Interrupt frame passed to handlers */
typedef struct {
    /* Pushed by our stub */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;

    /* Interrupt number and error code */
    uint64_t int_num;
    uint64_t error_code;

    /* Pushed by CPU */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} PACKED InterruptFrame;

/* Interrupt handler type */
typedef void (*InterruptHandler)(InterruptFrame *frame);

/* Initialize IDT */
void idt_init(void);

/* Register an interrupt handler */
void idt_register_handler(uint8_t vector, InterruptHandler handler);

/* Enable/disable interrupts */
void interrupts_enable(void);
void interrupts_disable(void);

/* PIC IRQ control */
void pic_enable_irq(uint8_t irq);
void pic_disable_irq(uint8_t irq);

#endif /* _OJJY_IDT_H */
