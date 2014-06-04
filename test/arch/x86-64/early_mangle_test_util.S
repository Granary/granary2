/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "test/arch/x86-64/include.S"

    .data

UINT64(deadbeef, 0xDEADBEEF)

    .text

BEGIN_FUNC(PushMem_GPR)
    pushq (%rdi);
    pop %rax;
    ret;
END_FUNC

BEGIN_FUNC(PushMem_GPR_GPR)
    pushq (%rdi, %rsi, 1);
    pop %rax;
    ret;
END_FUNC

BEGIN_FUNC(PushMem_RIP)
    pushq deadbeef(%rip);
    pop %rax;
    ret;
END_FUNC

BEGIN_FUNC(PushMem_STACK_DOWN)
    mov %rdi, -8(%rsp);
    pushq -8(%rsp);
    pop %rax;
    ret;
END_FUNC

BEGIN_FUNC(PushMem_STACK_TOP)
    push %rdi;
    push (%rsp);
    pop %rax;
    add $8, %rsp;
    ret;
END_FUNC

BEGIN_FUNC(PushMem_STACK_UP)
    push %rdi;
    sub $8, %rsp;
    push 8(%rsp);
    pop %rax;
    add $16, %rsp;
    ret;
END_FUNC

BEGIN_FUNC(PushImmWord)
    pushw $1;
    popw %ax;
    ret;
END_FUNC

BEGIN_FUNC(PushImmQuadWord)
    pushq $1;
    pop %rax;
    ret;
END_FUNC

BEGIN_FUNC(PushRSP)
    push %rsp;
    pop %rax;
    ret;
END_FUNC

BEGIN_FUNC(PushPopRSP)
    push %rsp;
    pop %rsp;
    ret;
END_FUNC

BEGIN_FUNC(PopRSP)
    mov %rsp, -8(%rsp);
    sub $8, %rsp;
    pop %rsp;
    ret;
END_FUNC

BEGIN_FUNC(PopMem_RSP_TOP)
    pushq $0;
    pushq $0xBEEF;
    pop (%rsp);
    lea 8(%rsp), %rsp;
    ret;
END_FUNC

BEGIN_FUNC(PopMem_RSP_UP)
    pushq $0;
    pushq $0;
    pushq $0xBEEF;
    pop 8(%rsp);
    lea 16(%rsp), %rsp;
    ret;
END_FUNC

BEGIN_FUNC(PopMem_RSP_DOWN)
    pushq $0xBEEF;
    pop -8(%rsp);  // Increment the stack pointer by 8.
    ret;
END_FUNC

BEGIN_FUNC(PopMem_GPR)
    pushq $0xBEEF;
    pop (%rdi);
    ret;
END_FUNC

BEGIN_FUNC(PopMem_GPR_GPR)
    pushq $0xBEEF;
    pop (%rdi, %rsi);
    ret;
END_FUNC

BEGIN_FUNC(PushPopGS)
    push %gs;
    pop %gs;
    ret;
END_FUNC

BEGIN_FUNC(PushwPopwGS)
    pushw %gs;
    popw %gs;
    ret;
END_FUNC

BEGIN_FUNC(SwapStacks_MOV)
    mov %rsp, %rax;
    mov %rax, %rsp;
    ret;
END_FUNC

BEGIN_FUNC(SwapStacks_XCHG_SELF)
    xchg %rsp, %rsp;
    ret;
END_FUNC

BEGIN_FUNC(SwapStacks_XCHG_OTHER)
    xchg %rsp, %rax;
    xchg %rax, %rsp;
    ret;
END_FUNC

BEGIN_FUNC(AccesTLSBase_Direct)
    mov %fs:0, %rax;
    ret;
END_FUNC

BEGIN_FUNC(AccesTLSBase_Indirect)
    xor %eax, %eax;
    mov %fs:(%rax), %rax;
    ret;
END_FUNC

BEGIN_FUNC(AccesTLSBase_Indirect32)
    mov $0, %eax;
    mov %fs:(%eax), %rax;
    ret;
END_FUNC

BEGIN_FUNC(AccesTLSBase_Indirect64)
    mov $0, %eax;
    mov %fs:(%rax), %rax;
    ret;
END_FUNC
