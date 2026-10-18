/* EL0 entry / complete integer trap frame. Created by Jonas Lee.
 * FP/SIMD traps at EL0; kernel uses general registers only.
 * Single CPU, one bounded runner, no nested user entry. */
.text
.align 4
.global a64_user_enter
a64_user_enter:
    stp x19, x20, [sp, #-96]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    adrp x9, saved_kernel_sp
    mov x10, sp
    str x10, [x9, :lo12:saved_kernel_sp]
    msr elr_el1, x0
    msr sp_el0, x1
    msr tpidr_el0, xzr
    msr tpidrro_el0, xzr
    msr spsr_el1, xzr
    mov sp, x2
    mov x0, xzr
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
    mov x6, xzr
    mov x7, xzr
    mov x8, xzr
    mov x9, xzr
    mov x10, xzr
    mov x11, xzr
    mov x12, xzr
    mov x13, xzr
    mov x14, xzr
    mov x15, xzr
    mov x16, xzr
    mov x17, xzr
    mov x18, xzr
    mov x19, xzr
    mov x20, xzr
    mov x21, xzr
    mov x22, xzr
    mov x23, xzr
    mov x24, xzr
    mov x25, xzr
    mov x26, xzr
    mov x27, xzr
    mov x28, xzr
    mov x29, xzr
    mov x30, xzr
    eret

.macro USER_TRAP handler
    sub sp, sp, #288
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mrs x0, sp_el0
    str x0, [sp, #248]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #256]
    mrs x0, esr_el1
    mrs x1, far_el1
    stp x0, x1, [sp, #272]
    mov x0, sp
    bl \handler
    cbnz x0, .Luser_leave
    ldr x0, [sp, #248]
    msr sp_el0, x0
    ldp x0, x1, [sp, #256]
    msr elr_el1, x0
    msr spsr_el1, x1
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #288
    eret
.endm
.global a64_user_sync_entry
a64_user_sync_entry:
    USER_TRAP a64_user_sync
.global a64_user_irq_entry
a64_user_irq_entry:
    USER_TRAP a64_user_irq
.Luser_leave:
    msr daifset, #0xf
    msr sp_el0, xzr
    msr tpidr_el0, xzr
    adrp x9, saved_kernel_sp
    ldr x9, [x9, :lo12:saved_kernel_sp]
    mov sp, x9
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    ldp x19, x20, [sp], #96
    ret
.bss
.align 3
saved_kernel_sp: .space 8
