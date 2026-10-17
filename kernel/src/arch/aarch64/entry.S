/*
 * ojjyOS v3 Kernel - aarch64 Entry Point + Exception Vectors
 *
 * GNU assembler syntax (clang integrated assembler).
 *
 * The UEFI bootloader jumps here with x0 = BootInfo*. UEFI may have
 * left us at EL2 (QEMU/edk2 default) - we drop to EL1 before entering
 * the kernel proper.
 *
 * ojjyOS - Created by Jonas Lee
 */

.section .text
.align 4
.global _start
.type _start, %function
.extern kernel_main
.extern a64_irq_handler
.extern a64_exception_handler

_start:
    /* Preserve the BootInfo pointer across the BSS clear */
    mov x19, x0

    /* Which exception level are we at? */
    mrs x9, CurrentEL
    lsr x9, x9, #2
    cmp x9, #2
    b.eq .Ldrop_from_el2
    cmp x9, #3
    b.eq .Ldrop_from_el3
    b .Lat_el1

.Ldrop_from_el3:
    /* EL3 (rare): route everything to EL2 and drop one level.
     * QEMU -M virt runs UEFI at EL2, so this path is defensive only. */
    mov x9, #0x5b1              /* SCR_EL3: RW (EL2 is AArch64), NS */
    msr scr_el3, x9
    adr x9, .Ldrop_from_el2
    msr elr_el3, x9
    mov x9, #0x3c9              /* SPSR_EL3: EL2h, DAIF masked */
    msr spsr_el3, x9
    eret

.Ldrop_from_el2:
    /* EL2: stop trapping anything, run EL1 naked */
    msr hcr_el2, xzr
    /* Allow EL1 access to physical counter/timer */
    mov x9, #0x3
    msr cnthctl_el2, x9
    msr cntvoff_el2, xzr
    /* If CPU implements GICv3 system registers, enable EL1 access (ICC_SRE_EL2) */
    mrs x9, id_aa64pfr0_el1
    ubfx x9, x9, #24, #4
    cbz x9, 1f
    mov x9, #0x9                /* Enable (bit 3) | SRE (bit 0) */
    msr S3_4_C12_C9_5, x9
    isb
1:
    /* Invalidate stale TLB state we may have from HCR bits */
    tlbi alle2
    tlbi alle1
    dsb nsh
    isb
    adr x9, .Lat_el1
    msr elr_el2, x9
    mov x9, #0x3c5              /* SPSR_EL2: EL1h, DAIF masked */
    msr spsr_el2, x9
    eret

.Lat_el1:
    /* Install the kernel stack (lives in .bss, see below) */
    ldr x9, =stack_top
    mov sp, x9

    /* Clear BSS (8 bytes at a time; linker keeps it 8-aligned) */
    ldr x9, =__bss_start
    ldr x10, =__bss_end
.Lclear_bss:
    cmp x9, x10
    b.hs .Lbss_done
    str xzr, [x9], #8
    b .Lclear_bss
.Lbss_done:

    /* Install the exception vector table */
    ldr x9, =vectors
    msr vbar_el1, x9
    isb

    /* Enter the kernel */
    mov x0, x19
    bl kernel_main

.Lhang:
    wfe
    b .Lhang

/*
 * Exception vector table (VBAR_EL1).
 * 16 slots of 0x80 bytes each, table aligned to 0x800.
 */
.macro VEC_SYNC
    .align 7
    stp x29, x30, [sp, #-16]!
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, spsr_el1
    mov x0, x1
    mov x1, x2
    mov x2, x3
    bl a64_exception_handler
    b .Lhang
.endm

.macro VEC_IRQ
    .align 7
    b .Lirq_common
.endm

.macro VEC_FIQ
    .align 7
    b .Lirq_common
.endm

.macro VEC_SERROR
    .align 7
    stp x29, x30, [sp, #-16]!
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, spsr_el1
    mov x0, x1
    mov x1, x2
    mov x2, x3
    bl a64_exception_handler
    b .Lhang
.endm

.align 11                         /* 2048-byte table alignment */
vectors:
    /* Current EL, using SP_EL0 (never used by this kernel) */
    VEC_SYNC
    VEC_IRQ
    VEC_FIQ
    VEC_SERROR

    /* Current EL, using SP_EL1 (the normal case) */
    VEC_SYNC
    VEC_IRQ
    VEC_FIQ
    VEC_SERROR

    /* Lower EL, AArch64 (no EL0 yet) */
    VEC_SYNC
    VEC_IRQ
    VEC_FIQ
    VEC_SERROR

    /* Lower EL, AArch32 (unused) */
    VEC_SYNC
    VEC_IRQ
    VEC_FIQ
    VEC_SERROR

.align 4
.Lirq_common:
    sub sp, sp, #176
    stp x0,  x1,  [sp, #0]
    stp x2,  x3,  [sp, #16]
    stp x4,  x5,  [sp, #32]
    stp x6,  x7,  [sp, #48]
    stp x8,  x9,  [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x29, [sp, #144]
    str x30,      [sp, #160]
    bl a64_irq_handler
    ldr x30,      [sp, #160]
    ldp x18, x29, [sp, #144]
    ldp x16, x17, [sp, #128]
    ldp x14, x15, [sp, #112]
    ldp x12, x13, [sp, #96]
    ldp x10, x11, [sp, #80]
    ldp x8,  x9,  [sp, #64]
    ldp x6,  x7,  [sp, #48]
    ldp x4,  x5,  [sp, #32]
    ldp x2,  x3,  [sp, #16]
    ldp x0,  x1,  [sp, #0]
    add sp, sp, #176
    eret

/*
 * Kernel stack in .bss (128 KB - the compositor has deep frames)
 */
.section .bss
.align 16
.global stack_bottom
.global stack_top
stack_bottom:
    .space 131072
stack_top:
