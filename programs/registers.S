/* Verify every non-result GPR across SVC and timer interrupts. Jonas Lee. */
#include "user_abi.h"
.text
.global _start
_start:
    sub sp, sp, #16
    mov x8, #USER_SYS_UPTIME
    svc #0
    str x0, [sp]
.macro SET n
    mov x\n, #(100 + \n)
.endm
.macro CHECK n
    cmp x\n, #(100 + \n)
    b.ne fail
.endm
.irp n,1,2,3,4,5,6,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
    SET \n
.endr
loop:
    mov x8, #USER_SYS_UPTIME
    svc #0
.irp n,1,2,3,4,5,6,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
    CHECK \n
.endr
    cmp x8, #USER_SYS_UPTIME
    b.ne fail
    /* Wait in EL0, without consuming the syscall budget. */
    mov x8, #0xF0000
delay:
    subs x8, x8, #1
    b.ne delay
    mov x8, #USER_SYS_UPTIME
    svc #0
    ldr x8, [sp]
    sub x0, x0, x8
    cmp x0, #10
    b.lo loop
    mov x0, #0
    b exit
fail:
    mov x0, #5
exit:
    mov x8, #USER_SYS_EXIT
    svc #0
    b .
