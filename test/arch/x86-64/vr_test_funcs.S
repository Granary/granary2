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

BEGIN_FUNC(RunFunctionInContext)
    /* Save all reg state */
    /*push %rax;
    push %rcx;
    push %rdx;
    push %rbx;
    push %rbp;
    push %rsi;
    push %rdi;
    push %r8;
    push %r9;
    push %r10;
    push %r11;
    push %r12;
    push %r13;
    push %r14;
    push %r15;
    */

    /* Copy over new regs state */
    xchg 0(%rsi), %rsp;     /* swap stacks. */
    push %rsi;              /* reg state pointer for state save & restore */
    push %rdi;              /* function to call */

    xchg 8(%rsi), %rax;
    xchg 16(%rsi), %rcx;
    xchg 24(%rsi), %rdx;
    xchg 32(%rsi), %rbx;
    xchg 40(%rsi), %rbp;
                            /* rdi would normally be here... */
    xchg 56(%rsi), %rdi;
    xchg 64(%rsi), %r8;
    xchg 72(%rsi), %r9;
    xchg 80(%rsi), %r10;
    xchg 88(%rsi), %r11;
    xchg 96(%rsi), %r12;
    xchg 104(%rsi), %r13;
    xchg 112(%rsi), %r14;
    xchg 120(%rsi), %r15;
    xchg 48(%rsi), %rsi;    /* ...but it's down here. */

    /* Overwrite the target of the call on the stack, assumes redzone */
    lea 8(%rsp), %rsp;
    call *-8(%rsp);
    xchg (%rsp), %rsp;      /* restore reg state pointer */

    /* Save current state and restore native state */
    xchg 8(%rsp), %rax;
    xchg 16(%rsp), %rcx;
    xchg 24(%rsp), %rdx;
    xchg 32(%rsp), %rbx;
    xchg 40(%rsp), %rbp;
    xchg 48(%rsp), %rsi;
    xchg 56(%rsp), %rdi;
    xchg 64(%rsp), %r8;
    xchg 72(%rsp), %r9;
    xchg 80(%rsp), %r10;
    xchg 88(%rsp), %r11;
    xchg 96(%rsp), %r12;
    xchg 104(%rsp), %r13;
    xchg 112(%rsp), %r14;
    xchg 120(%rsp), %r15;

    mov (%rsp), %rsp;      /* swap back to native stack */

    ret;
END_FUNC

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
