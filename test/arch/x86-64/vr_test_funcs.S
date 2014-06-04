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
