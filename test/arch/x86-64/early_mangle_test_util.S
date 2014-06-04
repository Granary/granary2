/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "test/arch/x86-64/include.S"

    .data

UINT64(deadbeef, 0xDEADBEEF)

    .text

BEGIN_FUNC(TestEarlyMangle_PushMem_GPR)
    pushq (%rdi);
    pop %rax;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PushMem_GPR_GPR)
    pushq (%rdi, %rsi, 1);
    pop %rax;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PushMem_RIP)
    pushq deadbeef(%rip);
    pop %rax;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PushMem_STACK_DOWN)
    mov %rdi, -8(%rsp);
    pushq -8(%rsp);
    pop %rax;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PushMem_STACK_TOP)
    push %rdi;
    push (%rsp);
    pop %rax;
    add $8, %rsp;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PushMem_STACK_UP)
    push %rdi;
    sub $8, %rsp;
    push 8(%rsp);
    pop %rax;
    add $16, %rsp;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PushImmWord)
    pushw $1;
    popw %ax;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PushImmQuadWord)
    pushq $1;
    pop %rax;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PushRSP)
    push %rsp;
    pop %rax;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PushPopRSP)
    push %rsp;
    pop %rsp;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PopRSP)
    mov %rsp, -8(%rsp);
    sub $8, %rsp;
    pop %rsp;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PopMem_RSP_TOP)
    pushq $0;
    pushq $0xBEEF;
    pop (%rsp);
    lea 8(%rsp), %rsp;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PopMem_RSP_UP)
    pushq $0;
    pushq $0;
    pushq $0xBEEF;
    pop 8(%rsp);
    lea 16(%rsp), %rsp;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PopMem_RSP_DOWN)
    pushq $0xBEEF;
    pop -8(%rsp);  // Increment the stack pointer by 8.
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PopMem_GPR)
    pushq $0xBEEF;
    pop (%rdi);
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PopMem_GPR_GPR)
    pushq $0xBEEF;
    pop (%rdi, %rsi);
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PushPopGS)
    push %gs;
    pop %gs;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PushwPopwGS)
    pushw %gs;
    popw %gs;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_SwapStacks_MOV)
    mov %rsp, %rax;
    mov %rax, %rsp;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_SwapStacks_XCHG_SELF)
    xchg %rsp, %rsp;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_SwapStacks_XCHG_OTHER)
    xchg %rsp, %rax;
    xchg %rax, %rsp;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_AccesTLSBase_Direct)
    mov %fs:0, %rax;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_AccesTLSBase_Indirect)
    xor %eax, %eax;
    mov %fs:(%rax), %rax;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_AccesTLSBase_Indirect32)
    mov $0, %eax;
    mov %fs:(%eax), %rax;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_AccesTLSBase_Indirect64)
    mov $0, %eax;
    mov %fs:(%rax), %rax;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_XLAT)
    xlat;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_ENTER_0)
    enter $16, $0;
    add $16 + 8, %rsp;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_ENTER_1)
    enter $16, $1;
    add $16 + 8 + 8, %rsp;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_ENTER_16)
    enter $16, $16;
    add $16 + 8 + 16 * 8, %rsp;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_ENTER_0_LEAVE)
    enter $16, $0;
    leave;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_ENTER_1_LEAVE)
    enter $16, $1;
    leave;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_ENTER_16_LEAVE)
    enter $16, $16;
    leave;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PUSHFW)
    pushfw;
    popw %ax;
    ret;
END_FUNC

BEGIN_FUNC(TestEarlyMangle_PUSHFQ)
    pushfq;
    pop %rax;
    ret;
END_FUNC
