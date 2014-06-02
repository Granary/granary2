/* Copyright 2014 Peter Goodman, all rights reserved. */

#define BEGIN_FUNC(name) \
    .align 16; \
    .globl name; \
    .type name, @function; \
    name: \
    .cfi_startproc; \

#define END_FUNC \
    .cfi_endproc;

#define UINT64(name, val) \
    .align 8; \
    .globl name; \
    .type name, @object; \
    name: \
    .quad val;

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

BEGIN_FUNC(PushImm)
    push $0xDEADBEEF;
    pop %rax;
    ret;
END_FUNC
