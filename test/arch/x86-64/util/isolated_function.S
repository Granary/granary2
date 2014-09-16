/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "test/arch/x86-64/util/include.S"

    .text

BEGIN_FUNC(RunFunctionInContext)

    /* Copy over new regs state */
    xchg (%rsi), %rsp;      /* Swap stacks. */
    push %rsi;              /* IsolatedRegState for state save/restore */
    push %rdi;              /* function to call */

    xchg 8(%rsi), %rax;
    xchg 16(%rsi), %rcx;
    xchg 24(%rsi), %rdx;
    xchg 32(%rsi), %rbx;
    xchg 40(%rsi), %rbp;
                            /* RSI would normally be here... */
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

#ifndef GRANARY_RECURSIVE
    push %rax;              /* Clear out the arithmetic flags */
    xor %rax, %rax;
    sahf;
    pop %rax;
    cld;
#endif

    /* Overwrite the target of the call on the stack, assumes redzone */
    lea 8(%rsp), %rsp;
    call *-8(%rsp);

    xchg (%rsp), %rsp;      /* Restore the IsolatedRegState pointer. */

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
