; ojjyOS v3 Kernel - Assembly Entry Point
;
; This file contains:
; - Kernel entry point (called from bootloader)
; - ISR stubs for interrupt handling
;
; NASM syntax, x86_64

[BITS 64]

; Export symbols
global _start
global isr_stub_0, isr_stub_1, isr_stub_2, isr_stub_3
global isr_stub_4, isr_stub_5, isr_stub_6, isr_stub_7
global isr_stub_8, isr_stub_9, isr_stub_10, isr_stub_11
global isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15
global isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19
global isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23
global isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27
global isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
global isr_stub_32, isr_stub_33, isr_stub_34, isr_stub_35
global isr_stub_36, isr_stub_37, isr_stub_38, isr_stub_39
global isr_stub_40, isr_stub_41, isr_stub_42, isr_stub_43
global isr_stub_44, isr_stub_45, isr_stub_46, isr_stub_47

; Import symbols
extern kernel_main
extern isr_handler

section .text

; ============================================================================
; Kernel Entry Point
; ============================================================================
; Called by UEFI bootloader with:
;   RDI = pointer to BootInfo structure
;
_start:
    ; Clear direction flag
    cld

    ; Set up a temporary stack (16KB at high address)
    ; We'll set up a proper stack later
    mov rsp, stack_top

    ; Save boot info pointer
    push rdi

    ; Clear BSS section
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, __bss_start
    xor al, al
    rep stosb

    ; Restore boot info pointer
    pop rdi

    ; Call kernel main
    call kernel_main

    ; If kernel_main returns, halt
.halt:
    cli
    hlt
    jmp .halt


; ============================================================================
; ISR Stub Macros
; ============================================================================
; Macro for ISRs that don't push an error code
%macro ISR_NOERRCODE 1
isr_stub_%1:
    push qword 0        ; Dummy error code
    push qword %1       ; Interrupt number
    jmp isr_common
%endmacro

; Macro for ISRs that push an error code
%macro ISR_ERRCODE 1
isr_stub_%1:
    push qword %1       ; Interrupt number (error code already pushed by CPU)
    jmp isr_common
%endmacro

; ============================================================================
; ISR Stubs
; ============================================================================
; CPU Exceptions (0-31)
ISR_NOERRCODE 0     ; Divide Error
ISR_NOERRCODE 1     ; Debug
ISR_NOERRCODE 2     ; NMI
ISR_NOERRCODE 3     ; Breakpoint
ISR_NOERRCODE 4     ; Overflow
ISR_NOERRCODE 5     ; Bound Range Exceeded
ISR_NOERRCODE 6     ; Invalid Opcode
ISR_NOERRCODE 7     ; Device Not Available
ISR_ERRCODE   8     ; Double Fault (has error code)
ISR_NOERRCODE 9     ; Coprocessor Segment Overrun
ISR_ERRCODE   10    ; Invalid TSS
ISR_ERRCODE   11    ; Segment Not Present
ISR_ERRCODE   12    ; Stack-Segment Fault
ISR_ERRCODE   13    ; General Protection Fault
ISR_ERRCODE   14    ; Page Fault
ISR_NOERRCODE 15    ; Reserved
ISR_NOERRCODE 16    ; x87 FPU Error
ISR_ERRCODE   17    ; Alignment Check
ISR_NOERRCODE 18    ; Machine Check
ISR_NOERRCODE 19    ; SIMD Floating-Point Exception
ISR_NOERRCODE 20    ; Virtualization Exception
ISR_NOERRCODE 21    ; Reserved
ISR_NOERRCODE 22    ; Reserved
ISR_NOERRCODE 23    ; Reserved
ISR_NOERRCODE 24    ; Reserved
ISR_NOERRCODE 25    ; Reserved
ISR_NOERRCODE 26    ; Reserved
ISR_NOERRCODE 27    ; Reserved
ISR_NOERRCODE 28    ; Reserved
ISR_NOERRCODE 29    ; Reserved
ISR_NOERRCODE 30    ; Reserved
ISR_NOERRCODE 31    ; Reserved

; Hardware IRQs (32-47)
ISR_NOERRCODE 32    ; Timer (IRQ 0)
ISR_NOERRCODE 33    ; Keyboard (IRQ 1)
ISR_NOERRCODE 34    ; Cascade (IRQ 2)
ISR_NOERRCODE 35    ; COM2 (IRQ 3)
ISR_NOERRCODE 36    ; COM1 (IRQ 4)
ISR_NOERRCODE 37    ; LPT2 (IRQ 5)
ISR_NOERRCODE 38    ; Floppy (IRQ 6)
ISR_NOERRCODE 39    ; LPT1 (IRQ 7)
ISR_NOERRCODE 40    ; RTC (IRQ 8)
ISR_NOERRCODE 41    ; IRQ 9
ISR_NOERRCODE 42    ; IRQ 10
ISR_NOERRCODE 43    ; IRQ 11
ISR_NOERRCODE 44    ; Mouse (IRQ 12)
ISR_NOERRCODE 45    ; FPU (IRQ 13)
ISR_NOERRCODE 46    ; Primary ATA (IRQ 14)
ISR_NOERRCODE 47    ; Secondary ATA (IRQ 15)


; ============================================================================
; Common ISR Handler
; ============================================================================
isr_common:
    ; Save all general-purpose registers
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Call C handler with pointer to interrupt frame
    mov rdi, rsp
    call isr_handler

    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    ; Remove interrupt number and error code
    add rsp, 16

    ; Return from interrupt
    iretq


; ============================================================================
; Data Section
; ============================================================================
section .bss

; Kernel stack (16KB)
align 16
stack_bottom:
    resb 16384
stack_top:

; BSS markers (set by linker)
global __bss_start
global __bss_end
